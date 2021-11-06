
/*
** FFI C call handling.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

#include "lj_obj.h"

#if LJ_HASFFI

#include "lj_gc.h"
#include "lj_err.h"
#include "lj_tab.h"
#include "lj_ctype.h"
#include "lj_cconv.h"
#include "lj_cdata.h"
#include "lj_ccall.h"
#include "lj_trace.h"

/* Target-specific handling of register arguments. */
#if LJ_TARGET_X86
/* -- x86 calling conventions --------------------------------------------- */

#if LJ_ABI_WIN

#define CCALL_HANDLE_STRUCTRET \
  /* Return structs bigger than 8 by reference (on stack only). */ \
  cc->retref = (sz > 8); \
  if (cc->retref) cc->stack[nsp++] = (GPRArg)dp;

#define CCALL_HANDLE_COMPLEXRET CCALL_HANDLE_STRUCTRET

#else

#if LJ_TARGET_OSX

#define CCALL_HANDLE_STRUCTRET \
  /* Return structs of size 1, 2, 4 or 8 in registers. */ \
  cc->retref = !(sz == 1 || sz == 2 || sz == 4 || sz == 8); \
  if (cc->retref) { \
    if (ngpr < maxgpr) \
      cc->gpr[ngpr++] = (GPRArg)dp; \
    else \
      cc->stack[nsp++] = (GPRArg)dp; \
  } else {  /* Struct with single FP field ends up in FPR. */ \
    cc->resx87 = ccall_classify_struct(cts, ctr); \
  }

#define CCALL_HANDLE_STRUCTRET2 \
  if (cc->resx87) sp = (uint8_t *)&cc->fpr[0]; \
  memcpy(dp, sp, ctr->size);

#else

#define CCALL_HANDLE_STRUCTRET \
  cc->retref = 1;  /* Return all structs by reference (in reg or on stack). */ \
  if (ngpr < maxgpr) \
    cc->gpr[ngpr++] = (GPRArg)dp; \
  else \
    cc->stack[nsp++] = (GPRArg)dp;

#endif

#define CCALL_HANDLE_COMPLEXRET \
  /* Return complex float in GPRs and complex double by reference. */ \
  cc->retref = (sz > 8); \
  if (cc->retref) { \
    if (ngpr < maxgpr) \
      cc->gpr[ngpr++] = (GPRArg)dp; \
    else \
      cc->stack[nsp++] = (GPRArg)dp; \
  }

#endif

#define CCALL_HANDLE_COMPLEXRET2 \
  if (!cc->retref) \
    *(int64_t *)dp = *(int64_t *)sp;  /* Copy complex float from GPRs. */

#define CCALL_HANDLE_STRUCTARG \
  ngpr = maxgpr;  /* Pass all structs by value on the stack. */

#define CCALL_HANDLE_COMPLEXARG \
  isfp = 1;  /* Pass complex by value on stack. */

#define CCALL_HANDLE_REGARG \
  if (!isfp) {  /* Only non-FP values may be passed in registers. */ \
    if (n > 1) {  /* Anything > 32 bit is passed on the stack. */ \
      if (!LJ_ABI_WIN) ngpr = maxgpr;  /* Prevent reordering. */ \
    } else if (ngpr + 1 <= maxgpr) { \
      dp = &cc->gpr[ngpr]; \
      ngpr += n; \
      goto done; \
    } \
  }

#elif LJ_TARGET_X64 && LJ_ABI_WIN
/* -- Windows/x64 calling conventions ------------------------------------- */

#define CCALL_HANDLE_STRUCTRET \
  /* Return structs of size 1, 2, 4 or 8 in a GPR. */ \
  cc->retref = !(sz == 1 || sz == 2 || sz == 4 || sz == 8); \
  if (cc->retref) cc->gpr[ngpr++] = (GPRArg)dp;

#define CCALL_HANDLE_COMPLEXRET CCALL_HANDLE_STRUCTRET

