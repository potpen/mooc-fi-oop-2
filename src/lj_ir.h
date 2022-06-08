/*
** SSA IR (Intermediate Representation) format.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

#ifndef _LJ_IR_H
#define _LJ_IR_H

#include "lj_obj.h"

/* -- IR instructions ----------------------------------------------------- */

/* IR instruction definition. Order matters, see below. ORDER IR */
#define IRDEF(_) \
  /* Guarded assertions. */ \
  /* Must be properly aligned to flip opposites (^1) and (un)ordered (^4). */ \
  _(LT,		N , ref, ref) \
  _(GE,		N , ref, ref) \
  _(LE,		N , ref, ref) \
  _(GT,		N , ref, ref) \
  \
  _(ULT,	N , ref, ref) \
  _(UGE,	N , ref, ref) \
  _(ULE,	N , ref, ref) \
  _(UGT,	N , ref, ref) \
  \
  _(EQ,		C , ref, ref) \
  _(NE,		C , ref, ref) \
  \
  _(ABC,	N , ref, ref) \
  _(RETF,	S , ref, ref) \
  \
  /* Miscellaneous ops. */ \
  _(NOP,	N , ___, ___) \
  _(BASE,	N , lit, lit) \
  _(PVAL,	N , lit, ___) \
  _(GCSTEP,	S , ___, ___) \
  _(HIOP,	S , ref, ref) \
  _(LOOP,	S , ___, ___) \
  _(USE,	S , ref, ___) \
  _(PHI,	S , ref, ref) \
  _(RENAME,	S , ref, lit) \
  _(PROF,	S , ___, ___) \
  \
  /* Constants. */ \
  _(KPRI,	N , ___, ___) \
  _(KINT,	N , cst, ___) \
  _(KGC,	N , cst, ___) \
  _(KPTR,	N , cst, ___) \
  _(KKPTR,	N , cst, ___) \
  _(KNULL,	N , cst, ___) \
  _(KNUM,	N , cst, ___) \
  _(KINT64,	N , cst, ___) \
  _(KSLOT,	N , ref, lit) \
  \
  /* Bit ops. */ \
  _(BNOT,	N , ref, ___) \
  _(BSWAP,	N , ref, ___) \
  _(BAND,	C , ref, ref) \
  _(BOR,	C , ref, ref) \
  _(BXOR,	C , ref, ref) \
  _(BSHL,	N , ref, ref) \
  _(BSHR,	N , ref, ref) \
  _(BSAR,	N , ref, ref) \
  _(BROL,	N , ref, ref) \
  _(BROR,	N , ref, ref) \
  \
  /* Arithmetic ops. ORDER ARITH */ \
  _(ADD,	C , ref, ref) \
  _(SUB,	N , ref, ref) \
  _(MUL,	C , ref, ref) \
  _(DIV,	N , ref, ref) \
  _(MOD,	N , ref, ref) \
  _(POW,	N , ref, ref) \
  _(NEG,	N , ref, ref) \
  \
  _(ABS,	N , ref, ref) \
  _(LDEXP,	N , ref, ref) \
  _(MIN,	C , ref, ref) \
  _(MAX,	C , ref, ref) \
  _(FPMATH,	N , ref, lit) \
  \
  /* Overflow-checking arithmetic ops. */ \
  _(ADDOV,	CW, ref, ref) \
  _(SUBOV,	NW, ref, ref) \
  _(MULOV,	CW, ref, ref) \
  \
  /* Memory ops. A = array, H = hash, U = upvalue, F = field, S = stack. */ \
  \
  /* Memory references. */ \
  _(AREF,	R , ref, ref) \
  _(HREFK,	R , ref, ref) \
  _(HREF,	L , ref, ref) \
  _(NEWREF,	S , ref, ref) \
  _(UREFO,	LW, ref, lit) \
  _(UREFC,	LW, ref, lit) \
  _(FREF,	R , ref, lit) \
  _(TMPREF,	S , ref, lit) \
  _(STRREF,	N , ref, ref) \
  _(LREF,	L , ___, ___) \
  \
  /* Loads and Stores. These must be in the same order. */ \
  _(ALOAD,	L , ref, ___) \
  _(HLOAD,	L , ref, ___) \
  _(ULOAD,	L , ref, ___) \
  _(FLOAD,	L , ref