
/*
** IR assembler (SSA IR -> machine code).
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

#define lj_asm_c
#define LUA_CORE

#include "lj_obj.h"

#if LJ_HASJIT

#include "lj_gc.h"
#include "lj_buf.h"
#include "lj_str.h"
#include "lj_tab.h"
#include "lj_frame.h"
#if LJ_HASFFI
#include "lj_ctype.h"
#endif
#include "lj_ir.h"
#include "lj_jit.h"
#include "lj_ircall.h"
#include "lj_iropt.h"
#include "lj_mcode.h"
#include "lj_trace.h"
#include "lj_snap.h"
#include "lj_asm.h"
#include "lj_dispatch.h"
#include "lj_vm.h"
#include "lj_target.h"

#ifdef LUA_USE_ASSERT
#include <stdio.h>
#endif

/* -- Assembler state and common macros ----------------------------------- */

/* Assembler state. */
typedef struct ASMState {
  RegCost cost[RID_MAX];  /* Reference and blended allocation cost for regs. */

  MCode *mcp;		/* Current MCode pointer (grows down). */
  MCode *mclim;		/* Lower limit for MCode memory + red zone. */
#ifdef LUA_USE_ASSERT
  MCode *mcp_prev;	/* Red zone overflow check. */
#endif

  IRIns *ir;		/* Copy of pointer to IR instructions/constants. */
  jit_State *J;		/* JIT compiler state. */

#if LJ_TARGET_X86ORX64
  x86ModRM mrm;		/* Fused x86 address operand. */
#endif

  RegSet freeset;	/* Set of free registers. */
  RegSet modset;	/* Set of registers modified inside the loop. */
  RegSet weakset;	/* Set of weakly referenced registers. */
  RegSet phiset;	/* Set of PHI registers. */

  uint32_t flags;	/* Copy of JIT compiler flags. */
  int loopinv;		/* Loop branch inversion (0:no, 1:yes, 2:yes+CC_P). */

  int32_t evenspill;	/* Next even spill slot. */
  int32_t oddspill;	/* Next odd spill slot (or 0). */

  IRRef curins;		/* Reference of current instruction. */
  IRRef stopins;	/* Stop assembly before hitting this instruction. */
  IRRef orignins;	/* Original T->nins. */

  IRRef snapref;	/* Current snapshot is active after this reference. */
  IRRef snaprename;	/* Rename highwater mark for snapshot check. */
  SnapNo snapno;	/* Current snapshot number. */
  SnapNo loopsnapno;	/* Loop snapshot number. */
  int snapalloc;	/* Current snapshot needs allocation. */
  BloomFilter snapfilt1, snapfilt2;	/* Filled with snapshot refs. */

  IRRef fuseref;	/* Fusion limit (loopref, 0 or FUSE_DISABLED). */
  IRRef sectref;	/* Section base reference (loopref or 0). */
  IRRef loopref;	/* Reference of LOOP instruction (or 0). */

  BCReg topslot;	/* Number of slots for stack check (unless 0). */
  int32_t gcsteps;	/* Accumulated number of GC steps (per section). */

  GCtrace *T;		/* Trace to assemble. */
  GCtrace *parent;	/* Parent trace (or NULL). */

  MCode *mcbot;		/* Bottom of reserved MCode. */
  MCode *mctop;		/* Top of generated MCode. */
  MCode *mctoporig;	/* Original top of generated MCode. */
  MCode *mcloop;	/* Pointer to loop MCode (or NULL). */
  MCode *invmcp;	/* Points to invertible loop branch (or NULL). */
  MCode *flagmcp;	/* Pending opportunity to merge flag setting ins. */
  MCode *realign;	/* Realign loop if not NULL. */

#ifdef RID_NUM_KREF
  intptr_t krefk[RID_NUM_KREF];
#endif
  IRRef1 phireg[RID_MAX];  /* PHI register references. */
  uint16_t parentmap[LJ_MAX_JSLOTS];  /* Parent instruction to RegSP map. */
} ASMState;

#ifdef LUA_USE_ASSERT
#define lj_assertA(c, ...)	lj_assertG_(J2G(as->J), (c), __VA_ARGS__)
#else
#define lj_assertA(c, ...)	((void)as)
#endif

#define IR(ref)			(&as->ir[(ref)])

#define ASMREF_TMP1		REF_TRUE	/* Temp. register. */
#define ASMREF_TMP2		REF_FALSE	/* Temp. register. */
#define ASMREF_L		REF_NIL		/* Stores register for L. */

/* Check for variant to invariant references. */
#define iscrossref(as, ref)	((ref) < as->sectref)

/* Inhibit memory op fusion from variant to invariant references. */
#define FUSE_DISABLED		(~(IRRef)0)
#define mayfuse(as, ref)	((ref) > as->fuseref)
#define neverfuse(as)		(as->fuseref == FUSE_DISABLED)
#define canfuse(as, ir)		(!neverfuse(as) && !irt_isphi((ir)->t))
#define opisfusableload(o) \
  ((o) == IR_ALOAD || (o) == IR_HLOAD || (o) == IR_ULOAD || \
   (o) == IR_FLOAD || (o) == IR_XLOAD || (o) == IR_SLOAD || (o) == IR_VLOAD)

/* Sparse limit checks using a red zone before the actual limit. */
#define MCLIM_REDZONE	64

static LJ_NORET LJ_NOINLINE void asm_mclimit(ASMState *as)
{
  lj_mcode_limiterr(as->J, (size_t)(as->mctop - as->mcp + 4*MCLIM_REDZONE));
}

static LJ_AINLINE void checkmclim(ASMState *as)
{
#ifdef LUA_USE_ASSERT
  if (as->mcp + MCLIM_REDZONE < as->mcp_prev) {
    IRIns *ir = IR(as->curins+1);
    lj_assertA(0, "red zone overflow: %p IR %04d  %02d %04d %04d\n", as->mcp,
      as->curins+1-REF_BIAS, ir->o, ir->op1-REF_BIAS, ir->op2-REF_BIAS);
  }
#endif
  if (LJ_UNLIKELY(as->mcp < as->mclim)) asm_mclimit(as);
#ifdef LUA_USE_ASSERT
  as->mcp_prev = as->mcp;
#endif
}

#ifdef RID_NUM_KREF
#define ra_iskref(ref)		((ref) < RID_NUM_KREF)
#define ra_krefreg(ref)		((Reg)(RID_MIN_KREF + (Reg)(ref)))
#define ra_krefk(as, ref)	(as->krefk[(ref)])

static LJ_AINLINE void ra_setkref(ASMState *as, Reg r, intptr_t k)
{
  IRRef ref = (IRRef)(r - RID_MIN_KREF);
  as->krefk[ref] = k;
  as->cost[r] = REGCOST(ref, ref);
}

#else
#define ra_iskref(ref)		0
#define ra_krefreg(ref)		RID_MIN_GPR
#define ra_krefk(as, ref)	0
#endif

/* Arch-specific field offsets. */
static const uint8_t field_ofs[IRFL__MAX+1] = {
#define FLOFS(name, ofs)	(uint8_t)(ofs),
IRFLDEF(FLOFS)
#undef FLOFS
  0
};

/* -- Target-specific instruction emitter --------------------------------- */

#if LJ_TARGET_X86ORX64
#include "lj_emit_x86.h"
#elif LJ_TARGET_ARM
#include "lj_emit_arm.h"
#elif LJ_TARGET_ARM64
#include "lj_emit_arm64.h"
#elif LJ_TARGET_PPC
#include "lj_emit_ppc.h"
#elif LJ_TARGET_MIPS
#include "lj_emit_mips.h"
#else
#error "Missing instruction emitter for target CPU"
#endif

/* Generic load/store of register from/to stack slot. */
#define emit_spload(as, ir, r, ofs) \
  emit_loadofs(as, ir, (r), RID_SP, (ofs))
#define emit_spstore(as, ir, r, ofs) \
  emit_storeofs(as, ir, (r), RID_SP, (ofs))

/* -- Register allocator debugging ---------------------------------------- */

