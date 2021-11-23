/*
** C type conversions.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

#include "lj_obj.h"

#if LJ_HASFFI

#include "lj_err.h"
#include "lj_buf.h"
#include "lj_tab.h"
#include "lj_ctype.h"
#include "lj_cdata.h"
#include "lj_cconv.h"
#include "lj_ccallback.h"

/* -- Conversion errors --------------------------------------------------- */

/* Bad conversion. */
LJ_NORET static void cconv_err_conv(CTState *cts, CType *d, CType *s,
				    CTInfo flags)
{
  const char *dst = strdata(lj_ctype_repr(cts->L, ctype_typeid(cts, d), NULL));
  const char *src;
  if ((flags & CCF_FROMTV))
    src = lj_obj_typename[1+(ctype_isnum(s->info) ? LUA_TNUMBER :
			     ctype_isarray(s->info) ? LUA_TSTRING : LUA_TNIL)];
  else
    src = strdata(lj_ctype_repr(cts->L, ctype_typeid(cts, s), NULL));
  if (CCF_GETARG(flags))
    lj_err_argv(cts->L, CCF_GETARG(flags), LJ_ERR_FFI_BADCONV, src, dst);
  else
    lj_err_callerv(cts->L, LJ_ERR_FFI_BADCONV, src, dst);
}

/* Bad conversion from TValue. */
LJ_NORET static void cconv_err_convtv(CTState *cts, CType *d, TValue *o,
				      CTInfo flags)
{
  const char *dst = strdata(lj_ctype_repr(cts->L, ctype_typeid(cts, d), NULL));
  const char *src = lj_typename(o);
  if (CCF_GETARG(flags))
    lj_err_argv(cts->L, CCF_GETARG(flags), LJ_ERR_FFI_BADCONV, src, dst);
  else
    lj_err_callerv(cts->L, LJ_ERR_FFI_BADCONV, src, dst);
}

/* Initializer overflow. */
LJ_NORET static void cconv_err_initov(CTState *cts, CType *d)
{
  const char *dst = strdata(lj_ctype_repr(cts->L, ctype_typeid(cts, d), NULL));
  lj_err_callerv(cts->L, LJ_ERR_FFI_INITOV, dst);
}

/* -- C type compatibility checks ----------------------------------------- */

/* Get raw type and qualifiers for a child type. Resolves enums, too. */
static CType *cconv_childqual(CTState *cts, CType *ct, CTInfo *qual)
{
  ct = ctype_child(cts, ct);
  for (;;) {
    if (ctype_isattrib(ct->info)) {
      if (ctype_attrib(ct->info) == CTA_QUAL) *qual |= ct->size;
    } else if (!ctype_isenum(ct->info)) {
      break;
    }
    ct = ctype_child(cts, ct);
  }
  *qual |= (ct->info & CTF_QUAL);
  return ct;
}

/* Check for compatible types when converting to a pointer.
** Note: these checks are more relaxed than what C99 mandates.
*/
int lj_cconv_compatptr(CTState *cts, CType *d, CType *s, CTInfo flags)
{
  if (!((flags & CCF_CAST) || d == s)) {
    CTInfo dqual = 0, squal = 0;
    d = cconv_childqual(cts, d, &dqual);
    if (!ctype_isstruct(s->info))
      s = cconv_childqual(cts, s, &squal);
    if ((flags & CCF_SAME)) {
      if (dqual != squal)
	return 0;  /* Different qualifiers. */
    } else if (!(flags & CCF_IGNQUAL)) {
      if ((dqual & squal) != squal)
	return 0;  /* Discarded qualifiers. */
      if (ctype_isvoid(d->info) || ctype_isvoid(s->info))
	return 1;  /* Converting to/from void * is always ok. */
    }
    if (ctype_type(d->info) != ctype_type(s->info) ||
	d->size != s->size)
      return 0;  /* Different type or different size. */
    if (ctype_isnum(d->info)) {
      if (((d->info ^ s->info) & (CTF_BOOL|CTF_FP)))
	return 0;  /* Different numeric types. */
    } else if (ctype_ispointer(d->info)) {
      /* Check child types for compatibility. */
      return lj_cconv_compatptr(cts, d, s, flags|CCF_SAME);
    } else if (ctype_isstruct(d->info)) {
      if (d != s)
	return 0;  /* Must be exact same type for struct/union. */
    } else if (ctype_isfunc(d->info)) {
      /* NYI: structural equality of functions. */
    }
  }
  return 1;  /* Types are compatible. */
}

/* -- C type to C type conversion ----------------------------------------- */

