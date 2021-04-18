
/*
** Public Lua/C API.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
**
** Major portions taken verbatim or adapted from the Lua interpreter.
** Copyright (C) 1994-2008 Lua.org, PUC-Rio. See Copyright Notice in lua.h
*/

#define lj_api_c
#define LUA_CORE

#include "lj_obj.h"
#include "lj_gc.h"
#include "lj_err.h"
#include "lj_debug.h"
#include "lj_str.h"
#include "lj_tab.h"
#include "lj_func.h"
#include "lj_udata.h"
#include "lj_meta.h"
#include "lj_state.h"
#include "lj_bc.h"
#include "lj_frame.h"
#include "lj_trace.h"
#include "lj_vm.h"
#include "lj_strscan.h"
#include "lj_strfmt.h"

/* -- Common helper functions --------------------------------------------- */

#define lj_checkapi_slot(idx) \
  lj_checkapi((idx) <= (L->top - L->base), "stack slot %d out of range", (idx))

static TValue *index2adr(lua_State *L, int idx)
{
  if (idx > 0) {
    TValue *o = L->base + (idx - 1);
    return o < L->top ? o : niltv(L);
  } else if (idx > LUA_REGISTRYINDEX) {
    lj_checkapi(idx != 0 && -idx <= L->top - L->base,
		"bad stack slot %d", idx);
    return L->top + idx;
  } else if (idx == LUA_GLOBALSINDEX) {
    TValue *o = &G(L)->tmptv;
    settabV(L, o, tabref(L->env));
    return o;
  } else if (idx == LUA_REGISTRYINDEX) {
    return registry(L);
  } else {
    GCfunc *fn = curr_func(L);
    lj_checkapi(fn->c.gct == ~LJ_TFUNC && !isluafunc(fn),
		"calling frame is not a C function");
    if (idx == LUA_ENVIRONINDEX) {
      TValue *o = &G(L)->tmptv;
      settabV(L, o, tabref(fn->c.env));
      return o;
    } else {
      idx = LUA_GLOBALSINDEX - idx;
      return idx <= fn->c.nupvalues ? &fn->c.upvalue[idx-1] : niltv(L);
    }
  }
}

static LJ_AINLINE TValue *index2adr_check(lua_State *L, int idx)
{
  TValue *o = index2adr(L, idx);
  lj_checkapi(o != niltv(L), "invalid stack slot %d", idx);
  return o;
}

static TValue *index2adr_stack(lua_State *L, int idx)
{
  if (idx > 0) {
    TValue *o = L->base + (idx - 1);
    if (o < L->top) {
      return o;
    } else {
      lj_checkapi(0, "invalid stack slot %d", idx);
      return niltv(L);
    }
    return o < L->top ? o : niltv(L);
  } else {
    lj_checkapi(idx != 0 && -idx <= L->top - L->base,
		"invalid stack slot %d", idx);
    return L->top + idx;
  }
}

static GCtab *getcurrenv(lua_State *L)
{
  GCfunc *fn = curr_func(L);
  return fn->c.gct == ~LJ_TFUNC ? tabref(fn->c.env) : tabref(L->env);
}

/* -- Miscellaneous API functions ----------------------------------------- */

LUA_API int lua_status(lua_State *L)
{
  return L->status;
}

LUA_API int lua_checkstack(lua_State *L, int size)
{
  if (size > LUAI_MAXCSTACK || (L->top - L->base + size) > LUAI_MAXCSTACK) {
    return 0;  /* Stack overflow. */
  } else if (size > 0) {
    lj_state_checkstack(L, (MSize)size);
  }
  return 1;
}

LUALIB_API void luaL_checkstack(lua_State *L, int size, const char *msg)
{
  if (!lua_checkstack(L, size))
    lj_err_callerv(L, LJ_ERR_STKOVM, msg);
}

LUA_API void lua_xmove(lua_State *L, lua_State *to, int n)
{
  TValue *f, *t;
  if (L == to) return;
  lj_checkapi_slot(n);
  lj_checkapi(G(L) == G(to), "move across global states");
  lj_state_checkstack(to, (MSize)n);
  f = L->top;
  t = to->top = to->top + n;
  while (--n >= 0) copyTV(to, --t, --f);
  L->top = f;
}

LUA_API const lua_Number *lua_version(lua_State *L)
{
  static const lua_Number version = LUA_VERSION_NUM;
  UNUSED(L);
  return &version;
}