/* #define LUAJIT_DEBUG_RA */

#ifdef LUAJIT_DEBUG_RA

#include <stdio.h>
#include <stdarg.h>

#define RIDNAME(name)	#name,
static const char *const ra_regname[] = {
  GPRDEF(RIDNAME)
  FPRDEF(RIDNAME)
  VRIDDEF(RIDNAME)
  NULL
};
#undef RIDNAME

static char ra_dbg_buf[65536];
static char *ra_dbg_p;
static char *ra_dbg_merge;
static MCode *ra_dbg_mcp;

static void ra_dstart(void)
{
  ra_dbg_p = ra_dbg_buf;
  ra_dbg_merge = NULL;
  ra_dbg_mcp = NULL;
}

static void ra_dflush(void)
{
  fwrite(ra_dbg_buf, 1, (size_t)(ra_dbg_p-ra_dbg_buf), stdout);
  ra_dstart();
}

static void ra_dprintf(ASMState *as, const char *fmt, ...)
{
  char *p;
  va_list argp;
  va_start(argp, fmt);
  p = ra_dbg_mcp == as->mcp ? ra_dbg_merge : ra_dbg_p;
  ra_dbg_mcp = NULL;
  p += sprintf(p, "%08x  \e[36m%04d ", (uintptr_t)as->mcp, as->curins-REF_BIAS);
  for (;;) {
    const char *e = strchr(fmt, '$');
    if (e == NULL) break;
    memcpy(p, fmt, (size_t)(e-fmt));
    p += e-fmt;
    if (e[1] == 'r') {
      Reg r = va_arg(argp, Reg) & RID_MASK;
      if (r <= RID_MAX) {
	const char *q;
	for (q = ra_regname[r]; *q; q++)
	  *p++ = *q >= 'A' && *q <= 'Z' ? *q + 0x20 : *q;
      } else {
	*p++ = '?';
	lj_assertA(0, "bad register %d for debug format \"%s\"", r, fmt);
      }
    } else if (e[1] == 'f' || e[1] == 'i') {
      IRRef ref;
      if (e[1] == 'f')
	ref = va_arg(argp, IRRef);
      else
	ref = va_arg(argp, IRIns *) - as->ir;
      if (ref >= REF_BIAS)
	p += sprintf(p, "%04d", ref - REF_BIAS);
      else
	p += sprintf(p, "K%03d", REF_BIAS - ref);
    } else if (e[1] == 's') {
      uint32_t slot = va_arg(argp, uint32_t);
      p += sprintf(p, "[sp+0x%x]", sps_scale(slot));
    } else if (e[1] == 'x') {
      p += sprintf(p, "%08x", va_arg(argp, int32_t));
    } else {
      lj_assertA(0, "bad debug format code");
    }
    fmt = e+2;
  }
  va_end(argp);
  while (*fmt)
    *p++ = *fmt++;
  *p++ = '\e'; *p++ = '['; *p++ = 'm'; *p++ = '\n';
  if (p > ra_dbg_buf+sizeof(ra_dbg_buf)-256) {
    fwrite(ra_dbg_buf, 1, (size_t)(p-ra_dbg_buf), stdout);
    p = ra_dbg_buf;
  }
  ra_dbg_p = p;
}

#define RA_DBG_START()	ra_dstart()
#define RA_DBG_FLUSH()	ra_dflush()
#define RA_DBG_REF() \
  do { char *_p = ra_dbg_p; ra_dprintf(as, ""); \
       ra_dbg_merge = _p; ra_dbg_mcp = as->mcp; } while (0)
#define RA_DBGX(x)	ra_dprintf x

#else
#define RA_DBG_START()	((void)0)
#define RA_DBG_FLUSH()	((void)0)
#define RA_DBG_REF()	((void)0)
#define RA_DBGX(x)	((void)0)
#endif

/* -- Register allocator -------------------------------------------------- */

#define ra_free(as, r)		rset_set(as->freeset, (r))
#define ra_modified(as, r)	rset_set(as->modset, (r))
#define ra_weak(as, r)		rset_set(as->weakset, (r))
#define ra_noweak(as, r)	rset_clear(as->weakset, (r))

#define ra_used(ir)		(ra_hasreg((ir)->r) || ra_hasspill((ir)->s))

/* Setup register allocator. */
static void ra_setup(ASMState *as)
{
  Reg r;
  /* Initially all regs (except the stack pointer) are free for use. */
  as->freeset = RSET_INIT;
  as->modset = RSET_EMPTY;
  as->weakset = RSET_EMPTY;
  as->phiset = RSET_EMPTY;
  memset(as->phireg, 0, sizeof(as->phireg));
  for (r = RID_MIN_GPR; r < RID_MAX; r++)
    as->cost[r] = REGCOST(~0u, 0u);
}

/* Rematerialize constants. */
static Reg ra_rematk(ASMState *as, IRRef ref)
{
  IRIns *ir;
  Reg r;
  if (ra_iskref(ref)) {
    r = ra_krefreg(ref);
    lj_assertA(!rset_test(as->freeset, r), "rematk of free reg %d", r);
    ra_free(as, r);
    ra_modified(as, r);
#if LJ_64
    emit_loadu64(as, r, ra_krefk(as, ref));
#else
    emit_loadi(as, r, ra_krefk(as, ref));
#endif
    return r;
  }
  ir = IR(ref);
  r = ir->r;
  lj_assertA(ra_hasreg(r), "rematk of K%03d has no reg", REF_BIAS - ref);
  lj_assertA(!ra_hasspill(ir->s),
	     "rematk of K%03d has spill slot [%x]", REF_BIAS - ref, ir->s);
  ra_free(as, r);
  ra_modified(as, r);
  ir->r = RID_INIT;  /* Do not keep any hint. */
  RA_DBGX((as, "remat     $i $r", ir, r));
#if !LJ_SOFTFP32
  if (ir->o == IR_KNUM) {
    emit_loadk64(as, r, ir);
  } else
#endif
  if (emit_canremat(REF_BASE) && ir->o == IR_BASE) {
    ra_sethint(ir->r, RID_BASE);  /* Restore BASE register hint. */
    emit_getgl(as, r, jit_base);
  } else if (emit_canremat(ASMREF_L) && ir->o == IR_KPRI) {
    /* REF_NIL stores ASMREF_L register. */
    lj_assertA(irt_isnil(ir->t), "rematk of bad ASMREF_L");
    emit_getgl(as, r, cur_L);
#if LJ_64
  } else if (ir->o == IR_KINT64) {
    emit_loadu64(as, r, ir_kint64(ir)->u64);
#if LJ_GC64
  } else if (ir->o == IR_KGC) {
    emit_loadu64(as, r, (uintptr_t)ir_kgc(ir));
  } else if (ir->o == IR_KPTR || ir->o == IR_KKPTR) {
    emit_loadu64(as, r, (uintptr_t)ir_kptr(ir));
#endif
#endif
  } else {
    lj_assertA(ir->o == IR_KINT || ir->o == IR_KGC ||
	       ir->o == IR_KPTR || ir->o == IR_KKPTR || ir->o == IR_KNULL,
	       "rematk of bad IR op %d", ir->o);
    emit_loadi(as, r, ir->i);
  }
  return r;
}

/* Force a spill. Allocate a new spill slot if needed. */
static int32_t ra_spill(ASMState *as, IRIns *ir)
{
  int32_t slot = ir->s;
  lj_assertA(ir >= as->ir + REF_TRUE,
	     "spill of K%03d", REF_BIAS - (int)(ir - as->ir));
  if (!ra_hasspill(slot)) {
    if (irt_is64(ir->t)) {
      slot = as->evenspill;
      as->evenspill += 2;
    } else if (as->oddspill) {
      slot = as->oddspill;
      as->oddspill = 0;
    } else {
      slot = as->evenspill;
      as->oddspill = slot+1;
      as->evenspill += 2;
    }
    if (as->evenspill > 256)
      lj_trace_err(as->J, LJ_TRERR_SPILLOV);
    ir->s = (uint8_t)slot;
  }
  return sps_scale(slot);
}

