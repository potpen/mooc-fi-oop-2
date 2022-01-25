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
      if (tp == IRT_CDATA) return 0;  /* NYI: aggregates. */
      if (mlp >= CREC_COPY_MAXUNROLL) return 0;
      ml[mlp].ofs = df->size;
      ml[mlp].tp = tp;
      mlp++;
      if (ctype_iscomplex(cct->info)) {
	if (mlp >= CREC_COPY_MAXUNROLL) return 0;
	ml[mlp].ofs = df->size + (cct->size >> 1);
	ml[mlp].tp = tp;
	mlp++;
      }
    } else if (!ctype_isconstval(df->info)) {
      /* NYI: bitfields and sub-structures. */
      return 0;
    }
  }
  return mlp;
}

/* Generate unrolled copy list, from highest to lowest step size/alignment. */
static MSize crec_copy_unroll(CRecMemList *ml, CTSize len, CTSize step,
			      IRType tp)
{
  CTSize ofs = 0;
  MSize mlp = 0;
  if (tp == IRT_CDATA) tp = IRT_U8 + 2*lj_fls(step);
  do {
    while (ofs + step <= len) {
      if (mlp >= CREC_COPY_MAXUNROLL) return 0;
      ml[mlp].ofs = ofs;
      ml[mlp].tp = tp;
      mlp++;
      ofs += step;
    }
    step >>= 1;
    tp -= 2;
  } while (ofs < len);
  return mlp;
}

/*
** Emit copy list with windowed loads/stores.
** LJ_TARGET_UNALIGNED: may emit unaligned loads/stores (not marked as such).
*/
static void crec_copy_emit(jit_State *J, CRecMemList *ml, MSize mlp,
			   TRef trdst, TRef trsrc)
{
  MSize i, j, rwin = 0;
  for (i = 0, j = 0; i < mlp; ) {
    TRef trofs = lj_ir_kintp(J, ml[i].ofs);
    TRef trsptr = emitir(IRT(IR_ADD, IRT_PTR), trsrc, trofs);
    ml[i].trval = emitir(IRT(IR_XLOAD, ml[i].tp), trsptr, 0);
    ml[i].trofs = trofs;
    i++;
    rwin += (LJ_SOFTFP32 && ml[i].tp == IRT_NUM) ? 2 : 1;
    if (rwin >= CREC_COPY_REGWIN || i >= mlp) {  /* Flush buffered stores. */
      rwin = 0;
      for ( ; j < i; j++) {
	TRef trdptr = emitir(IRT(IR_ADD, IRT_PTR), trdst, ml[j].trofs);
	emitir(IRT(IR_XSTORE, ml[j].tp), trdptr, ml[j].trval);
      }
    }
  }
}

/* Optimized memory copy. */
static void crec_copy(jit_State *J, TRef trdst, TRef trsrc, TRef trlen,
		      CType *ct)
{
  if (tref_isk(trlen)) {  /* Length must be constant. */
    CRecMemList ml[CREC_COPY_MAXUNROLL];
    MSize mlp = 0;
    CTSize step = 1, len = (CTSize)IR(tref_ref(trlen))->i;
    IRType tp = IRT_CDATA;
    int needxbar = 0;
    if (len == 0) return;  /* Shortcut. */
    if (len > CREC_COPY_MAXLEN) goto fallback;
    if (ct) {
      CTState *cts = ctype_ctsG(J2G(J));
      lj_assertJ(ctype_isarray(ct->info) || ctype_isstruct(ct->info),
		 "copy of non-aggregate");
      if (ctype_isarray(ct->info)) {
	CType *cct = ctype_rawchild(cts, ct);
	tp = crec_ct2irt(cts, cct);
	if (tp == IRT_CDATA) goto rawcopy;
	step = lj_ir_type_size[tp];
	lj_assertJ((len & (step-1)) == 0, "copy of fractional size");
      } else if ((ct->info & CTF_UNION)) {
	step = (1u << ctype_align(ct->info));
	goto rawcopy;
      } else {
	mlp = crec_copy_struct(ml, cts, ct);
	goto emitcopy;
      }
    } else {
    rawcopy:
      needxbar = 1;
      if (LJ_TARGET_UNALIGNED || step >= CTSIZE_PTR)
	step = CTSIZE_PTR;
    }
    mlp = crec_copy_unroll(ml, len, step, tp);
  emitcopy:
    if (mlp) {
      crec_copy_emit(J, ml, mlp, trdst, trsrc);
      if (needxbar)
	emitir(IRT(IR_XBAR, IRT_NIL), 0, 0);
      return;
    }
  }
fallback:
  /* Call memcpy. Always needs a barrier to disable alias analysis. */
  lj_ir_call(J, IRCALL_memcpy, trdst, trsrc, trlen);
  emitir(IRT(IR_XBAR, IRT_NIL), 0, 0);
}