/* -- Stack manipulation -------------------------------------------------- */

LUA_API int lua_gettop(lua_State *L)
{
  return (int)(L->top - L->base);
}

LUA_API void lua_settop(lua_State *L, int idx)
{
  if (idx >= 0) {
    lj_checkapi(idx <= tvref(L->maxstack) - L->base, "bad stack slot %d", idx);
    if (L->base + idx > L->top) {
      if (L->base + idx >= tvref(L->maxstack))
	lj_state_growstack(L, (MSize)idx - (MSize)(L->top - L->base));
      do { setnilV(L->top++); } while (L->top < L->base + idx);
    } else {
      L->top = L->base + idx;
    }
  } else {
    lj_checkapi(-(idx+1) <= (L->top - L->base), "bad stack slot %d", idx);
    L->top += idx+1;  /* Shrinks top (idx < 0). */
  }
}

LUA_API void lua_remove(lua_State *L, int idx)
{
  TValue *p = index2adr_stack(L, idx);
  while (++p < L->top) copyTV(L, p-1, p);
  L->top--;
}

LUA_API void lua_insert(lua_State *L, int idx)
{
  TValue *q, *p = index2adr_stack(L, idx);
  for (q = L->top; q > p; q--) copyTV(L, q, q-1);
  copyTV(L, p, L->top);
}

static void copy_slot(lua_State *L, TValue *f, int idx)
{
  if (idx == LUA_GLOBALSINDEX) {
    lj_checkapi(tvistab(f), "stack slot %d is not a table", idx);
    /* NOBARRIER: A thread (i.e. L) is never black. */
    setgcref(L->env, obj2gco(tabV(f)));
  } else if (idx == LUA_ENVIRONINDEX) {
    GCfunc *fn = curr_func(L);
    if (fn->c.gct != ~LJ_TFUNC)
      lj_err_msg(L, LJ_ERR_NOENV);
    lj_checkapi(tvistab(f), "stack slot %d is not a table", idx);
    setgcref(fn->c.env, obj2gco(tabV(f)));
    lj_gc_barrier(L, fn, f);
  } else {
    TValue *o = index2adr_check(L, idx);
    copyTV(L, o, f);
    if (idx < LUA_GLOBALSINDEX)  /* Need a barrier for upvalues. */
      lj_gc_barrier(L, curr_func(L), f);
  }
}

LUA_API void lua_replace(lua_State *L, int idx)
{
  lj_checkapi_slot(1);
  copy_slot(L, L->top - 1, idx);
  L->top--;
}

LUA_API void lua_copy(lua_State *L, int fromidx, int toidx)
{
  copy_slot(L, index2adr(L, fromidx), toidx);
}

LUA_API void lua_pushvalue(lua_State *L, int idx)
{
  copyTV(L, L->top, index2adr(L, idx));
  incr_top(L);
}

/* -- Stack getters ------------------------------------------------------- */

LUA_API int lua_type(lua_State *L, int idx)
{
  cTValue *o = index2adr(L, idx);
  if (tvisnumber(o)) {
    return LUA_TNUMBER;
#if LJ_64 && !LJ_GC64
  } else if (tvislightud(o)) {
    return LUA_TLIGHTUSERDATA;
#endif
  } else if (o == niltv(L)) {
    return LUA_TNONE;
  } else {  /* Magic internal/external tag conversion. ORDER LJ_T */
    uint32_t t = ~itype(o);
#if LJ_64
    int tt = (int)((U64x(75a06,98042110) >> 4*t) & 15u);
#else
    int tt = (int)(((t < 8 ? 0x98042110u : 0x75a06u) >> 4*(t&7)) & 15u);
#endif
    lj_assertL(tt != LUA_TNIL || tvisnil(o), "bad tag conversion");
    return tt;
  }
}

LUALIB_API void luaL_checktype(lua_State *L, int idx, int tt)
{
  if (lua_type(L, idx) != tt)
    lj_err_argt(L, idx, tt);
}

LUALIB_API void luaL_checkany(lua_State *L, int idx)
{
  if (index2adr(L, idx) == niltv(L))
    lj_err_arg(L, idx, LJ_ERR_NOVAL);
}

LUA_API const char *lua_typename(lua_State *L, int t)
{
  UNUSED(L);
  return lj_obj_typename[t+1];
}

