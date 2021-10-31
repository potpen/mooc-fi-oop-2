/*
** Buffer handling.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

#ifndef _LJ_BUF_H
#define _LJ_BUF_H

#include "lj_obj.h"
#include "lj_gc.h"
#include "lj_str.h"

/* Resizable string buffers. */

/* The SBuf struct definition is in lj_obj.h:
**   char *w;	Write pointer.
**   char *e;	End pointer.
**   char *b;	Base pointer.
**   MRef L;	lua_State, used for buffer resizing. Extension bits in 3 LSB.
*/

/* Extended string buffer. */
typedef struct SBufExt {
  SBufHeader;
  union {
    GCRef cowref;	/* Copy-on-write object reference. */
    MRef bsb;		/* Borrowed string buffer. */
  };
  char *r;		/* Read pointer. */
  GCRef dict_str;	/* Serialization string dictionary table. */
  GCRef dict_mt;	/* Serialization metatable dictionary table. */
  int depth;		/* Remaining recursion depth. */
} SBufExt;

#define sbufsz(sb)		((MSize)((sb)->e - (sb)->b))
#define sbuflen(sb)		((MSize)((sb)->w - (sb)->b))
#define sbufleft(sb)		((MSize)((sb)->e - (sb)->w))
#define sbufxlen(sbx)		((MSize)((sbx)->w - (sbx)->r))
#define sbufxslack(sbx)		((MSize)((sbx)->r - (sbx)->b))

#define SBUF_MASK_FLAG		(7)
#define SBUF_MASK_L		(~(GCSize)SBUF_MASK_FLAG)
#define SBUF_FLAG_EXT		1	/* Extended 