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
#define SBUF_FLAG_EXT		1	/* Extended string buffer. */
#define SBUF_FLAG_COW		2	/* Copy-on-write buffer. */
#define SBUF_FLAG_BORROW	4	/* Borrowed string buffer. */

#define sbufL(sb) \
  ((lua_State *)(void *)(uintptr_t)(mrefu((sb)->L) & SBUF_MASK_L))
#define setsbufL(sb, l)		(setmref((sb)->L, (l)))
#define setsbufXL(sb, l, flag) \
  (setmrefu((sb)->L, (GCSize)(uintptr_t)(void *)(l) + (flag)))
#define setsbufXL_(sb, l) \
  (setmrefu((sb)->L, (GCSize)(uintptr_t)(void *)(l) | (mrefu((sb)->L) & SBUF_MASK_FLAG)))

#define sbufflag(sb)		(mrefu((sb)->L))
#define sbufisext(sb)		(sbufflag((sb)) & SBUF_FLAG_EXT)
#define sbufiscow(sb)		(sbufflag((sb)) & SBUF_FLAG_COW)
#define sbufisborrow(sb)	(sbufflag((sb)) & SBUF_FLAG_BORROW)
#define sbufiscoworborrow(sb)	(sbufflag((sb)) & (SBUF_FLAG_COW|SBUF_FLAG_BORROW))
#define sbufX(sb) \
  (lj_assertG_(G(sbufL(sb)), sbufisext(sb), "not an SBufExt"), (SBufExt *)(sb))
#define setsbufflag(sb, flag)	(setmrefu((sb)->L, (flag)))

#define tvisbuf(o) \
  (LJ_HASBUFFER && tvisudata(o) && udataV(o)->udtype == UDTYPE_BUFFER)
#define bufV(o)		check_exp(tvisbuf(o), ((SBufExt *)uddata(udataV(o))))

/* Buffer management */
LJ_FUNC char *LJ_FASTCALL lj_buf_need2(SBuf *sb, MSize sz);
LJ_FUNC char *LJ_FASTCALL lj_buf_more2(SBuf *sb, MSize sz);
LJ_FUNC void LJ_FASTCALL lj_buf_shrink(lua_State *L, SBuf *sb);
LJ_FUNC char * LJ_FASTCALL lj_buf_tmp(lua_S