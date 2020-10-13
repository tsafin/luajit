/*
** Implementation of memory profiler.
**
** Major portions taken verbatim or adapted from the LuaVela.
** Copyright (C) 2015-2019 IPONWEB Ltd.
*/

#include "profile/ljp_memprof.h"
#include "lmisclib.h"
#include "lj_def.h"
#include "lj_arch.h"

#if LJ_HASMEMPROF

#include <stdbool.h>

#if LJ_IS_THREAD_SAFE
#include <pthread.h>
#endif

#include "lua.h"

#include "lj_obj.h"
#include "lj_frame.h"
#include "lj_debug.h"
#include "lj_gc.h"
#include "profile/ljp_symtab.h"
#include "profile/ljp_write.h"

/* Allocation events: */
#define AEVENT_ALLOC   ((uint8_t)1)
#define AEVENT_FREE    ((uint8_t)2)
#define AEVENT_REALLOC ((uint8_t)(AEVENT_ALLOC | AEVENT_FREE))

/* Allocation sources: */
#define ASOURCE_INT   ((uint8_t)(1 << 2))
#define ASOURCE_LFUNC ((uint8_t)(2 << 2))
#define ASOURCE_CFUNC ((uint8_t)(3 << 2))

/* Aux bits: */

/*
** There is ~1 second between each two events marked with this flag. This will
** possibly be used later to implement dumps of the evolving heap.
*/
#define LJM_TIMESTAMP ((uint8_t)(0x40))

#define LJM_EPILOGUE_HEADER 0x80

/*
** Yep, 8Mb. Tuned in order not to bother the platform with too often flushes.
*/
#define STREAM_BUFFER_SIZE (8 * 1024 * 1024)

enum memprof_state {
  MPS_IDLE, /* memprof not running */
  MPS_PROFILE /* memprof running */
};

struct alloc {
  lua_Alloc allocf; /* Allocating function. */
  void *state; /* Opaque allocator's state. */
};

struct memprof {
  global_State *g; /* Profiled VM. */
  enum memprof_state state; /* Internal state. */
  struct ljp_buffer out; /* Output accumulator. */
  struct alloc orig_alloc; /* Original allocator. */
  struct luam_Prof_options opt; /* Profiling options. */
};

#if LJ_IS_THREAD_SAFE

pthread_mutex_t memprof_mutex = PTHREAD_MUTEX_INITIALIZER;

static LJ_AINLINE void memprof_lock(void)
{
  pthread_mutex_lock(&memprof_mutex);
}

static LJ_AINLINE void memprof_unlock(void)
{
  pthread_mutex_unlock(&memprof_mutex);
}

#else /* LJ_IS_THREAD_SAFE */

#define memprof_lock()
#define memprof_unlock()

#endif /* LJ_IS_THREAD_SAFE */

/*
** Event stream format:
**
** stream         := symtab memprof
** symtab         := see <ljp_symtab.h>
** memprof        := prologue event* epilogue
** prologue       := 'l' 'j' 'm' version reserved
** version        := <BYTE>
** reserved       := <BYTE> <BYTE> <BYTE>
** prof-id        := <ULEB128>
** event          := event-alloc | event-realloc | event-free
** event-alloc    := event-header loc? naddr nsize
** event-realloc  := event-header loc? oaddr osize naddr nsize
** event-free     := event-header loc? oaddr osize
** event-header   := <BYTE>
** loc            := loc-lua | loc-c
** loc-lua        := sym-addr line-no
** loc-c          := sym-addr
** sym-addr       := <ULEB128>
** line-no        := <ULEB128>
** oaddr          := <ULEB128>
** naddr          := <ULEB128>
** osize          := <ULEB128>
** nsize          := <ULEB128>
** epilogue       := event-header
**
** <BYTE>   :  A single byte (no surprises here)
** <ULEB128>:  Unsigned integer represented in ULEB128 encoding
**
** (Order of bits below is hi -> lo)
**
** version: [VVVVVVVV]
**  * VVVVVVVV: Byte interpreted as a plain integer version number
**
** event-header: [FTUUSSEE]
**  * EE   : 2 bits for representing allocation event type (AEVENT_*)
**  * SS   : 2 bits for representing allocation source type (ASOURCE_*)
**  * UU   : 2 unused bits
**  * T    : 0 for regular events, 1 for the events marked with the timestamp
**           mark. It is assumed that the time distance between two marked
**           events is approximately the same and is equal to 1 second.
**  * F    : 0 for regular events, 1 for epilogue's *F*inal header
**           (if F is set to 1, all other bits are currently ignored)
*/

static struct memprof memprof = {0};

const char ljm_header[] = {'l', 'j', 'm', LJM_CURRENT_FORMAT_VERSION,
			   0x0, 0x0, 0x0};

