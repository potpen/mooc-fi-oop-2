/*
** Table handling.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

#ifndef _LJ_TAB_H
#define _LJ_TAB_H

#include "lj_obj.h"

/* Hash constants. Tuned using a brute force search. */
#define HASH_BIAS	(-0x04c11db7)
#define HASH_ROT1	14
#define HASH_ROT2	5
#define HASH_ROT3	13

/* Scramble the bits of numbers and pointers. */
static LJ_AINLINE uint32_t hashrot(uint32_t lo, uint32_t hi)
{
#if LJ_TARGET_X86ORX64
  /* Prefer variant that compiles well for a 2-operand CPU. */
  lo ^= hi; hi = lj_rol(hi, HASH_ROT1);
  lo -= hi; hi = lj_rol(hi, HASH_ROT2);
  hi ^= lo; hi -= lj_rol(lo, HASH_ROT3);
#else
  lo ^= hi;
  lo = lo - lj_rol(hi, HASH_ROT1