LUA_API int lua_iscfunction(lua_State *L, int idx)
{
  cTValue *o = index2adr(L, idx);
  return tvisfunc(o) && !isluafunc(funcV(o));
}

LUA_API int lua_isnumber(lua_State *L, int idx)
{
  cTValue *o = index2adr(L, idx);
  TValue tmp;
  return (tvisnumber(o) || (tvisstr(o) && lj_strscan_number(strV(o), &tmp)));
}

LUA_API int lua_isstring(lua_State *L, int idx)
{
  cTValue *o = index2adr(L, idx);
  return (tvisstr(o) || tvisnumber(o));
}

LUA_API int lua_isuserdata(lua_State *L, int idx)
{
  cTValue *o = index2adr(L, idx);
  return (tvisudata(o) || tvislightud(o));
}

LUA_API int lua_rawequal(lua_State *L, int idx1, int idx2)
{
  cTValue *o1 = index2adr(L, idx1);
  cTValue *o2 = index2adr(L, idx2);
  return (o1 == niltv(L) || o2 == niltv(L)) ? 0 : lj_obj_equal(o1, o2);
}

LUA_API int lua_equal(lua_State *L, int idx1, int idx2)
{
  cTValue *o1 = index2adr(L, idx1);
  cTValue *o2 = index2adr(L, idx2);
  if (tvisint(o1) && tvisint(o2)) {
    return intV(o1) == intV(o2);
  } else if (tvisnumber(o1) && tvisnumber(o2)) {
    return numberVnum(o1) == numberVnum(o2);
  } else if (itype(o1) != itype(o2)) {
    return 0;
  } else if (tvispri(o1)) {
    return o1 != niltv(L) && o2 != niltv(L);
#if LJ_64 && !LJ_GC64
  } else if (tvislightud(o1)) {
    return o1->u64 == o2->u64;
#endif
  } else if (gcrefeq(o1->gcr, o2->gcr)) {
    return 1;
  } else if (!tvistabud(o1)) {
    return 0;
  } else {
    TValue *base = lj_meta_equal(L, gcV(o1), gcV(o2), 0);
    if ((uintptr_t)base <= 1) {
      return (int)(uintptr_t)base;
    } else {
      L->top = base+2;
      lj_vm_call(L, base, 1+1);
      L->top -= 2+LJ_FR2;
      return tvistruecond(L->top+1+LJ_FR2);
    }
  }
}

LUA_API int lua_lessthan(lua_State *L, int idx1, int idx2)
{
  cTValue *o1 = index2adr(L, idx1);
  cTValue *o2 = index2adr(L, idx2);
  if (o1 == niltv(L) || o2 == niltv(L)) {
    return 0;
  } else if (tvisint(o1) && tvisint(o2)) {
    return intV(o1) < intV(o2);
  } else if (tvisnumber(o1) && tvisnumber(o2)) {
    return numberVnum(o1) < numberVnum(o2);
  } else {
    TValue *base = lj_meta_comp(L, o1, o2, 0);
    if ((uintptr_t)base <= 1) {
      return (int)(uintptr_t)base;
    } else {
      L->top = base+2;
      lj_vm_call(L, base, 1+1);
      L->top -= 2+LJ_FR2;
      return tvistruecond(L->top+1+LJ_FR2);
    }
  }
}

LUA_API lua_Number lua_tonumber(lua_State *L, int idx)
{
  cTValue *o = index2adr(L, idx);
  TValue tmp;
  if (LJ_LIKELY(tvisnumber(o)))
    return numberVnum(o);
  else if (tvisstr(o) && lj_strscan_num(strV(o), &tmp))
    return numV(&tmp);
  else
    return 0;
}

LUA_API lua_Number lua_tonumberx(lua_State *L, int idx, int *ok)
{
  cTValue *o = index2adr(L, idx);
  TValue tmp;
  if (LJ_LIKELY(tvisnumber(o))) {
    if (ok) *ok = 1;
    return numberVnum(o);
  } else if (tvisstr(o) && lj_strscan_num(strV(o), &tmp)) {
    if (ok) *ok = 1;
    return numV(&tmp);
  } else {
    if (ok) *ok = 0;
    return 0;
  }
}

