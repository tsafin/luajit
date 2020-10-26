/*
** Miscellaneous Lua extensions library.
**
** Major portions taken verbatim or adapted from the LuaVela interpreter.
** Copyright (C) 2015-2019 IPONWEB Ltd.
*/

#define lib_misc_c
#define LUA_LIB

#include <stdio.h>

#include "lua.h"
#include "lmisclib.h"

#include "lj_obj.h"
#include "lj_str.h"
#include "lj_tab.h"
#include "lj_lib.h"

#include "profile/ljp_memprof.h"

/* ------------------------------------------------------------------------ */

static LJ_AINLINE void setnumfield(struct lua_State *L, GCtab *t,
				   const char *name, int64_t val)
{
  setnumV(lj_tab_setstr(L, t, lj_str_newz(L, name)), (double)val);
}

#define LJLIB_MODULE_misc

LJLIB_CF(misc_getmetrics)
{
  struct luam_Metrics metrics;
  GCtab *m;

  lua_createtable(L, 0, 19);
  m = tabV(L->top - 1);

  luaM_metrics(L, &metrics);

  setnumfield(L, m, "strhash_hit", metrics.strhash_hit);
  setnumfield(L, m, "strhash_miss", metrics.strhash_miss);

  setnumfield(L, m, "gc_strnum", metrics.gc_strnum);
  setnumfield(L, m, "gc_tabnum", metrics.gc_tabnum);
  setnumfield(L, m, "gc_udatanum", metrics.gc_udatanum);
  setnumfield(L, m, "gc_cdatanum", metrics.gc_cdatanum);

  setnumfield(L, m, "gc_total", metrics.gc_total);
  setnumfield(L, m, "gc_freed", metrics.gc_freed);
  setnumfield(L, m, "gc_allocated", metrics.gc_allocated);

  setnumfield(L, m, "gc_steps_pause", metrics.gc_steps_pause);
  setnumfield(L, m, "gc_steps_propagate", metrics.gc_steps_propagate);
  setnumfield(L, m, "gc_steps_atomic", metrics.gc_steps_atomic);
  setnumfield(L, m, "gc_steps_sweepstring", metrics.gc_steps_sweepstring);
  setnumfield(L, m, "gc_steps_sweep", metrics.gc_steps_sweep);
  setnumfield(L, m, "gc_steps_finalize", metrics.gc_steps_finalize);

  setnumfield(L, m, "jit_snap_restore", metrics.jit_snap_restore);
  setnumfield(L, m, "jit_trace_abort", metrics.jit_trace_abort);
  setnumfield(L, m, "jit_mcode_size", metrics.jit_mcode_size);
  setnumfield(L, m, "jit_trace_num", metrics.jit_trace_num);

  return 1;
}

/* ------------------------------------------------------------------------ */

#include "lj_libdef.h"

/* ----- misc.memprof module ---------------------------------------------- */

#define LJLIB_MODULE_misc_memprof

/* Default buffer writer function. Just call fwrite to corresponding FILE. */
static size_t buffer_writer_default(const void *data, size_t len, void *opt)
{
	FILE *stream = (FILE *)opt;
	return fwrite(data, 1, len, stream);
}

/* Default on stop callback. Just close corresponding stream. */
static int on_stop_cb_default(void *opt)
{
	FILE *stream = (FILE *)opt;
	return fclose(stream);
}

/* local started = ujit.memprof.start(fname) */
LJLIB_CF(misc_memprof_start)
{
  struct luam_Prof_options opt = {0};
  const char *fname;
  int started;

  fname = strdata(lj_lib_checkstr(L, 1));

  opt.arg = fopen(fname, "wb");

  if (opt.arg == NULL) {
    lua_pushboolean(L, 0);
    lua_pushnil(L);
    return 2;
  }

  opt.writer = buffer_writer_default;
  opt.on_stop = on_stop_cb_default;
  started = ljp_memprof_start(L, &opt) == LUAM_PROFILE_SUCCESS;
  lua_pushboolean(L, started);

  if (LJ_UNLIKELY(!started)) {
    fclose(opt.arg);
    remove(fname);
  }

  return 1;
}

/* local stopped = misc.memprof.stop() */
LJLIB_CF(misc_memprof_stop)
{
  lua_pushboolean(L, ljp_memprof_stop() == LUAM_PROFILE_SUCCESS);
  return 1;
}

/* local running = misc.memprof.is_running() */
LJLIB_CF(misc_memprof_is_running)
{
  lua_pushboolean(L, ljp_memprof_is_running());
  return 1;
}

#include "lj_libdef.h"

/* ------------------------------------------------------------------------ */

LUALIB_API int luaopen_misc(struct lua_State *L)
{
  LJ_LIB_REG(L, LUAM_MISCLIBNAME, misc);
  LJ_LIB_REG(L, LUAM_MISCLIBNAME ".memprof", misc_memprof);
  return 1;
}
