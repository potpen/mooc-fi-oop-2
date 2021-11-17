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
    } else if (cty