#define CCALL_HANDLE_COMPLEXRET2 \
  if (!cc->retref) \
    *(int64_t *)dp = *(int64_t *)sp;  /* Copy complex float from GPRs. */

#define CCALL_HANDLE_STRUCTARG \
  /* Pass structs of size 1, 2, 4 or 8 in a GPR by value. */ \
  if (!(sz == 1 || sz == 2 || sz == 4 || sz == 8)) { \
    rp = cdataptr(lj_cdata_new(cts, did, sz)); \
    sz = CTSIZE_PTR;  /* Pass all other structs by reference. */ \
  }

#define CCALL_HANDLE_COMPLEXARG \
  /* Pass complex float in a GPR and complex double by reference. */ \
  if (sz != 2*sizeof(float)) { \
    rp = cdataptr(lj_cdata_new(cts, did, sz)); \
    sz = CTSIZE_PTR; \
  }

/* Windows/x64 argument registers are strictly positional (use ngpr). */
#define CCALL_HANDLE_REGARG \
  if (isfp) { \
    if (ngpr < maxgpr) { dp = &cc->fpr[ngpr++]; nfpr = ngpr; goto done; } \
  } else { \
    if (ngpr < maxgpr) { dp = &cc->gpr[ngpr++]; goto done; } \
  }

#elif LJ_TARGET_X64
/* -- POSIX/x64 calling conventions --------------------------------------- */

#define CCALL_HANDLE_STRUCTRET \
  int rcl[2]; rcl[0] = rcl[1] = 0; \
  if (ccall_classify_struct(cts, ctr, rcl, 0)) { \
    cc->retref = 1;  /* Return struct by reference. */ \
    cc->gpr[ngpr++] = (GPRArg)dp; \
  } else { \
    cc->retref = 0;  /* Return small structs in registers. */ \
  }

#define CCALL_HANDLE_STRUCTRET2 \
  int rcl[2]; rcl[0] = rcl[1] = 0; \
  ccall_classify_struct(cts, ctr, rcl, 0); \
  ccall_struct_ret(cc, rcl, dp, ctr->size);

#define CCALL_HANDLE_COMPLEXRET \
  /* Complex values are returned in one or two FPRs. */ \
  cc->retref = 0;

#define CCALL_HANDLE_COMPLEXRET2 \
  if (ctr->size == 2*sizeof(float)) {  /* Copy complex float from FPR. */ \
    *(int64_t *)dp = cc->fpr[0].l[0]; \
  } else {  /* Copy non-contiguous complex double from FPRs. */ \
    ((int64_t *)dp)[0] = cc->fpr[0].l[0]; \
    ((int64_t *)dp)[1] = cc->fpr[1].l[0]; \
  }

#define CCALL_HANDLE_STRUCTARG \
  int rcl[2]; rcl[0] = rcl[1] = 0; \
  if (!ccall_classify_struct(cts, d, rcl, 0)) { \
    cc->nsp = nsp; cc->ngpr = ngpr; cc->nfpr = nfpr; \
    if (ccall_struct_arg(cc, cts, d, rcl, o, narg)) goto err_nyi; \
    nsp = cc->nsp; ngpr = cc->ngpr; nfpr = cc->nfpr; \
    continue; \
  }  /* Pass all other structs by value on stack. */

#define CCALL_HANDLE_COMPLEXARG \
  isfp = 2;  /* Pass complex in FPRs or on stack. Needs postprocessing. */

#define CCALL_HANDLE_REGARG \
  if (isfp) {  /* Try to pass argument in FPRs. */ \
    int n2 = ctype_isvector(d->info) ? 1 : n; \
    if (nfpr + n2 <= CCALL_NARG_FPR) { \
      dp = &cc->fpr[nfpr]; \
      nfpr += n2; \
      goto done; \
    } \
  } else {  /* Try to pass argument in GPRs. */ \
    /* Note that reordering is explicitly allowed in the x64 ABI. */ \
    if (n <= 2 && ngpr + n <= maxgpr) { \
      dp = &cc->gpr[ngpr]; \
      ngpr += n; \
      goto done; \
    } \
  }

