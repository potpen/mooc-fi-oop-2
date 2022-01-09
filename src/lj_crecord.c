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
  tr = emitir(IRT(IR_FLOAD, IRT_INT), tr, IRFL_CDATA_INT);
  emitir(IRTG(IR_EQ, IRT_INT), tr, lj_ir_kint(J, (int32_t)id));
  return id;
}

static CTypeID argv2ctype(jit_State *J, TRef tr, cTValue *o)
{
  if (tref_isstr(tr)) {
    GCstr *s = strV(o);
    CPState cp;
    CTypeID oldtop;
    /* Specialize to the string containing the C type declaration. */
    emitir(IRTG(IR_EQ, IRT_STR), tr, lj_ir_kstr(J, s));
    cp.L = J->L;
    cp.cts = ctype_cts(J->L);
    oldtop = cp.cts->top;
    cp.srcname = strdata(s);
    cp.p = strdata(s);
    cp.param = NULL;
    cp.mode = CPARSE_MODE_ABSTRACT|CPARSE_MODE_NOIMPLICIT;
    if (lj_cparse(&cp) || cp.cts->top > oldtop)  /* Avoid new struct defs. */
      lj_trace_err(J, LJ_TRERR_BADTYPE);
    return cp.val.id;
  } else {
    GCcdata *cd = argv2cdata(J, tr, o);
    return cd->ctypeid == CTID_CTYPEID ? crec_constructor(J, cd, tr) :
					cd->ctypeid;
  }
}

/* Convert CType to IRType (if possible). */
static IRType crec_ct2irt(CTState *cts, CType *ct)
{
  if (ctype_isenum(ct->info)) ct = ctype_child(cts, ct);
  if (LJ_LIKELY(ctype_isnum(ct->info))) {
    if ((ct->info & CTF_FP)) {
      if (ct->size == sizeof(double))
	return IRT_NUM;
      else if (ct->size == sizeof(float))
	return IRT_FLOAT;
    } else {
      uint32_t b = lj_fls(ct->size);
      if (b <= 3)
	return IRT_I8 + 2*b + ((ct->info & CTF_UNSIGNED) ? 1 : 0);
    }
  } else if (ctype_isptr(ct->info)) {
    return (LJ_64 && ct->size == 8) ? IRT_P64 : IRT_P32;
  } else if (ctype_iscomplex(ct->info)) {
    if (ct->size == 2*sizeof(double))
      return IRT_NUM;
    else if (ct->size == 2*sizeof(float))
      return IRT_FLOAT;
  }
  return IRT_CDATA;
}

/* -- Optimized memory fill and copy -------------------------------------- */

/* Maximum length and unroll of inlined copy/fill. */
#define CREC_COPY_MAXUNROLL		16
#define CREC_COPY_MAXLEN		128

#define CREC_FILL_MAXUNROLL		16

/* Number of windowed registers used for optimized memory copy. */
#if LJ_TARGET_X86
#define CREC_COPY_REGWIN		2
#elif LJ_TARGET_PPC || LJ_TARGET_MIPS
#define CREC_COPY_REGWIN		8
#else
#define CREC_COPY_REGWIN		4
#endif

/* List of memory offsets for copy/fill. */
typedef struct CRecMemList {
  CTSize ofs;		/* Offset in bytes. */
  IRType tp;		/* Type of load/store. */
  TRef trofs;		/* TRef of interned offset. */
  TRef trval;		/* TRef of load value. */
} CRecMemList;

/* Generate copy list for element-wise struct copy. */
static MSize crec_copy_struct(CRecMemList *ml, CTState *cts, CType *ct)
{
  CTypeID fid = ct->sib;
  MSize mlp = 0;
  while (fid) {
    CType *df = ctype_get(cts, fid);
    fid = df->sib;
    if (ctype_isfield(df->info)) {
      CType *cct;
      IRType tp;
      if (!gcref(df->name)) continue;  /* Ignore unnamed fields. */
      cct = ctype_rawchild(cts, df);  /* Field type. */
      tp = crec_ct2irt(cts, cct);
    