LUALIB_API lua_Number luaL_checknumber(lua_State *L, int idx)
{
  cTValue *o = index2adr(L, idx);
  TValue tmp;
  if (LJ_LIKELY(tvisnumber(o)))
    return numberVnum(o);
  else if (!(tvisstr(o) && lj_strscan_num(strV(o), &tmp)))
    lj_err_argt(L, idx, LUA_TNUMBER);
  return numV(&tmp);
}

LUALIB_API lua_Number luaL_optnumber(lua_State *L, int idx, lua_Number def)
{
  cTValue *o = index2adr(L, idx);
  TValue tmp;
  if (LJ_LIKELY(tvisnumber(o)))
    return numberVnum(o);
  else if (tvisnil(o))
    return def;
  else if (!(tvisstr(o) && lj_strscan_num(strV(o), &tmp)))
    lj_err_argt(L, idx, LUA_TNUMBER);
  return numV(&tmp);
}

LUA_API lua_Integer lua_tointeger(lua_State *L, int idx)
{
  cTValue *o = index2adr(L, idx);
  TValue tmp;
  lua_Number n;
  if (LJ_LIKELY(tvisint(o))) {
    return intV(o);
  } else if (LJ_LIKELY(tvisnum(o))) {
    n = numV(o);
  } else {
    if (!(tvisstr(o) && lj_strscan_number(strV(o), &tmp)))
      return 0;
    if (tvisint(&tmp))
      return intV(&tmp);
    n = numV(&tmp);
  }
#if LJ_64
  return (lua_Integer)n;
#else
  return lj_num2int(n);
#endif
}

LUA_API lua_Integer lua_tointegerx(lua_State *L, int idx, int *ok)
{
  cTValue *o = index2adr(L, idx);
  TValue tmp;
  lua_Number n;
  if (LJ_LIKELY(tvisint(o))) {
    if (ok) *ok = 1;
    return intV(o);
  } else if (LJ_LIKELY(tvisnum(o))) {
    n = numV(o);
  } else {
    if (!(tvisstr(o) && lj_strscan_number(strV(o), &tmp))) {
      if (ok) *ok = 0;
      return 0;
    }
    if (tvisint(&tmp)) {
      if (ok) *ok = 1;
      return intV(&tmp);
    }
    n = numV(&tmp);
  }
  if (ok) *ok = 1;
#if LJ_64
  return (lua_Integer)n;
#else
  return lj_num2int(n);
#endif
}

LUALIB_API lua_Integer luaL_checkinteger(lua_State *L, int idx)
{
  cTValue *o = index2adr(L, idx);
  TValue tmp;
  lua_Number n;
  if (LJ_LIKELY(tvisint(o))) {
    return intV(o);
  } else if (LJ_LIKELY(tvisnum(o))) {
    n = numV(o);
  } else {
    if (!(tvisstr(o) && lj_strscan_number(strV(o), &tmp)))
      lj_err_argt(L, idx, LUA_TNUMBER);
    if (tvisint(&tmp))
      return (lua_Integer)intV(&tmp);
    n = numV(&tmp);
  }
#if LJ_64
  return (lua_Integer)n;
#else
  return lj_num2int(n);
#endif
}

LUALIB_API lua_Integer luaL_optinteger(lua_State *L, int idx, lua_Integer def)
{
  cTValue *o = index2adr(L, idx);
  TValue tmp;
  lua_Number n;
  if (LJ_LIKELY(tvisint(o))) {
    return intV(o);
  } else if (LJ_LIKELY(tvisnum(o))) {
    n = numV(o);
  } else if (tvisnil(o)) {
    return def;
  } else {
    if (!(tvisstr(o) && lj_strscan_number(strV(o), &tmp)))
      lj_err_argt(L, idx, LUA_TNUMBER);
    if (tvisint(&tmp))
      return (lua_Integer)intV(&tmp);
    n = numV(&tmp);
  }
#if LJ_64
  return (lua_Integer)n;
#else
  return lj_num2int(n);
#endif
}

LUA_API int lua_toboolean(lua_State *L, int idx)
{
  cTValue *o = index2adr(L, idx);
  return tvistruecond(o);
}

