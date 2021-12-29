/*
** C data management.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

#ifndef _LJ_CDATA_H
#define _LJ_CDATA_H

#include "lj_obj.h"
#include "lj_gc.h"
#include "lj_ctype.h"

#if LJ_HASFFI

/* Get C data pointer. */
static LJ_AINLINE void *cdata_getptr(void *p, CTSize sz)
{
  if (LJ_64 && sz == 4) {  /* Support 32 bit pointers on 64 bit targets. */
    return ((void *)(uintptr_t)*(uint32_t *)p);
  } else {
    lj_assertX(sz == CTSIZE_PTR, "bad pointer size %d", sz);
    return *(void **)p;
  }
}

/* Set C data pointer. */
static LJ_AINLINE void cdata_setptr(void *p, CTSize sz, const void *v)
{
  if (LJ_64 && sz == 4) {  /* Support 32 bit pointers on 64 bit targets. */
    *(uint32_t *)p = (uint32_t)(uintptr_t)v;
  } else {
    lj_assertX(sz == CTSIZE_PTR, "bad pointer size %d", sz);
    *(void **)p = (void *)v;
  }
}

/* Allocate fixed-size C data object. */
static LJ_AINL