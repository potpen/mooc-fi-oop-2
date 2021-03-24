
/*
** JIT library.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

#define lib_jit_c
#define LUA_LIB

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "lj_obj.h"
#include "lj_gc.h"
#include "lj_err.h"
#include "lj_debug.h"
#include "lj_str.h"
#include "lj_tab.h"
#include "lj_state.h"
#include "lj_bc.h"
#if LJ_HASFFI
#include "lj_ctype.h"
#endif
#if LJ_HASJIT
#include "lj_ir.h"
#include "lj_jit.h"
#include "lj_ircall.h"
#include "lj_iropt.h"
#include "lj_target.h"
#endif
#include "lj_trace.h"
#include "lj_dispatch.h"
#include "lj_vm.h"
#include "lj_vmevent.h"
#include "lj_lib.h"

#include "luajit.h"

/* -- jit.* functions ----------------------------------------------------- */

#define LJLIB_MODULE_jit

static int setjitmode(lua_State *L, int mode)
{
  int idx = 0;
  if (L->base == L->top || tvisnil(L->base)) {  /* jit.on/off/flush([nil]) */
    mode |= LUAJIT_MODE_ENGINE;
  } else {
    /* jit.on/off/flush(func|proto, nil|true|false) */
    if (tvisfunc(L->base) || tvisproto(L->base))
      idx = 1;
    else if (!tvistrue(L->base))  /* jit.on/off/flush(true, nil|true|false) */
      goto err;
    if (L->base+1 < L->top && tvisbool(L->base+1))
      mode |= boolV(L->base+1) ? LUAJIT_MODE_ALLFUNC : LUAJIT_MODE_ALLSUBFUNC;
    else
      mode |= LUAJIT_MODE_FUNC;
  }
  if (luaJIT_setmode(L, idx, mode) != 1) {
    if ((mode & LUAJIT_MODE_MASK) == LUAJIT_MODE_ENGINE)
      lj_err_caller(L, LJ_ERR_NOJIT);
  err:
    lj_err_argt(L, 1, LUA_TFUNCTION);
  }
  return 0;
}

LJLIB_CF(jit_on)
{
  return setjitmode(L, LUAJIT_MODE_ON);
}

LJLIB_CF(jit_off)
{
  return setjitmode(L, LUAJIT_MODE_OFF);
}

LJLIB_CF(jit_flush)
{
#if LJ_HASJIT
  if (L->base < L->top && tvisnumber(L->base)) {
    int traceno = lj_lib_checkint(L, 1);
    luaJIT_setmode(L, traceno, LUAJIT_MODE_FLUSH|LUAJIT_MODE_TRACE);
    return 0;
  }
#endif
  return setjitmode(L, LUAJIT_MODE_FLUSH);
}

#if LJ_HASJIT
/* Push a string for every flag bit that is set. */
static void flagbits_to_strings(lua_State *L, uint32_t flags, uint32_t base,
				const char *str)
{
  for (; *str; base <<= 1, str += 1+*str)
    if (flags & base)
      setstrV(L, L->top++, lj_str_new(L, str+1, *(uint8_t *)str));
}
#endif

LJLIB_CF(jit_status)
{
#if LJ_HASJIT
  jit_State *J = L2J(L);
  L->top = L->base;
  setboolV(L->top++, (J->flags & JIT_F_ON) ? 1 : 0);
  flagbits_to_strings(L, J->flags, JIT_F_CPU, JIT_F_CPUSTRING);
  flagbits_to_strings(L, J->flags, JIT_F_OPT, JIT_F_OPTSTRING);
  return (int)(L->top - L->base);
#else
  setboolV(L->top++, 0);
  return 1;
#endif
}

LJLIB_CF(jit_security)
{
  int idx = lj_lib_checkopt(L, 1, -1, LJ_SECURITY_MODESTRING);
  setintV(L->top++, ((LJ_SECURITY_MODE >> (2*idx)) & 3));
  return 1;
}

LJLIB_CF(jit_attach)
{
#ifdef LUAJIT_DISABLE_VMEVENT
  luaL_error(L, "vmevent API disabled");
#else
  GCfunc *fn = lj_lib_checkfunc(L, 1);
  GCstr *s = lj_lib_optstr(L, 2);
  luaL_findtable(L, LUA_REGISTRYINDEX, LJ_VMEVENTS_REGKEY, LJ_VMEVENTS_HSIZE);
  if (s) {  /* Attach to given event. */
    const uint8_t *p = (const uint8_t *)strdata(s);
    uint32_t h = s->len;
    while (*p) h = h ^ (lj_rol(h, 6) + *p++);
    lua_pushvalue(L, 1);
    lua_rawseti(L, -2, VMEVENT_HASHIDX(h));
    G(L)->vmevmask = VMEVENT_NOCACHE;  /* Invalidate cache. */
  } else {  /* Detach if no event given. */
    setnilV(L->top++);
    while (lua_next(L, -2)) {
      L->top--;
      if (tvisfunc(L->top) && funcV(L->top) == fn) {
	setnilV(lj_tab_set(L, tabV(L->top-2), L->top-1));
      }
    }
  }
#endif
  return 0;
}

LJLIB_PUSH(top-5) LJLIB_SET(os)
LJLIB_PUSH(top-4) LJLIB_SET(arch)
LJLIB_PUSH(top-3) LJLIB_SET(version_num)
LJLIB_PUSH(top-2) LJLIB_SET(version)

#include "lj_libdef.h"

/* -- jit.util.* functions ------------------------------------------------ */

#define LJLIB_MODULE_jit_util