/* Generate unrolled fill list, from highest to lowest step size/alignment. */
static MSize crec_fill_unroll(CRecMemList *ml, CTSize len, CTSize step)
{
  CTSize ofs = 0;
  MSize mlp = 0;
  IRType tp = IRT_U8 + 2*lj_fls(step);
  do {
    while (ofs + step <= len) {
      if (mlp >= CREC_COPY_MAXUNROLL) return 0;
      ml[mlp].ofs = ofs;
      ml[mlp].tp = tp;
      mlp++;
      ofs += step;
    }
    step >>= 1;
    tp -= 2;
  } while (ofs < len);
  return mlp;
}

/*
** Emit stores for fill list.
** LJ_TARGET_UNALIGNED: may emit unaligned stores (not marked as such).
*/
static void crec_fill_emit(jit_State *J, CRecMemList *ml, MSize mlp,
			   TRef trdst, TRef trfill)
{
  MSize i;
  for (i = 0; i < mlp; i++) {
    TRef trofs = lj_ir_kintp(J, ml[i].ofs);
    TRef trdptr = emitir(IRT(IR_ADD, IRT_PTR), trdst, trofs);
    emitir(IRT(IR_XSTORE, ml[i].tp), trdptr, trfill);
  }
}

/* Optimized memory fill. */
static void crec_fill(jit_State *J, TRef trdst, TRef trlen, TRef trfill,
		      CTSize step)
{
  if (tref_isk(trlen)) {  /* Length must be constant. */
    CRecMemList ml[CREC_FILL_MAXUNROLL];
    MSize mlp;
    CTSize len = (CTSize)IR(tref_ref(trlen))->i;
    if (len == 0) return;  /* Shortcut. */
    if (LJ_TARGET_UNALIGNED || step >= CTSIZE_PTR)
      step = CTSIZE_PTR;
    if (step * CREC_FILL_MAXUNROLL < len) goto fallback;
    mlp = crec_fill_unroll(ml, len, step);
    if (!mlp) goto fallback;
    if (tref_isk(trfill) || ml[0].tp != IRT_U8)
      trfill = emitconv(trfill, IRT_INT, IRT_U8, 0);
    if (ml[0].tp != IRT_U8) {  /* Scatter U8 to U16/U32/U64. */
      if (CTSIZE_PTR == 8 && ml[0].tp == IRT_U64) {
	if (tref_isk(trfill))  /* Pointless on x64 with zero-extended regs. */
	  trfill = emitconv(trfill, IRT_U64, IRT_U32, 0);
	trfill = emitir(IRT(IR_MUL, IRT_U64), trfill,
			lj_ir_kint64(J, U64x(01010101,01010101)));
      } else {
	trfill = emitir(IRTI(IR_MUL), trfill,
		   lj_ir_kint(J, ml[0].tp == IRT_U16 ? 0x0101 : 0x01010101));
      }
    }
    crec_fill_emit(J, ml, mlp, trdst, trfill);
  } else {
fallback:
    /* Call memset. Always needs a barrier to disable alias analysis. */
    lj_ir_call(J, IRCALL_memset, trdst, trfill, trlen);  /* Note: arg order! */
  }
  emitir(IRT(IR_XBAR, IRT_NIL), 0, 0);
}

/* -- Convert C type to C type -------------------------------------------- */

/*
** This code mirrors the code in lj_cconv.c. It performs the same steps
** for the trace recorder that lj_cconv.c does for the interpreter.
**
** One major difference is that we can get away with much fewer checks
** here. E.g. checks for casts, constness or correct types can often be
** omitted, even if they might fail. The interpreter subsequently throws
** an error, which aborts the trace.
**
** All operations are specialized to their C types, so the on-trace
** outcome must be the same as the outcome in the interpreter. If the
** interpreter doesn't throw an error, then the trace is correct, too.
** Care must be taken not to generate invalid (temporary) IR or to
** trigger asserts.
*/

