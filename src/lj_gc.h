/*
** Garbage collector.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

#ifndef _LJ_GC_H
#define _LJ_GC_H

#include "lj_obj.h"

/* Garbage collector states. Order matters. */
enum {
  GCSpause, GCSpropagate, GCSatomic, GCSsweepstring, GCSsweep, GCSfinalize
};

/* Bitmasks for marked field of GCobj. */
#define LJ_GC_WHITE0	0x01
#define LJ_GC_WHITE1	0x02
#define LJ_GC_BLACK	0x04
#define LJ_GC_FINALIZED	0x08
#define LJ_GC_WEAKKEY	0x08
#define LJ_GC_WEAKVAL	0x10
#define LJ_GC_CDATA_FIN	0x10
#define LJ_GC_FIXED	0x20
#define LJ_GC_SFIXED	0x40

#define LJ_GC_WHITES	(LJ_GC_WHITE0 | LJ_GC_WHITE1)
#define LJ_GC_COLORS	(LJ_GC_WHITES | LJ_GC_BLACK)
#define LJ_GC_WEAK	(LJ_GC_WEAKKEY | LJ_GC_WEAKVAL)

/* Macros to test and set GCobj colors. */
#define iswhite(x)	((x)->gch.marked & LJ_GC_WHITES)
#define isblack(x)	((x)->gch.marked & LJ_GC_BLACK)
#define isgray(x)	(!((x)->gch.marked & (LJ_GC_BLACK|LJ_GC_WHITES)))
#define tviswhite(x)	(tvisgcv(x) && iswhite(gcV(x)))
#define otherwhite(g)	(g->gc.currentwhite ^ LJ_GC_WHITES)
#define isdead(g, v)	((v)->gch.marked & otherwhite(g) & LJ_GC_WHITES)

#define curwhite(g)	((g)->gc.currentwhite & LJ_GC_WHITES)
#define newwhite(g, x)	(obj2gco(x)->gch.marked = (uint8_t)curwhite(g))
#define makewhite(g, x) \
  ((x)->gch.marked = ((x)->gch.marked & (uint8_t)~LJ_GC_COLORS) | curwhite(g))
#define flipwhite(x)	((x)->gch.marked ^= LJ_GC_WHITES)
#define black2gray(x)	((x)->gch.marked &= (uint8_t)~LJ_GC_BLACK)
#define fixstring(s)	((s)->marked |= LJ_GC_FIXED)
#define markfinalized(x)	((x)->gch.marked |= LJ_GC_FINALIZED)

/* Collector. */
LJ_FUNC size_t lj_gc_separateudata(global_State *g, int all);
LJ_FUNC void lj_gc_finalize_udata(lua_State *L);
#if LJ_HASFFI
LJ_FUNC void lj_gc_finalize_cdata(lua_State *L);
#else
#define lj_gc_finalize_cdata(L)		UNUSED(L)
#endif
LJ_FUNC void lj_gc_freeall(global_State *g);
LJ_FUNCA int LJ_FASTCALL lj_gc_step(lua_State *L);
LJ_FUNCA void LJ_FASTCALL lj_gc_step_fixtop(lua_State *L);
#if LJ_HASJIT
LJ_FUNC int LJ_FASTCALL lj_gc_step_jit(global_State *g, MSize steps);
#endif
LJ_FUNC void lj_gc_fullgc(lua_Sta