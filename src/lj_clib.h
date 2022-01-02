/*
** FFI C library loader.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

#ifndef _LJ_CLIB_H
#define _LJ_CLIB_H

#include "lj_obj.h"

#if LJ_HASFFI

/* Namespace for C library indexing. */
#define CLNS_INDEX	((1u<<CT_FUNC)|(1u<<CT_EXTERN)|(1u<<CT_CONSTVAL))

/* C library namespace. */
typedef struct CLibrary {
  void 