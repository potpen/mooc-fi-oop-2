/*
** Trace recorder for C data operations.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

#define lj_ffrecord_c
#define LUA_CORE

#include "lj_obj.h"

#if LJ_HASJIT && LJ_HASFFI

#include "lj_err.h"
#include "lj_tab.h"
#include "lj_frame.h"
#include "lj_ctype.h"
#include "lj_cdata.h"
#include "lj_cparse.h"
#include "lj_cconv.h"
#include "lj_carith.h"
#include "lj_clib.h"
#include "lj_ccall.h"
#include "lj_ff.h"
#include "lj_ir.h"
#include "lj_jit.h"
#include "lj_ircall.h"
#include "lj_iropt.h"
#include "lj_trace.h"
#include "lj_record.h"
#include "lj_ffrecord.h"
#include "lj_snap.h"
#include "lj_crecord.h"
#include "lj_dispatch.h"
#include "lj_strfmt.h"

/* Some local macros to save typing. Undef'd at the end. */
#define IR(ref)			(&J->cur.ir[(ref)])

/* Pass IR on to next optimization in chain (FOLD). */
#define emitir(ot, a, b)	(lj_ir_set(J, (ot), (a), (b)), lj_opt_fold(J))

#define emitconv(a, dt, st, flags) \
  emitir(IRT(IR_CONV, (dt)), (a), (st)|((dt) << 5)|(flags))

/* -- C type checks ------------------------------------------------------- */

static GCcdata *argv2cdata(jit_State *J, TRef tr, cTValue *o)
{
  GCcdata *cd;
  TRef trtypeid;
  if (!tref_iscdata(tr))
    lj_trace_err(J, LJ_TRERR_BADTYPE);
  cd = cdataV(o);
  /* Specialize to the CTypeID. */
  trtypeid = emitir(IRT(IR_FLOAD, IRT_U16), tr, IRFL_CDATA_CTYPEID);
  emitir(IRTG(IR_EQ, IRT_INT), trtypeid, lj_ir_kint(J, (int32_t)cd->ctypeid));
  return cd;
}

/* Specialize to the CTypeID held by a cdata constructor. */
static CTypeID crec_constructor(jit_State *J, GCcdata *cd, TRef tr)
{
  CTypeID id;
  lj_assertJ(tref_iscdata(tr) && cd->ctypeid == CTID_CTYPEID,
	     "expected CTypeID cdata");
  id = *(CTypeID *)cdataptr(cd);
  tr = emitir(IRT(IR_FLOAD, IRT_INT), tr, IRFL