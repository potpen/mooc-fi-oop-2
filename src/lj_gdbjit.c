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
0xf7fd9fc4 <TRACE_1+10>:	movsd  xmm7,QWORD PTR [edx+0x20]
[...]
0xf7fd9ff8 <TRACE_1+62>:	jmp    0xf7fd2014
End of assembler dump.
(gdb) tbreak TRACE_2
Function "TRACE_2" not defined.
Temporary breakpoint 2 (TRACE_2) pending.
(gdb) cont
Continuing.

Temporary breakpoint 2, TRACE_2 () at x.lua:1
1	for outer=1,100 do
(gdb) info frame
Stack level 0, frame at 0xffffd7c0:
 eip = 0xf7fd9f60 in TRACE_2 (x.lua:1); saved eip 0x8053690
 called by frame at 0xffffd7e0
 source language unknown.
 Arglist at 0xffffd78c, args:
 Locals at 0xffffd78c, Previous frame's sp is 0xffffd7c0
 Saved registers:
  ebx at 0xffffd7ac, ebp at 0xffffd7b8, esi at 0xffffd7b0, edi at 0xffffd7b4,
  eip at 0xffffd7bc
(gdb)

** ------------------------------------------------------------------------
*/

/* -- GDB JIT API --------------------------------------------------------- */

/* GDB JIT actions. */
enum {
  GDBJIT_NOACTION = 0,
  GDBJIT_REGISTER,
  GDBJIT_UNREGISTER
};

/* GDB JIT entry. */
typedef struct GDBJITentry {
  struct GDBJITentry *next_entry;
  struct GDBJITentry *prev_entry;
  const char *symfile_addr;
  uint64_t symfile_size;
} GDBJITentry;

/* GDB JIT descriptor. */
typedef struct GDBJITdesc {
  uint32_t version;
  uint32_t action_flag;
  GDBJITentry *relevant_entry;
  GDBJITentry *first_entry;
} GDBJITdesc;

GDBJITdesc __jit_debug_descriptor = {
  1, GDBJIT_NOACTION, NULL, NULL
};

/* GDB sets a breakpoint at this function. */
void LJ_NOINLINE __jit_debug_register_code()
{
  __asm__ __volatile__("");
};

/* -- In-memory ELF object definitions ------------------------------------ */

/* ELF definitions. */
typedef struct ELFheader {
  uint8_t emagic[4];
  uint8_t eclass;
  uint8_t eendian;
  uint8_t eversion;
  uint8_t eosabi;
  uint8_t eabiversion;
  uint8_t epad[7];
  uint16_t type;
  uint16_t machine;
  uint32_t version;
  uintptr_t entry;
  uintptr_t phofs;
  uintptr_t shofs;
  uint32_t flags;
  uint16_t ehsize;
  uint16_t phentsize;
  uint16_t phnum;
  uint16_t shentsize;
  uint16_t shnum;
  uint16_t shstridx;
} ELFheader;

typedef struct ELFsectheader {
  uint32_t name;
  uint32_t type;
  uintptr_t flags;
  uintptr_t addr;
  uintptr_t ofs;
  uintptr_t size;
  uint32_t link;
  uint32_t info;
  uintptr_t align;
  uintptr_t entsize;
} ELFsectheader;

#define ELFSECT_IDX_ABS		0xfff1

enum {
  ELFSECT_TYPE_PROGBITS = 1,
  ELFSECT_TYPE_SYMTAB = 2,
  ELFSECT_TYPE_STRTAB = 3,
  ELFSECT_TYPE_NOBITS = 8
};

#define ELFSECT_FLAGS_WRITE	1
#define ELFSECT_FLAGS_ALLOC	2
#define ELFSECT_FLAGS_EXEC	4

typedef struct ELFsymbol {
#if LJ_64
  uint32_t name;
  uint8_t info;
  uint8_t other;
  uint16_t sectidx;
  uintptr_t value;
  uint64_t size;
#else
  uint32_t name;
  uintptr_t value;
  uint32_t size;
  uint8_t info;
  uint8_t other;
  uint16_t sectidx;
#endif
} ELFsymbol;

enum {
  ELFSYM_TYPE_FUNC = 2,
  ELFSYM_TYPE_FILE = 4,
  ELFSYM_BIND_LOCAL = 0 << 4,
  ELFSYM_BIND_GLOBAL = 1 << 4,
};

/* DWARF definitions. */
#define DW_CIE_VERSION	1

enum {
  DW_CFA_nop = 0x0,
  DW_CFA_offset_extended = 0x5,
  DW_CFA_def_cfa = 0xc,
  DW_CFA_def_cfa_offset = 0xe,
  DW_CFA_offset_extended_sf = 0x11,
  DW_CFA_advance_loc = 0x40,
  DW_CFA_offset = 0x80
};

enum {
  DW_EH_PE_udata4 = 3,
  DW_EH_PE_textrel = 0x20
};

enum {
  DW_TAG_compile_unit = 0x11
};

enum {
  DW_children_no = 0,
  DW_children_yes = 1
};

enum {
  DW_AT_name = 0x03,
  DW_AT_stmt_list = 0x10,
  DW_AT_low_pc = 0x11,
  DW_AT_high_pc = 0x12
};

enum {
  DW_FORM_addr = 0x01,
  DW_FORM_data4 = 0x06,
  DW_FORM_string = 0x08
};

enum {
  DW_LNS_extended_op = 0,
  DW_LNS_copy = 1,
  DW_LNS_advance_pc = 2,
  DW_LNS_advance_line = 3
};

enum {
  DW_LNE_end_sequence = 1,
  DW_LNE_set_address = 2
};

enum {
#if LJ_TARGET_X86
  DW_REG_AX, DW_REG_CX, DW_REG_DX, DW_REG_BX,
  DW_REG_SP, DW_REG_BP, DW_REG_SI, DW_REG_DI,
  DW_REG_RA,
#elif LJ_TARGET_X64
  /* Yes, the order is strange, but correct. */
  DW_REG_AX, DW_REG_DX, DW_REG_CX, DW_REG_BX,
  DW_REG_SI, DW_REG_DI, DW_REG_BP, DW_REG_SP,
  DW_REG_8, DW_REG_9, DW_REG_10, DW_REG_11,
  DW_REG_12, DW_REG_13, DW_REG_14, DW_REG_15,
  DW_REG_RA,
#elif LJ_TARGET_ARM
  DW_REG_SP = 13,
  DW_REG_RA = 14,
#elif LJ_TARGET_ARM64
  DW_REG_SP = 31,
  DW_REG_RA = 30,
#elif LJ_TARGET_PPC
  DW_REG_SP = 1,
  DW_REG_RA =