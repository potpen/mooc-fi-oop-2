/*
** LuaJIT core and libraries amalgamation.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

#define ljamalg_c
#define LUA_CORE

/* To get the mremap prototype. Must be defined before any system includes. */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#ifndef WINVER
#define WINVER 0x0501
#endif

#include "lua.h"
#include "lauxlib.h"

#include "lj_assert.c"
#include "lj_gc.c"
#include "lj_err.c"
#include "lj_char.c"
#include "lj_bc.c"
#include "lj_obj.c"
#include "lj_buf.c"
#include "lj_str.c"
#include "lj_tab.c"
#include "lj_func.c"
#include "lj_udata.c"
#include "lj_meta.c"
#include "lj_debug.c"
#include "lj_prng.c"
#include "lj_state.c"
#include "lj_dispatch.c"
#include "lj_vmevent.c"
#include "lj_vmmath.c"
#include "lj_strscan.c"
#include "lj_strfmt.c"
#include "lj_strfmt_num.c"
#include "lj_serialize.c"
#include "lj_api.c"
#include "lj_profile.c"
#include "lj_lex.c"
#include "lj_parse.c"
#include "lj_bcread.c"
#include "lj_bcwrite.c"
#include "lj_load.c"
#include "lj_ctype.c"
#include "lj_cdata.c"
#include "lj_cconv.c"
#include "lj_ccall.c"
#include "lj_ccallback.c"
#include "lj_carith.c"
#include "lj_clib.c"
#include "lj_cparse.c"
#include "lj_lib.c"
#incl