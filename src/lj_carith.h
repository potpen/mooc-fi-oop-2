/*
** C data arithmetic.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

#ifndef _LJ_CARITH_H
#define _LJ_CARITH_H

#include "lj_obj.h"

#if LJ_HASFFI

LJ_FUNC int lj_carith_op(lua_State *L, MMS mm);

#if LJ_32
LJ_FUNC uint64_t lj_carith_shl64(uint64_t x, int32_t sh);
LJ_FUNC uint64_t lj_carith_shr64(uint64_t x, int32_t sh);
LJ_FUNC uint64_t lj_carith_sar64(ui