/* Convert C type to C type. Caveat: expects to get the raw CType!
**
** Note: This is only used by the interpreter and not optimized at all.
** The JIT compiler will do a much better job specializing for each case.
*/
void lj_cconv_ct_ct(CTState *cts, CType *d, CType *s,
		    uint8_t *dp, uint8_t *sp, CTInfo flags)
{
  CTSize dsize = d->size, ssize = s->size;
  CTInfo dinfo = d->info, sinfo = s->info;
  void *tmpptr;

  lj_assertCTS(!ctype_isenum(dinfo) && !ctype_isenum(sinfo),
	       "unresolved enum");
  lj_assertCTS(!ctype_isattrib(dinfo) && !ctype_isattrib(sinfo),
	       "unstripped attribute");

  if (ctype_type(dinfo) > CT_MAYCONVERT || ctype_type(sinfo) > CT_MAYCONVERT)
    goto err_conv;

  /* Some basic sanity checks. */
  lj_assertCTS(!ctype_isnum(dinfo) || dsize > 0, "bad size for number type");
  lj_assertCTS(!ctype_isnum(sinfo) || ssize > 0, "bad size for number type");
  lj_assertCTS(!ctype_isbool(dinfo) || dsize == 1 || dsize == 4,
	       "bad size for bool type");
  lj_assertCTS(!ctype_isbool(sinfo) || ssize == 1 || ssize == 4,
	       "bad size for bool type");
  lj_assertCTS(!ctype_isinteger(dinfo) || (1u<<lj_fls(dsize)) == dsize,
	       "bad size for integer type");
  lj_assertCTS(!ctype_isinteger(sinfo) || (1u<<lj_fls(ssize)) == ssize,
	       "bad size for integer type");

  switch (cconv_idx2(dinfo, sinfo)) {
  /* Destination is a bool. */
  case CCX(B, B):
    /* Source operand is already normalized. */
    if (dsize == 1) *dp = *sp; else *(int *)dp = *sp;
    break;
  case CCX(B, I): {
    MSize i;
    uint8_t b = 0;
    for (i = 0; i < ssize; i++) b |= sp[i];
    b = (b != 0);
    if (dsize == 1) *dp = b; else *(int *)dp = b;
    break;
    }
  case CCX(B, F): {
    uint8_t b;
    if (ssize == sizeof(double)) b = (*(double *)sp != 0);
    else if (ssize == sizeof(float)) b = (*(float *)sp != 0);
    else goto err_conv;  /* NYI: long double. */
    if (dsize == 1) *dp = b; else *(int *)dp = b;
    break;
    }

  /* Destination is an integer. */
  case CCX(I, B):
  case CCX(I, I):
  conv_I_I:
    if (dsize > ssize) {  /* Zero-extend or sign-extend LSB. */
#if LJ_LE
      uint8_t fill = (!(sinfo & CTF_UNSIGNED) && (sp[ssize-1]&0x80)) ? 0xff : 0;
      memcpy(dp, sp, ssize);
      memset(dp + ssize, fill, dsize-ssize);
#else
      uint8_t fill = (!(sinfo & CTF_UNSIGNED) && (sp[0]&0x80)) ? 0xff : 0;
      memset(dp, fill, dsize-ssize);
      memcpy(dp + (dsize-ssize), sp, ssize);
#endif
    } else {  /* Copy LSB. */
#if LJ_LE
      memcpy(dp, sp, dsize);
#else
      memcpy(dp, sp + (ssize-dsize), dsize);
#endif
    }
    break;
  case CCX(I, F): {
    double n;  /* Always convert via double. */
  conv_I_F:
    /* Convert source to double. */
    if (ssize == sizeof(double)) n = *(double *)sp;
    else if (ssize == sizeof(float)) n = (double)*(float *)sp;
    else goto err_conv;  /* NYI: long double. */
    /* Then convert double to integer. */
    /* The conversion must exactly match the semantics of JIT-compiled code! */
    if (dsize < 4 || (dsize == 4 && !(dinfo & CTF_UNSIGNED))) {
      int32_t i = (int32_t)n;
      if (dsize == 4) *(int32_t *)dp = i;
      else if (dsize == 2) *(int16_t *)dp = (int16_t)i;
      else *(int8_t *)dp = (int8_t)i;
    } else if (dsize == 4) {
      *(uint32_t *)dp = (uint32_t)n;
    } else if (dsize == 8) {
      if (!(dinfo & CTF_UNSIGNED))
	*(int64_t *)dp = (int64_t)n;
      else
	*(uint64_t *)dp = lj_num2u64(n);
    } else {
      goto err_conv;  /* NYI: conversion to >64 bit integers. */
    }
    break;
    }
  case CCX(I, C):
    s = ctype_child(cts, s);
    sinfo = s->info;
    ssize = s->size;
    goto conv_I_F;  /* Just convert re. */
  case CCX(I, P):
    if (!(flags & CCF_CAST)) goto err_conv;
    sinfo = CTINFO(CT_NUM, CTF_UNSIGNED);
    goto conv_I_I;
  case CCX(I, A):
    if (!(flags & CCF_CAST)) goto err_conv;
    sinfo = CTINFO(CT_NUM, CTF_UNSIGNED);
    ssize = CTSIZE_PTR;
    tmpptr = sp;
    sp = (uint8_t *)&tmpptr;
    goto conv_I_I;

  /* Destination is a floating-point number. */
  case C