/* Determine whether a passed number or cdata number is non-zero. */
static int crec_isnonzero(CType *s, void *p)
{
  if (p == (void *)0)
    return 0;
  if (p == (void *)1)
    return 1;
  if ((s->info & CTF_FP)) {
    if (s->size == sizeof(float))
      return (*(float *)p != 0);
    else
      return (*(double *)p != 0);
  } else {
    if (s->size == 1)
      return (*(uint8_t *)p != 0);
    else if (s->size == 2)
      return (*(uint16_t *)p != 0);
    else if (s->size == 4)
      return (*(uint32_t *)p != 0);
    else
      return (*(uint64_t *)p != 0);
  }
}

static TRef crec_ct_ct(jit_State *J, CType *d, CType *s, TRef dp, TRef sp,
		       void *svisnz)
{
  IRType dt = crec_ct2irt(ctype_ctsG(J2G(J)), d);
  IRType st = crec_ct2irt(ctype_ctsG(J2G(J)), s);
  CTSize dsize = d->size, ssize = s->size;
  CTInfo dinfo = d->info, sinfo = s->info;

  if (ctype_type(dinfo) > CT_MAYCONVERT || ctype_type(sinfo) > CT_MAYCONVERT)
    goto err_conv;

  /*
  ** Note: Unlike lj_cconv_ct_ct(), sp holds the _value_ of pointers and
  ** numbers up to 8 bytes. Otherwise sp holds a pointer.
  */

  switch (cconv_idx2(dinfo, sinfo)) {
  /* Destination is a bool. */
  case CCX(B, B):
    goto xstore;  /* Source operand is already normalized. */
  case CCX(B, I):
  case CCX(B, F):
    if (st != IRT_CDATA) {
      /* Specialize to the result of a comparison against 0. */
      TRef zero = (st == IRT_NUM  || st == IRT_FLOAT) ? lj_ir_knum(J, 0) :
		  (st == IRT_I64 || st == IRT_U64) ? lj_ir_kint64(J, 0) :
		  lj_ir_kint(J, 0);
      int isnz = crec_isnonzero(s, svisnz);
      emitir(IRTG(isnz ? IR_NE : IR_EQ, st), sp, zero);
      sp = lj_ir_kint(J, isnz);
      goto xstore;
    }
    goto err_nyi;

  /* Destination is an integer. */
  case CCX(I, B):
  case CCX(I, I):
  conv_I_I:
    if (dt == IRT_CDATA || st == IRT_CDATA) goto err_nyi;
    /* Extend 32 to 64 bit integer. */
    if (dsize == 8 && ssize < 8 && !(LJ_64 && (sinfo & CTF_UNSIGNED)))
      sp = emitconv(sp, dt, ssize < 4 ? IRT_INT : st,
		    (sinfo & CTF_UNSIGNED) ? 0 : IRCONV_SEXT);
    else if (dsize < 8 && ssize == 8)  /* Truncate from 64 bit integer. */
      sp = emitconv(sp, dsize < 4 ? IRT_INT : dt, st, 0);
    else if (st == IRT_INT)
      sp = lj_opt_narrow_toint(J, sp);
  xstore:
    if (dt == IRT_I64 || dt == IRT_U64) lj_needsplit(J);
    if (dp == 0) return sp;
    emitir(IRT(IR_XSTORE, dt), dp, sp);
    break;
  case CCX(I, C):
    sp = emitir(IRT(IR_XLOAD, st), sp, 0);  /* Load re. */
    /* fallthrough */
  case CCX(I, F):
    if (dt == IRT_CDATA || st == IRT_CDATA) goto err_nyi;
    sp = emitconv(sp, dsize < 4 ? IRT_INT : dt, st, IRCONV_ANY);
    goto xstore;
  case CCX(I, P):
  case CCX(I, A):
    sinfo = CTINFO(CT_NUM, CTF_UNSIGNED);
    ssize = CTSIZE_PTR;
    st = IRT_UINTP;
    if (((dsize ^ ssize) & 8) == 0) {  /* Must insert no-op type conversion. */
      sp = emitconv(sp, dsize < 4 ? IRT_INT : dt, IRT_PTR, 0);
      goto xstore;
    }
    goto conv_I_I;

  /* Destination is a floating-point number. */
  case CCX(F, B):
  case CCX(F, I):
  conv_F_I:
    if (dt == IRT_CDATA || st == IRT_CDATA) goto err_nyi;
    sp = emitconv(sp, dt, ssize < 4 ? IRT_INT : st, 0);
    goto xstore;
  case CCX(F, C):
    sp = emitir(IRT(IR_XLOAD, st), sp, 0);  /* Load re. */
    /* fallthrough */
  case CCX(F, F):
  conv_F_F:
    if (dt == IRT_CDATA || st == IRT_CDATA) goto err_nyi;
    if (dt != st) sp = emitconv(sp, dt, st, 0);
    goto xstore;

  /* Destination is a complex number. */
  case CCX(C, I):
  case CCX(C, F):
    {  /* Clear im. */
      TRef ptr = emitir(IRT(IR_ADD, IRT_PTR), dp, lj_ir_kintp(J, (dsize >> 1)));
      emitir(IRT(IR_XSTORE, dt), ptr, lj_ir_knum(J, 0));
    }
    /* Convert to re. */
    if ((sinfo & CTF_FP)) goto conv_F_F; else goto conv_F_I;

  case CCX(C, C):
    if (dt == IRT_CDATA || st == IRT_CDATA) goto err_nyi;
    {
      TRef re, im, ptr;
      re = emitir(IRT(IR_XLOAD, st), sp, 0);
      ptr = emitir(IRT(IR_ADD, IRT_PTR), sp, lj_ir_kintp(J, (ssize >> 1)));
      im = emitir(IRT(IR_XLOAD, st), ptr, 0);
      if (dt != st) {
	re = emitconv(re, dt, st, 0);
	im = emitconv(im, dt, st, 0);
      }
      emitir(IRT(IR_XSTORE, dt), dp, re);
      ptr = emitir(IRT(IR_ADD, IRT_PTR), dp, lj_ir_kintp(J, (dsize >> 1)));
      emitir(IRT(IR_XSTORE, dt), ptr, im);
    }
    break;

  /* Destination is a vector. */
  case CCX(V, I):
  case CCX(V, F):
  case CCX(V, C):
  case CCX(V, V):
    goto err_nyi;

  /* Destination is a pointer. */
  case CCX(P, P):
  case CCX(P, A):
  case CCX(P, S):
    /* There are only 32 bit pointers/addresses on 32 bit machines.
    ** Also ok on x64, since all 32 bit ops clear the upper part of the reg.
    */
    goto xstore;
  case CCX(P, I):
    if (st == IRT_CDATA) goto err_nyi;
    if (!LJ_64 && ssize == 8)  /* Truncate from 64 bit integer. */
      sp = emitconv(sp, IRT_U32, st, 0);
    goto xstore;
  case CCX(P, F):
    if (st == IRT_CDATA) goto err_nyi;
    /* The signed conversion is cheaper. x64 really has 47 bit pointers. */
    sp = emitconv(sp, (LJ_64 && dsize == 8) ? IRT_I64 : IRT_U32,
		  st, IRCONV_ANY);
    goto xstore;

  /* Destination is an array. */
  case CCX(A, A):
  /* Destination is a struct/union. */
  case CCX(S, S):
    if (dp == 0) goto err_conv;
    crec_copy(J, dp, sp, lj_ir_kint(J, dsize), d);
    break;

  default:
  err_conv:
  err_nyi:
    lj_trace_err(J, LJ_TRERR_NYICONV);
    break;
  }
  return 0;
}