static void memprof_write_lfunc(struct ljp_buffer *out, uint8_t header,
				GCfunc *fn, struct lua_State *L,
				cTValue *nextframe)
{
  const BCLine line = lj_debug_frameline(L, fn, nextframe);
  ljp_write_byte(out, header | ASOURCE_LFUNC);
  ljp_write_u64(out, (uint64_t)funcproto(fn));
  ljp_write_u64(out, line >= 0 ? (uint64_t)line : 0);
}

static void memprof_write_cfunc(struct ljp_buffer *out, uint8_t header,
				const GCfunc *fn)
{
  ljp_write_byte(out, header | ASOURCE_CFUNC);
  ljp_write_u64(out, (uint64_t)fn->c.f);
}

static void memprof_write_ffunc(struct ljp_buffer *out, uint8_t header,
				GCfunc *fn, struct lua_State *L,
				cTValue *frame)
{
  cTValue *pframe = frame_prev(frame);
  GCfunc *pfn = frame_func(pframe);

  /*
  ** NB! If a fast function is called by a Lua function, report the
  ** Lua function for more meaningful output. Otherwise report the fast
  ** function as a C function.
  */
  if (pfn != NULL && isluafunc(pfn))
    memprof_write_lfunc(out, header, pfn, L, frame);
  else
    memprof_write_cfunc(out, header, fn);
}

static void memprof_write_func(struct memprof *mp, uint8_t header)
{
  struct ljp_buffer *out = &mp->out;
  lua_State *L = gco2th(gcref(mp->g->mem_L));
  cTValue *frame = L->base - 1;
  GCfunc *fn;

  fn = frame_func(frame);

  if (isluafunc(fn))
    memprof_write_lfunc(out, header, fn, L, NULL);
  else if (isffunc(fn))
    memprof_write_ffunc(out, header, fn, L, frame);
  else if (iscfunc(fn))
    memprof_write_cfunc(out, header, fn);
  else
    lua_assert(0);
}

static void memprof_write_hvmstate(struct memprof *mp, uint8_t header)
{
  ljp_write_byte(&mp->out, header | ASOURCE_INT);
}

/*
** NB! In ideal world, we should report allocations from traces as well.
** But since traces must follow the semantics of the original code, behaviour of
** Lua and JITted code must match 1:1 in terms of allocations, which makes
** using memprof with enabled JIT virtually redundant. Hence the stub below.
*/
static void memprof_write_trace(struct memprof *mp, uint8_t header)
{
  ljp_write_byte(&mp->out, header | ASOURCE_INT);
}

typedef void (*memprof_writer)(struct memprof *mp, uint8_t header);

static const memprof_writer memprof_writers[] = {
  memprof_write_hvmstate, /* LJ_VMST_INTERP */
  memprof_write_func, /* LJ_VMST_LFUNC */
  memprof_write_func, /* LJ_VMST_FFUNC */
  memprof_write_func, /* LJ_VMST_CFUNC */
  memprof_write_hvmstate, /* LJ_VMST_GC */
  memprof_write_hvmstate, /* LJ_VMST_EXIT */
  memprof_write_hvmstate, /* LJ_VMST_RECORD */
  memprof_write_hvmstate, /* LJ_VMST_OPT */
  memprof_write_hvmstate, /* LJ_VMST_ASM */
  memprof_write_trace /* LJ_VMST_TRACE */
};

static void memprof_write_caller(struct memprof *mp, uint8_t aevent)
{
  const global_State *g = mp->g;
  const uint32_t _vmstate = g->vmstate;
  const uint32_t vmstate = _vmstate < LJ_VMST_TRACE ? _vmstate : LJ_VMST_TRACE;
  const uint8_t header = aevent;

  memprof_writers[vmstate](mp, header);
}

static int memprof_stop(const struct lua_State *L);

static void *memprof_allocf(void *ud, void *ptr, size_t osize, size_t nsize)
{
  struct memprof *mp = &memprof;
  struct alloc *oalloc = &mp->orig_alloc;
  struct ljp_buffer *out = &mp->out;
  void *nptr;

  lua_assert(MPS_PROFILE == mp->state);
  lua_assert(oalloc->allocf != memprof_allocf);
  lua_assert(oalloc->allocf != NULL);
  lua_assert(ud == oalloc->state);

  nptr = oalloc->allocf(ud, ptr, osize, nsize);

  if (nsize == 0) {
    memprof_write_caller(mp, AEVENT_FREE);
    ljp_write_u64(out, (uint64_t)ptr);
    ljp_write_u64(out, (uint64_t)osize);
  } else if (ptr == NULL) {
    memprof_write_caller(mp, AEVENT_ALLOC);
    ljp_write_u64(out, (uint64_t)nptr);
    ljp_write_u64(out, (uint64_t)nsize);
  } else {
    memprof_write_caller(mp, AEVENT_REALLOC);
    ljp_write_u64(out, (uint64_t)ptr);
    ljp_write_u64(out, (uint64_t)osize);
    ljp_write_u64(out, (uint64_t)nptr);
    ljp_write_u64(out, (uint64_t)nsize);
  }

  return nptr;
}