/* Release the temporarily allocated register in ASMREF_TMP1/ASMREF_TMP2. */
static Reg ra_releasetmp(ASMState *as, IRRef ref)
{
  IRIns *ir = IR(ref);
  Reg r = ir->r;
  lj_assertA(ra_hasreg(r), "release of TMP%d has no reg", ref-ASMREF_TMP1+1);
  lj_assertA(!ra_hasspill(ir->s),
	     "release of TMP%d has spill slot [%x]", ref-ASMREF_TMP1+1, ir->s);
  ra_free(as, r);
  ra_modified(as, r);
  ir->r = RID_INIT;
  return r;
}

/* Restore a register (marked as free). Rematerialize or force a spill. */
static Reg ra_restore(ASMState *as, IRRef ref)
{
  if (emit_canremat(ref)) {
    return ra_rematk(as, ref);
  } else {
    IRIns *ir = IR(ref);
    int32_t ofs = ra_spill(as, ir);  /* Force a spill slot. */
    Reg r = ir->r;
    lj_assertA(ra_hasreg(r), "restore of IR %04d has no reg", ref - REF_BIAS);
    ra_sethint(ir->r, r);  /* Keep hint. */
    ra_free(as, r);
    if (!rset_test(as->weakset, r)) {  /* Only restore non-weak references. */
      ra_modified(as, r);
      RA_DBGX((as, "restore   $i $r", ir, r));
      emit_spload(as, ir, r, ofs);
    }
    return r;
  }
}

/* Save a register to a spill slot. */
static void ra_save(ASMState *as, IRIns *ir, Reg r)
{
  RA_DBGX((as, "save      $i $r", ir, r));
  emit_spstore(as, ir, r, sps_scale(ir->s));
}

