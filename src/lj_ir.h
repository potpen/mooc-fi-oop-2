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
  _(KNULL,	N , cst, _