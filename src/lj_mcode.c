/*
** Machine code management.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

#define lj_mcode_c
#define LUA_CORE

#include "lj_obj.h"
#if LJ_HASJIT
#include "lj_gc.h"
#include "lj_err.h"
#include "lj_jit.h"
#include "lj_mcode.h"
#include "lj_trace.h"
#include "lj_dispatch.h"
#include "lj_prng.h"
#endif
#if LJ_HASJIT || LJ_HASFFI
#include "lj_vm.h"
#endif

/* -- OS-specific functions ----------------------------------------------- */

#if LJ_HASJIT || LJ_HASFFI

/* Define this if you want to run LuaJIT with Valgrind. */
#ifdef LUAJIT_USE_VALGRIND
#include <valgrind/valgrind.h>
#endif

#if LJ_TARGET_IOS
void sys_icache_invalidate(void *start, size_t len);
#endif

/* Synchronize data/instruction cache. */
void lj_mcode_sync(void *start, void *end)
{
#ifdef LUAJIT_USE_VALGRIND
  VALGRIND_DISCARD_TRANSLATIONS(start, (char *)end-(char *)start);
#endif
#if LJ_TARGET_X86ORX64
  UNUSED(start); UNUSED(end);
#elif LJ_TARGET_IOS
  sys_icache_invalidate(start, (char *)end-(char *)start);
#elif LJ_TARGET_PPC
  lj_vm_cachesync(start, end);
#elif defined(__GNUC__) || defined(__clang__)
  __clear_cache(start, end);
#else
#error "Missing builtin to flush instruction cache"
#endif
}

#endif

#if LJ_HASJIT

#if LJ_TARGET_WINDOWS

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define MCPROT_RW	PAGE_READWRITE
#define MCPROT_RX	PAGE_EXECUTE_READ
#define MCPROT_RWX	PAGE_EXECUTE_READWRITE

static void *mcode_alloc_at(jit_State *J, uintptr_t hint, size_t sz, DWORD prot)
{
  void *p = LJ_WIN_VALLOC((void *)hint, sz,
			  MEM_RESERVE|MEM_COMMIT|MEM_TOP_DOWN, prot);
  if (!p && !hint)
    lj_trace_err(J, LJ_TRERR_MCODEAL);
  return p;
}

static void mcode_free(jit_State *J, void *p, size_t sz)
{
  UNUSED(J); UNUSED(sz);
  VirtualFree(p, 0, MEM_RELEASE);
}

static int mcode_setprot(void *p, size_t sz, DWORD prot)
{
  DWORD oprot;
  return !LJ_WIN_VPROTECT(p, sz, prot, &oprot);
}

#elif LJ_TARGET_POSIX

#include <sys/mman.h>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS	MAP_ANON
#endif

#define MCPROT_RW	(PROT_READ|PROT_WRITE)
#define MCPROT_RX	(PROT_READ|PROT_EXEC)
#define MCPROT_RWX	(PROT_READ|PROT_WRITE|PROT_EXEC)
#ifdef PROT_MPROTECT
#define MCPROT_CREATE	(PROT_MPROTECT(MCPROT_RWX))
#else
#define MCPROT_CREATE	0
#endif

static void *mcode_alloc_at(jit_State *J, uintptr_t hint, size_t sz, int prot)
{
  void *p = mmap((void *)hint, sz, prot|MCPROT_CREATE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  if (p == MAP_FAILED) {
    if (!hint) lj_trace_err(J, LJ_TRERR_MCODEAL);
    p = NULL;
  }
  return p;
}

static void mcode_free(jit_State *J, void *p, size_t sz)
{
  UNUSED(J);
  munmap(p, sz);
}

static int mcode_setprot(void *p, size_t sz, int prot)
{
  return mprotect(p, sz, prot);
}

#else

#error "Missing OS support for explicit placement of executable memory"

#endif

/* -- MCode area protection ----------------------------------------------- */

#if LUAJIT_SECURITY_MCODE == 0

/* Define this ONLY if page protection twiddling becomes a bottleneck.
**
** It's generally considered to be a potential security risk to have
** pages with simultaneous write *and* execute access in a process.
**
** Do not even think about using this mode for server processes or
** apps handling untrusted external data.
**
** The security risk is not in LuaJIT itself -- but if an adversary finds
** any *other* flaw in your C application logic, then any RWX memory pages
** simplify writing an exploit considerably.
*/
#define MCPROT_GEN	MCPROT_RWX
#define MCPROT_RUN	MCPROT_RWX

static void mcode_protect(jit_State *J, int prot)
{
  UNUSED(J); UNUSED(prot); UNUSED(mcode_setprot);
}

#else

/* This is the default behaviour and much safer:
**
** Most of the time the memory pages holding machine code are executable,
** but NONE of them is writable.
**
** The current memory area is marked read-write (but NOT executable) only
** during the short time window while the assembler generates machine code.
*/
#define MCPROT_GEN	MCPROT_RW
#define MCPROT_RUN	MCPROT_RX

/* Protection twiddling failed. Probably due to kernel security. */
static LJ_NORET LJ_NOINLINE void mcode_protfail(jit_State *J)
{
  lua_CFunction panic = J2G(J)->panic;
  if (panic) {
    lua_State *L = J->L;
    setstrV(L, L->top++, lj_err_str(L, LJ_ERR_JITPROT));
    panic(L);
  }
  exit(EXIT_FAILURE);
}

/* Change protection of MCode area. */
static void mcode_protect(jit_State *J, int prot)
{
  if (J->mcprot != prot) {
    if (LJ_UNLIKELY(mcode_setprot(J->mcarea, J->szmcarea, prot)))
      mcode_protfail(J);
    J->mcprot = prot;
  }
}

#endif

/* -- MCode area allocation ----------------------------------------------- */

#if LJ_64
#define mcode_validptr(p)	(p)
#else
#define mcode_validptr(p)	((p) && (uintptr_t)(p) < 0xffff0000)
#endif

#ifdef LJ_TARGET_JUMPRANGE

/* Get memory within relative jump distance of our code in 64 bit mode. */
static void *mcode_alloc(jit_State *J, size_t sz)
{
  /* Target an address in the static assembler code (64K aligned).
  ** Try addresses within a distance of target-range/2+1MB..target+range/2-1MB.
  ** Use half the jump range so every address in the range can reach any other.
  */
#if LJ_TARGET_MIPS
  /* Use the middle of the 256MB-aligned region. */
  uintptr_t targe