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
  sys