/*
** Debugging and introspection.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

#ifndef _LJ_DEBUG_H
#define _LJ_DEBUG_H

#include "lj_obj.h"

typedef struct lj_Debug {
  /* Common fields. Must be in the same order as in lua.h. */
  int event;
  const char *name;
  const char *namewhat;
  const char *what;
  const char *source;
  int currentline;
  int nups;
  int linedefined;
  int lastlinedefined;
  char short_src[LUA_IDSIZE];
  int i_ci;
  /* Extended fields. Only valid if lj_debug_getinfo() is called with ext = 1.*/
  int nparams;
  int isvararg;
} lj_Debug;

LJ_FUNC cTValue *lj_debug_frame(lua_State *L, int level, int *size);
LJ_FUNC BCLine LJ_FASTCALL lj_debug_line(GCproto *pt, BCPos pc);
LJ_FUNC const char *lj_debug_uvname(GCproto *pt, uint32_t idx);
LJ_FUNC const char *lj_debug_uvnamev(cTValue *o, uint32_t 