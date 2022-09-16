/*
** String formatting.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

#ifndef _LJ_STRFMT_H
#define _LJ_STRFMT_H

#include "lj_obj.h"

typedef uint32_t SFormat;  /* Format indicator. */

/* Format parser state. */
typedef struct FormatState {
  const uint8_t *p;	/* Current format string pointer. */
  const uint8_t *e;	/* End of format string. */
  const char *str;	/* Returned literal string. */
  MSize len;		/* Size of literal string. */
} FormatState;

/* Format types (max. 16). */
typedef enum FormatType {
  STRFMT_EOF, STRFMT_ERR, STRFMT_LIT,
  STRFMT_INT, STRFMT_UINT, STRFMT_NUM, STRFMT_STR, STRFMT_CHAR, STRFMT_PTR
} FormatType;

/* Format subtypes (bits are reused). */
#define STRFMT_T_HEX	0x0010	/* STRFMT_UINT */
#define STRFMT_T_OCT	0x0020	/* STRFMT_UINT */
#define STRFMT_T_FP_A	0x0000	/* STRFMT_NUM */
#define STRFMT_T_FP_E	0x0010	/* STRFMT_NUM */
#define STRFMT_T_FP_F	0x0020	/* STRFMT_NUM */
#define STRFMT_T_FP_G	0x0030	/* STRFMT_NUM */
#define STRFMT_T_QUOTED	0x0010	/* STRFMT_STR */

/* Format flags. */
#define STRFMT_F_LEFT	0x0100
#define STRFMT_F_PLUS	0x0200
#define STRFMT_F_ZERO	0x0400
#define STRFMT_F_SPACE	0x0800
#define STRFMT_F_ALT	0x1