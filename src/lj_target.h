
/*
** Definitions for target CPU.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

#ifndef _LJ_TARGET_H
#define _LJ_TARGET_H

#include "lj_def.h"
#include "lj_arch.h"

/* -- Registers and spill slots ------------------------------------------- */

/* Register type (uint8_t in ir->r). */
typedef uint32_t Reg;

/* The hi-bit is NOT set for an allocated register. This means the value
** can be directly used without masking. The hi-bit is set for a register
** allocation hint or for RID_INIT, RID_SINK or RID_SUNK.
*/
#define RID_NONE		0x80
#define RID_MASK		0x7f
#define RID_INIT		(RID_NONE|RID_MASK)
#define RID_SINK		(RID_INIT-1)
#define RID_SUNK		(RID_INIT-2)

#define ra_noreg(r)		((r) & RID_NONE)
#define ra_hasreg(r)		(!((r) & RID_NONE))

/* The ra_hashint() macro assumes a previous test for ra_noreg(). */
#define ra_hashint(r)		((r) < RID_SUNK)
#define ra_gethint(r)		((Reg)((r) & RID_MASK))
#define ra_sethint(rr, r)	rr = (uint8_t)((r)|RID_NONE)
#define ra_samehint(r1, r2)	(ra_gethint((r1)^(r2)) == 0)

/* Spill slot 0 means no spill slot has been allocated. */
#define SPS_NONE		0

#define ra_hasspill(s)		((s) != SPS_NONE)

/* Combined register and spill slot (uint16_t in ir->prev). */
typedef uint32_t RegSP;

#define REGSP(r, s)		((r) + ((s) << 8))
#define REGSP_HINT(r)		((r)|RID_NONE)
#define REGSP_INIT		REGSP(RID_INIT, 0)

#define regsp_reg(rs)		((rs) & 255)
#define regsp_spill(rs)		((rs) >> 8)
#define regsp_used(rs) \
  (((rs) & ~REGSP(RID_MASK, 0)) != REGSP(RID_NONE, 0))

/* -- Register sets ------------------------------------------------------- */

/* Bitset for registers. 32 registers suffice for most architectures.
** Note that one set holds bits for both GPRs and FPRs.
*/
#if LJ_TARGET_PPC || LJ_TARGET_MIPS || LJ_TARGET_ARM64
typedef uint64_t RegSet;
#else
typedef uint32_t RegSet;
#endif

#define RID2RSET(r)		(((RegSet)1) << (r))
#define RSET_EMPTY		((RegSet)0)
#define RSET_RANGE(lo, hi)	((RID2RSET((hi)-(lo))-1) << (lo))

#define rset_test(rs, r)	((int)((rs) >> (r)) & 1)
#define rset_set(rs, r)		(rs |= RID2RSET(r))
#define rset_clear(rs, r)	(rs &= ~RID2RSET(r))
#define rset_exclude(rs, r)	(rs & ~RID2RSET(r))
#if LJ_TARGET_PPC || LJ_TARGET_MIPS || LJ_TARGET_ARM64
#define rset_picktop(rs)	((Reg)(__builtin_clzll(rs)^63))
#define rset_pickbot(rs)	((Reg)__builtin_ctzll(rs))
#else
#define rset_picktop(rs)	((Reg)lj_fls(rs))