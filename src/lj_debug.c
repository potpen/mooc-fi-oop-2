
/*
** Debugging and introspection.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

#define lj_debug_c
#define LUA_CORE

#include "lj_obj.h"
#include "lj_err.h"
#include "lj_debug.h"
#include "lj_buf.h"
#include "lj_tab.h"
#include "lj_state.h"
#include "lj_frame.h"
#include "lj_bc.h"
#include "lj_strfmt.h"
#if LJ_HASJIT
#include "lj_jit.h"
#endif

/* -- Frames -------------------------------------------------------------- */

/* Get frame corresponding to a level. */
cTValue *lj_debug_frame(lua_State *L, int level, int *size)
{
  cTValue *frame, *nextframe, *bot = tvref(L->stack)+LJ_FR2;
  /* Traverse frames backwards. */
  for (nextframe = frame = L->base-1; frame > bot; ) {
    if (frame_gc(frame) == obj2gco(L))
      level++;  /* Skip dummy frames. See lj_err_optype_call(). */
    if (level-- == 0) {
      *size = (int)(nextframe - frame);
      return frame;  /* Level found. */
    }
    nextframe = frame;
    if (frame_islua(frame)) {
      frame = frame_prevl(frame);
    } else {
      if (frame_isvarg(frame))
	level++;  /* Skip vararg pseudo-frame. */
      frame = frame_prevd(frame);
    }
  }
  *size = level;
  return NULL;  /* Level not found. */
}

/* Invalid bytecode position. */
#define NO_BCPOS	(~(BCPos)0)

/* Return bytecode position for function/frame or NO_BCPOS. */
static BCPos debug_framepc(lua_State *L, GCfunc *fn, cTValue *nextframe)
{
  const BCIns *ins;
  GCproto *pt;
  BCPos pos;
  lj_assertL(fn->c.gct == ~LJ_TFUNC || fn->c.gct == ~LJ_TTHREAD,
	     "function or frame expected");
  if (!isluafunc(fn)) {  /* Cannot derive a PC for non-Lua functions. */
    return NO_BCPOS;
  } else if (nextframe == NULL) {  /* Lua function on top. */
    void *cf = cframe_raw(L->cframe);
    if (cf == NULL || (char *)cframe_pc(cf) == (char *)cframe_L(cf))
      return NO_BCPOS;
    ins = cframe_pc(cf);  /* Only happens during error/hook handling. */
  } else {
    if (frame_islua(nextframe)) {
      ins = frame_pc(nextframe);
    } else if (frame_iscont(nextframe)) {
      ins = frame_contpc(nextframe);
    } else {
      /* Lua function below errfunc/gc/hook: find cframe to get the PC. */
      void *cf = cframe_raw(L->cframe);
      TValue *f = L->base-1;
      for (;;) {
	if (cf == NULL)
	  return NO_BCPOS;
	while (cframe_nres(cf) < 0) {
	  if (f >= restorestack(L, -cframe_nres(cf)))
	    break;
	  cf = cframe_raw(cframe_prev(cf));
	  if (cf == NULL)
	    return NO_BCPOS;
	}
	if (f < nextframe)
	  break;
	if (frame_islua(f)) {
	  f = frame_prevl(f);
	} else {
	  if (frame_isc(f) || (frame_iscont(f) && frame_iscont_fficb(f)))
	    cf = cframe_raw(cframe_prev(cf));
	  f = frame_prevd(f);
	}
      }
      ins = cframe_pc(cf);
      if (!ins) return NO_BCPOS;
    }
  }
  pt = funcproto(fn);
  pos = proto_bcpos(pt, ins) - 1;
#if LJ_HASJIT
  if (pos > pt->sizebc) {  /* Undo the effects of lj_trace_exit for JLOOP. */
    if (bc_isret(bc_op(ins[-1]))) {
      GCtrace *T = (GCtrace *)((char *)(ins-1) - offsetof(GCtrace, startins));
      pos = proto_bcpos(pt, mref(T->startpc, const BCIns));
    } else {
      pos = NO_BCPOS;  /* Punt in case of stack overflow for stitched trace. */
    }
  }
#endif
  return pos;
}

/* -- Line numbers -------------------------------------------------------- */

/* Get line number for a bytecode position. */
BCLine LJ_FASTCALL lj_debug_line(GCproto *pt, BCPos pc)
{
  const void *lineinfo = proto_lineinfo(pt);
  if (pc <= pt->sizebc && lineinfo) {
    BCLine first = pt->firstline;
    if (pc == pt->sizebc) return first + pt->numline;
    if (pc-- == 0) return first;
    if (pt->numline < 256)
      return first + (BCLine)((const uint8_t *)lineinfo)[pc];
    else if (pt->numline < 65536)
      return first + (BCLine)((const uint16_t *)lineinfo)[pc];
    else
      return first + (BCLine)((const uint32_t *)lineinfo)[pc];
  }
  return 0;
}

/* Get line number for function/frame. */
static BCLine debug_frameline(lua_State *L, GCfunc *fn, cTValue *nextframe)
{
  BCPos pc = debug_framepc(L, fn, nextframe);