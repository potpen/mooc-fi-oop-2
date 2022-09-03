/*
** Low-overhead profiling.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

#define lj_profile_c
#define LUA_CORE

#include "lj_obj.h"

#if LJ_HASPROFILE

#include "lj_buf.h"
#include "lj_frame.h"
#include "lj_debug.h"
#include "lj_dispatch.h"
#if LJ_HASJIT
#include "lj_jit.h"
#include "lj_trace.h"
#endif
#include "lj_pr