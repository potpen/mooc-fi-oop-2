/*
** Base and coroutine library.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
**
** Major portions taken verbatim or adapted from the Lua interpreter.
** Copyright (C) 1994-2011 Lua.org, PUC-Rio. See Copyright Notice in lua.h
*/

#include <stdio.h>

#define lib_base_c
#define LUA_LIB

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "lj_obj.h"
#include "lj_gc.h"
#include "lj_err.h"
#include "lj_debug.h"
#include "lj_buf.h"
#include "lj_str.h"
#include "lj_tab.h"
#include "lj_meta.h"
#include "lj_state.h"
#include "lj_frame.h"
#if LJ_HASFFI
#include "lj_ctype.h"
#include "lj_cconv.h"
#endif
#include "lj_bc.h"
#include "lj_ff.h"
#include "lj_dispatch.h"
#include "lj_char.h"
#include "lj_strscan.h"
#include "lj_strfmt.h"
#include "lj_lib.h"

/* -- Base library: checks ------------------------------------------------ */

#define LJLIB_MODULE_base

LJLIB_ASM(assert)		LJLIB_REC(.)
{
  lj_lib_checkany(L, 1);
  if (L->top == L->base+1)
    lj_err_caller(L, LJ_ERR_ASSERT);
  else if (tvisstr(L->base+1) || tvisnumber(L->base+1))
    lj_err_callermsg(L, strdata(lj_lib_checkstr(L, 2)));
  else
    lj_err_run(L);
  return FFH_UNREACHABLE;
}

/* ORDER LJ_T */
LJLIB_PUSH("nil")
LJLIB_PUSH("boolean")
LJLIB_PUSH(top-1)  /* boolean */
LJLIB_PUSH("userdata")
LJLIB_PUSH("string")
LJLIB_PUSH("upval")
LJLIB_PUSH("thread")
LJLIB_PUSH("proto")
LJLIB_PUSH("function")
LJLIB_PUSH("trace")
LJLIB_PUSH("cdata")
LJLIB_PUSH("table")
LJLIB_PUSH(top-9)  /* userdata */
LJLIB_PUSH("number")
LJLIB_ASM_(type)		LJLIB_REC(.)
/* Recycle the lj_lib_checkany(L, 1) from assert. */

/* -- Base library: iterators --------------------------------------------- */

/* This solves a circular dependency problem -- change FF_next_N as needed. */
LJ_STATIC_ASSERT((int)FF_next == FF_next_N);

LJLIB_ASM(next)			LJLIB_REC(.)
{
  lj_lib_checktab(L, 1);
  lj_err_msg(L, LJ_ERR_NEXTIDX);
  return FFH_UNREACHABLE;
}

#if LJ_52 || LJ_HASFFI
static int ffh_pairs(lua_State *L, MMS mm)
{
  TValue *o = lj_lib_checkany(L, 1);
  cTValue *mo = lj_meta_lookup(L, o, mm);
  if ((LJ_52 || tviscdata(o)) && !tvisnil(mo)) {
    L->top = o+1;  /* Only keep one argument. */
    copyTV(L, L->base-1-LJ_FR2, mo);  /* Replace callable. */
    return FFH_TAILCALL;
  } else {
    if (!tvistab(o)) lj_err_argt(L, 1, LUA_TTABLE);
    if (LJ_FR2) { copyTV(L, o-1, o); o--; }
    setfuncV(L, o-1, funcV(lj_lib_upvalue(L, 1)));
    if (mm == MM_pairs) setnilV(o+1); else setintV(o+1, 0);
    return FFH_RES(3);
  }
}
#else
#define ffh_pairs(L, mm)	(lj_lib_checktab(L, 1), FFH_UNREACHABLE)
#endif

LJLIB_PUSH(lastcl)
LJLIB_ASM(pairs)		LJLIB_REC(xpairs 0)
{
  return ffh_pairs(L, MM_pairs);
}

LJLIB_NOREGUV LJLIB_ASM(ipairs_aux)	LJLIB_REC(.)
{
  lj_lib_checktab(L, 1);
  lj_lib_checkint(L, 2);
  return FFH_UNREACHABLE;
}

LJLIB_PUSH(lastcl)
LJLIB_ASM(ipairs)		LJLIB_REC(xpairs 1)
{
  return ffh_pairs(L, MM_ipairs);
}

/* -- Base library: getters and setters ----------------------------------- */

LJLIB_ASM_(getmetatable)	LJLIB_REC(.)
/* Recycle the lj_lib_checkany(L, 1) from assert. */

LJLIB_ASM(setmetatable)		LJLIB_REC(.)
{
  GCtab *t = lj_lib_checktab(L, 1);
  GCtab *mt = lj_lib_checktabornil(L, 2);
  if (!tvisnil(lj_meta_lookup(L, L->base, MM_metatable)))
    lj_err_caller(L, LJ_ERR_PROTMT);
  setgcref(t->metatable, obj2gco(mt));
  if (mt) { lj_gc_objbarriert(L, t, mt); }
  settabV(L, L->base-1-LJ_FR2, t);
  return FFH_RES(1);
}

