
/*
** Garbage collector.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
**
** Major portions taken verbatim or adapted from the Lua interpreter.
** Copyright (C) 1994-2008 Lua.org, PUC-Rio. See Copyright Notice in lua.h
*/

#define lj_gc_c
#define LUA_CORE

#include "lj_obj.h"
#include "lj_gc.h"
#include "lj_err.h"
#include "lj_buf.h"
#include "lj_str.h"
#include "lj_tab.h"
#include "lj_func.h"
#include "lj_udata.h"
#include "lj_meta.h"
#include "lj_state.h"
#include "lj_frame.h"
#if LJ_HASFFI
#include "lj_ctype.h"
#include "lj_cdata.h"
#endif
#include "lj_trace.h"
#include "lj_dispatch.h"
#include "lj_vm.h"

#define GCSTEPSIZE	1024u
#define GCSWEEPMAX	40
#define GCSWEEPCOST	10
#define GCFINALIZECOST	100

/* Macros to set GCobj colors and flags. */
#define white2gray(x)		((x)->gch.marked &= (uint8_t)~LJ_GC_WHITES)
#define gray2black(x)		((x)->gch.marked |= LJ_GC_BLACK)
#define isfinalized(u)		((u)->marked & LJ_GC_FINALIZED)

/* -- Mark phase ---------------------------------------------------------- */

/* Mark a TValue (if needed). */
#define gc_marktv(g, tv) \
  { lj_assertG(!tvisgcv(tv) || (~itype(tv) == gcval(tv)->gch.gct), \
	       "TValue and GC type mismatch"); \
    if (tviswhite(tv)) gc_mark(g, gcV(tv)); }

/* Mark a GCobj (if needed). */
#define gc_markobj(g, o) \
  { if (iswhite(obj2gco(o))) gc_mark(g, obj2gco(o)); }

/* Mark a string object. */
#define gc_mark_str(s)		((s)->marked &= (uint8_t)~LJ_GC_WHITES)

/* Mark a white GCobj. */
static void gc_mark(global_State *g, GCobj *o)
{
  int gct = o->gch.gct;
  lj_assertG(iswhite(o), "mark of non-white object");
  lj_assertG(!isdead(g, o), "mark of dead object");
  white2gray(o);
  if (LJ_UNLIKELY(gct == ~LJ_TUDATA)) {
    GCtab *mt = tabref(gco2ud(o)->metatable);
    gray2black(o);  /* Userdata are never gray. */
    if (mt) gc_markobj(g, mt);
    gc_markobj(g, tabref(gco2ud(o)->env));
    if (LJ_HASBUFFER && gco2ud(o)->udtype == UDTYPE_BUFFER) {
      SBufExt *sbx = (SBufExt *)uddata(gco2ud(o));
      if (sbufiscow(sbx) && gcref(sbx->cowref))
	gc_markobj(g, gcref(sbx->cowref));
      if (gcref(sbx->dict_str))
	gc_markobj(g, gcref(sbx->dict_str));
      if (gcref(sbx->dict_mt))
	gc_markobj(g, gcref(sbx->dict_mt));
    }
  } else if (LJ_UNLIKELY(gct == ~LJ_TUPVAL)) {
    GCupval *uv = gco2uv(o);
    gc_marktv(g, uvval(uv));
    if (uv->closed)
      gray2black(o);  /* Closed upvalues are never gray. */
  } else if (gct != ~LJ_TSTR && gct != ~LJ_TCDATA) {
    lj_assertG(gct == ~LJ_TFUNC || gct == ~LJ_TTAB ||
	       gct == ~LJ_TTHREAD || gct == ~LJ_TPROTO || gct == ~LJ_TTRACE,
	       "bad GC type %d", gct);
    setgcrefr(o->gch.gclist, g->gc.gray);
    setgcref(g->gc.gray, o);
  }
}

/* Mark GC roots. */
static void gc_mark_gcroot(global_State *g)
{
  ptrdiff_t i;
  for (i = 0; i < GCROOT_MAX; i++)
    if (gcref(g->gcroot[i]) != NULL)
      gc_markobj(g, gcref(g->gcroot[i]));
}

/* Start a GC cycle and mark the root set. */
static void gc_mark_start(global_State *g)
{
  setgcrefnull(g->gc.gray);
  setgcrefnull(g->gc.grayagain);
  setgcrefnull(g->gc.weak);
  gc_markobj(g, mainthread(g));
  gc_markobj(g, tabref(mainthread(g)->env));
  gc_marktv(g, &g->registrytv);
  gc_mark_gcroot(g);
  g->gc.state = GCSpropagate;
}

/* Mark open upvalues. */
static void gc_mark_uv(global_State *g)
{
  GCupval *uv;
  for (uv = uvnext(&g->uvhead); uv != &g->uvhead; uv = uvnext(uv)) {
    lj_assertG(uvprev(uvnext(uv)) == uv && uvnext(uvprev(uv)) == uv,
	       "broken upvalue chain");
    if (isgray(obj2gco(uv)))
      gc_marktv(g, uvval(uv));
  }
}

/* Mark userdata in mmudata list. */
static void gc_mark_mmudata(global_State *g)
{
  GCobj *root = gcref(g->gc.mmudata);
  GCobj *u = root;
  if (u) {
    do {
      u = gcnext(u);
      makewhite(g, u);  /* Could be from previous GC. */
      gc_mark(g, u);
    } while (u != root);
  }