/* -- Reflection API for Lua functions ------------------------------------ */

/* Return prototype of first argument (Lua function or prototype object) */
static GCproto *check_Lproto(lua_State *L, int nolua)
{
  TValue *o = L->base;
  if (L->top > o) {
    if (tvisproto(o)) {
      return protoV(o);
    } else if (tvisfunc(o)) {
      if (isluafunc(funcV(o)))
	return funcproto(funcV(o));
      else if (nolua)
	return NULL;
    }
  }
  lj_err_argt(L, 1, LUA_TFUNCTION);
  return NULL;  /* unreachable */
}

static void setintfield(lua_State *L, GCtab *t, const char *name, int32_t val)
{
  setintV(lj_tab_setstr(L, t, lj_str_newz(L, name)), val);
}

/* local info = jit.util.funcinfo(func [,pc]) */
LJLIB_CF(jit_util_funcinfo)
{
  GCproto *pt = check_Lproto(L, 1);
  if (pt) {
    BCPos pc = (BCPos)lj_lib_optint(L, 2, 0);
    GCtab *t;
    lua_createtable(L, 0, 16);  /* Increment hash size if fields are added. */
    t = tabV(L->top-1);
    setintfield(L, t, "linedefined", pt->firstline);
    setintfield(L, t, "lastlinedefined", pt->firstline + pt->numline);
    setintfield(L, t, "stackslots", pt->framesize);
    setintfield(L, t, "params", pt->numparams);
    setintfield(L, t, "bytecodes", (int32_t)pt->sizebc);
    setintfield(L, t, "gcconsts", (int32_t)pt->sizekgc);
    setintfield(L, t, "nconsts", (int32_t)pt->sizekn);
    setintfield(L, t, "upvalues", (int32_t)pt->sizeuv);
    if (pc < pt->sizebc)
      setintfield(L, t, "currentline", lj_debug_line(pt, pc));
    lua_pushboolean(L, (pt->flags & PROTO_VARARG));
    lua_setfield(L, -2, "isvararg");
    lua_pushboolean(L, (pt->flags & PROTO_CHILD));
    lua_setfield(L, -2, "children");
    setstrV(L, L->top++, proto_chunkname(pt));
    lua_setfield(L, -2, "source");
    lj_debug_pushloc(L, pt, pc);
    lua_setfield(L, -2, "loc");
    setprotoV(L, lj_tab_setstr(L, t, lj_str_newlit(L, "proto")), pt);
  } else {
    GCfunc *fn = funcV(L->base);
    GCtab *t;
    lua_createtable(L, 0, 4);  /* Increment hash size if fields are added. */
    t = tabV(L->top-1);
    if (!iscfunc(fn))
      setintfield(L, t, "ffid", fn->c.ffid);
    setintptrV(lj_tab_setstr(L, t, lj_str_newlit(L, "addr")),
	       (intptr_t)(void *)fn->c.f);
    setintfield(L, t, "upvalues", fn->c.nupvalues);
  }
  return 1;
}

/* local ins, m = jit.util.funcbc(func, pc) */
LJLIB_CF(jit_util_funcbc)
{
  GCproto *pt = check_Lproto(L, 0);
  BCPos pc = (BCPos)lj_lib_checkint(L, 2);
  if (pc < pt->sizebc) {
    BCIns ins = proto_bc(pt)[pc];
    BCOp op = bc_op(ins);
    lj_assertL(op < BC__MAX, "bad bytecode op %d", op);
    setintV(L->top, ins);
    setintV(L->top+1, lj_bc_mode[op]);
    L->top += 2;
    return 2;
  }
  return 0;
}

/* local k = jit.util.funck(func, idx) */
LJLIB_CF(jit_util_funck)
{
  GCproto *pt = check_Lproto(L, 0);
  ptrdiff_t idx = (ptrdiff_t)lj_lib_checkint(L, 2);
  if (idx >= 0) {
    if (idx < (ptrdiff_t)pt->sizekn) {
      copyTV(L, L->top-1, proto_knumtv(pt, idx));
      return 1;
    }
  } else {
    if (~idx < (ptrdiff_t)pt->sizekgc) {
      GCobj *gc = proto_kgc(pt, idx);
      setgcV(L, L->top-1, gc, ~gc->gch.gct);
      return 1;
    }
  }
  return 0;
}

/* local name = jit.util.funcuvname(func, idx) */
LJLIB_CF(jit_util_funcuvname)
{
  GCproto *pt = check_Lproto(L, 0);
  uint32_t idx = (uint32_t)lj_lib_checkint(L, 2);
  if (idx < pt->sizeuv) {
    setstrV(L, L->top-1, lj_str_newz(L, lj_debug_uvname(pt, idx)));
    return 1;
  }
  return 0;
}

/* -- Reflection API for traces ------------------------------------------- */

#if LJ_HASJIT

/* Check trace argument. Must not throw for non-existent trace numbers. */
static GCtrace *jit_checktrace(lua_State *L)
{
  TraceNo tr = (TraceNo)lj_lib_checkint(L, 1);
  jit_State *J = L2J(L);
  if (tr > 0 && tr < J->sizetrace)
    return traceref(J, tr);
  return NULL;
}

/* Names of link types. ORDER LJ_TRLINK */
static const char *const jit_trlinkname[] = {
  "none", "root", "loop", "tail-recursion", "up-recursion", "down-recursion",
  "interpreter", "return", "stitch"
};

/* local info = jit.util.traceinfo(tr) */
LJLIB_CF(jit_util_traceinfo)