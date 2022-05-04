
/*
** Error handling.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

#define lj_err_c
#define LUA_CORE

#include "lj_obj.h"
#include "lj_err.h"
#include "lj_debug.h"
#include "lj_str.h"
#include "lj_func.h"
#include "lj_state.h"
#include "lj_frame.h"
#include "lj_ff.h"
#include "lj_trace.h"
#include "lj_vm.h"
#include "lj_strfmt.h"

/*
** LuaJIT can either use internal or external frame unwinding:
**
** - Internal frame unwinding (INT) is free-standing and doesn't require
**   any OS or library support.
**
** - External frame unwinding (EXT) uses the system-provided unwind handler.
**
** Pros and Cons:
**
** - EXT requires unwind tables for *all* functions on the C stack between
**   the pcall/catch and the error/throw. C modules used by Lua code can
**   throw errors, so these need to have unwind tables, too. Transitively
**   this applies to all system libraries used by C modules -- at least
**   when they have callbacks which may throw an error.
**
** - INT is faster when actually throwing errors, but this happens rarely.
**   Setting up error handlers is zero-cost in any case.
**
** - INT needs to save *all* callee-saved registers when entering the
**   interpreter. EXT only needs to save those actually used inside the
**   interpreter. JIT-compiled code may need to save some more.
**
** - EXT provides full interoperability with C++ exceptions. You can throw
**   Lua errors or C++ exceptions through a mix of Lua frames and C++ frames.
**   C++ destructors are called as needed. C++ exceptions caught by pcall
**   are converted to the string "C++ exception". Lua errors can be caught
**   with catch (...) in C++.
**
** - INT has only limited support for automatically catching C++ exceptions
**   on POSIX systems using DWARF2 stack unwinding. Other systems may use
**   the wrapper function feature. Lua errors thrown through C++ frames
**   cannot be caught by C++ code and C++ destructors are not run.
**
** - EXT can handle errors from internal helper functions that are called
**   from JIT-compiled code (except for Windows/x86 and 32 bit ARM).
**   INT has no choice but to call the panic handler, if this happens.
**   Note: this is mainly relevant for out-of-memory errors.
**
** EXT is the default on all systems where the toolchain produces unwind
** tables by default (*). This is hard-coded and/or detected in src/Makefile.
** You can thwart the detection with: TARGET_XCFLAGS=-DLUAJIT_UNWIND_INTERNAL
**
** INT is the default on all other systems.
**
** EXT can be manually enabled for toolchains that are able to produce
** conforming unwind tables:
**   "TARGET_XCFLAGS=-funwind-tables -DLUAJIT_UNWIND_EXTERNAL"
** As explained above, *all* C code used directly or indirectly by LuaJIT
** must be compiled with -funwind-tables (or -fexceptions). C++ code must
** *not* be compiled with -fno-exceptions.
**
** If you're unsure whether error handling inside the VM works correctly,
** try running this and check whether it prints "OK":
**
**   luajit -e "print(select(2, load('OK')):match('OK'))"
**
** (*) Originally, toolchains only generated unwind tables for C++ code. For
** interoperability reasons, this can be manually enabled for plain C code,
** too (with -funwind-tables). With the introduction of the x64 architecture,
** the corresponding POSIX and Windows ABIs mandated unwind tables for all
** code. Over the following years most desktop and server platforms have
** enabled unwind tables by default on all architectures. OTOH mobile and
** embedded platforms do not consistently mandate unwind tables.
*/

/* -- Error messages ------------------------------------------------------ */

/* Error message strings. */
LJ_DATADEF const char *lj_err_allmsg =
#define ERRDEF(name, msg)	msg "\0"
#include "lj_errmsg.h"
;

/* -- Internal frame unwinding -------------------------------------------- */

/* Unwind Lua stack and move error message to new top. */
LJ_NOINLINE static void unwindstack(lua_State *L, TValue *top)
{
  lj_func_closeuv(L, top);
  if (top < L->top-1) {
    copyTV(L, top, L->top-1);
    L->top = top+1;
  }
  lj_state_relimitstack(L);
}