LUA_API const char *lua_tolstring(lua_State *L, int idx, size_t *len)
{
  TValue *o = index2adr(L, idx);
  GCstr *s;
  if (LJ_LIKELY(tvisstr(o))) {
    s = strV(o);
  } else if (tvisnumber(o)) {
    lj_gc_check(L);
    o = index2adr(L, idx);  /* GC may move the stack. */
    s = lj_strfmt_number(L, o);
    setstrV(L, o, s);
  } else {
    if (len != NULL) *len = 0;
    return NULL;
  }
  if (len != NULL) *len = s->len;
  return strdata(s);
}

LUALIB_API const char *luaL_checklstring(lua_State *L, int idx, size_t *len)
{
  TValue *o = index2adr(L, idx);
  GCstr *s;
  if (LJ_LIKELY(tvisstr(o))) {
    s = strV(o);
  } else if (tvisnumber(o)) {
    lj_gc_check(L);
    o = index2adr(L, idx);  /* GC may move the stack. */
    s = lj_strfmt_number(L, o);
    setstrV(L, o, s);
  } else {
    lj_err_argt(L, idx, LUA_TSTRING);
  }
  if (len != NULL) *len = s->len;
  return strdata(s);
}

LUALIB_API const char *luaL_optlstring(lua_State *L, int idx,
				       const char *def, size_t *len)
{
  TValue *o = index2adr(L, idx);
  GCstr *s;
  if (LJ_LIKELY(tvisstr(o))) {
    s = strV(o);
  } else if (tvisnil(o)) {
    if (len != NULL) *len = def ? strlen(def) : 0;
    return def;
  } else if (tvisnumber(o)) {
    lj_gc_check(L);
    o = index2adr(L, idx);  /* GC may move the stack. */
    s = lj_strfmt_number(L, o);
    setstrV(L, o, s);
  } else {
    lj_err_argt(L, idx, LUA_TSTRING);
  }
  if (len != NULL) *len = s->len;
  return strdata(s);
}

LUALIB_API int luaL_checkoption(lua_State *L, int idx, const char *def,
				const char *const lst[])
{
  ptrdiff_t i;
  const char *s = lua_tolstring(L, idx, NULL);
  if (s == NULL && (s = def) == NULL)
    lj_err_argt(L, idx, LUA_TSTRING);
  for (i = 0; lst[i]; i++)
    if (strcmp(lst[i], s) == 0)
      return (int)i;
  lj_err_argv(L, idx, LJ_ERR_INVOPTM, s);
}

LUA_API size_t lua_objlen(lua_State *L, int idx)
{
  TValue *o = index2adr(L, idx);
  if (tvisstr(o)) {
    return strV(o)->len;
  } else if (tvistab(o)) {
    return (size_t)lj_tab_len(tabV(o));
  } else if (tvisudata(o)) {
    return udataV(o)->len;
  } else if (tvisnumber(o)) {
    GCstr *s = lj_strfmt_number(L, o);
    setstrV(L, o, s);
    return s->len;
  } else {
    return 0;
  }
}

LUA_API lua_CFunction lua_tocfunction(lua_State *L, int idx)
{
  cTValue *o = index2adr(L, idx);
  if (tvisfunc(o)) {
    BCOp op = bc_op(*mref(funcV(o)->c.pc, BCIns));
    if (op == BC_FUNCC || op == BC_FUNCCW)
      return funcV(o)->c.f;
  }
  return NULL;
}

LUA_API void *lua_touserdata(lua_State *L, int idx)
{
  cTValue *o = index2adr(L, idx);
  if (tvisudata(o))
    return uddata(udataV(o));
  else if (tvislightud(o))
    return lightudV(G(L), o);
  else
    return NULL;
}

LUA_API lua_State *lua_tothread(lua_State *L, int idx)
{
  cTValue *o = index2adr(L, idx);
  return (!tvisthread(o)) ? NULL : threadV(o);
}

LUA_API const void *lua_topointer(lua_State *L, int idx)
{
  return lj_obj_ptr(G(L), index2adr(L, idx));
}

/* -- Stack setters (object creation) ------------------------------------- */

LUA_API void lua_pushnil(lua_State *L)
{
  setnilV(L->top);
  incr_top(L);
}

LUA_API void lua_pushnumber(lua_State *L, lua_Number n)
{
  setnumV(L->top, n);
  if (LJ_UNLIKELY(tvisnan(L->top)))
    setnanV(L->top);  /* Canonicalize injected NaNs. */
  incr_top(L);
}

LUA_API void lua_pushinteger(lua_State *L, lua_Integer n)
{
  setintptrV(L->top, n);
  incr_top(L);
}