LJLIB_CF(getfenv)		LJLIB_REC(.)
{
  GCfunc *fn;
  cTValue *o = L->base;
  if (!(o < L->top && tvisfunc(o))) {
    int level = lj_lib_optint(L, 1, 1);
    o = lj_debug_frame(L, level, &level);
    if (o == NULL)
      lj_err_arg(L, 1, LJ_ERR_INVLVL);
    if (LJ_FR2) o--;
  }
  fn = &gcval(o)->fn;
  settabV(L, L->top++, isluafunc(fn) ? tabref(fn->l.env) : tabref(L->env));
  return 1;
}

LJLIB_CF(setfenv)
{
  GCfunc *fn;
  GCtab *t = lj_lib_checktab(L, 2);
  cTValue *o = L->base;
  if (!(o < L->top && tvisfunc(o))) {
    int level = lj_lib_checkint(L, 1);
    if (level == 0) {
      /* NOBARRIER: A thread (i.e. L) is never black. */
      setgcref(L->env, obj2gco(t));
      return 0;
    }
    o = lj_debug_frame(L, level, &level);
    if (o == NULL)
      lj_err_arg(L, 1, LJ_ERR_INVLVL);
    if (LJ_FR2) o--;
  }
  fn = &gcval(o)->fn;
  if (!isluafunc(fn))
    lj_err_caller(L, LJ_ERR_SETFENV);
  setgcref(fn->l.env, obj2gco(t));
  lj_gc_objbarrier(L, obj2gco(fn), t);
  setfuncV(L, L->top++, fn);
  return 1;
}

LJLIB_ASM(rawget)		LJLIB_REC(.)
{
  lj_lib_checktab(L, 1);
  lj_lib_checkany(L, 2);
  return FFH_UNREACHABLE;
}

LJLIB_CF(rawset)		LJLIB_REC(.)
{
  lj_lib_checktab(L, 1);
  lj_lib_checkany(L, 2);
  L->top = 1+lj_lib_checkany(L, 3);
  lua_rawset(L, 1);
  return 1;
}

LJLIB_CF(rawequal)		LJLIB_REC(.)
{
  cTValue *o1 = lj_lib_checkany(L, 1);
  cTValue *o2 = lj_lib_checkany(L, 2);
  setboolV(L->top-1, lj_obj_equal(o1, o2));
  return 1;
}

#if LJ_52
LJLIB_CF(rawlen)		LJLIB_REC(.)
{
  cTValue *o = L->base;
  int32_t len;
  if (L->top > o && tvisstr(o))
    len = (int32_t)strV(o)->len;
  else
    len = (int32_t)lj_tab_len(lj_lib_checktab(L, 1));
  setintV(L->top-1, len);
  return 1;
}
#endif

LJLIB_CF(unpack)
{
  GCtab *t = lj_lib_checktab(L, 1);
  int32_t n, i = lj_lib_optint(L, 2, 1);
  int32_t e = (L->base+3-1 < L->top && !tvisnil(L->base+3-1)) ?
	      lj_lib_checkint(L, 3) : (int32_t)lj_tab_len(t);
  uint32_t nu;
  if (i > e) return 0;
  nu = (uint32_t)e - (uint32_t)i;
  n = (int32_t)(nu+1);
  if (nu >= LUAI_MAXCSTACK || !lua_checkstack(L, n))
    lj_err_caller(L, LJ_ERR_UNPACK);
  do {
    cTValue *tv = lj_tab_getint(t, i);
    if (tv) {
      copyTV(L, L->top++, tv);
    } else {
      setnilV(L->top++);
    }
  } while (i++ < e);
  return n;
}

LJLIB_CF(select)		LJLIB_REC(.)
{
  int32_t n = (int32_t)(L->top - L->base);
  if (n >= 1 && tvisstr(L->base) && *strVdata(L->base) == '#') {
    setintV(L->top-1, n-1);
    return 1;
  } else {
    int32_t i = lj_lib_checkint(L, 1);
    if (i < 0) i = n + i; else if (i > n) i = n;
    if (i < 1)
      lj_err_arg(L, 1, LJ_ERR_IDXRNG);
    return n - i;
  }
}

/* -- Base library: conversions ------------------------------------------- */

LJLIB_ASM(tonumber)		LJLIB_REC(.)
{
  int32_t base = lj_lib_optint(L, 2, 10);
  if (base == 10) {
    TValue *o = lj_lib_checkany(L, 1);
    if (lj_strscan_numberobj(o)) {
      copyTV(L, L->base-1-LJ_FR2, o);
      return FFH_RES(1);
    }
#if LJ_HASFFI
    if (tviscdata(o)) {
 