/* Unwind until stop frame. Optionally cleanup frames. */
static void *err_unwind(lua_State *L, void *stopcf, int errcode)
{
  TValue *frame = L->base-1;
  void *cf = L->cframe;
  while (cf) {
    int32_t nres = cframe_nres(cframe_raw(cf));
    if (nres < 0) {  /* C frame without Lua frame? */
      TValue *top = restorestack(L, -nres);
      if (frame < top) {  /* Frame reached? */
	if (errcode) {
	  L->base = frame+1;
	  L->cframe = cframe_prev(cf);
	  unwindstack(L, top);
	}
	return cf;
      }
    }
    if (frame <= tvref(L->stack)+LJ_FR2)
      break;
    switch (frame_typep(frame)) {
    case FRAME_LUA:  /* Lua frame. */
    case FRAME_LUAP:
      frame = frame_prevl(frame);
      break;
    case FRAME_C:  /* C frame. */
    unwind_c:
#if LJ_UNWIND_EXT
      if (errcode) {
	L->base = frame_prevd(frame) + 1;
	L->cframe = cframe_prev(cf);
	unwindstack(L, frame - LJ_FR2);
      } else if (cf != stopcf) {
	cf = cframe_prev(cf);
	frame = frame_prevd(frame);
	break;
      }
      return NULL;  /* Continue unwinding. */
#else
      UNUSED(stopcf);
      cf = cframe_prev(cf);
      frame = frame_prevd(frame);
      break;
#endif
    case FRAME_CP:  /* Protected C frame. */
      if (cframe_canyield(cf)) {  /* Resume? */
	if (errcode) {
	  hook_leave(G(L));  /* Assumes nobody uses coroutines inside hooks. */
	  L->cframe = NULL;
	  L->status = (uint8_t)errcode;
	}
	return cf;
      }
      if (errcode) {
	L->base = frame_prevd(frame) + 1;
	L->cframe = cframe_prev(cf);
	unwindstack(L, frame - LJ_FR2);
      }
      return cf;
    case FRAME_CONT:  /* Continuation frame. */
      if (frame_iscont_fficb(frame))
	goto unwind_c;
      /* fallthrough */
    case FRAME_VARG:  /* Vararg frame. */
      frame = frame_prevd(frame);
      break;
    case FRAME_PCALL:  /* FF pcall() frame. */
    case FRAME_PCALLH:  /* FF pcall() frame inside hook. */
      if (errcode) {
	if (errcode == LUA_YIELD) {
	  frame = frame_prevd(frame);
	  break;
	}
	if (frame_typep(frame) == FRAME_PCALL)
	  hook_leave(G(L));
	L->base = frame_prevd(frame) + 1;
	L->cframe = cf;
	unwindstack(L, L->base);
      }
      return (void *)((intptr_t)cf | CFRAME_UNWIND_FF);
    }
  }
  /* No C frame. */
  if (errcode) {
    L->base = tvref(L->stack)+1+LJ_FR2;
    L->cframe = NULL;
    unwindstack(L, L->base);
    if (G(L)->panic)
      G(L)->panic(L);
    exit(EXIT_FAILURE);
  }
  return L;  /* Anything non-NULL will do. */
}

/* -- External frame unwinding -------------------------------------------- */

#if LJ_ABI_WIN

/*
** Someone in Redmond owes me several days of my life. A lot of this is
** undocumented or just plain wrong on MSDN. Some of it can be gathered
** from 3rd party docs or must be found by trial-and-error. They really
** don't want you to write your own language-specific exception handler
** or to interact gracefully with MSVC. :-(
**
** Apparently MSVC doesn't call C++ destructors for foreign exceptions
** unless you compile your C++ code with /EHa. Unfortunately this means
** catch (...) also catches things like access violations. The use of
** _set_se_translator doesn't really help, because it requires /EHa, too.
*/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#if LJ_TARGET_X86
typedef void *UndocumentedDispatcherContext;  /* Unused on x86. */
#else
/* Taken from: http://www.nynaeve.net/?p=99 */
typedef struct UndocumentedDispatcherContext {
  ULONG64 ControlPc;
  ULONG64 ImageBase;
  PRUNTIME_FUNCTION FunctionEntry;
  ULONG64 EstablisherFrame;
  ULONG64 TargetIp;
  PCONTEXT ContextRecord;
  void (*LanguageHandler)(void);
  PVOID HandlerData;
  PUNWIND_HISTORY_TABLE HistoryTable;
  ULONG ScopeIndex;
  ULONG Fill0;
} UndocumentedDispatcherContext;
#endif

/* Another wild guess. */
extern void __DestructExceptionObject(EXCEPTION_RECORD *rec, int nothrow);

#if LJ_TARGET_X64 && defined(MINGW_SDK_INIT)
/* Workaround for broken MinGW64 declaration. */
VOID RtlUnwindEx_FIXED(PVOID,PVOID,PVOID,PVOID,PVOID,PVOID) asm("RtlUnwindEx");
#define RtlUnwindEx RtlUnwindEx_FIXED
#endif

#define LJ_MSVC_EXCODE		((DWORD)0xe06d7363)
#define LJ_GCC_EXCODE		((DWORD)0x20474343)