LUA_API void lua_pushlstring(lua_State *L, const char *str, size_t len)
{
  GCstr *s;
  lj_gc_check(L);
  s = lj_str_new(L, str, len);
  setstrV(L, L->top, s);
  incr_top(L);
}

LUA_API void lua_pushstring(lua_State *L, const char *str)
{
  if (str == NULL) {
    setnilV(L->top);
  } else {
    GCstr *s;
    lj_gc_check(L);
    s = lj_str_newz(L, str);
    setstrV(L, L->top, s);
  }
  incr_top(L);
}

LUA_API const char *lua_pushvfstring(lua_State *L, const char *fmt,
				     va_list argp)
{
  lj_gc_check(L);
  return lj_strfmt_pushvf(L, fmt, argp);
}

LUA_API const char *lua_pushfstring(lua_State *L, const char *fmt, ...)
{
  const char *ret;
  va_list argp;
  lj_gc_check(L);
  va_start(argp, fmt);
  ret = lj_strfmt_pushvf(L, fmt, argp);
  va_end(argp);
  return ret;
}

LUA_API void lua_pushcclosure(lua_State *L, lua_CFunction f, int n)
{
  GCfunc *fn;
  lj_gc_check(L);
  lj_checkapi_slot(n);
  fn = lj_func_newC(L, (MSize)n, getcurrenv(L));
  fn->c.f = f;
  L->top -= n;
  while (n--)
    copyTV(L, &fn->c.upvalue[n], L->top+n);
  setfuncV(L, L->top, fn);
  lj_assertL(iswhite(obj2gco(fn)), "new GC object is not white");
  incr_top(L);
}

LUA_API void lua_pushboolean(lua_State *L, int b)
{
  setboolV(L->top, (b != 0));
  incr_top(L);
}

LUA_API void lua_pushlightuserdata(lua_State *L, void *p)
{
#if LJ_64
  p = lj_lightud_intern(L, p);
#endif
  setrawlightudV(L->top, p);
  incr_top(L);
}

LUA_API void lua_createtable(lua_State *L, int narray, int nrec)
{
  lj_gc_check(L);
  settabV(L, L->top, lj_tab_new_ah(L, narray, nrec));
  incr_top(L);
}

LUALIB_API int luaL_newmetatable(lua_State *L, const char *tname)
{
  GCtab *regt = tabV(registry(L));
  TValue *tv = lj_tab_setstr(L, regt, lj_str_newz(L, tname));
  if (tvisnil(tv)) {
    GCtab *mt = lj_tab_new(L, 0, 1);
    settabV(L, tv, mt);
    settabV(L, L->top++, mt);
    lj_gc_anybarriert(L, regt);
    return 1;
  } else {
    copyTV(L, L->top++, tv);
    return 0;
  }
}

LUA_API int lua_pushthread(lua_State *L)
{
  setthreadV(L, L->top, L);
  incr_top(L);
  return (mainthread(G(L)) == L);
}

LUA_API lua_State *lua_newthread(lua_State *L)
{
  lua_State *L1;
  lj_gc_check(L);
  L1 = lj_state_new(L);
  setthreadV(L, L->top, L1);
  incr_top(L);
  return L1;
}

LUA_API void *lua_newuserdata(lua_State *L, size_t size)
{
  GCudata *ud;
  lj_gc_check(L);
  if (size > LJ_MAX_UDATA)
    lj_err_msg(L, LJ_ERR_UDATAOV);
  ud = lj_udata_new(L, (MSize)size, getcurrenv(L));
  setudataV(L, L->top, ud);
  incr_top(L);
  return uddata(ud);
}

LUA_API void lua_concat(lua_State *L, int n)
{
  lj_checkapi_slot(n);
  if (n >= 2) {
    n--;
    do {
      TValue *top = lj_meta_cat(L, L->top-1, -n);
      if (top == NULL) {
	L->top -= n;
	break;
      }
      n -= (int)(L->top - (top - 2*LJ_FR2));
      L->top = top+2;
      lj_vm_call(L, top, 1+1);
      L->top -= 1+LJ_FR2;
      copyTV(L, L->top-1, L->top+LJ_FR2);
    } while (--n > 0);
  } else if (n == 0) {  /* Push empty string. */
    setstrV(L, L->top, &G(L)->strempty);