/*
** Client for the GDB JIT API.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

#define lj_gdbjit_c
#define LUA_CORE

#include "lj_obj.h"

#if LJ_HASJIT

#include "lj_gc.h"
#include "lj_err.h"
#include "lj_debug.h"
#include "lj_frame.h"
#include "lj_buf.h"
#include "lj_strfmt.h"
#include "lj_jit.h"
#include "lj_dispatch.h"

/* This is not compiled in by default.
** Enable with -DLUAJIT_USE_GDBJIT in the Makefile and recompile everything.
*/
#ifdef LUAJIT_USE_GDBJIT

/* The GDB JIT API allows JIT compilers to pass debug information about
** JIT-compiled code back to GDB. You need at least GDB 7.0 or higher
** to see it in action.
**
** This is a passive API, so it works even when not running under GDB
** or when attaching to an already running process. Alas, this implies
** enabling it always has a non-negligible overhead -- do not use in
** release mode!
**
** The LuaJIT GDB JIT client is rather minimal at the moment. It gives
** each trace a symbol name and adds a source location and frame unwind
** information. Obviously LuaJIT itself and any embedding C application
** should be compiled with debug symbols, too (see the Makefile).
**
** Traces are named TRACE_1, TRACE_2, ... these correspond to the trace
** numbers from -jv or -jdump. Use "break TRACE_1" or "tbreak TRACE_1" etc.
** to set breakpoints on specific t