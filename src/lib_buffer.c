
/*
** Buffer library.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

#define lib_buffer_c
#define LUA_LIB

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "lj_obj.h"

#if LJ_HASBUFFER
#include "lj_gc.h"
#include "lj_err.h"
#include "lj_buf.h"
#include "lj_str.h"
#include "lj_tab.h"
#include "lj_udata.h"
#include "lj_meta.h"
#if LJ_HASFFI
#include "lj_ctype.h"
#include "lj_cdata.h"
#include "lj_cconv.h"
#endif
#include "lj_strfmt.h"
#include "lj_serialize.h"
#include "lj_lib.h"

/* -- Helper functions ---------------------------------------------------- */

/* Check that the first argument is a string buffer. */
static SBufExt *buffer_tobuf(lua_State *L)
{
  if (!(L->base < L->top && tvisbuf(L->base)))
    lj_err_argtype(L, 1, "buffer");
  return bufV(L->base);
}