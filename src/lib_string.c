/*
** String library.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
**
** Major portions taken verbatim or adapted from the Lua interpreter.
** Copyright (C) 1994-2008 Lua.org, PUC-Rio. See Copyright Notice in lua.h
*/

#define lib_string_c
#define LUA_LIB

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "lj_obj.h"
#include "lj_gc.h"
#include "lj_err.h"
#include "lj_buf.h"
#include "lj_str.h"
#include "lj_tab.h"
#include "lj_meta.h"
#include "lj_state.h"
#include "lj_ff.h"
#include "lj_bcdump.h"
#include "lj_char.h"
#include "lj_strfmt.h"
#include "lj_lib.h"

/* ------------------------------------------------------------------------ */

#define LJLIB_MODULE_string

LJLIB_LUA(string_len) /*
  function(s)
    CHECK_str(s)
    return #s
  end
*/

LJLIB_ASM(string_byte)		LJLIB_REC(string_range 0)
{
  GCstr *s = lj_lib_checkstr(L, 1);
  int32_t len = (int32_t)s->len;
  int32_t start = lj_lib_optint(L, 2, 1);
  int32_t stop = lj_lib_optint(L, 3, start);
  int32_t n, i;
  const unsigned char *p;
  if (stop < 0) stop += len+1;
  if (start < 0) start += len+1;
  if (start <= 0) start = 1;
  if (stop > len) stop = len;
  if (start > stop) return FFH_RES(0);  /* Empty interval: return no results. */
  start--;
  n = stop - start;
  if ((uint32_t)n > LUAI_MAXCSTACK)
    lj_err_caller(L, LJ_ERR_STRSLC);
  lj_state_checkstack(L, (MSize)n);
  p = (const unsigned char *)strdata(s) + start;
  for (i = 0; i < n; i++)
    setintV(L->base + i-1-LJ_FR2, p[i]);
  return FFH_RES(n);
}

LJLIB_ASM(string_char)		LJLIB_REC(.)
{
  int i, nargs = (int)(L->top - L->base);
  char *buf = lj_buf_tmp(L, (MSize)nargs);
  for (i = 1; i <= nargs; i++) {
    int32_t k = lj_lib_checkint(L, i);
    if (!checku8(k))
      lj_err_arg(L, i, LJ_ERR_BADVAL);
    buf[i-1] = (char)k;
  }
  setstrV(L, L->base-1-LJ_FR2, lj_str_new(L, buf, (size_t)nargs));
  return FFH_RES(1);
}

LJLIB_ASM(string_sub)		LJLIB_REC(string_range 1)
{
  lj_lib_checkstr(L, 1);
  lj_lib_checkint(L, 2);
  setintV(L->base+2, lj_lib_optint(L, 3, -1));
  return FFH_RETRY;
}

LJLIB_CF(string_rep)		LJLIB_REC(.)
{
  GCstr *s = lj_lib_checkstr(L, 1);
  int32_t rep = lj_lib_checkint(L, 2);
  GCstr *sep = lj_lib_optstr(L, 3);
  SBuf *sb = lj_buf_tmp_(L);
  if (sep && rep > 1) {
    GCstr *s2 = lj_buf_cat2str(L, sep, s);
    lj_buf_reset(sb);
    lj_buf_putstr(sb, s);
    s = s2;
    rep--;
  }
  sb = lj_buf_putstr_rep(sb, s, rep);
  setstrV(L, L->top-1, lj_buf_str(L, sb));
  lj_gc_check(L);
  return 1;
}

LJLIB_ASM(string_reverse)  LJLIB_REC(string_op IRCALL_lj_buf_putstr_reverse)
{
  lj_lib_checkstr(L, 1);
  return FFH_RETRY;
}
LJLIB_ASM_(string_lower)  LJLIB_REC(string_op IRCALL_lj_buf_putstr_lower)
LJLIB_ASM_(string_upper)  LJLIB_REC(string_op IRCALL_lj_buf_putstr_upper)

/* ------------------------------------------------------------------------ */

static int writer_buf(lua_State *L, const void *p, size_t size, void *sb)
{
  lj_buf_putmem((SBuf *)sb, p, (MSize)size);
  UNUSED(L);
  return 0;
}

LJLIB_CF(string_dump)
{
  GCfunc *fn = lj_lib_checkfunc(L, 1);
  int strip = L->base+1 < L->top && tvistruecond(L->base+1);
  SBuf *sb = lj_buf_tmp_(L);  /* Assumes lj_bcwrite() doesn't use tmpbuf. */
  L->top = L->base+1;
  if (!isluafunc(fn) || lj_bcwrite(L, funcproto(fn), writer_buf, sb, strip))
    lj_err_caller(L, LJ_ERR_STRDUMP);
  setstrV(L, L->top-1, lj_buf_str(L, sb));
  lj_gc_check(L);
  return 1;
}

/* ------------------------------------------------------------------------ */

/* macro to `unsign' a character */
#define uchar(c)	((unsigned char)(c))

#define CAP_UNFINISHED	(-1)
#define CAP_POSITION	(-2)

typedef struct MatchState {
  const char *src_init;  /* init of source string */
  const char *src_end;  /* end (`\0') of source string */
  lua_State *L;
  int level;  /* total number of captures (finished or unfinished) */
  int depth;
  struct {
    const char *init;
    ptrdiff_t len;
  } capture[LUA_MAXCAPTURES];
} MatchState;

#define L_ESC		'%'

static int check_capture(MatchState *ms, int l)
{
  l -= '1';
  if (l < 0 || l >= ms->level || ms->capture[l].len == CAP_UNFINISHED)
    lj_err_caller(ms->L, LJ_ERR_STRCAPI);
  return l;
}

static int capture_to_close(MatchState *ms)
{
  int level = ms->level;
  for (level--; level>=0; level--)
    if (ms->capture[level].len == CAP_UNFINISHED) return level;
  lj_err_caller(ms->L, LJ_ERR_STRPATC);
  return 0;  /* unreachable */
}

static const char *classend(MatchState *ms, const char *p)
{
  switch (*p++) {
  case L_ESC:
    if (*p == '\0')
      lj_err_caller(ms->L, LJ_ERR_STRPATE);
    return p+1;
  case '[':
    if (*p == '^') p++;
    do {  /* look for a `]' */
      if (*p == '\0')
	lj_err_caller(ms->L, LJ_ERR_STRPATM);
      if (*(p++) == L_ESC && *p != '\0')
	p++;  /* skip escapes (e.g. `%]') */
    } while (*p != ']');
    return p+1;
  default:
    return p;
  }
}

static const unsigned char match_class_map[32] = {
  0,LJ_CHAR_ALPHA,0,LJ_CHAR_CNTRL,LJ_CHAR_DIGIT,0,0,LJ_CHAR_GRAPH,0,0,0,0,
  LJ_CHAR_LOWER,0,0,0,LJ_CHAR_PUNCT,0,0,LJ_CHAR_SPACE,0,
  LJ_CHAR_UPPER,0,LJ_CHAR_ALNUM,LJ_CHAR_XDIGIT,0,0,0,0,0,0,0
};

static int match_class(int c, int cl)
{
  if ((cl & 0xc0) == 0x40) {
    int t = match_class_map[(cl&0x1f)];
    if (t) {
      t = lj_char_isa(c, t);
      return (cl & 0x20) ? t : !t;
    }
    if (cl == 'z') return c == 0;
    if (cl == 'Z') return c != 0;
  }
  return (cl == c);
}

static int matchbracketclass(in