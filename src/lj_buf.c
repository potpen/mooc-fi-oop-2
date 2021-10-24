
/*
** Buffer handling.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

#define lj_buf_c
#define LUA_CORE

#include "lj_obj.h"
#include "lj_gc.h"
#include "lj_err.h"
#include "lj_buf.h"
#include "lj_str.h"
#include "lj_tab.h"
#include "lj_strfmt.h"

/* -- Buffer management --------------------------------------------------- */

static void buf_grow(SBuf *sb, MSize sz)
{
  MSize osz = sbufsz(sb), len = sbuflen(sb), nsz = osz;
  char *b;
  GCSize flag;
  if (nsz < LJ_MIN_SBUF) nsz = LJ_MIN_SBUF;
  while (nsz < sz) nsz += nsz;
  flag = sbufflag(sb);
  if ((flag & SBUF_FLAG_COW)) {  /* Copy-on-write semantics. */
    lj_assertG_(G(sbufL(sb)), sb->w == sb->e, "bad SBuf COW");
    b = (char *)lj_mem_new(sbufL(sb), nsz);
    setsbufflag(sb, flag & ~(GCSize)SBUF_FLAG_COW);
    setgcrefnull(sbufX(sb)->cowref);
    memcpy(b, sb->b, osz);
  } else {
    b = (char *)lj_mem_realloc(sbufL(sb), sb->b, osz, nsz);
  }
  if ((flag & SBUF_FLAG_EXT)) {
    sbufX(sb)->r = sbufX(sb)->r - sb->b + b;  /* Adjust read pointer, too. */
  }
  /* Adjust buffer pointers. */
  sb->b = b;
  sb->w = b + len;
  sb->e = b + nsz;
  if ((flag & SBUF_FLAG_BORROW)) {  /* Adjust borrowed buffer pointers. */
    SBuf *bsb = mref(sbufX(sb)->bsb, SBuf);
    bsb->b = b;
    bsb->w = b + len;
    bsb->e = b + nsz;
  }
}

LJ_NOINLINE char *LJ_FASTCALL lj_buf_need2(SBuf *sb, MSize sz)
{
  lj_assertG_(G(sbufL(sb)), sz > sbufsz(sb), "SBuf overflow");
  if (LJ_UNLIKELY(sz > LJ_MAX_BUF))
    lj_err_mem(sbufL(sb));
  buf_grow(sb, sz);
  return sb->b;
}

LJ_NOINLINE char *LJ_FASTCALL lj_buf_more2(SBuf *sb, MSize sz)
{
  if (sbufisext(sb)) {
    SBufExt *sbx = (SBufExt *)sb;
    MSize len = sbufxlen(sbx);
    if (LJ_UNLIKELY(sz > LJ_MAX_BUF || len + sz > LJ_MAX_BUF))
      lj_err_mem(sbufL(sbx));
    if (len + sz > sbufsz(sbx)) {  /* Must grow. */
      buf_grow((SBuf *)sbx, len + sz);
    } else if (sbufiscow(sb) || sbufxslack(sbx) < (sbufsz(sbx) >> 3)) {
      /* Also grow to avoid excessive compactions, if slack < size/8. */
      buf_grow((SBuf *)sbx, sbuflen(sbx) + sz);  /* Not sbufxlen! */
      return sbx->w;
    }
    if (sbx->r != sbx->b) {  /* Compact by moving down. */
      memmove(sbx->b, sbx->r, len);
      sbx->r = sbx->b;
      sbx->w = sbx->b + len;
      lj_assertG_(G(sbufL(sbx)), len + sz <= sbufsz(sbx), "bad SBuf compact");
    }
  } else {
    MSize len = sbuflen(sb);
    lj_assertG_(G(sbufL(sb)), sz > sbufleft(sb), "SBuf overflow");
    if (LJ_UNLIKELY(sz > LJ_MAX_BUF || len + sz > LJ_MAX_BUF))
      lj_err_mem(sbufL(sb));
    buf_grow(sb, len + sz);
  }
  return sb->w;
}

void LJ_FASTCALL lj_buf_shrink(lua_State *L, SBuf *sb)
{
  char *b = sb->b;
  MSize osz = (MSize)(sb->e - b);
  if (osz > 2*LJ_MIN_SBUF) {
    MSize n = (MSize)(sb->w - b);
    b = lj_mem_realloc(L, b, osz, (osz >> 1));
    sb->b = b;
    sb->w = b + n;
    sb->e = b + (osz >> 1);
  }
  lj_assertG_(G(sbufL(sb)), !sbufisext(sb), "YAGNI shrink SBufExt");
}

char * LJ_FASTCALL lj_buf_tmp(lua_State *L, MSize sz)
{
  SBuf *sb = &G(L)->tmpbuf;
  setsbufL(sb, L);
  return lj_buf_need(sb, sz);
}

#if LJ_HASBUFFER && LJ_HASJIT
void lj_bufx_set(SBufExt *sbx, const char *p, MSize len, GCobj *ref)
{