/* -- Convert C type to TValue (load) ------------------------------------- */

static TRef crec_tv_ct(jit_State *J, CType *s, CTypeID sid, TRef sp)
{
  CTState *cts = ctype_ctsG(J2G(J));
  IRType t = crec_ct2irt(cts, s);
  CTInfo sinfo = s->info;
  if (ctype_isnum(sinfo)) {
    TRef tr;
    if (t == IRT_CDATA)
      goto err_nyi;  /* NYI: copyval of >64 bit integers. */
    tr = emitir(IRT(IR_XLOAD, t), sp, 0);
    if (t == IRT_FLOAT || t == IRT_U32) {  /* Keep uint32_t/float as numbers. */
      return emitconv(tr, IRT_NUM, t, 0);
    } else if (t == IRT_I64 || t == IRT_U64) {  /* Box 64 bit integer. */
      sp = tr;
      lj_needsplit(J);
    } else if ((sinfo & CTF_BOOL)) {
      /* Assume not equal to zero. Fixup and emit pending guard later. */
      lj_ir_set(J, IRTGI(IR_NE), tr, lj_ir_kint(J, 0));
      J->postproc = LJ_POST_FIXGUARD;
      return TREF_TRUE;
    } else {
      return tr;
    }
  } else if (ctype_isptr(sinfo) || ctype_isenum(sinfo)) {
    sp = emitir(IRT(IR_XLOAD, t), sp, 0);  /* Box pointers and enums. */
  } else if (ctype_isrefarray(sinfo) || ctype_isstruct(sinfo)) {
    cts->L = J->L;
    sid = lj_ctype_intern(cts, CTINFO_REF(sid), CTSIZE_PTR);  /* Create ref. */
  } else if (ctype_iscomplex(sinfo)) {  /* Unbox/box complex. */
    ptrdiff_t esz = (ptrdiff_t)(s->size >> 1);
    TRef ptr, tr1, tr2, dp;
    dp = emitir(IRTG(IR_CNEW, IRT_CDATA), lj_ir_kint(J, sid), TREF_NIL);
    tr1 = emitir(IRT(IR_XLOAD, t), sp, 0);
    ptr = emitir(IRT(IR_ADD, IRT_PTR), sp, lj_ir_kintp(J, esz));
    tr2 = emitir(IRT(IR_XLOAD, t), ptr, 0);
    ptr = emitir(IRT(IR_ADD, IRT_PTR), dp, lj_ir_kintp(J, sizeof(GCcdata)));
    emitir(IRT(IR_XSTORE, t), ptr, tr1);
    ptr = emitir(IRT(IR_ADD, IRT_PTR), dp, lj_ir_kintp(J, sizeof(GCcdata)+esz));
    emitir(IRT(IR_XSTORE, t), ptr, tr2);
    return dp;
  } else {
    /* NYI: copyval of vectors. */
  err_nyi:
    lj_trace_err(J, LJ_TRERR_NYICONV);
  }
  /* Box pointer, ref, enum or 64 bit integer. */
  return emitir(IRTG(IR_CNEWI, IRT_CDATA), lj_ir_kint(J, sid), sp);
}