static void memprof_write_prologue(struct ljp_buffer *out)
{
  size_t i = 0;
  const size_t len = sizeof(ljm_header) / sizeof(ljm_header[0]);

  for (; i < len; i++)
    ljp_write_byte(out, ljm_header[i]);
}

int ljp_memprof_start(struct lua_State *L, const struct luam_Prof_options *opt)
{
  struct memprof *mp = &memprof;
  struct alloc *oalloc = &mp->orig_alloc;
  uint8_t *buf;

  if (opt->writer == NULL || opt->on_stop == NULL)
    return LUAM_PROFILE_ERR;

  memprof_lock();

  if (mp->state != MPS_IDLE) {
    memprof_unlock();
    return LUAM_PROFILE_ERR;
  }

  buf = (uint8_t *)lj_mem_new(L, STREAM_BUFFER_SIZE);
  if (NULL == buf) {
    memprof_unlock();
    return LUAM_PROFILE_ERRMEM;
  }

  /* Init options: */
  memcpy(&mp->opt, opt, sizeof(*opt));

  /* Init general fields: */
  mp->g = G(L);
  mp->state = MPS_PROFILE;

  /* Init output: */
  ljp_write_init(&mp->out, mp->opt.writer, mp->opt.arg, buf,
		 STREAM_BUFFER_SIZE);
  ljp_symtab_write(&mp->out, mp->g);
  memprof_write_prologue(&mp->out);

  if (LJ_UNLIKELY(ljp_write_test_flag(&mp->out, STREAM_ERR_IO))) {
    lj_mem_free(mp->g, mp->out.buf, STREAM_BUFFER_SIZE);
    ljp_write_terminate(&mp->out);
    memprof_unlock();
    return LUAM_PROFILE_ERRIO;
  }

  /* Override allocating function: */
  oalloc->allocf = lua_getallocf(L, &oalloc->state);
  lua_assert(oalloc->allocf != NULL);
  lua_assert(oalloc->allocf != memprof_allocf);
  lua_assert(oalloc->state != NULL);
  lua_setallocf(L, memprof_allocf, oalloc->state);

  memprof_unlock();
  return LUAM_PROFILE_SUCCESS;
}

static int memprof_stop(const struct lua_State *L)
{
  struct memprof *mp = &memprof;
  struct alloc *oalloc = &mp->orig_alloc;
  struct ljp_buffer *out = &mp->out;
  struct lua_State *main_L;
  int return_status = LUAM_PROFILE_SUCCESS;
  int cb_status;

  memprof_lock();

  if (mp->state != MPS_PROFILE) {
    memprof_unlock();
    return LUAM_PROFILE_ERR;
  }

  if (L != NULL && mp->g != G(L)) {
    memprof_unlock();
    return LUAM_PROFILE_ERR;
  }

  mp->state = MPS_IDLE;

  lua_assert(mp->g != NULL);
  main_L = mainthread(mp->g);

  lua_assert(memprof_allocf == lua_getallocf(main_L, NULL));
  lua_assert(oalloc->allocf != NULL);
  lua_assert(oalloc->state != NULL);
  lua_setallocf(main_L, oalloc->allocf, oalloc->state);

  ljp_write_byte(out, LJM_EPILOGUE_HEADER);

  ljp_write_flush_buffer(out);

  cb_status = mp->opt.on_stop(mp->opt.arg);
  if (LJ_UNLIKELY(ljp_write_test_flag(out, STREAM_ERR_IO) || cb_status != 0))
    return_status = LUAM_PROFILE_ERRIO;

  lj_mem_free(mp->g, out->buf, STREAM_BUFFER_SIZE);
  ljp_write_terminate(out);

  memprof_unlock();
  return return_status;
}

int ljp_memprof_stop(void)
{
  return memprof_stop(NULL);
}

int ljp_memprof_stop_vm(const struct lua_State *L)
{
  return memprof_stop(L);
}

bool ljp_memprof_is_running(void)
{
  struct memprof *mp = &memprof;
  bool running;

  memprof_lock();
  running = mp->state == MPS_PROFILE;
  memprof_unlock();

  return running;
}

#else /* LJ_HASMEMPROF */

int ljp_memprof_start(struct lua_State *L, const struct luam_Prof_options *opt)
{
  UNUSED(L);
  UNUSED(opt);
  return LUAM_PROFILE_ERR;
}

int ljp_memprof_stop(void)
{
  return LUAM_PROFILE_ERR;
}

int ljp_memprof_stop_vm(const struct lua_State *L)
{
  UNUSED(L);
  return LUAM_PROFILE_ERR;
}

bool ljp_memprof_is_running(void)
{
  return false;
}

#endif /* LJ_HASMEMPROF */
