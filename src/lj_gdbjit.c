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
** to set breakpoints on specific traces (even ahead of their creation).
**
** The source location for each trace allows listing the corresponding
** source lines with the GDB command "list" (but only if the Lua source
** has been loaded from a file). Currently this is always set to the
** location where the trace has been started.
**
** Frame unwind information can be inspected with the GDB command
** "info frame". This also allows proper backtraces across JIT-compiled
** code with the GDB command "bt".
**
** You probably want to add the following settings to a .gdbinit file
** (or add them to ~/.gdbinit):
**   set disassembly-flavor intel
**   set breakpoint pending on
**
** Here's a sample GDB session:
** ------------------------------------------------------------------------

$ cat >x.lua
for outer=1,100 do
  for inner=1,100 do end
end
^D

$ luajit -jv x.lua
[TRACE   1 x.lua:2]
[TRACE   2 (1/3) x.lua:1 -> 1]

$ gdb --quiet --args luajit x.lua
(gdb) tbreak TRACE_1
Function "TRACE_1" not defined.
Temporary breakpoint 1 (TRACE_1) pending.
(gdb) run
Starting program: luajit x.lua

Temporary breakpoint 1, TRACE_1 () at x.lua:2
2	  for inner=1,100 do end
(gdb) list
1	for outer=1,100 do
2	  for inner=1,100 do end
3	end
(gdb) bt
#0  TRACE_1 () at x.lua:2
#1  0x08053690 in lua_pcall [...]
[...]
#7  0x0806ff90 in main [...]
(gdb) disass TRACE_1
Dump of assembler code for function TRACE_1:
0xf7fd9fba <TRACE_1+0>:	mov    DWORD PTR ds:0xf7e0e2a0,0x1
