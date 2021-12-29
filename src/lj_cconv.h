/*
** C type conversions.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

#ifndef _LJ_CCONV_H
#define _LJ_CCONV_H

#include "lj_obj.h"
#include "lj_ctype.h"

#if LJ_HASFFI

/* Compressed C type index. ORDER CCX. */
enum {
  CCX_B,	/* Bool. */
  CCX_I,	/* Integer. */
  CCX_F,	/* Floating-point number. */
  CCX_C,	/* Complex. */
  CCX_V,	/* Vector. */
  CCX_P,	/* Pointer. */
  CCX_A,	/* Refarray. */
  CCX_S		/* Struct/union. */
};

/* Convert C ty