#define MINCOST(name) \
  if (rset_test(RSET_ALL, RID_##name) && \
      LJ_LIKELY(allow&RID2RSET(RID_##name)) && as->cost[RID_##name] < cost) \
    cost = as->cost[RID_##name];

/* Evict the register with the lowest cost, forcing a restore. */
static Reg ra_evict(ASMState *as, RegSet allow)
{
  IRRef ref;
  RegCost cost = ~(RegCost)0;
  lj_assertA(allow != RSET_EMPTY, "evict from empty set");
  if (RID_NUM_FPR == 0 || allow < RID2RSET(RID_MAX_GPR)) {
    GPRDEF(MINCOST)
  } else {
    FPRDEF(MINCOST)
  }
  ref = regcost_ref(cost);
  lj_assertA(ra_iskref(ref) || (ref >= as->T->nk && ref < as->T->nins),
	     "evict of out-of-range IR %04d", ref - REF_BIAS);
  /* Preferably pick any weak ref instead of a non-weak, non-const ref. */
  if (!irref_isk(ref) && (as->weakset & allow)) {
    IRIns *ir = IR(ref);
    if (!rset_test(as->weakset, ir->r))
      ref = regcost_ref(as->cost[rset_pickbot((as->weakset & allow))]);
  }
  return ra_restore(as, ref);
}

/* Pick any register (marked as free). Evict on-demand. */
static Reg ra_pick(ASMState *as, RegSet allow)
{
  RegSet pick = as->freeset & allow;
  if (!pick)
    return ra_evict(as, allow);
  else
    return rset_picktop(pick);
}

/* Get a scratch register (marked as free). */
static Reg ra_scratch(ASMState *as, RegSet allow)
{
  Reg r = ra_pick(as, allow);
  ra_modified(as, r);
  RA_DBGX((as, "scratch        $r", r));
  return r;
}

/* Evict all registers from a set (if not free). */
static void ra_evictset(ASMState *as, RegSet drop)
{
  RegSet work;
  as->modset |= drop;
#if !LJ_SOFTFP
  work = (drop & ~as->freeset) & RSET_FPR;
  while (work) {
    Reg r = rset_pickbot(work);
    ra_restore(as, regcost_ref(as->cost[r]));
    rset_clear(work, r);
    checkmclim(as);
  }
#endif
  work = (drop & ~as->freeset);
  while (work) {
    Reg r = rset_pickbot(work);
    ra_restore(as, regcost_ref(as->cost[r]));
    rset_clear(work, r);
    checkmclim(as);
  }
}

/* Evict (rematerialize) all registers allocated to constants. */
static void ra_evictk(ASMState *as)
{
  RegSet work;
#if !LJ_SOFTFP
  work = ~as->freeset & RSET_FPR;
  while (work) {
    Reg r = rset_pickbot(work);
    IRRef ref = regcost_ref(as->cost[r]);
    if (emit_canremat(ref) && irref_isk(ref)) {
      ra_rematk(as, ref);
      checkmclim(as);
    }
    rset_clear(work, r);
  }
#endif
  work = ~as->freeset & RSET_GPR;
  while (work) {
    Reg r = rset_pickbot(work);
    IRRef ref = regcost_ref(as->cost[r]);
    if (emit_canremat(ref) && irref_isk(ref)) {
      ra_rematk(as, ref);
      checkmclim(as);
    }
    rset_clear(work, r);
  }
}

#ifdef RID_NUM_KREF
/* Allocate a register for a constant. */
static Reg ra_allock(ASMState *as, intptr_t k, RegSet allow)
{
  /* First try to find a register which already holds the same constant. */
  RegSet pick, work = ~as->freeset & RSET_GPR;
  Reg r;
  while (work) {
    IRRef ref;
    r = rset_pickbot(work);
    ref = regcost_ref(as->cost[r]);
#if LJ_64
    if (ref < ASMREF_L) {
      if (ra_iskref(ref)) {
	if (k == ra_krefk(as, ref))
	  return r;
      } else {
	IRIns *ir = IR(ref);
	if ((ir->o == IR_KINT64 && k == (int64_t)ir_kint64(ir)->u64) ||
#if LJ_GC64
	    (ir->o == IR_KINT && k == ir->i) ||
	    (ir->o == IR_KGC && k == (intptr_t)ir_kgc(ir)) ||
	    ((ir->o == IR_KPTR || ir->o == IR_KKPTR) &&
	     k == (intptr_t)ir_kptr(ir))
#else
	    (ir->o != IR_KINT64 && k == ir->i)
#endif
	   )
	  return r;
      }
    }
#else
    if (ref < ASMREF_L &&
	k == (ra_iskref(ref) ? ra_krefk(as, ref) : IR(ref)->i))
      return r;
#endif
    rset_clear(work, r);
  }
  pick = as->freeset & allow;
  if (pick) {
    /* Constants should preferably get unmodified registers. */
    if ((pick & ~as->modset))
      pick &= ~as->modset;
    r = rset_pickbot(pick);  /* Reduce conflicts with inverse allocation. */
  } else {
    r = ra_evict(as, allow);
  }
  RA_DBGX((as, "allock    $x $r", k, r));
  ra_setkref(as, r, k);
  rset_clear(as->freeset, r);
  ra_noweak(as, r);
  return r;
}

/* Allocate a specific register for a constant. */
static void ra_allockreg(ASMState *as, intptr_t k, Reg r)
{
  Reg kr = ra_allock(as, k, RID2RSET(r));
  if (kr != r) {
    IRIns irdummy;
    irdummy.t.irt = IRT_INT;
    ra_scratch(as, RID2RSET(r));
    emit_movrr(as, &irdummy, r, kr);
  }
}
#else
#define ra_allockreg(as, k, r)		emit_loadi(as, (r), (k))
#endif

/* Allocate a register for ref from the allowed set of registers.
** Note: this function assumes the ref does NOT have a register yet!
** Picks an optimal register, sets the cost and marks the register as non-free.
*/
static Reg ra_allocref(ASMState *as, IRRef ref, RegSet allow)
{
  IRIns *ir = IR(ref);
  RegSet pick = as->freeset & allow;
  Reg r;
  lj_assertA(ra_noreg(ir->r),
	     "IR %04d already has reg %d", ref - REF_BIAS, ir->r);
  if (pick) {
    /* First check register hint from propagation or PHI. */
    if (ra_hashint(ir->r)) {
      r = ra_gethint(ir->r);
      if (rset_test(pick, r))  /* Use hint register if possible. */
	goto found;
      /* Rematerialization is cheaper than missing a hint. */
      if (rset_test(allow, r) && emit_canremat(regcost_ref(as->cost[r]))) {
	ra_rematk(as, regcost_ref(as->cost[r]));
	goto found;
      }
      RA_DBGX((as, "hintmiss  $f $r", ref, r));
    }
    /* Invariants should preferably get unmodified registers. */
    if (ref < as->loopref && !irt_isphi(ir->t)) {
      if ((pick & ~as->modset))
	pick &= ~as->modset;
      r = rset_pickbot(pick);  /* Reduce conflicts with inverse allocation. */
    } else {
      /* We've got plenty of regs, so get callee-save regs if possible. */
      if (RID_NUM_GPR > 8 && (pick & ~RSET_SCRATCH))
	pick &= ~RSET_SCRATCH;
      r = rset_picktop(pick);
    }
  } else {
    r = ra_evict(as, allow);
  }
found:
  RA_DBGX((as, "alloc     $f $r", ref, r));
  ir->r = (uint8_t)r;
  rset_clear(as->freeset, r);
  ra_noweak(as, r);
  as->cost[r] = REGCOST_REF_T(ref, irt_t(ir->t));
  return r;
}

/* Allocate a register on-demand. */
static Reg ra_alloc1(ASMState *as, IRRef ref, RegSet allow)
{
  Reg r = IR(ref)->r;
  /* Note: allow is ignored if the register is already allocated. */
  if (ra_noreg(r)) r = ra_allocref(as, ref, allow);
  ra_noweak(as, r);
  return r;
}

/* Add a register rename to the IR. */
static void ra_addrename(ASMState *as, Reg down, IRRef ref, SnapNo snapno)
{
  IRRef ren;
  lj_ir_set(as->J, IRT(IR_RENAME, IRT_NIL), ref, snapno);
  ren = tref_ref(lj_ir_emit(as->J));
  as->J->cur.ir[ren].r = (uint8_t)down;
  as->J->cur.ir[ren].s = SPS_NONE;
}

/* Rename register allocation and emit move. */
static void ra_rename(ASMState *as, Reg down, Reg up)
{
  IRRef ref = regcost_ref(as->cost[up] = as->cost[down]);
  IRIns *ir = IR(ref);
  ir->r = (uint8_t)up;
  as->cost[down] = 0;
  lj_assertA((down < RID_MAX_GPR) == (up < RID_MAX_GPR),
	     "rename between GPR/FPR %d and %d", down, up);
  lj_assertA(!rset_test(as->freeset, down), "rename from free reg %d", down);
  lj_assertA(rset_test(as->freeset, up), "rename to non-free reg %d", up);
  ra_free(as, down);  /* 'down' is free ... */
  ra_modified(as, down);
  rset_clear(as->freeset, up);  /* ... and 'up' is now allocated. */
  ra_noweak(as, up);
  RA_DBGX((as, "rename    $f $r $r", regcost_ref(as->cost[up]), down, up));
  emit_movrr(as, ir, down, up);  /* Backwards codegen needs inverse move. */
  if (!ra_hasspill(IR(ref)->s)) {  /* Add the rename to the IR. */
    /*
    ** The rename is effective at the subsequent (already emitted) exit
    ** branch. This is for the current snapshot (as->snapno). Except if we
    ** haven't yet allocated any refs for the snapshot (as->snapalloc == 1),
    ** then it belongs to the next snapshot.
    ** See also the discussion at asm_snap_checkrename().
    */
    ra_addrename(as, down, ref, as->snapno + as->snapalloc);
  }
}

/* Pick a destination register (marked as free).
** Caveat: allow is ignored if there's already a destination register.
** Use ra_destreg() to get a specific register.
*/
static Reg ra_dest(ASMState *as, IRIns *ir, RegSet allow)
{
  Reg dest = ir->r;
  if (ra_hasreg(dest)) {
    ra_free(as, dest);
    ra_modified(as, dest);
  } else {
    if (ra_hashint(dest) && rset_test((as->freeset&allow), ra_gethint(dest))) {
      dest = ra_gethint(dest);
      ra_modified(as, dest);
      RA_DBGX((as, "dest           $r", dest));
    } else {
      dest = ra_scratch(as, allow);
    }
    ir->r = dest;
  }
  if (LJ_UNLIKELY(ra_hasspill(ir->s))) ra_save(as, ir, dest);
  return dest;
}

/* Force a specific destination register (marked as free). */
static void ra_destreg(ASMState *as, IRIns *ir, Reg r)
{
  Reg dest = ra_dest(as, ir, RID2RSET(r));
  if (dest != r) {
    lj_assertA(rset_test(as->freeset, r), "dest reg %d is not free", r);
    ra_modified(as, r);
    emit_movrr(as, ir, dest, r);
  }
}

#if LJ_TARGET_X86ORX64
/* Propagate dest register to left reference. Emit moves as needed.
** This is a required fixup step for all 2-operand machine instructions.
*/
static void ra_left(ASMState *as, Reg dest, IRRef lref)
{
  IRIns *ir = IR(lref);
  Reg left = ir->r;
  if (ra_noreg(left)) {
    if (irref_isk(lref)) {
      if (ir->o == IR_KNUM) {
	/* FP remat needs a load except for +0. Still better than eviction. */
	if (tvispzero(ir_knum(ir)) || !(as->freeset & RSET_FPR)) {
	  emit_loadk64(as, dest, ir);
	  return;
	}
#if LJ_64
      } else if (ir->o == IR_KINT64) {
	emit_loadk64(as, dest, ir);
	return;
#if LJ_GC64
      } else if (ir->o == IR_KGC || ir->o == IR_KPTR || ir->o == IR_KKPTR) {
	emit_loadk64(as, dest, ir);
	return;
#endif
#endif
      } else if (ir->o != IR_KPRI) {
	lj_assertA(ir->o == IR_KINT || ir->o == IR_KGC ||
		   ir->o == IR_KPTR || ir->o == IR_KKPTR || ir->o == IR_KNULL,
		   "K%03d has bad IR op %d", REF_BIAS - lref, ir->o);
	emit_loadi(as, dest, ir->i);
	return;
      }
    }
    if (!ra_hashint(left) && !iscrossref(as, lref))
      ra_sethint(ir->r, dest);  /* Propagate register hint. */
    left = ra_allocref(as, lref, dest < RID_MAX_GPR ? RSET_GPR : RSET_FPR);
  }
  ra_noweak(as, left);
  /* Move needed for true 3-operand instruction: y=a+b ==> y=a; y+=b. */
  if (dest != left) {
    /* Use register renaming if dest is the PHI reg. */
    if (irt_isphi(ir->t) && as->phireg[dest] == lref) {
      ra_modified(as, left);
      ra_rename(as, left, dest);
    } else {
      emit_movrr(as, ir, dest, left);
    }
  }
}
#else
/* Similar to ra_left, except we override any hints. */
static void ra_leftov(ASMState *as, Reg dest, IRRef lref)
{
  IRIns *ir = IR(lref);
  Reg left = ir->r;
  if (ra_noreg(left)) {
    ra_sethint(ir->r, dest);  /* Propagate register hint. */
    left = ra_allocref(as, lref,
		       (LJ_SOFTFP || dest < RID_MAX_GPR) ? RSET_GPR : RSET_FPR);
  }
  ra_noweak(as, left);
  if (dest != left) {
    /* Use register renaming if dest is the PHI reg. */
    if (irt_isphi(ir->t) && as->phireg[dest] == lref) {
      ra_modified(as, left);
      ra_rename(as, left, dest);
    } else {
      emit_movrr(as, ir, dest, left);
    }
  }
}
#endif

/* Force a RID_RETLO/RID_RETHI destination register pair (marked as free). */
static void ra_destpair(ASMState *as, IRIns *ir)
{
  Reg destlo = ir->r, desthi = (ir+1)->r;
  IRIns *irx = (LJ_64 && !irt_is64(ir->t)) ? ir+1 : ir;
  /* First spill unrelated refs blocking the destination registers. */
  if (!rset_test(as->freeset, RID_RETLO) &&
      destlo != RID_RETLO && desthi != RID_RETLO)
    ra_restore(as, regcost_ref(as->cost[RID_RETLO]));
  if (!rset_test(as->freeset, RID_RETHI) &&
      destlo != RID_RETHI && desthi != RID_RETHI)
    ra_restore(as, regcost_ref(as->cost[RID_RETHI]));
  /* Next free the destination registers (if any). */
  if (ra_hasreg(destlo)) {
    ra_free(as, destlo);
    ra_modified(as, destlo);
  } else {
    destlo = RID_RETLO;
  }
  if (ra_hasreg(desthi)) {
    ra_free(as, desthi);
    ra_modified(as, desthi);
  } else {
    desthi = RID_RETHI;
  }
  /* Check for conflicts and shuffle the registers as needed. */
  if (destlo == RID_RETHI) {
    if (desthi == RID_RETLO) {
#if LJ_TARGET_X86ORX64
      *--as->mcp = XI_XCHGa + RID_RETHI;
      if (LJ_64 && irt_is64(irx->t)) *--as->mcp = 0x48;
#else
      emit_movrr(as, irx, RID_RETHI, RID_TMP);
      emit_movrr(as, irx, RID_RETLO, RID_RETHI);
      emit_movrr(as, irx, RID_TMP, RID_RETLO);
#endif
    } else {
      emit_movrr(as, irx, RID_RETHI, RID_RETLO);
      if (desthi != RID_RETHI) emit_movrr(as, irx, desthi, RID_RETHI);
    }
  } else if (desthi == RID_RETLO) {
    emit_movrr(as, irx, RID_RETLO, RID_RETHI);
    if (destlo != RID_RETLO) emit_movrr(as, irx, destlo, RID_RETLO);
  } else {
    if (desthi != RID_RETHI) emit_movrr(as, irx, desthi, RID_RETHI);
    if (destlo != RID_RETLO) emit_movrr(as, irx, destlo, RID_RETLO);
  }
  /* Restore spill slots (if any). */
  if (ra_hasspill((ir+1)->s)) ra_save(as, ir+1, RID_RETHI);
  if (ra_hasspill(ir->s)) ra_save(as, ir, RID_RETLO);
}

/* -- Snapshot handling --------- ----------------------------------------- */

/* Can we rematerialize a KNUM instead of forcing a spill? */
static int asm_snap_canremat(ASMState *as)
{
  Reg r;
  for (r = RID_MIN_FPR; r < RID_MAX_FPR; r++)
    if (irref_isk(regcost_ref(as->cost[r])))
      return 1;
  return 0;
}

/* Check whether a sunk store corresponds to an allocation. */
static int asm_sunk_store(ASMState *as, IRIns *ira, IRIns *irs)
{
  if (irs->s == 255) {
    if (irs->o == IR_ASTORE || irs->o == IR_HSTORE ||
	irs->o == IR_FSTORE || irs->o == IR_XSTORE) {
      IRIns *irk = IR(irs->op1);
      if (irk->o == IR_AREF || irk->o == IR_HREFK)
	irk = IR(irk->op1);
      return (IR(irk->op1) == ira);
    }
    return 0;
  } else {
    return (ira + irs->s == irs);  /* Quick check. */
  }
}

/* Allocate register or spill slot for a ref that escapes to a snapshot. */
static void asm_snap_alloc1(ASMState *as, IRRef ref)
{
  IRIns *ir = IR(ref);
  if (!irref_isk(ref) && ir->r != RID_SUNK) {
    bloomset(as->snapfilt1, ref);
    bloomset(as->snapfilt2, hashrot(ref, ref + HASH_BIAS));
    if (ra_used(ir)) return;
    if (ir->r == RID_SINK) {
      ir->r = RID_SUNK;
#if LJ_HASFFI
      if (ir->o == IR_CNEWI) {  /* Allocate CNEWI value. */
	asm_snap_alloc1(as, ir->op2);
	if (LJ_32 && (ir+1)->o == IR_HIOP)
	  asm_snap_alloc1(as, (ir+1)->op2);
      } else
#endif
      {  /* Allocate stored values for TNEW, TDUP and CNEW. */
	IRIns *irs;
	lj_assertA(ir->o == IR_TNEW || ir->o == IR_TDUP || ir->o == IR_CNEW,
		   "sink of IR %04d has bad op %d", ref - REF_BIAS, ir->o);
	for (irs = IR(as->snapref-1); irs > ir; irs--)
	  if (irs->r == RID_SINK && asm_sunk_store(as, ir, irs)) {
	    lj_assertA(irs->o == IR_ASTORE || irs->o == IR_HSTORE ||
		       irs->o == IR_FSTORE || irs->o == IR_XSTORE,
		       "sunk store IR %04d has bad op %d",
		       (int)(irs - as->ir) - REF_BIAS, irs->o);
	    asm_snap_alloc1(as, irs->op2);
	    if (LJ_32 && (irs+1)->o == IR_HIOP)
	      asm_snap_alloc1(as, (irs+1)->op2);
	  }
      }
    } else {
      RegSet allow;
      if (ir->o == IR_CONV && ir->op2 == IRCONV_NUM_INT) {
	IRIns *irc;
	for (irc = IR(as->curins); irc > ir; irc--)
	  if ((irc->op1 == ref || irc->op2 == ref) &&
	      !(irc->r == RID_SINK || irc->r == RID_SUNK))
	    goto nosink;  /* Don't sink conversion if result is used. */
	asm_snap_alloc1(as, ir->op1);
	return;
      }
    nosink:
      allow = (!LJ_SOFTFP && irt_isfp(ir->t)) ? RSET_FPR : RSET_GPR;
      if ((as->freeset & allow) ||
	       (allow == RSET_FPR && asm_snap_canremat(as))) {
	/* Get a weak register if we have a free one or can rematerialize. */
	Reg r = ra_allocref(as, ref, allow);  /* Allocate a register. */
	if (!irt_isphi(ir->t))
	  ra_weak(as, r);  /* But mark it as weakly referenced. */
	checkmclim(as);
	RA_DBGX((as, "snapreg   $f $r", ref, ir->r));
      } else {
	ra_spill(as, ir);  /* Otherwise force a spill slot. */
	RA_DBGX((as, "snapspill $f $s", ref, ir->s));
      }
    }
  }
}

/* Allocate refs escaping to a snapshot. */
static void asm_snap_alloc(ASMState *as, int snapno)
{
  SnapShot *snap = &as->T->snap[snapno];
  SnapEntry *map = &as->T->snapmap[snap->mapofs];
  MSize n, nent = snap->nent;
  as->snapfilt1 = as->snapfilt2 = 0;
  for (n = 0; n < nent; n++) {
    SnapEntry sn = map[n];
    IRRef ref = snap_ref(sn);
    if (!irref_isk(ref)) {
      asm_snap_alloc1(as, ref);
      if (LJ_SOFTFP && (sn & SNAP_SOFTFPNUM)) {
	lj_assertA(irt_type(IR(ref+1)->t) == IRT_SOFTFP,
		   "snap %d[%d] points to bad SOFTFP IR %04d",
		   snapno, n, ref - REF_BIAS);
	asm_snap_alloc1(as, ref+1);
      }
    }
  }
}

/* All guards for a snapshot use the same exitno. This is currently the
** same as the snapshot number. Since the exact origin of the exit cannot
** be determined, all guards for the same snapshot must exit with the same
** RegSP mapping.
** A renamed ref which has been used in a prior guard for the same snapshot
** would cause an inconsistency. The easy way out is to force a spill slot.
*/
static int asm_snap_checkrename(ASMState *as, IRRef ren)
{
  if (bloomtest(as->snapfilt1, ren) &&
      bloomtest(as->snapfilt2, hashrot(ren, ren + HASH_BIAS))) {
    IRIns *ir = IR(ren);
    ra_spill(as, ir);  /* Register renamed, so force a spill slot. */
    RA_DBGX((as, "snaprensp $f $s", ren, ir->s));
    return 1;  /* Found. */
  }
  return 0;  /* Not found. */
}

/* Prepare snapshot for next guard or throwing instruction. */
static void asm_snap_prep(ASMState *as)
{
  if (as->snapalloc) {
    /* Alloc on first invocation for each snapshot. */
    as->snapalloc = 0;
    asm_snap_alloc(as, as->snapno);
    as->snaprename = as->T->nins;
  } else {
    /* Check any renames above the highwater mark. */
    for (; as->snaprename < as->T->nins; as->snaprename++) {
      IRIns *ir = &as->T->ir[as->snaprename];
      if (asm_snap_checkrename(as, ir->op1))
	ir->op2 = REF_BIAS-1;  /* Kill rename. */
    }
  }
}

/* Move to previous snapshot when we cross the current snapshot ref. */
static void asm_snap_prev(ASMState *as)
{
  if (as->curins < as->snapref) {
    uintptr_t ofs = (uintptr_t)(as->mctoporig - as->mcp);
    if (ofs >= 0x10000) lj_trace_err(as->J, LJ_TRERR_MCODEOV);
    do {
      if (as->snapno == 0) return;
      as->snapno--;
      as->snapref = as->T->snap[as->snapno].ref;
      as->T->snap[as->snapno].mcofs = (uint16_t)ofs;  /* Remember mcode ofs. */
    } while (as->curins < as->snapref);  /* May have no ins inbetween. */
    as->snapalloc = 1;
  }
}

/* Fixup snapshot mcode offsetst. */
static void asm_snap_fixup_mcofs(ASMState *as)
{
  uint32_t sz = (uint32_t)(as->mctoporig - as->mcp);
  SnapShot *snap = as->T->snap;
  SnapNo i;
  for (i = as->T->nsnap-1; i > 0; i--) {
    /* Compute offset from mcode start and store in correct snapshot. */
    snap[i].mcofs = (uint16_t)(sz - snap[i-1].mcofs);
  }
  snap[0].mcofs = 0;
}

/* -- Miscellaneous helpers ----------------------------------------------- */

/* Calculate stack adjustment. */
static int32_t asm_stack_adjust(ASMState *as)
{
  if (as->evenspill <= SPS_FIXED)
    return 0;
  return sps_scale(sps_align(as->evenspill));
}

/* Must match with hash*() in lj_tab.c. */
static uint32_t ir_khash(ASMState *as, IRIns *ir)
{
  uint32_t lo, hi;
  UNUSED(as);
  if (irt_isstr(ir->t)) {
    return ir_kstr(ir)->sid;
  } else if (irt_isnum(ir->t)) {
    lo = ir_knum(ir)->u32.lo;
    hi = ir_knum(ir)->u32.hi << 1;
  } else if (irt_ispri(ir->t)) {
    lj_assertA(!irt_isnil(ir->t), "hash of nil key");
    return irt_type(ir->t)-IRT_FALSE;
  } else {
    lj_assertA(irt_isgcv(ir->t), "hash of bad IR type %d", irt_type(ir->t));
    lo = u32ptr(ir_kgc(ir));
#if LJ_GC64
    hi = (uint32_t)(u64ptr(ir_kgc(ir)) >> 32) | (irt_toitype(ir->t) << 15);
#else
    hi = lo + HASH_BIAS;
#endif
  }
  return hashrot(lo, hi);
}

/* -- Allocations --------------------------------------------------------- */

static void asm_gencall(ASMState *as, const CCallInfo *ci, IRRef *args);
static void asm_setupresult(ASMState *as, IRIns *ir, const CCallInfo *ci);

static void asm_snew(ASMState *as, IRIns *ir)
{
  const CCallInfo *ci = &lj_ir_callinfo[IRCALL_lj_str_new];
  IRRef args[3];
  asm_snap_prep(as);
  args[0] = ASMREF_L;  /* lua_State *L    */
  args[1] = ir->op1;   /* const char *str */
  args[2] = ir->op2;   /* size_t len      */
  as->gcsteps++;
  asm_setupresult(as, ir, ci);  /* GCstr * */
  asm_gencall(as, ci, args);
}

static void asm_tnew(ASMState *as, IRIns *ir)
{
  const CCallInfo *ci = &lj_ir_callinfo[IRCALL_lj_tab_new1];
  IRRef args[2];
  asm_snap_prep(as);
  args[0] = ASMREF_L;     /* lua_State *L    */
  args[1] = ASMREF_TMP1;  /* uint32_t ahsize */
  as->gcsteps++;
  asm_setupresult(as, ir, ci);  /* GCtab * */
  asm_gencall(as, ci, args);
  ra_allockreg(as, ir->op1 | (ir->op2 << 24), ra_releasetmp(as, ASMREF_TMP1));
}

static void asm_tdup(ASMState *as, IRIns *ir)
{
  const CCallInfo *ci = &lj_ir_callinfo[IRCALL_lj_tab_dup];
  IRRef args[2];
  asm_snap_prep(as);
  args[0] = ASMREF_L;  /* lua_State *L    */
  args[1] = ir->op1;   /* const GCtab *kt */
  as->gcsteps++;
  asm_setupresult(as, ir, ci);  /* GCtab * */
  asm_gencall(as, ci, args);
}

static void asm_gc_check(ASMState *as);

/* Explicit GC step. */
static void asm_gcstep(ASMState *as, IRIns *ir)
{
  IRIns *ira;
  for (ira = IR(as->stopins+1); ira < ir; ira++)
    if ((ira->o == IR_TNEW || ira->o == IR_TDUP ||
	 (LJ_HASFFI && (ira->o == IR_CNEW || ira->o == IR_CNEWI))) &&
	ra_used(ira))
      as->gcsteps++;
  if (as->gcsteps)
    asm_gc_check(as);
  as->gcsteps = 0x80000000;  /* Prevent implicit GC check further up. */
}

/* -- Buffer operations --------------------------------------------------- */

static void asm_tvptr(ASMState *as, Reg dest, IRRef ref, MSize mode);
#if LJ_HASBUFFER
static void asm_bufhdr_write(ASMState *as, Reg sb);
#endif

static void asm_bufhdr(ASMState *as, IRIns *ir)
{
  Reg sb = ra_dest(as, ir, RSET_GPR);
  switch (ir->op2) {
  case IRBUFHDR_RESET: {
    Reg tmp = ra_scratch(as, rset_exclude(RSET_GPR, sb));
    IRIns irbp;
    irbp.ot = IRT(0, IRT_PTR);  /* Buffer data pointer type. */
    emit_storeofs(as, &irbp, tmp, sb, offsetof(SBuf, w));
    emit_loadofs(as, &irbp, tmp, sb, offsetof(SBuf, b));
    break;
    }
  case IRBUFHDR_APPEND: {
    /* Rematerialize const buffer pointer instead of likely spill. */
    IRIns *irp = IR(ir->op1);
    if (!(ra_hasreg(irp->r) || irp == ir-1 ||
	  (irp == ir-2 && !ra_used(ir-1)))) {
      while (!(irp->o == IR_BUFHDR && irp->op2 == IRBUFHDR_RESET))
	irp = IR(irp->op1);
      if (irref_isk(irp->op1)) {
	ra_weak(as, ra_allocref(as, ir->op1, RSET_GPR));
	ir = irp;
      }
    }
    break;
    }
#if LJ_HASBUFFER
  case IRBUFHDR_WRITE:
    asm_bufhdr_write(as, sb);
    break;
#endif
  default: lj_assertA(0, "bad BUFHDR op2 %d", ir->op2); break;
  }
#if LJ_TARGET_X86ORX64
  ra_left(as, sb, ir->op1);
#else
  ra_leftov(as, sb, ir->op1);
#endif
}

static void asm_bufput(ASMState *as, IRIns *ir)
{
  const CCallInfo *ci = &lj_ir_callinfo[IRCALL_lj_buf_putstr];
  IRRef args[3];
  IRIns *irs;
  int kchar = -129;
  args[0] = ir->op1;  /* SBuf * */
  args[1] = ir->op2;  /* GCstr * */
  irs = IR(ir->op2);
  lj_assertA(irt_isstr(irs->t),
	     "BUFPUT of non-string IR %04d", ir->op2 - REF_BIAS);
  if (irs->o == IR_KGC) {
    GCstr *s = ir_kstr(irs);
    if (s->len == 1) {  /* Optimize put of single-char string constant. */
      kchar = (int8_t)strdata(s)[0];  /* Signed! */
      args[1] = ASMREF_TMP1;  /* int, truncated to char */
      ci = &lj_ir_callinfo[IRCALL_lj_buf_putchar];
    }
  } else if (mayfuse(as, ir->op2) && ra_noreg(irs->r)) {
    if (irs->o == IR_TOSTR) {  /* Fuse number to string conversions. */
      if (irs->op2 == IRTOSTR_NUM) {
	args[1] = ASMREF_TMP1;  /* TValue * */
	ci = &lj_ir_callinfo[IRCALL_lj_strfmt_putnum];
      } else {
	lj_assertA(irt_isinteger(IR(irs->op1)->t),
		   "TOSTR of non-numeric IR %04d", irs->op1);
	args[1] = irs->op1;  /* int */
	if (irs->op2 == IRTOSTR_INT)
	  ci = &lj_ir_callinfo[IRCALL_lj_strfmt_putint];
	else
	  ci = &lj_ir_callinfo[IRCALL_lj_buf_putchar];
      }
    } else if (irs->o == IR_SNEW) {  /* Fuse string allocation. */
      args[1] = irs->op1;  /* const void * */
      args[2] = irs->op2;  /* MSize */
      ci = &lj_ir_callinfo[IRCALL_lj_buf_putmem];
    }
  }
  asm_setupresult(as, ir, ci);  /* SBuf * */
  asm_gencall(as, ci, args);
  if (args[1] == ASMREF_TMP1) {
    Reg tmp = ra_releasetmp(as, ASMREF_TMP1);
    if (kchar == -129)
      asm_tvptr(as, tmp, irs->op1, IRTMPREF_IN1);
    else
      ra_allockreg(as, kchar, tmp);
  }
}

static void asm_bufstr(ASMState *as, IRIns *ir)
{
  const CCallInfo *ci = &lj_ir_callinfo[IRCALL_lj_buf_tostr];
  IRRef args[1];
  args[0] = ir->op1;  /* SBuf *sb */
  as->gcsteps++;
  asm_setupresult(as, ir, ci);  /* GCstr * */
  asm_gencall(as, ci, args);
}

/* -- Type conversions ---------------------------------------------------- */

static void asm_tostr(ASMState *as, IRIns *ir)
{
  const CCallInfo *ci;
  IRRef args[2];
  asm_snap_prep(as);
  args[0] = ASMREF_L;
  as->gcsteps++;
  if (ir->op2 == IRTOSTR_NUM) {
    args[1] = ASMREF_TMP1;  /* cTValue * */
    ci = &lj_ir_callinfo[IRCALL_lj_strfmt_num];
  } else {
    args[1] = ir->op1;  /* int32_t k */
    if (ir->op2 == IRTOSTR_INT)
      ci = &lj_ir_callinfo[IRCALL_lj_strfmt_int];
    else
      ci = &lj_ir_callinfo[IRCALL_lj_strfmt_char];
  }
  asm_setupresult(as, ir, ci);  /* GCstr * */
  asm_gencall(as, ci, args);
  if (ir->op2 == IRTOSTR_NUM)
    asm_tvptr(as, ra_releasetmp(as, ASMREF_TMP1), ir->op1, IRTMPREF_IN1);
}

#if LJ_32 && LJ_HASFFI && !LJ_SOFTFP && !LJ_TARGET_X86
static void asm_conv64(ASMState *as, IRIns *ir)
{
  IRType st = (IRType)((ir-1)->op2 & IRCONV_SRCMASK);
  IRType dt = (((ir-1)->op2 & IRCONV_DSTMASK) >> IRCONV_DSH);
  IRCallID id;
  IRRef args[2];
  lj_assertA((ir-1)->o == IR_CONV && ir->o == IR_HIOP,
	     "not a CONV/HIOP pair at IR %04d", (int)(ir - as->ir) - REF_BIAS);
  args[LJ_BE] = (ir-1)->op1;
  args[LJ_LE] = ir->op1;
  if (st == IRT_NUM || st == IRT_FLOAT) {
    id = IRCALL_fp64_d2l + ((st == IRT_FLOAT) ? 2 : 0) + (dt - IRT_I64);
    ir--;
  } else {
    id = IRCALL_fp64_l2d + ((dt == IRT_FLOAT) ? 2 : 0) + (st - IRT_I64);
  }
  {
#if LJ_TARGET_ARM && !LJ_ABI_SOFTFP
    CCallInfo cim = lj_ir_callinfo[id], *ci = &cim;
    cim.flags |= CCI_VARARG;  /* These calls don't use the hard-float ABI! */
#else
    const CCallInfo *ci = &lj_ir_callinfo[id];
#endif
    asm_setupresult(as, ir, ci);
    asm_gencall(as, ci, args);
  }
}
#endif

/* -- Memory references --------------------------------------------------- */

static void asm_newref(ASMState *as, IRIns *ir)
{
  const CCallInfo *ci = &lj_ir_callinfo[IRCALL_lj_tab_newkey];
  IRRef args[3];
  if (ir->r == RID_SINK)
    return;
  asm_snap_prep(as);
  args[0] = ASMREF_L;     /* lua_State *L */
  args[1] = ir->op1;      /* GCtab *t     */
  args[2] = ASMREF_TMP1;  /* cTValue *key */
  asm_setupresult(as, ir, ci);  /* TValue * */
  asm_gencall(as, ci, args);
  asm_tvptr(as, ra_releasetmp(as, ASMREF_TMP1), ir->op2, IRTMPREF_IN1);
}

static void asm_tmpref(ASMState *as, IRIns *ir)
{
  Reg r = ra_dest(as, ir, RSET_GPR);
  asm_tvptr(as, r, ir->op1, ir->op2);
}

static void asm_lref(ASMState *as, IRIns *ir)
{
  Reg r = ra_dest(as, ir, RSET_GPR);
#if LJ_TARGET_X86ORX64
  ra_left(as, r, ASMREF_L);
#else
  ra_leftov(as, r, ASMREF_L);
#endif
}

/* -- Calls --------------------------------------------------------------- */

/* Collect arguments from CALL* and CARG instructions. */
static void asm_collectargs(ASMState *as, IRIns *ir,
			    const CCallInfo *ci, IRRef *args)
{
  uint32_t n = CCI_XNARGS(ci);
  /* Account for split args. */
  lj_assertA(n <= CCI_NARGS_MAX*2, "too many args %d to collect", n);
  if ((ci->flags & CCI_L)) { *args++ = ASMREF_L; n--; }
  while (n-- > 1) {
    ir = IR(ir->op1);
    lj_assertA(ir->o == IR_CARG, "malformed CALL arg tree");
    args[n] = ir->op2 == REF_NIL ? 0 : ir->op2;
  }
  args[0] = ir->op1 == REF_NIL ? 0 : ir->op1;
  lj_assertA(IR(ir->op1)->o != IR_CARG, "malformed CALL arg tree");
}

/* Reconstruct CCallInfo flags for CALLX*. */
static uint32_t asm_callx_flags(ASMState *as, IRIns *ir)
{
  uint32_t nargs = 0;
  if (ir->op1 != REF_NIL) {  /* Count number of arguments first. */
    IRIns *ira = IR(ir->op1);
    nargs++;
    while (ira->o == IR_CARG) { nargs++; ira = IR(ira->op1); }
  }
#if LJ_HASFFI
  if (IR(ir->op2)->o == IR_CARG) {  /* Copy calling convention info. */
    CTypeID id = (CTypeID)IR(IR(ir->op2)->op2)->i;
    CType *ct = ctype_get(ctype_ctsG(J2G(as->J)), id);
    nargs |= ((ct->info & CTF_VARARG) ? CCI_VARARG : 0);
#if LJ_TARGET_X86
    nargs |= (ctype_cconv(ct->info) << CCI_CC_SHIFT);
#endif
  }
#endif
  return (nargs | (ir->t.irt << CCI_OTSHIFT));
}

static void asm_callid(ASMState *as, IRIns *ir, IRCallID id)
{
  const CCallInfo *ci = &lj_ir_callinfo[id];
  IRRef args[2];
  args[0] = ir->op1;
  args[1] = ir->op2;
  asm_setupresult(as, ir, ci);
  asm_gencall(as, ci, args);
}

static void asm_call(ASMState *as, IRIns *ir)
{
  IRRef args[CCI_NARGS_MAX];
  const CCallInfo *ci = &lj_ir_callinfo[ir->op2];
  asm_collectargs(as, ir, ci, args);
  asm_setupresult(as, ir, ci);
  asm_gencall(as, ci, args);
}

/* -- PHI and loop handling ----------------------------------------------- */

/* Break a PHI cycle by renaming to a free register (evict if needed). */
static void asm_phi_break(ASMState *as, RegSet blocked, RegSet blockedby,
			  RegSet allow)
{
  RegSet candidates = blocked & allow;
  if (candidates) {  /* If this register file has candidates. */
    /* Note: the set for ra_pick cannot be empty, since each register file
    ** has some registers never allocated to PHIs.
    */
    Reg down, up = ra_pick(as, ~blocked & allow);  /* Get a free register. */
    if (candidates & ~blockedby)  /* Optimize shifts, else it's a cycle. */
      candidates = candidates & ~blockedby;
    down = rset_picktop(candidates);  /* Pick candidate PHI register. */
    ra_rename(as, down, up);  /* And rename it to the free register. */
  }
}

/* PHI register shuffling.
**
** The allocator tries hard to preserve PHI register assignments across
** the loop body. Most of the time this loop does nothing, since there
** are no register mismatches.
**
** If a register mismatch is detected and ...
** - the register is currently free: rename it.
** - the register is blocked by an invariant: restore/remat and rename it.
** - Otherwise the register is used by another PHI, so mark it as blocked.
**
** The renames are order-sensitive, so just retry the loop if a register
** is marked as blocked, but has been freed in the meantime. A cycle is
** detected if all of the blocked registers are allocated. To break the
** cycle rename one of them to a free register and retry.
**
** Note that PHI spill slots are kept in sync and don't need to be shuffled.
*/
static void asm_phi_shuffle(ASMState *as)
{
  RegSet work;

  /* Find and resolve PHI register mismatches. */
  for (;;) {
    RegSet blocked = RSET_EMPTY;
    RegSet blockedby = RSET_EMPTY;
    RegSet phiset = as->phiset;
    while (phiset) {  /* Check all left PHI operand registers. */
      Reg r = rset_pickbot(phiset);
      IRIns *irl = IR(as->phireg[r]);
      Reg left = irl->r;
      if (r != left) {  /* Mismatch? */
	if (!rset_test(as->freeset, r)) {  /* PHI register blocked? */
	  IRRef ref = regcost_ref(as->cost[r]);
	  /* Blocked by other PHI (w/reg)? */
	  if (!ra_iskref(ref) && irt_ismarked(IR(ref)->t)) {
	    rset_set(blocked, r);
	    if (ra_hasreg(left))
	      rset_set(blockedby, left);
	    left = RID_NONE;
	  } else {  /* Otherwise grab register from invariant. */
	    ra_restore(as, ref);
	    checkmclim(as);
	  }
	}
	if (ra_hasreg(left)) {
	  ra_rename(as, left, r);
	  checkmclim(as);
	}
      }
      rset_clear(phiset, r);
    }
    if (!blocked) break;  /* Finished. */
    if (!(as->freeset & blocked)) {  /* Break cycles if none are free. */
      asm_phi_break(as, blocked, blockedby, RSET_GPR);
      if (!LJ_SOFTFP) asm_phi_break(as, blocked, blockedby, RSET_FPR);
      checkmclim(as);
    }  /* Else retry some more renames. */
  }

  /* Restore/remat invariants whose registers are modified inside the loop. */
#if !LJ_SOFTFP
  work = as->modset & ~(as->freeset | as->phiset) & RSET_FPR;
  while (work) {
    Reg r = rset_pickbot(work);
    ra_restore(as, regcost_ref(as->cost[r]));
    rset_clear(work, r);
    checkmclim(as);
  }
#endif
  work = as->modset & ~(as->freeset | as->phiset);
  while (work) {
    Reg r = rset_pickbot(work);
    ra_restore(as, regcost_ref(as->cost[r]));
    rset_clear(work, r);
    checkmclim(as);
  }

  /* Allocate and save all unsaved PHI regs and clear marks. */
  work = as->phiset;
  while (work) {
    Reg r = rset_picktop(work);
    IRRef lref = as->phireg[r];
    IRIns *ir = IR(lref);
    if (ra_hasspill(ir->s)) {  /* Left PHI gained a spill slot? */
      irt_clearmark(ir->t);  /* Handled here, so clear marker now. */
      ra_alloc1(as, lref, RID2RSET(r));
      ra_save(as, ir, r);  /* Save to spill slot inside the loop. */
      checkmclim(as);
    }
    rset_clear(work, r);
  }
}

/* Copy unsynced left/right PHI spill slots. Rarely needed. */
static void asm_phi_copyspill(ASMState *as)
{
  int need = 0;
  IRIns *ir;
  for (ir = IR(as->orignins-1); ir->o == IR_PHI; ir--)
    if (ra_hasspill(ir->s) && ra_hasspill(IR(ir->op1)->s))
      need |= irt_isfp(ir->t) ? 2 : 1;  /* Unsynced spill slot? */
  if ((need & 1)) {  /* Copy integer spill slots. */
#if !LJ_TARGET_X86ORX64
    Reg r = RID_TMP;
#else
    Reg r = RID_RET;
    if ((as->freeset & RSET_GPR))
      r = rset_pickbot((as->freeset & RSET_GPR));
    else
      emit_spload(as, IR(regcost_ref(as->cost[r])), r, SPOFS_TMP);
#endif
    for (ir = IR(as->orignins-1); ir->o == IR_PHI; ir--) {
      if (ra_hasspill(ir->s)) {
	IRIns *irl = IR(ir->op1);
	if (ra_hasspill(irl->s) && !irt_isfp(ir->t)) {
	  emit_spstore(as, irl, r, sps_scale(irl->s));
	  emit_spload(as, ir, r, sps_scale(ir->s));
	  checkmclim(as);
	}
      }
    }
#if LJ_TARGET_X86ORX64
    if (!rset_test(as->freeset, r))
      emit_spstore(as, IR(regcost_ref(as->cost[r])), r, SPOFS_TMP);
#endif
  }
#if !LJ_SOFTFP
  if ((need & 2)) {  /* Copy FP spill slots. */
#if LJ_TARGET_X86
    Reg r = RID_XMM0;
#else
    Reg r = RID_FPRET;
#endif
    if ((as->freeset & RSET_FPR))
      r = rset_pickbot((as->freeset & RSET_FPR));
    if (!rset_test(as->freeset, r))
      emit_spload(as, IR(regcost_ref(as->cost[r])), r, SPOFS_TMP);
    for (ir = IR(as->orignins-1); ir->o == IR_PHI; ir--) {
      if (ra_hasspill(ir->s)) {
	IRIns *irl = IR(ir->op1);
	if (ra_hasspill(irl->s) && irt_isfp(ir->t)) {
	  emit_spstore(as, irl, r, sps_scale(irl->s));
	  emit_spload(as, ir, r, sps_scale(ir->s));
	  checkmclim(as);
	}
      }
    }
    if (!rset_test(as->freeset, r))
      emit_spstore(as, IR(regcost_ref(as->cost[r])), r, SPOFS_TMP);
  }
#endif
}
