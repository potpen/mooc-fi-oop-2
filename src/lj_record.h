/*
** Trace recorder (bytecode -> SSA IR).
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

#ifndef _LJ_RECORD_H
#define _LJ_RECORD_H

#include "lj_obj.h"
#include "lj_jit.h"

#if LJ_HASJIT
/* Context for recording an indexed load/store. */
typedef struct RecordIndex {
  TValue tabv;		/* Runtime value of table (or indexed object). */
  TValue keyv;		/* Runtime value of key. */
  TValue valv;		/* Runtime value of stored value. */
  TValue mobjv;		/* Runtime value of metamethod object. */
  GCtab *mtv;		/* Runtime value of metatable object. */
  cTValue *oldv;	/* Runtime value of previously stored value. */
  TRef tab;		/* Table (or indexed object) reference. */
  TRef key;		/* Key reference. */
  TRef val;		/* Value reference for a store or 0 for a load. */
  TRef mt;		/* Metatable reference. */
  TRef mobj;		/* Metamethod object reference. */
  int idxchain;		/* Index indirections left or 0 for raw lookup. */
} RecordIndex;

LJ_FUNC int lj_record_objc