#define LJ_EXCODE		((DWORD)0xe24c4a00)
#define LJ_EXCODE_MAKE(c)	(LJ_EXCODE | (DWORD)(c))
#define LJ_EXCODE_CHECK(cl)	(((cl) ^ LJ_EXCODE) <= 0xff)
#define LJ_EXCODE_ERRCODE(cl)	((int)((cl) & 0xff))

/* Windows exception handler for interpreter frame. */
LJ_FUNCA int lj_err_unwind_win(EXCEPTION_RECORD *rec,
  void *f, CONTEXT *ctx, UndocumentedDispatcherContext *dispatch)
{
#if LJ_TARGET_X86
  void *cf = (char *)f - CFRAME_OFS_SEH;
#else
  void *cf = f;
#endif
  lua_State *L = cframe_L(cf);
  int errcode = LJ_EXCODE_CHECK(rec->ExceptionCode) ?
		LJ_EXCODE_ERRCODE(rec->ExceptionCode) : LUA_ERRRUN;
  if ((rec->ExceptionFlags & 6)) {  /* EH_UNWINDING|EH_EXIT_UNWIND */
    /* Unwind internal frames. */
    err_unwind(L, cf, errcode);
  } else {
    void *cf2 = err_unwind(L, cf, 0);
    if (cf2) {  /* We catch it, so start unwinding the upper frames. */
      if (rec->ExceptionCode == LJ_MSVC_EXCODE ||
	  rec->ExceptionCode == LJ_GCC_EXCODE) {
#if !LJ_TARGET_CYGWIN
	__DestructExceptionObject(rec, 1);
#endif
	setstrV(L, L->top++, lj_err_str(L, LJ_ERR_ERRCPP));
      } else if (!LJ_EXCODE_CHECK(rec->ExceptionCode)) {
	/* Don't catch access violations etc. */
	return 1;  /* ExceptionContinueSearch */
      }
#if LJ_TARGET_X86
      UNUSED(ctx);
      UNUSED(dispatch);
      /* Call all handlers for all lower C frames (including ourselves) again
      ** with EH_UNWINDING set. Then call the specified function, passing cf
      ** and errcode.
      */
      lj_vm_rtlunwind(cf, (void *)rec,
	(cframe_unwind_ff(cf2) && errcode != LUA_YIELD) ?
	(void *)lj_vm_unwind_ff : (void *)lj_vm_unwind_c, errcode);
      /* lj_vm_rtlunwind does not return. */
#else
      /* Unwind the stack and call all handlers for all lower C frames
      ** (including ourselves) again with EH_UNWINDING set. Then set
      ** stack pointer = cf, result = errcode and jump to the specified target.
      */
      RtlUnwindEx(cf, (void *)((cframe_unwind_ff(cf2) && errcode != LUA_YIELD) ?
			       lj_vm_unwind_ff_eh :
			       lj_vm_unwind_c_eh),
		  rec, (void *)(uintptr_t)errcode, ctx, dispatch->HistoryTable);
      /* RtlUnwindEx should never return. */
#endif
    }
  }
  return 1;  /* ExceptionContinueSearch */
}

#if LJ_UNWIND_JIT

#if LJ_TARGET_X64
#define CONTEXT_REG_PC	Rip
#elif LJ_TARGET_ARM64
#define CONTEXT_REG_PC	Pc
#else
#error "NYI: Windows arch-specific unwinder for JIT-compiled code"
#endif

/* Windows unwinder for JIT-compiled code. */
static void err_unwind_win_jit(global_State *g, int errcode)
{
  CONTEXT ctx;
  UNWIND_HISTORY_TABLE hist;

  memset(&hist, 0, sizeof(hist));
  RtlCaptureContext(&ctx);
  while (1) {
    DWORD64 frame, base, addr = ctx.CONTEXT_REG_PC;
    void *hdata;
    PRUNTIME_FUNCTION func = RtlLookupFunctionEntry(addr, &base, &hist);
    if (!func) {  /* Found frame without .pdata: must be JIT-compiled code. */
      ExitNo exitno;
      uintptr_t stub = lj_trace_unwind(G2J(g), (uintptr_t)(addr - sizeof(MCode)), &exitno);
      if (stub) {  /* Jump to side exit to unwind the trace. */
	ctx.CONTEXT_REG_PC = stub;
	G2J(g)->exitcode = errcode;
	RtlRestoreContext(&ctx, NULL);  /* Does not return. */
      }
      break;
    }
    RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, addr, func,
		     &ctx, &hdata, &frame, NULL);