#elif LJ_TARGET_ARM
/* -- ARM calling conventions --------------------------------------------- */

#if LJ_ABI_SOFTFP

#define CCALL_HANDLE_STRUCTRET \
  /* Return structs of size <= 4 in a GPR. */ \
  cc->retref = !(sz <= 4); \
  if (cc->retref) cc->gpr[ngpr++] = (GPRArg)dp;

#define CCALL_HANDLE_COMPLEXRET \
  cc->retref = 1;  /* Return all complex values by reference. */ \
  cc->gpr[ngpr++] = (GPRArg)dp;

#define CCALL_HANDLE_COMPLEXRET2 \
  UNUSED(dp); /* Nothing to do. */

#define CCALL_HANDLE_STRUCTARG \
  /* Pass all structs by value in registers and/or on the stack. */

#define CCALL_HANDLE_COMPLEXARG \
  /* Pass complex by value in 2 or 4 GPRs. */

#define CCALL_HANDLE_REGARG_FP1
#define CCALL_HANDLE_REGARG_FP2

#else

#define CCALL_HANDLE_STRUCTRET \
  cc->retref = !ccall_classify_struct(cts, ctr, ct); \
  if (cc->retref) cc->gpr[ngpr++] = (GPRArg)dp;

#define CCALL_HANDLE_STRUCTRET2 \
  if (ccall_classify_struct(cts, ctr, ct) > 1) sp = (uint8_t *)&cc->fpr[0]; \
  memcpy(dp, sp, ctr->size);

#define CCALL_HANDLE_COMPLEXRET \
  if (!(ct->info & CTF_VARARG)) cc->retref = 0;  /* Return complex in FPRs. */

#define CCALL_HANDLE_COMPLEXRET2 \
  if (!(ct->info & CTF_VARARG)) memcpy(dp, &cc->fpr[0], ctr->size);

#define CCALL_HANDLE_STRUCTARG \
  isfp = (ccall_classify_struct(cts, d, ct) > 1);
  /* Pass all structs by value in registers and/or on the stack. */

#define CCALL_HANDLE_COMPLEXARG \
  isfp = 1;  /* Pass complex by value in FPRs or on stack. */

