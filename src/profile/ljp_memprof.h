/*
** Memory profiler.
**
** Major portions taken verbatim or adapted from the LuaVela.
** Copyright (C) 2015-2019 IPONWEB Ltd.
*/

#ifndef _LJP_MEMPROF_H
#define _LJP_MEMPROF_H

#include <stdint.h>
#include <stdbool.h>

struct lua_State;

#define LJM_CURRENT_FORMAT_VERSION 0x02

struct luam_Prof_options;

/*
** Starts profiling. Returns LUAM_PROFILE_SUCCESS on success and one of
** LUAM_PROFILE_ERR* codes otherwise. Destroyer is not called in case of
** LUAM_PROFILE_ERR*.
*/
int ljp_memprof_start(struct lua_State *L, const struct luam_Prof_options *opt);

/*
** Stops profiling. Returns LUAM_PROFILE_SUCCESS on success and one of
** LUAM_PROFILE_ERR* codes otherwise. If writer() function returns zero
** on call at buffer flush, or on_stop() callback returns non-zero
** value, returns LUAM_PROFILE_ERRIO.
*/
int ljp_memprof_stop(void);

/* Check that profiler is running. */
bool ljp_memprof_is_running(void);

/*
** VM g is currently being profiled, behaves exactly as ljp_memprof_stop().
** Otherwise does nothing and returns LUAM_PROFILE_ERR.
*/
int ljp_memprof_stop_vm(const struct lua_State *L);

#endif