/* -- Convert TValue to C type (store) ------------------------------------ */

static TRef crec_ct_tv(jit_State *J, CType *d, TRef dp, TRef sp, cTValue *sval)
{
  CTState *cts = ctype_ctsG(J2G(J));
  CTypeID sid = CTID_P_VOID;
  void *svisnz = 0;
  CType *s;
  if (LJ_LIKELY(tref_isinteger(sp))) {
    sid = CTID_INT32;
    svisnz = (void *)(intptr_t)(tvisint(sval)?(intV(sval)!=0):!tviszero(sval));
  } else if (tref_isnum(sp)) {
    sid = CTID_DOUBLE;
    svisnz = (void *)(intptr_t)(tvisint(sval)?(intV(sval)!=0):!tviszero(sval));
  } else if (tref_isbool(sp)) {
    sp = lj_ir_kint(J, tref_istrue(sp) ? 1 : 0);
    sid = CTID_BOOL;
  } else if (tref_isnil(sp)) {
    sp = lj_ir_kptr(J, NULL);
  } else if (tref_isudata(sp)) {
    GCudata *ud = udataV(sval);
    if (ud->udtype == UDTYPE_IO_FILE || ud->udtype == UDTYPE_BUFFER) {
      TRef tr = emitir(IRT(IR_FLOAD, IRT_U8), sp, IRFL_UDATA_UDTYPE);
      emitir(IRTGI(IR_EQ), tr, lj_ir_kint(J, ud->udtype));
      sp = emitir(IRT(IR_FLOAD, IRT_PTR), sp,
		  ud->udtype == UDTYPE_IO_FILE ? IRFL_UDATA_FILE :
						 IRFL_SBUF_R);
    } else {
      sp = emitir(IRT(IR_ADD, IRT_PTR), sp, lj_ir_kintp(J, sizeof(GCudata)));
    }
  } else if (tref_isstr(sp)) {
    if (ctype_isenum(d->info)) {  /* Match string against enum constant. */
      GCstr *str = strV(sval);
      CTSize ofs;
      CType *cct = lj_ctype_getfield(cts, d, str, &ofs);
      /* Specialize to the name of the enum constant. */
      emitir(IRTG(IR_EQ, IRT_STR), sp, lj_ir_kstr(J, str));
      if (cct && ctype_isconstval(cct->info)) {
	lj_assertJ(ctype_child(cts, cct)->size == 4,
		   "only 32 bit const supported");  /* NYI */
	svisnz = (void *)(intptr_t)(ofs != 0);
	sp = lj_ir_kint(J, (int32_t)ofs);
	sid = ctype_cid(cct->info);
      }  /* else: interpreter will throw. */
    } else if (ctype_isrefarray(d->info)) {  /* Copy string to array. */
      lj_trac