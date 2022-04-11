/*
** Trace recorder for C data operations.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

#ifndef _LJ_CRECORD_H
#define _LJ_CRECORD_H

#include "lj_obj.h"
#include "lj_jit.h"
#include "lj_ffrecord.h"

#if LJ_HASJIT && LJ_HASFFI
LJ_FUNC void LJ_FASTCALL recff_cdata_index(jit_State *J, RecordFFData *rd);
LJ_FUNC void LJ_FASTCALL recff_cdata_call(jit_State *J, R