#define CCALL_HANDLE_REGARG_FP1 \
  if (isfp && !(ct->info & CTF_VARARG)) { \
    if ((d->info & CTF_ALIGN) > CTALIGN_PTR) { \
      if (nfpr + (n >> 1) <= CCALL_NARG_FPR) { \
	dp = &cc->fpr[nfpr]; \
	nfpr += (n >> 1); \
	goto done; \
      } \
    } else { \
      if (sz > 1 && fprodd != nfpr) fprodd = 0; \
      if (fprodd) { \
	if (2*nfpr+n <= 2*CCALL_NARG_FPR+1) { \
	  dp = (void *)&cc->fpr[fprodd-1].f[1]; \
	  nfpr += (n >> 1); \
	  if ((n & 1)) fprodd = 0; else fprodd = nfpr-1; \
	  goto done; \
	} \
      } else { \
	if (2*nfpr+n <= 2*CCALL_NARG_FPR) { \
	  dp = (void *)&cc->fpr[nfpr]; \
	  nfpr += (n >> 1); \
	  if ((n & 1)) fprodd = ++nfpr; else fprodd = 0; \
	  goto done; \
	} \
      } \
    } \
    fprodd = 0;  /* No reordering after the first FP value is on stack. */ \
  } else {

#define CCALL_HANDLE_REGARG_FP2	}

#endif

#define CCALL_HANDLE_REGARG \
  CCALL_HANDLE_REGARG_FP1 \
  if ((d->info & CTF_ALIGN) > CTALIGN_PTR) { \
    if (ngpr < maxgpr) \
      ngpr = (ngpr + 1u) & ~1u;  /* Align to regpair. */ \
  } \
  if (ngpr < maxgpr) { \
    dp = &cc->gpr[ngpr]; \
    if (ngpr + n > maxgpr) { \
      nsp += ngpr + n - maxgpr;  /* Assumes contiguous gpr/stack fields. */ \
      if (nsp > CCALL_MAXSTACK) goto err_nyi;  /* Too many arguments. */ \
      ngpr = maxgpr; \
    } else { \
      ngpr += n; \
    } \
    goto done; \
  } CCALL_HANDLE_REGARG_FP2

#define CCALL_HANDLE_RET \
  if ((ct->info & CTF_VARARG)) sp = (uint8_t *)&cc->gpr[0];

#elif LJ_TARGET_ARM64
/* -- ARM64 calling conventions ------------------------------------------- */

#define CCALL_HANDLE_STRUCTRET \
  cc->retref = !ccall_classify_struct(cts, ctr); \
  if (cc->retref) cc->retp = dp;

#define CCALL_HANDLE_STRUCTRET2 \
  unsigned int cl = ccall_classify_struct(cts, ctr); \
  if ((cl & 4)) { /* Combine float HFA from separate registers. */ \
    CTSize i = (cl >> 8) - 1; \
    do { ((uint32_t *)dp)[i] = cc->fpr[i].lo; } while (i--); \
  } else { \
    if (cl > 1) sp = (uint8_t *)&cc->fpr[0]; \
    memcpy(dp, sp, ctr->size); \
  }

#define CCALL_HANDLE_COMPLEXRET \
  /* Complex values are returned in one or two FPRs. */ \
  cc->retref = 0;

#define CCALL_HANDLE_COMPLEXRET2 \
  if (ctr->size == 2*sizeof(float)) {  /* Copy complex float from FPRs. */ \
    ((float *)dp)[0] = cc->fpr[0].f; \
    ((float *)dp)[1] = cc->fpr[1].f; \
  } else {  /* Copy complex double from FPRs. */ \
    ((double *)dp)[0] = cc->fpr[0].d; \
    ((double *)dp)[1] = cc->fpr[1].d; \
  }

#define CCALL_HANDLE_STRUCTARG \
  unsigned int cl = ccall_classify_struct(cts, d); \
  if (cl == 0) {  /* Pass struct by reference. */ \
    rp = cdataptr(lj_cdata_new(cts, did, sz)); \
    sz = CTSIZE_PTR; \
  } else if (cl > 1) {  /* Pass struct in FPRs or on stack. */ \
    isfp = (cl & 4) ? 2 : 1; \
  }  /* else: Pass struct in GPRs or on stack. */

#define CCALL_HANDLE_COMPLEXARG \
  /* Pass complex by value in separate (!) FPRs or on stack. */ \
  isfp = sz == 2*sizeof(float) ? 2 : 1;

#define CCALL_HANDLE_REGARG \
  if (LJ_TARGET_OSX && isva) { \
    /* IOS: All variadic arguments are on the stack. */ \
  } else if (isfp) {  /* Try to pass argument in FPRs. */ \
    int n2 = ctype_isvector(d->info) ? 1 : \
	     isfp == 1 ? n : (d->size >> (4-isfp)); \
    if (nfpr + n2 <= CCALL_NARG_FPR) { \
      dp = &cc->fpr[nfpr]; \
      nfpr += n2; \
      goto done; \
    } else { \
      nfpr = CCALL_NARG_FPR;  /* Prevent reordering. */ \
      if (LJ_TARGET_OSX && d->size < 8) goto err_nyi; \
    } \
  } else {  /* Try to pass argument in GPRs. */ \
    if (!LJ_TARGET_OSX && (d->info & CTF_ALIGN) > CTALIGN_PTR) \
      ngpr = (ngpr + 1u) & ~1u;  /* Align to regpair. */ \
    if (ngpr + n <= maxgpr) { \
      dp = &cc->gpr[ngpr]; \
      ngpr += n; \
      goto done; \
    } else { \
      ngpr = maxgpr;  /* Prevent reordering. */ \
      if (LJ_TARGET_OSX && d->size < 8) goto err_nyi; \
    } \
  }

#if LJ_BE
#define CCALL_HANDLE_RET \
  if (ctype_isfp(ctr->info) && ctr->size == sizeof(float)) \
    sp = (uint8_t *)&cc->fpr[0].f;
#endif


#elif LJ_TARGET_PPC
/* -- PPC calling conventions --------------------------------------------- */

#define CCALL_HANDLE_STRUCTRET \
  cc->retref = 1;  /* Return all structs by reference. */ \
  cc->gpr[ngpr++] = (GPRArg)dp;

#define CCALL_HANDLE_COMPLEXRET \
  /* Complex values are returned in 2 or 4 GPRs. */ \
  cc->retref = 0;

#define CCALL_HANDLE_COMPLEXRET2 \
  memcpy(dp, sp, ctr->size);  /* Copy complex from GPRs. */

#define CCALL_HANDLE_STRUCTARG \
  rp = cdataptr(lj_cdata_new(cts, did, sz)); \
  sz = CTSIZE_PTR;  /* Pass all structs by reference. */

#define CCALL_HANDLE_COMPLEXARG \
  /* Pass complex by value in 2 or 4 GPRs. */

#define CCALL_HANDLE_GPR \
  /* Try to pass argument in GPRs. */ \
  if (n > 1) { \
    /* int64_t or complex (float). */ \
    lj_assertL(n == 2 || n == 4, "bad GPR size %d", n); \
    if (ctype_isinteger(d->info) || ctype_isfp(d->info)) \
      ngpr = (ngpr + 1u) & ~1u;  /* Align int64_t to regpair. */ \
    else if (ngpr + n > maxgpr) \
      ngpr = maxgpr;  /* Prevent reordering. */ \
  } \
  if (ngpr + n <= maxgpr) { \
    dp = &cc->gpr[ngpr]; \
    ngpr += n; \
    goto done; \
  } \

#if LJ_ABI_SOFTFP
#define CCALL_HANDLE_REGARG  CCALL_HANDLE_GPR
#else
#define CCALL_HANDLE_REGARG \
  if (isfp) {  /* Try to pass argument in FPRs. */ \
    if (nfpr + 1 <= CCALL_NARG_FPR) { \
      dp = &cc->fpr[nfpr]; \
      nfpr += 1; \
      d = ctype_get(cts, CTID_DOUBLE);  /* FPRs always hold doubles. */ \
      goto done; \
    } \
  } else { \
    CCALL_HANDLE_GPR \
  }
#endif

#if !LJ_ABI_SOFTFP
#define CCALL_HANDLE_RET \
  if (ctype_isfp(ctr->info) && ctr->size == sizeof(float)) \
    ctr = ctype_get(cts, CTID_DOUBLE);  /* FPRs always hold doubles. */
#endif

#elif LJ_TARGET_MIPS32
/* -- MIPS o32 calling conventions ---------------------------------------- */

#define CCALL_HANDLE_STRUCTRET \
  cc->retref = 1;  /* Return all structs by reference. */ \
  cc->gpr[ngpr++] = (GPRArg)dp;

#define CCALL_HANDLE_COMPLEXRET \
  /* Complex values are returned in 1 or 2 FPRs. */ \
  cc->retref = 0;

#if LJ_ABI_SOFTFP
#define CCALL_HANDLE_COMPLEXRET2 \
  if (ctr->size == 2*sizeof(float)) {  /* Copy complex float from GPRs. */ \
    ((intptr_t *)dp)[0] = cc->gpr[0]; \
    ((intptr_t *)dp)[1] = cc->gpr[1]; \
  } else {  /* Copy complex double from GPRs. */ \
    ((intptr_t *)dp)[0] = cc->gpr[0]; \
    ((intptr_t *)dp)[1] = cc->gpr[1]; \
    ((intptr_t *)dp)[2] = cc->gpr[2]; \
    ((intptr_t *)dp)[3] = cc->gpr[3]; \
  }
#else
#define CCALL_HANDLE_COMPLEXRET2 \
  if (ctr->size == 2*sizeof(float)) {  /* Copy complex float from FPRs. */ \
    ((float *)dp)[0] = cc->fpr[0].f; \
    ((float *)dp)[1] = cc->fpr[1].f; \
  } else {  /* Copy complex double from FPRs. */ \
    ((double *)dp)[0] = cc->fpr[0].d; \
    ((double *)dp)[1] = cc->fpr[1].d; \
  }
#endif

#define CCALL_HANDLE_STRUCTARG \
  /* Pass all structs by value in registers and/or on the stack. */

#define CCALL_HANDLE_COMPLEXARG \
  /* Pass complex by value in 2 or 4 GPRs. */

#define CCALL_HANDLE_GPR \
  if ((d->info & CTF_ALIGN) > CTALIGN_PTR) \
    ngpr = (ngpr + 1u) & ~1u;  /* Align to regpair. */ \
  if (ngpr < maxgpr) { \
    dp = &cc->gpr[ngpr]; \
    if (ngpr + n > maxgpr) { \
     nsp += ngpr + n - maxgpr;  /* Assumes contiguous gpr/stack fields. */ \
     if (nsp > CCALL_MAXSTACK) goto err_nyi;  /* Too many arguments. */ \
     ngpr = maxgpr; \
    } else { \
     ngpr += n; \
    } \
    goto done; \
  }

#if !LJ_ABI_SOFTFP	/* MIPS32 hard-float */
#define CCALL_HANDLE_REGARG \
  if (isfp && nfpr < CCALL_NARG_FPR && !(ct->info & CTF_VARARG)) { \
    /* Try to pass argument in FPRs. */ \
    dp = n == 1 ? (void *)&cc->fpr[nfpr].f : (void *)&cc->fpr[nfpr].d; \
    nfpr++; ngpr += n; \
    goto done; \
  } else {  /* Try to pass argument in GPRs. */ \
    nfpr = CCALL_NARG_FPR; \
    CCALL_HANDLE_GPR \
  }
#else			/* MIPS32 soft-float */
#define CCALL_HANDLE_REGARG CCALL_HANDLE_GPR
#endif

#if !LJ_ABI_SOFTFP
/* On MIPS64 soft-float, position of float return values is endian-dependant. */
#define CCALL_HANDLE_RET \
  if (ctype_isfp(ctr->info) && ctr->size == sizeof(float)) \
    sp = (uint8_t *)&cc->fpr[0].f;
#endif

#elif LJ_TARGET_MIPS64
/* -- MIPS n64 calling conventions ---------------------------------------- */

#define CCALL_HANDLE_STRUCTRET \
  cc->retref = !(sz <= 16); \
  if (cc->retref) cc->gpr[ngpr++] = (GPRArg)dp;

#define CCALL_HANDLE_STRUCTRET2 \
  ccall_copy_struct(cc, ctr, dp, sp, ccall_classify_struct(cts, ctr, ct));

#define CCALL_HANDLE_COMPLEXRET \
  /* Complex values are returned in 1 or 2 FPRs. */ \
  cc->retref = 0;

#if LJ_ABI_SOFTFP	/* MIPS64 soft-float */

#define CCALL_HANDLE_COMPLEXRET2 \
  if (ctr->size == 2*sizeof(float)) {  /* Copy complex float from GPRs. */ \
    ((intptr_t *)dp)[0] = cc->gpr[0]; \
  } else {  /* Copy complex double from GPRs. */ \
    ((intptr_t *)dp)[0] = cc->gpr[0]; \
    ((intptr_t *)dp)[1] = cc->gpr[1]; \
  }

#define CCALL_HANDLE_COMPLEXARG \
  /* Pass complex by value in 2 or 4 GPRs. */

/* Position of soft-float 'float' return value depends on endianess.  */
#define CCALL_HANDLE_RET \
  if (ctype_isfp(ctr->info) && ctr->size == sizeof(float)) \
    sp = (uint8_t *)cc->gpr + LJ_ENDIAN_SELECT(0, 4);

#else			/* MIPS64 hard-float */

#define CCALL_HANDLE_COMPLEXRET2 \
  if (ctr->size == 2*sizeof(float)) {  /* Copy complex float from FPRs. */ \
    ((float *)dp)[0] = cc->fpr[0].f; \
    ((float *)dp)[1] = cc->fpr[1].f; \
  } else {  /* Copy complex double from FPRs. */ \
    ((double *)dp)[0] = cc->fpr[0].d; \
    ((double *)dp)[1] = cc->fpr[1].d; \
  }

#define CCALL_HANDLE_COMPLEXARG \
  if (sz == 2*sizeof(float)) { \
    isfp = 2; \
    if (ngpr < maxgpr) \
      sz *= 2; \
  }

#define CCALL_HANDLE_RET \
  if (ctype_isfp(ctr->info) && ctr->size == sizeof(float)) \
    sp = (uint8_t *)&cc->fpr[0].f;

#endif

#define CCALL_HANDLE_STRUCTARG \
  /* Pass all structs by value in registers and/or on the stack. */

#define CCALL_HANDLE_REGARG \
  if (ngpr < maxgpr) { \
    dp = &cc->gpr[ngpr]; \
    if (ngpr + n > maxgpr) { \
      nsp += ngpr + n - maxgpr;  /* Assumes contiguous gpr/stack fields. */ \
      if (nsp > CCALL_MAXSTACK) goto err_nyi;  /* Too many arguments. */ \
      ngpr = maxgpr; \
    } else { \
      ngpr += n; \
    } \
    goto done; \
  }

#else
#error "Missing calling convention definitions for this architecture"
#endif

#ifndef CCALL_HANDLE_STRUCTRET2
#define CCALL_HANDLE_STRUCTRET2 \
  memcpy(dp, sp, ctr->size);  /* Copy struct return value from GPRs. */
#endif

/* -- x86 OSX ABI struct classification ----------------------------------- */

#if LJ_TARGET_X86 && LJ_TARGET_OSX

/* Check for struct with single FP field. */
static int ccall_classify_struct(CTState *cts, CType *ct)
{
  CTSize sz = ct->size;
  if (!(sz == sizeof(float) || sz == sizeof(double))) return 0;
  if ((ct->info & CTF_UNION)) return 0;
  while (ct->sib) {
    ct = ctype_get(cts, ct->sib);
    if (ctype_isfield(ct->info)) {
      CType *sct = ctype_rawchild(cts, ct);
      if (ctype_isfp(sct->info)) {
	if (sct->size == sz)
	  return (sz >> 2);  /* Return 1 for float or 2 for double. */
      } else if (ctype_isstruct(sct->info)) {
	if (sct->size)
	  return ccall_classify_struct(cts, sct);
      } else {
	break;
      }
    } else if (ctype_isbitfield(ct->info)) {
      break;
    } else if (ctype_isxattrib(ct->info, CTA_SUBTYPE)) {
      CType *sct = ctype_rawchild(cts, ct);
      if (sct->size)
	return ccall_classify_struct(cts, sct);
    }
  }
  return 0;
}

#endif

/* -- x64 struct classification ------------------------------------------- */
