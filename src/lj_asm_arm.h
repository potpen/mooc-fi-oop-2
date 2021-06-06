/*
** ARM IR assembler (SSA IR -> machine code).
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

/* -- Register allocator extensions --------------------------------------- */

/* Allocate a register with a hint. */
static Reg ra_hintalloc(ASMState *as, IRRef ref, Reg hint, RegSet allow)
{
  Reg r = IR(ref)->r;
  if (ra_noreg(r)) {
    if (!ra_hashint(r) && !iscrossref(as, ref))
      ra_sethint(IR(ref)->r, hint);  /* Propagate register hint. */
    r = ra_allocref(as, ref, allow);
  }
  ra_noweak(as, r);
  return r;
}

/* Allocate a scratch register pair. */
static Reg ra_scratchpair(ASMState *as, RegSet allow)
{
  RegSet pick1 = as->freeset & allow;
  RegSet pick2 = pick1 & (pick1 >> 1) & RSET_GPREVEN;
  Reg r;
  if (pick2) {
    r = rset_picktop(pick2);
  } else {
    RegSet pick = pick1 & (allow >> 1) & RSET_GPREVEN;
    if (pick) {
      r = rset_picktop(pick);
      ra_restore(as, regcost_ref(as->cost[r+1]));
    } else {
      pick = pick1 & (allow << 1) & RSET_GPRODD;
      if (pick) {
	r = ra_restore(as, regcost_ref(as->cost[rset_picktop(pick)-1]));
      } else {
	r = ra_evict(as, allow & (allow >> 1) & RSET_GPREVEN);
	ra_restore(as, regcost_ref(as->cost[r+1]));
      }
    }
  }
  lj_assertA(rset_test(RSET_GPREVEN, r), "odd reg %d", r);
  ra_modified(as, r);
  ra_modified(as, r+1);
  RA_DBGX((as, "scratchpair    $r $r", r, r+1));
  return r;
}

#if !LJ_SOFTFP
/* Allocate two source registers for three-operand instructions. */
static Reg ra_alloc2(ASMState *as, IRIns *ir, RegSet allow)
{
  IRIns *irl = IR(ir->op1), *irr = IR(ir->op2);
  Reg left = irl->r, right = irr->r;
  if (ra_hasreg(left)) {
    ra_noweak(as, left);
    if (ra_noreg(right))
      right = ra_allocref(as, ir->op2, rset_exclude(allow, left));
    else
      ra_noweak(as, right);
  } else if (ra_hasreg(right)) {
    ra_noweak(as, right);
    left = ra_allocref(as, ir->op1, rset_exclude(allow, right));
  } else if (ra_hashint(right)) {
    right = ra_allocref(as, ir->op2, allow);
    left = ra_alloc1(as, ir->op1, rset_exclude(allow, right));
  } else {
    left = ra_allocref(as, ir->op1, allow);
    right = ra_alloc1(as, ir->op2, rset_exclude(allow, left));
  }
  return left | (right << 8);
}
#endif

/* -- Guard handling ------------------------------------------------------ */

/* Generate an exit stub group at the bottom of the reserved MCode memory. */
static MCode *asm_exitstub_gen(ASMState *as, ExitNo group)
{
  MCode *mxp = as->mcbot;
  int i;
  if (mxp + 4*4+4*EXITSTUBS_PER_GROUP >= as->mctop)
    asm_mclimit(as);
  /* str lr, [sp]; bl ->vm_exit_handler; .long DISPATCH_address, group. */
  *mxp++ = ARMI_STR|ARMI_LS_P|ARMI_LS_U|ARMF_D(RID_LR)|ARMF_N(RID_SP);
  *mxp = ARMI_BL|((((MCode *)(void *)lj_vm_exit_handler-mxp)-2)&0x00ffffffu);
  mxp++;
  *mxp++ = (MCode)i32ptr(J2GG(as->J)->dispatch);  /* DISPATCH address */
  *mxp++ = group*EXITSTUBS_PER_GROUP;
  for (i = 0; i < EXITSTUBS_PER_GROUP; i++)
    *mxp++ = ARMI_B|((-6-i)&0x00ffffffu);
  lj_mcode_sync(as->mcbot, mxp);
  lj_mcode_commitbot(as->J, mxp);
  as->mcbot = mxp;
  as->mclim = as->mcbot + MCLIM_REDZONE;
  return mxp - EXITSTUBS_PER_GROUP;
}

/* Setup all needed exit stubs. */
static void asm_exitstub_setup(ASMState *as, ExitNo nexits)
{
  ExitNo i;
  if (nexits >= EXITSTUBS_PER_GROUP*LJ_MAX_EXITSTUBGR)
    lj_trace_err(as->J, LJ_TRERR_SNAPOV);
  for (i = 0; i < (nexits+EXITSTUBS_PER_GROUP-1)/EXITSTUBS_PER_GROUP; i++)
    if (as->J->exitstubgroup[i] == NULL)
      as->J->exitstubgroup[i] = asm_exitstub_gen(as, i);
}

/* Emit conditional branch to exit for guard. */
static void asm_guardcc(ASMState *as, ARMCC cc)
{
  MCode *target = exitstub_addr(as->J, as->snapno);
  MCode *p = as->mcp;
  if (LJ_UNLIKELY(p == as->invmcp)) {
    as->loopinv = 1;
    *p = ARMI_BL | ((target-p-2) & 0x00ffffffu);
    emit_branch(as, ARMF_CC(ARMI_B, cc^1), p+1);
    return;
  }
  emit_branch(as, ARMF_CC(ARMI_BL, cc), target);
}

/* -- Operand fusion ------------------------------------------------------ */

/* Limit linear search to this distance. Avoids O(n^2) behavior. */
#define CONFLICT_SEARCH_LIM	31

/* Check if there's no conflicting instruction between curins and ref. */
static int noconflict(ASMState *as, IRRef ref, IROp conflict)
{
  IRIns *ir = as->ir;
  IRRef i = as->curins;
  if (i > ref + CONFLICT_SEARCH_LIM)
    return 0;  /* Give up, ref is too far away. */
  while (--i > ref)
    if (ir[i].o == conflict)
      return 0;  /* Conflict found. */
  return 1;  /* Ok, no conflict. */
}

/* Fuse the array base of colocated arrays. */
static int32_t asm_fuseabase(ASMState *as, IRRef ref)
{
  IRIns *ir = IR(ref);
  if (ir->o == IR_TNEW && ir->op1 <= LJ_MAX_COLOSIZE &&
      !neverfuse(as) && noconflict(as, ref, IR_NEWREF))
    return (int32_t)sizeof(GCtab);
  return 0;
}

/* Fuse array/hash/upvalue reference into register+offset operand. */
static Reg asm_fuseahuref(ASMState *as, IRRef ref, int32_t *ofsp, RegSet allow,
			  int lim)
{
  IRIns *ir = IR(ref);
  if (ra_noreg(ir->r)) {
    if (ir->o == IR_AREF) {
      if (mayfuse(as, ref)) {
	if (irref_isk(ir->op2)) {
	  IRRef tab = IR(ir->op1)->op1;
	  int32_t ofs = asm_fuseabase(as, tab);
	  IRRef refa = ofs ? tab : ir->op1;
	  ofs += 8*IR(ir->op2)->i;
	  if (ofs > -lim && ofs < lim) {
	    *ofsp = ofs;
	    return ra_alloc1(as, refa, allow);
	  }
	}
      }
    } else if (ir->o == IR_HREFK) {
      if (mayfuse(as, ref)) {
	int32_t ofs = (int32_t)(IR(ir->op2)->op2 * sizeof(Node));
	if (ofs < lim) {
	  *ofsp = ofs;
	  return ra_alloc1(as, ir->op1, allow);
	}
      }
    } else if (ir->o == IR_UREFC) {
      if (irref_isk(ir->op1)) {
	GCfunc *fn = ir_kfunc(IR(ir->op1));
	int32_t ofs = i32ptr(&gcref(fn->l.uvptr[(ir->op2 >> 8)])->uv.tv);
	*ofsp = (ofs & 255);  /* Mask out less bits to allow LDRD. */
	return ra_allock(as, (ofs & ~255), allow);
      }
    } else if (ir->o == IR_TMPREF) {
      *ofsp = 0;
      return RID_SP;
    }
  }
  *ofsp = 0;
  return ra_alloc1(as, ref, allow);
}

/* Fuse m operand into arithmetic/logic instructions. */
static uint32_t asm_fuseopm(ASMState *as, ARMIns ai, IRRef ref, RegSet allow)
{
  IRIns *ir = IR(ref);
  if (ra_hasreg(ir->r)) {
    ra_noweak(as, ir->r);
    return ARMF_M(ir->r);
  } else if (irref_isk(ref)) {
    uint32_t k = emit_isk12(ai, ir->i);
    if (k)
      return k;
  } else if (mayfuse(as, ref)) {
    if (ir->o >= IR_BSHL && ir->o <= IR_BROR) {
      Reg m = ra_alloc1(as, ir->op1, allow);
      ARMShift sh = ir->o == IR_BSHL ? ARMSH_LSL :
		    ir->o == IR_BSHR ? ARMSH_LSR :
		    ir->o == IR_BSAR ? ARMSH_ASR : ARMSH_ROR;
      if (irref_isk(ir->op2)) {
	return m | ARMF_SH(sh, (IR(ir->op2)->i & 31));
      } else {
	Reg s = ra_alloc1(as, ir->op2, rset_exclude(allow, m));
	return m | ARMF_RSH(sh, s);
      }
    } else if (ir->o == IR_ADD && ir->op1 == ir->op2) {
      Reg m = ra_alloc1(as, ir->op1, allow);
      return m | ARMF_SH(ARMSH_LSL, 1);
    }
  }
  return ra_allocref(as, ref, allow);
}

/* Fuse shifts into loads/stores. Only bother with BSHL 2 => lsl #2. */
static IRRef asm_fuselsl2(ASMState *as, IRRef ref)
{
  IRIns *ir = IR(ref);
  if (ra_noreg(ir->r) && mayfuse(as, ref) && ir->o == IR_BSHL &&
      irref_isk(ir->op2) && IR(ir->op2)->i == 2)
    return ir->op1;
  return 0;  /* No fusion. */
}

/* Fuse XLOAD/XSTORE reference into load/store operand. */
static void asm_fusexref(ASMState *as, ARMIns ai, Reg rd, IRRef ref,
			 RegSet allow, int32_t ofs)
{
  IRIns *ir = IR(ref);
  Reg base;
  if (ra_noreg(ir->r) && canfuse(as, ir)) {
    int32_t lim = (!LJ_SOFTFP && (ai & 0x08000000)) ? 1024 :
		   (ai & 0x04000000) ? 4096 : 256;
    if (ir->o == IR_ADD) {
      int32_t ofs2;
      if (irref_isk(ir->op2) &&
	  (ofs2 = ofs + IR(ir->op2)->i) > -lim && ofs2 < lim &&
	  (!(!LJ_SOFTFP && (ai & 0x08000000)) || !(ofs2 & 3))) {
	ofs = ofs2;
	ref = ir->op1;
      } else if (ofs == 0 && !(!LJ_SOFTFP && (ai & 0x08000000))) {
	IRRef lref = ir->op1, rref = ir->op2;
	Reg rn, rm;
	if ((ai & 0x04000000)) {
	  IRRef sref = asm_fuselsl2(as, rref);
	  if (sref) {
	    rref = sref;
	    ai |= ARMF_SH(ARMSH_LSL, 2);
	  } else if ((sref = asm_fuselsl2(as, lref)) != 0) {
	    lref = rref;
	    rref = sref;
	    ai |= ARMF_SH(ARMSH_LSL, 2);
	  }
	}
	rn = ra_alloc1(as, lref, allow);
	rm = ra_alloc1(as, rref, rset_exclude(allow, rn));
	if ((ai & 0x04000000)) ai |= ARMI_LS_R;
	emit_dnm(as, ai|ARMI_LS_P|ARMI_LS_U, rd, rn, rm);
	return;
      }
    } else if (ir->o == IR_STRREF && !(!LJ_SOFTFP && (ai & 0x08000000))) {
      lj_assertA(ofs == 0, "bad usage");
      ofs = (int32_t)sizeof(GCstr);
      if (irref_isk(ir->op2)) {
	ofs += IR(ir->op2)->i;
	ref = ir->op1;
      } else if (irref_isk(ir->op1)) {
	ofs += IR(ir->op1)->i;
	ref = ir->op2;
      } else {
	/* NYI: Fuse ADD with constant. */
	Reg rn = ra_alloc1(as, ir->op1, allow);
	uint32_t m = asm_fuseopm(as, 0, ir->op2, rset_exclude(allow, rn));
	if ((ai & 0x04000000))
	  emit_lso(as, ai, rd, rd, ofs);
	else
	  emit_lsox(as, ai, rd, rd, ofs);
	emit_dn(as, ARMI_ADD^m, rd, rn);
	return;
      }
      if (ofs <= -lim || ofs >= lim) {
	Reg rn = ra_alloc1(as, ref, allow);
	Reg rm = ra_allock(as, ofs, rset_exclude(allow, rn));
	if ((ai & 0x04000000)) ai |= ARMI_LS_R;
	emit_dnm(as, ai|ARMI_LS_P|ARMI_LS_U, rd, rn, rm);
	return;
      }
    }
  }
  base = ra_alloc1(as, ref, allow);
#if !LJ_SOFTFP
  if ((ai & 0x08000000))
    emit_vlso(as, ai, rd, base, ofs);
  else
#endif
  if ((ai & 0x04000000))
    emit_lso(as, ai, rd, base, ofs);
  else
    emit_lsox(as, ai, rd, base, ofs);
}

#if !LJ_SOFTFP
/*
** Fuse to multiply-add/sub instruction.
** VMLA rounds twice (UMA, not FMA) -- no need to check for JIT_F_OPT_FMA.
** VFMA needs VFPv4, which is uncommon on the remaining ARM32 targets.
*/
static int asm_fusemadd(ASMState *as, IRIns *ir, ARMIns ai, ARMIns air)
{
  IRRef lref = ir->op1, rref = ir->op2;
  IRIns *irm;
  if (lref != rref &&
      ((mayfuse(as, lref) && (irm = IR(lref), irm->o == IR_MUL) &&
	ra_noreg(irm->r)) ||
       (mayfuse(as, rref) && (irm = IR(rref), irm->o == IR_MUL) &&
	(rref = lref, ai = air, ra_noreg(irm->r))))) {
    Reg dest = ra_dest(as, ir, RSET_FPR);
    Reg add = ra_hintalloc(as, rref, dest, RSET_FPR);
    Reg right, left = ra_alloc2(as, irm,
			rset_exclude(rset_exclude(RSET_FPR, dest), add));
    right = (left >> 8); left &= 255;
    emit_dnm(as, ai, (dest & 15), (left & 15), (right & 15));
    if (dest != add) emit_dm(as, ARMI_VMOV_D, (dest & 15), (add & 15));
    return 1;
  }
  return 0;
}
#endif

/* -- Calls --------------------------------------------------------------- */

/* Generate a call to a C function. */
static void asm_gencall(ASMState *as, const CCallInfo *ci, IRRef *args)
{
  uint32_t n, nargs = CCI_XNARGS(ci);
  int32_t ofs = 0;
#if LJ_SOFTFP
  Reg gpr = REGARG_FIRSTGPR;
#else
  Reg gpr, fpr = REGARG_FIRSTFPR, fprodd = 0;
#endif
  if ((void *)ci->func)
    emit_call(as, (void *)ci->func);
#if !LJ_SOFTFP
  for (gpr = REGARG_FIRSTGPR; gpr <= REGARG_LASTGPR; gpr++)
    as->cost[gpr] = REGCOST(~0u, ASMREF_L);
  gpr = REGARG_FIRSTGPR;
#endif
  for (n = 0; n < nargs; n++) {  /* Setup args. */
    IRRef ref = args[n];
    IRIns *ir = IR(ref);
#if !LJ_SOFTFP
    if (ref && irt_isfp(ir->t)) {
      RegSet of = as->freeset;
      Reg src;
      if (!LJ_ABI_SOFTFP && !(ci->flags & CCI_VARARG)) {
	if (irt_isnum(ir->t)) {
	  if (fpr <= REGARG_LASTFPR) {
	    ra_leftov(as, fpr, ref);
	    fpr++;
	    continue;
	  }
	} else if (fprodd) {  /* Ick. */
	  src = ra_alloc1(as, ref, RSET_FPR);
	  emit_dm(as, ARMI_VMOV_S, (fprodd & 15), (src & 15) | 0x00400000);
	  fprodd = 0;
	  continue;
	} else if (fpr <= REGARG_LASTFPR) {
	  ra_leftov(as, fpr, ref);
	  fprodd = fpr++;
	  continue;
	}
	/* Workaround to protect argument GPRs from being used for remat. */
	as->freeset &= ~RSET_RANGE(REGARG_FIRSTGPR, REGARG_LASTGPR+1);
	src = ra_alloc1(as, ref, RSET_FPR);  /* May alloc GPR to remat FPR. */
	as->freeset |= (of & RSET_RANGE(REGARG_FIRSTGPR, REGARG_LASTGPR+1));
	fprodd = 0;
	goto stackfp;
      }
      /* Workaround to protect argument GPRs from being used for remat. */
      as->freeset &= ~RSET_RANGE(REGARG_FIRSTGPR, REGARG_LASTGPR+1);
      src = ra_alloc1(as, ref, RSET_FPR);  /* May alloc GPR to remat FPR. */
      as->freeset |= (of & RSET_RANGE(REGARG_FIRSTGPR, REGARG_LASTGPR+1));
      if (irt_isnum(ir->t)) gpr = (gpr+1) & ~1u;
      if (gpr <= REGARG_LASTGPR) {
	lj_assertA(rset_test(as->freeset, gpr),
		   "reg %d not free", gpr);  /* Must have been evicted. */
	if (irt_isnum(ir->t)) {
	  lj_assertA(rset_test(as->freeset, gpr+1),
		     "reg %d not free", gpr+1);  /* Ditto. */
	  emit_dnm(as, ARMI_VMOV_RR_D, gpr, gpr+1, (src & 15));
	  gpr += 2;
	} else {
	  emit_dn(as, ARMI_VMOV_R_S, gpr, (src & 15));
	  gpr++;
	}
      } else {
      stackfp:
	if (irt_isnum(ir->t)) ofs = (ofs + 4) & ~4;
	emit_spstore(as, ir, src, ofs);
	ofs += irt_isnum(ir->t) ? 8 : 4;
      }
    } else
#endif
    {
      if (gpr <= REGARG_LASTGPR) {
	lj_assertA(rset_test(as->freeset, gpr),
		   "reg %d not free", gpr);  /* Must have been evicted. */
	if (ref) ra_leftov(as, gpr, ref);
	gpr++;
      } else {
	if (ref) {
	  Reg r = ra_alloc1(as, ref, RSET_GPR);
	  emit_spstore(as, ir, r, ofs);
	}
	ofs += 4;
      }
    }
  }
}

/* Setup result reg/sp for call. Evict scratch regs. */
static void asm_setupresult(ASMState *as, IRIns *ir, const CCallInfo *ci)
{
  RegSet drop = RSET_SCRATCH;
  int hiop = ((ir+1)->o == IR_HIOP && !irt_isnil((ir+1)->t));
  if (ra_hasreg(ir->r))
    rset_clear(drop, ir->r);  /* Dest reg handled below. */
  if (hiop && ra_hasreg((ir+1)->r))
    rset_clear(drop, (ir+1)->r);  /* Dest reg handled below. */
  ra_evictset(as, drop);  /* Evictions must be performed first. */
  if (ra_used(ir)) {
    lj_assertA(!irt_ispri(ir->t), "PRI dest");
    if (!LJ_SOFTFP && irt_isfp(ir->t)) {
      if (LJ_ABI_SOFTFP || (ci->flags & (CCI_CASTU64|CCI_VARARG))) {
	Reg dest = (ra_dest(as, ir, RSET_FPR) & 15);
	if (irt_isnum(ir->t))
	  emit_dnm(as, ARMI_VMOV_D_RR, RID_RETLO, RID_RETHI, dest);
	else
	  emit_dn(as, ARMI_VMOV_S_R, RID_RET, dest);
      } else {
	ra_destreg(as, ir, RID_FPRET);
      }
    } else if (hiop) {
      ra_destpair(as, ir);
    } else {
      ra_destreg(as, ir, RID_RET);
    }
  }
  UNUSED(ci);
}

static void asm_callx(ASMState *as, IRIns *ir)
{
  IRRef args[CCI_NARGS_MAX*2];
  CCallInfo ci;
  IRRef func;
  IRIns *irf;
  ci.flags = asm_callx_flags(as, ir);
  asm_collectargs(as, ir, &ci, args);
  asm_setupresult(as, ir, &ci);
  func = ir->op2; irf = IR(func);
  if (irf->o == IR_CARG) { func = irf->op1; irf = IR(func); }
  if (irref_isk(func)) {  /* Call to constant address. */
    ci.func = (ASMFunction)(void *)(irf->i);
  } else {  /* Need a non-argument register for indirect calls. */
    Reg freg = ra_alloc1(as, func, RSET_RANGE(RID_R4, RID_R12+1));
    emit_m(as, ARMI_BLXr, freg);
    ci.func = (ASMFunction)(void *)0;
  }
  asm_gencall(as, &ci, args);
}

/* -- Returns ------------------------------------------------------------- */

/* Return to lower frame. Guard that it goes to the right spot. */
static void asm_retf(ASMState *as, IRIns *ir)
{
  Reg base = ra_alloc1(as, REF_BASE, RSET_GPR);
  void *pc = ir_kptr(IR(ir->op2));
  int32_t delta = 1+LJ_FR2+bc_a(*((const BCIns *)pc - 1));
  as->topslot -= (BCReg)delta;
  if ((int32_t)as->topslot < 0) as->topslot = 0;
  irt_setmark(IR(REF_BASE)->t);  /* Children must not coalesce with BASE reg. */
  /* Need to force a spill on REF_BASE now to update the stack slot. */
  emit_lso(as, ARMI_STR, base, RID_SP, ra_spill(as, IR(REF_BASE)));
  emit_setgl(as, base, jit_base);
  emit_addptr(as, base, -8*delta);
  asm_guardcc(as, CC_NE);
  emit_nm(as, ARMI_CMP, RID_TMP,
	  ra_allock(as, i32ptr(pc), rset_exclude(RSET_GPR, base)));
  emit_lso(as, ARMI_LDR, RID_TMP, base, -4);
}

/* -- Buffer operations --------------------------------------------------- */

#if LJ_HASBUFFER
static void asm_bufhdr_write(ASMState *as, Reg sb)
{
  Reg tmp = ra_scratch(as, rset_exclude(RSET_GPR, sb));
  IRIns irgc;
  int32_t addr = i32ptr((void *)&J2G(as->J)->cur_L);
  irgc.ot = IRT(0, IRT_PGC);  /* GC type. */
  emit_storeofs(as, &irgc, RID_TMP, sb, offsetof(SBuf, L));
  if ((as->flags & JIT_F_ARMV6T2)) {
    emit_dnm(as, ARMI_BFI, RID_TMP, lj_fls(SBUF_MASK_FLAG), tmp);
  } else {
    emit_dnm(as, ARMI_ORR, RID_TMP, RID_TMP, tmp);
    emit_dn(as, ARMI_AND|ARMI_K12|SBUF_MASK_FLAG, tmp, tmp);
  }
  emit_lso(as, ARMI_LDR, RID_TMP,
	   ra_allock(as, (addr & ~4095),
		     rset_exclude(rset_exclude(RSET_GPR, sb), tmp)),
	   (addr & 4095));
  emit_loadofs(as, &irgc, tmp, sb, offsetof(SBuf, L));
}
#endif

/* -- Type conversions ---------------------------------------------------- */

#if !LJ_SOFTFP
static void asm_tointg(ASMState *as, IRIns *ir, Reg left)
{
  Reg tmp = ra_scratch(as, rset_exclude(RSET_FPR, left));
  Reg dest = ra_dest(as, ir, RSET_GPR);
  asm_guardcc(as, CC_NE);
  emit_d(as, ARMI_VMRS, 0);
  emit_dm(as, ARMI_VCMP_D, (tmp & 15), (left & 15));
  emit_dm(as, ARMI_VCVT_F64_S32, (tmp & 15), (tmp & 15));
  emit_dn(as, ARMI_VMOV_R_S, dest, (tmp & 15));
  emit_dm(as, ARMI_VCVT_S32_F64, (tmp & 15), (left & 15));
}

static void asm_tobit(ASMState *as, IRIns *ir)
{
  RegSet allow = RSET_FPR;
  Reg left = ra_alloc1(as, ir->op1, allow);
  Reg right = ra_alloc1(as, ir->op2, rset_clear(allow, left));
  Reg tmp = ra_scratch(as, rset_clear(allow, right));
  Reg dest = ra_dest(as, ir, RSET_GPR);
  emit_dn(as, ARMI_VMOV_R_S, dest, (tmp & 15));
  emit_dnm(as, ARMI_VADD_D, (tmp & 15), (left & 15), (right & 15));
}
#endif

static void asm_conv(ASMState *as, IRIns *ir)
{
  IRType st = (IRType)(ir->op2 & IRCONV_SRCMASK);
#if !LJ_SOFTFP
  int stfp = (st == IRT_NUM || st == IRT_FLOAT);
#endif
  IRRef lref = ir->op1;
  /* 64 bit integer conversions are handled by SPLIT. */
  lj_assertA(!irt_isint64(ir->t) && !(st == IRT_I64 || st == IRT_U64),
	     "IR %04d has unsplit 64 bit type",
	     (int)(ir - as->ir) - REF_BIAS);
#if LJ_SOFTFP
  /* FP conversions are handled by SPLIT. */
  lj_assertA(!irt_isfp(ir->t) && !(st == IRT_NUM || st == IRT_FLOAT),
	     "IR %04d has FP type",
	     (int)(ir - as->ir) - REF_BIAS);
  /* Can't check for same types: SPLIT uses CONV int.int + BXOR for sfp NEG. */
#else
  lj_assertA(irt_type(ir->t) != st, "inconsistent types for CONV");
  if (irt_isfp(ir->t)) {
    Reg dest = ra_dest(as, ir, RSET_FPR);
    if (stfp) {  /* FP to FP conversion. */
      emit_dm(as, st == IRT_NUM ? ARMI_VCVT_F32_F64 : ARMI_VCVT_F64_F32,
	      (dest & 15), (ra_alloc1(as, lref, RSET_FPR) & 15));
    } else {  /* Integer to FP conversion. */
      Reg left = ra_alloc1(as, lref, RSET_GPR);
      ARMIns ai = irt_isfloat(ir->t) ?
	(st == IRT_INT ? ARMI_VCVT_F32_S32 : ARMI_VCVT_F32_U32) :
	(st == IRT_INT ? ARMI_VCVT_F64_S32 : ARMI_VCVT_F64_U32);
      emit_dm(as, ai, (dest & 15), (dest & 15));
      emit_dn(as, ARMI_VMOV_S_R, left, (dest & 15));
    }
  } else if (stfp) {  /* FP to integer conversion. */
    if (irt_isguard(ir->t)) {
      /* Checked conversions are only supported from number to int. */
      lj_assertA(irt_isint(ir->t) && st == IRT_NUM,
		 "bad type for checked CONV");
      asm_tointg(as, ir, ra_alloc1(as, lref, RSET_FPR));
    } else {
      Reg left = ra_alloc1(as, lref, RSET_FPR);
      Reg tmp = ra_scratch(as, rset_exclude(RSET_FPR, left));
      Reg dest = ra_dest(as, ir, RSET_GPR);
      ARMIns ai;
      emit_dn(as, ARMI_VMOV_R_S, dest, (tmp & 15));
      ai = irt_isint(ir->t) ?
	(st == IRT_NUM ? ARMI_VCVT_S32_F64 : ARMI_VCVT_S32_F32) :
	(st == IRT_NUM ? ARMI_VCVT_U32_F64 : ARMI_VCVT_U32_F32);
      emit_dm(as, ai, (tmp & 15), (left & 15));
    }
  } else
#endif
  {
    Reg dest = ra_dest(as, ir, RSET_GPR);
    if (st >= IRT_I8 && st <= IRT_U16) {  /* Extend to 32 bit integer. */
      Reg left = ra_alloc1(as, lref, RSET_GPR);
      lj_assertA(irt_isint(ir->t) || irt_isu32(ir->t), "bad type for CONV EXT");
      if ((as->flags & JIT_F_ARMV6)) {
	ARMIns ai = st == IRT_I8 ? ARMI_SXTB :
		    st == IRT_U8 ? ARMI_UXTB :
		    st == IRT_I16 ? ARMI_SXTH : ARMI_UXTH;
	emit_dm(as, ai, dest, left);
      } else if (st == IRT_U8) {
	emit_dn(as, ARMI_AND|ARMI_K12|255, dest, left);
      } else {
	uint32_t shift = st == IRT_I8 ? 24 : 16;
	ARMShift sh = st == IRT_U16 ? ARMSH_LSR : ARMSH_ASR;
	emit_dm(as, ARMI_MOV|ARMF_SH(sh, shift), dest, RID_TMP);
	emit_dm(as, ARMI_MOV|ARMF_SH(ARMSH_LSL, shift), RID_TMP, left);
      }
    } else {  /* Handle 32/32 bit no-op (cast). */
      ra_leftov(as, dest, lref);  /* Do nothing, but may need to move regs. */
    }
  }
}

static void asm_strto(ASMState *as, IRIns *ir)
{
  const CCallInfo *ci = &lj_ir_callinfo[IRCALL_lj_strscan_num];
  IRRef args[2];
  Reg rlo = 0, rhi = 0, tmp;
  int destused = ra_used(ir);
  int32_t ofs = 0;
  ra_evictset(as, RSET_SCRATCH);
#if LJ_SOFTFP
  if (destused) {
    if (ra_hasspill(ir->s) && ra_hasspill((ir+1)->s) &&
	(ir->s & 1) == 0 && ir->s + 1 == (ir+1)->s) {
      int i;
      for (i = 0; i < 2; i++) {
	Reg r = (ir+i)->r;
	if (ra_hasreg(r)) {
	  ra_free(as, r);
	  ra_modified(as, r);
	  emit_spload(as, ir+i, r, sps_scale((ir+i)->s));
	}
      }
      ofs = sps_scale(ir->s);
      destused = 0;
    } else {
      rhi = ra_dest(as, ir+1, RSET_GPR);
      rlo = ra_dest(as, ir, rset_exclude(RSET_GPR, rhi));
    }
  }
  asm_guardcc(as, CC_EQ);
  if (destused) {
    emit_lso(as, ARMI_LDR, rhi, RID_SP, 4);
    emit_lso(as, ARMI_LDR, rlo, RID_SP, 0);
  }
#else
  UNUSED(rhi);
  if (destused) {
    if (ra_hasspill(ir->s)) {
      ofs = sps_scale(ir->s);
      destused = 0;
      if (ra_hasreg(ir->r)) {
	ra_free(as, ir->r);
	ra_modified(as, ir->r);
	emit_spload(as, ir, ir->r, ofs);
      }
    } else {
      rlo = ra_dest(as, ir, RSET_FPR);
    }
  }
  asm_guardcc(as, CC_EQ);
  if (destused)
    emit_vlso(as, ARMI_VLDR_D, rlo, RID_SP, 0);
#endif
  emit_n(as, ARMI_CMP|ARMI_K12|0, RID_RET);  /* Test return status. */
  args[0] = ir->op1;      /* GCstr *str */
  args[1] = ASMREF_TMP1;  /* TValue *n  */
  asm_gencall(as, ci, args);
  tmp = ra_releasetmp(as, ASMREF_TMP1);
  if (ofs == 0)
    emit_dm(as, ARMI_MOV, tmp, RID_SP);
  else
    emit_opk(as, ARMI_ADD, tmp, RID_SP, ofs, RSET_GPR);
}

/* -- Memory references --------------------------------------------------- */

/* Get pointer to TValue. */
static void asm_tvptr(ASMState *as, Reg dest, IRRef ref, MSize mode)
{
  if ((mode & IRTMPREF_IN1)) {
    IRIns *ir = IR(ref);
    if (irt_isnum(ir->t)) {
      if ((mode & IRTMPREF_OUT1)) {
#if LJ_SOFTFP
	lj_assertA(irref_isk(ref), "unsplit FP op");
	emit_dm(as, ARMI_MOV, dest, RID_SP);
	emit_lso(as, ARMI_STR,
		 ra_allock(as, (int32_t)ir_knum(ir)->u32.lo, RSET_GPR),
		 RID_SP, 0);
	emit_lso(as, ARMI_STR,
		 ra_allock(as, (int32_t)ir_knum(ir)->u32.hi, RSET_GPR),
		 RID_SP, 4);
#else
	Reg src = ra_alloc1(as, ref, RSET_FPR);
	emit_dm(as, ARMI_MOV, dest, RID_SP);
	emit_vlso(as, ARMI_VSTR_D, src, RID_SP, 0);
#endif
      } else if (irref_isk(ref)) {
	/* Use the number constant itself as a TValue. */
	ra_allockreg(as, i32ptr(ir_knum(ir)), dest);
      } else {
#if LJ_SOFTFP
	lj_assertA(0, "unsplit FP op");
#else
	/* Otherwise force a spill and use the spill slot. */
	emit_opk(as, ARMI_ADD, dest, RID_SP, ra_spill(as, ir), RSET_GPR);
#endif
      }
    } else {
      /* Otherwise use [sp] and [sp+4] to hold the TValue.
      ** This assumes the following call has max. 4 args.
      */
      Reg type;
      emit_dm(as, ARMI_MOV, dest, RID_SP);
      if (!irt_ispri(ir->t)) {
	Reg src = ra_alloc1(as, ref, RSET_GPR);
	emit_lso(as, ARMI_STR, src, RID_SP, 0);
      }
      if (LJ_SOFTFP && (ir+1)->o == IR_HIOP && !irt_isnil((ir+1)->t))
	type = ra_alloc1(as, ref+1, RSET_GPR);
      else
	type = ra_allock(as, irt_toitype(ir->t), RSET_GPR);
      emit_lso(as, ARMI_STR, type, RID_SP, 4);
    }
  } else {
    emit_dm(as, ARMI_MOV, dest, RID_SP);
  }
}

static void asm_aref(ASMState *as, IRIns *ir)
{
  Reg dest = ra_dest(as, ir, RSET_GPR);
  Reg idx, base;
  if (irref_isk(ir->op2)) {
    IRRef tab = IR(ir->op1)->op1;
    int32_t ofs = asm_fuseabase(as, tab);
    IRRef refa = ofs ? tab : ir->op1;
    uint32_t k = emit_isk12(ARMI_ADD, ofs + 8*IR(ir->op2)->i);
    if (k) {
      base = ra_alloc1(as, refa, RSET_GPR);
      emit_dn(as, ARMI_ADD^k, dest, base);
      return;
    }
  }
  base = ra_alloc1(as, ir->op1, RSET_GPR);
  idx = ra_alloc1(as, ir->op2, rset_exclude(RSET_GPR, base));
  emit_dnm(as, ARMI_ADD|ARMF_SH(ARMSH_LSL, 3), dest, base, idx);
}

/* Inlined hash lookup. Specialized for key type and for const keys.
** The equivalent C code is:
**   Node *n = hashkey(t, key);
**   do {
**     if (lj_obj_equal(&n->key, key)) return &n->val;
**   } while ((n = nextnode(n)));
**   return niltv(L);
*/
static void asm_href(ASMState *as, IRIns *ir, IROp merge)
{
  RegSet allow = RSET_GPR;
  int destused = ra_used(ir);
  Reg dest = ra_dest(as, ir, allow);
  Reg tab = ra_alloc1(as, ir->op1, rset_clear(allow, dest));
  Reg key = 0, keyhi = 0, keynumhi = RID_NONE, tmp = RID_TMP;
  IRRef refkey = ir->op2;
  IRIns *irkey = IR(refkey);
  IRType1 kt = irkey->t;
  int32_t k = 0, khi = emit_isk12(ARMI_CMP, irt_toitype(kt));
  uint32_t khash;
  MCLabel l_end, l_loop;
  rset_clear(allow, tab);
  if (!irref_isk(refkey) || irt_isstr(kt)) {
#if LJ_SOFTFP
    key = ra_alloc1(as, refkey, allow);
    rset_clear(allow, key);
    if (irkey[1].o == IR_HIOP) {
      if (ra_hasreg((irkey+1)->r)) {
	keynumhi = (irkey+1)->r;
	keyhi = RID_TMP;
	ra_noweak(as, keynumhi);
      } else {
	keyhi = keynumhi = ra_allocref(as, refkey+1, allow);
      }
      rset_clear(allow, keynumhi);
      khi = 0;
    }
#else
    if (irt_isnum(kt)) {
      key = ra_scratch(as, allow);
      rset_clear(allow, key);
      keyhi = keynumhi = ra_scratch(as, allow);
      rset_clear(allow, keyhi);
      khi = 0;
    } else {
      key = ra_alloc1(as, refkey, allow);
      rset_clear(allow, key);
    }
#endif
  } else if (irt_isnum(kt)) {
    int32_t val = (int32_t)ir_knum(irkey)->u32.lo;
    k = emit_isk12(ARMI_CMP, val);
    if (!k) {
      key = ra_allock(as, val, allow);
      rset_clear(allow, key);
    }
    val = (int32_t)ir_knum(irkey)->u32.hi;
    khi = emit_isk12(ARMI_CMP, val);
    if (!khi) {
      keyhi = ra_allock(as, val, allow);
      rset_clear(allow, keyhi);
    }
  } else if (!irt_ispri(kt)) {
    k = emit_isk12(ARMI_CMP, irkey->i);
    if (!k) {
      key = ra_alloc1(as, refkey, allow);
      rset_clear(allow, key);
    }
  }
  if (!irt_ispri(kt))
    tmp = ra_scratchpair(as, allow);

  /* Key not found in chain: jump to exit (if merged) or load niltv. */
  l_end = emit_label(as);
  as->invmcp = NULL;
  if (merge == IR_NE)
    asm_guardcc(as, CC_AL);
  else if (destused)
    emit_loada(as, dest, niltvg(J2G(as->J)));

  /* Follow hash chain until the end. */
  l_loop = --as->mcp;
  emit_n(as, ARMI_CMP|ARMI_K12|0, dest);
  emit_lso(as, ARMI_LDR, dest, dest, (int32_t)offsetof(Node, next));

  /* Type and value comparison. */
  if (merge == IR_EQ)
    asm_guardcc(as, CC_EQ);
  else
    emit_branch(as, ARMF_CC(ARMI_B, CC_EQ), l_end);
  if (!irt_ispri(kt)) {
    emit_nm(as, ARMF_CC(ARMI_CMP, CC_EQ)^k, tmp, key);
    emit_nm(as, ARMI_CMP^khi, tmp+1, keyhi);
    emit_lsox(as, ARMI_LDRD, tmp, dest, (int32_t)offsetof(Node, key));
  } else {
    emit_n(as, ARMI_CMP^khi, tmp);
    emit_lso(as, ARMI_LDR, tmp, dest, (int32_t)offsetof(Node, key.it));
  }
  *l_loop = ARMF_CC(ARMI_B, CC_NE) | ((as->mcp-l_loop-2) & 0x00ffffffu);

  /* Load main position relative to tab->node into dest. */
  khash = irref_isk(refkey) ? ir_khash(as, irkey) : 1;
  if (khash == 0) {
    emit_lso(as, ARMI_LDR, dest, tab, (int32_t)offsetof(GCtab, node));
  } else {
    emit_dnm(as, ARMI_ADD|ARMF_SH(ARMSH_LSL, 3), dest, dest, tmp);
    emit_dnm(as, ARMI_ADD|ARMF_SH(ARMSH_LSL, 1), tmp, tmp, tmp);
    if (irt_isstr(kt)) {  /* Fetch of str->sid is cheaper than ra_allock. */
      emit_dnm(as, ARMI_AND, tmp, tmp+1, RID_TMP);
      emit_lso(as, ARMI_LDR, dest, tab, (int32_t)offsetof(GCtab, node));
      emit_lso(as, ARMI_LDR, tmp+1, key, (int32_t)offsetof(GCstr, sid));
      emit_lso(as, ARMI_LDR, RID_TMP, tab, (int32_t)offsetof(GCtab, hmask));
    } else if (irref_isk(refkey)) {
      emit_opk(as, ARMI_AND, tmp, RID_TMP, (int32_t)khash,
	       rset_exclude(rset_exclude(RSET_GPR, tab), dest));
      emit_lso(as, ARMI_LDR, dest, tab, (int32_t)offsetof(GCtab, node));
      emit_lso(as, ARMI_LDR, RID_TMP, tab, (int32_t)offsetof(GCtab, hmask));
    } else {  /* Must match with hash*() in lj_tab.c. */
      if (ra_hasreg(keynumhi)) {  /* Canonicalize +-0.0 to 0.0. */
	if (keyhi == RID_TMP)
	  emit_dm(as, ARMF_CC(ARMI_MOV, CC_NE), keyhi, keynumhi);
	emit_d(as, ARMF_CC(ARMI_MOV, CC_EQ)|ARMI_K12|0, keyhi);
      }
      emit_dnm(as, ARMI_AND, tmp, tmp, RID_TMP);
      emit_dnm(as, ARMI_SUB|ARMF_SH(ARMSH_ROR, 32-HASH_ROT3), tmp, tmp, tmp+1);
      emit_lso(as, ARMI_LDR, dest, tab, (int32_t)offsetof(GCtab, node));
      emit_dnm(as, ARMI_EOR|ARMF_SH(ARMSH_ROR, 32-((HASH_ROT2+HASH_ROT1)&31)),
	       tmp, tmp+1, tmp);
      emit_lso(as, ARMI_LDR, RID_TMP, tab, (int32_t)offsetof(GCtab, hmask));
      emit_dnm(as, ARMI_SUB|ARMF_SH(ARMSH_ROR, 32-HASH_ROT1), tmp+1, tmp+1, tmp);
      if (ra_hasreg(keynumhi)) {
	emit_dnm(as, ARMI_EOR, tmp+1, tmp, key);
	emit_dnm(as, ARMI_ORR|ARMI_S, RID_TMP, tmp, key);  /* Test for +-0.0. */
	emit_dnm(as, ARMI_ADD, tmp, keynumhi, keynumhi);
#if !LJ_SOFTFP
	emit_dnm(as, ARMI_VMOV_RR_D, key, keynumhi,
		 (ra_alloc1(as, refkey, RSET_FPR) & 15));
#endif
      } else {
	emit_dnm(as, ARMI_EOR, tmp+1, tmp, key);
	emit_opk(as, ARMI_ADD, tmp, key, (int32_t)HASH_BIAS,
		 rset_exclude(rset_exclude(RSET_GPR, tab), key));
      }
    }
  }
}

static void asm_hrefk(ASMState *as, IRIns *ir)
{
  IRIns *kslot = IR(ir->op2);
  IRIns *irkey = IR(kslot->op1);
  int32_t ofs = (int32_t)(kslot->op2 * sizeof(Node));
  int32_t kofs = ofs + (int32_t)offsetof(Node, key);
  Reg dest = (ra_used(ir) || ofs > 4095) ? ra_dest(as, ir, RSET_GPR) : RID_NONE;
  Reg node = ra_alloc1(as, ir->op1, RSET_GPR);
  Reg key = RID_NONE, type = RID_TMP, idx = node;
  RegSet allow = rset_exclude(RSET_GPR, node);
  lj_assertA(ofs % sizeof(Node) == 0, "unaligned HREFK slot");
  if (ofs > 4095) {
    idx = dest;
    rset_clear(allow, dest);
    kofs = (int32_t)offsetof(Node, key);
  } else if (ra_hasreg(dest)) {
    emit_opk(as, ARMI_ADD, dest, node, ofs, allow);
  }
  asm_guardcc(as, CC_NE);
  if (!irt_ispri(irkey->t)) {
    RegSet even = (as->freeset & allow);
    even = even & (even >> 1) & RSET_GPREVEN;
    if (even) {
      key = ra_scratch(as, even);
      if (rset_test(as->freeset, key+1)) {
	type = key+1;
	ra_modified(as, type);
      }
    } else {
      key = ra_scratch(as, allow);
    }
    rset_clear(allow, key);
  }
  rset_clear(allow, type);
  if (irt_isnum(irkey->t)) {
    emit_opk(as, ARMF_CC(ARMI_CMP, CC_EQ), 0, type,
	     (int32_t)ir_knum(irkey)->u32.hi, allow);
    emit_opk(as, ARMI_CMP, 0, key,
	     (int32_t)ir_knum(irkey)->u32.lo, allow);
  } else {
    if (ra_hasreg(key))
      emit_opk(as, ARMF_CC(ARMI_CMP, CC_EQ), 0, key, irkey->i, allow);
    emit_n(as, ARMI_CMN|ARMI_K12|-irt_toitype(irkey->t), type);
  }
  emit_lso(as, ARMI_LDR, type, idx, kofs+4);
  if (ra_hasreg(key)) emit_lso(as, ARMI_LDR, key, idx, kofs);
  if (ofs > 4095)
    emit_opk(as, ARMI_ADD, dest, node, ofs, RSET_GPR);
}

static void asm_uref(ASMState *as, IRIns *ir)
{
  Reg dest = ra_dest(as, ir, RSET_GPR);
  if (irref_isk(ir->op1)) {
    GCfunc *fn = ir_kfunc(IR(ir->op1));
    MRef *v = &gcref(fn->l.uvptr[(ir->op2 >> 8)])->uv.v;
    emit_lsptr(as, ARMI_LDR, dest, v);
  } else {
    Reg uv = ra_scratch(as, RSET_GPR);
    Reg func = ra_alloc1(as, ir->op1, RSET_GPR);
    if (ir->o == IR_UREFC) {
      asm_guardcc(as, CC_NE);
      emit_n(as, ARMI_CMP|ARMI_K12|1, RID_TMP);
      emit_opk(as, ARMI_ADD, dest, uv,
	       (int32_t)offsetof(GCupval, tv), RSET_GPR);
      emit_lso(as, ARMI_LDRB, RID_TMP, uv, (int32_t)offsetof(GCupval, closed));
    } else {
      emit_lso(as, ARMI_LDR, dest, uv, (int32_t)offsetof(GCupval, v));
    }
    emit_lso(as, ARMI_LDR, uv, func,
	     (int32_t)offsetof(GCfuncL, uvptr) + 4*(int32_t)(ir->op2 >> 8));
  }
}

static void asm_fref(ASMState *as, IRIns *ir)
{
  UNUSED(as); UNUSED(ir);
  lj_assertA(!ra_used(ir), "unfused FREF");
}

static void asm_strref(ASMState *as, IRIns *ir)
{
  Reg dest = ra_dest(as, ir, RSET_GPR);
  IRRef ref = ir->op2, refk = ir->op1;
  Reg r;
  if (irref_isk(ref)) {
    IRRef tmp = refk; refk = ref; ref = tmp;
  } else if (!irref_isk(refk)) {
    uint32_t k, m = ARMI_K12|sizeof(GCstr);
    Reg right, left = ra_alloc1(as, ir->op1, RSET_GPR);
    IRIns *irr = IR(ir->op2);
    if (ra_hasreg(irr->r)) {
      ra_noweak(as, irr->r);
      right = irr->r;
    } else if (mayfuse(as, irr->op2) &&
	       irr->o == IR_ADD && irref_isk(irr->op2) &&
	       (k = emit_isk12(ARMI_ADD,
			       (int32_t)sizeof(GCstr) + IR(irr->op2)->i))) {
      m = k;
      right = ra_alloc1(as, irr->op1, rset_exclude(RSET_GPR, left));
    } else {
      right = ra_allocref(as, ir->op2, rset_exclude(RSET_GPR, left));
    }
    emit_dn(as, ARMI_ADD^m, dest, dest);
    emit_dnm(as, ARMI_ADD, dest, left, right);
    return;
  }
  r = ra_alloc1(as, ref, RSET_GPR);
  emit_opk(as, ARMI_ADD, dest, r,
	   sizeof(GCstr) + IR(refk)->i, rset_exclude(RSET_GPR, r));
}

/* -- Loads and stores ---------------------------------------------------- */

static ARMIns asm_fxloadins(ASMState *as, IRIns *ir)
{
  UNUSED(as);
  switch (irt_type(ir->t)) {
  case IRT_I8: return ARMI_LDRSB;
  case IRT_U8: return ARMI_LDRB;
  case IRT_I16: return ARMI_LDRSH;
  case IRT_U16: return ARMI_LDRH;
  case IRT_NUM: lj_assertA(!LJ_SOFTFP, "unsplit FP op"); return ARMI_VLDR_D;
  case IRT_FLOAT: if (!LJ_SOFTFP) return ARMI_VLDR_S;  /* fallthrough */
  default: return ARMI_LDR;
  }
}

static ARMIns asm_fxstoreins(ASMState *as, IRIns *ir)
{
  UNUSED(as);
  switch (irt_type(ir->t)) {
  case IRT_I8: case IRT_U8: return ARMI_STRB;
  case IRT_I16: case IRT_U16: return ARMI_STRH;
  case IRT_NUM: lj_assertA(!LJ_SOFTFP, "unsplit FP op"); return ARMI_VSTR_D;
  case IRT_FLOAT: if (!LJ_SOFTFP) return ARMI_VSTR_S;  /* fallthrough */
  default: return ARMI_STR;
  }
}

static void asm_fload(ASMState *as, IRIns *ir)
{
  Reg dest = ra_dest(as, ir, RSET_GPR);
  ARMIns ai = asm_fxloadins(as, ir);
  Reg idx;
  int32_t ofs;
  if (ir->op1 == REF_NIL) {  /* FLOAD from GG_State with offset. */
    idx = ra_allock(as, (int32_t)(ir->op2<<2) + (int32_t)J2GG(as->J), RSET_GPR);
    ofs = 0;
  } else {
    idx = ra_alloc1(as, ir->op1, RSET_GPR);
    if (ir->op2 == IRFL_TAB_ARRAY) {
      ofs = asm_fuseabase(as, ir->op1);
      if (ofs) {  /* Turn the t->array load into an add for colocated arrays. */
	emit_dn(as, ARMI_ADD|ARMI_K12|ofs, dest, idx);
	return;
      }
    }
    ofs = field_ofs[ir->op2];
  }
  if ((ai & 0x04000000))
    emit_lso(as, ai, dest, idx, ofs);
  else
    emit_lsox(as, ai, dest, idx, ofs);
}

static void asm_fstore(ASMState *as, IRIns *ir)
{
  if (ir->r != RID_SINK) {
    Reg src = ra_alloc1(as, ir->op2, RSET_GPR);
    IRIns *irf = IR(ir->op1);
    Reg idx = ra_alloc1(as, irf->op1, rset_exclude(RSET_GPR, src));
    int32_t ofs = field_ofs[irf->op2];
    ARMIns ai = asm_fxstoreins(as, ir);
    if ((ai & 0x04000000))
      emit_lso(as, ai, src, idx, ofs);
    else
      emit_lsox(as, ai, src, idx, ofs);
  }
}

static void asm_xload(ASMState *as, IRIns *ir)
{
  Reg dest = ra_dest(as, ir,
		     (!LJ_SOFTFP && irt_isfp(ir->t)) ? RSET_FPR : RSET_GPR);
  lj_assertA(!(ir->op2 & IRXLOAD_UNALIGNED), "unaligned XLOAD");
  asm_fusexref(as, asm_fxloadins(as, ir), dest, ir->op1, RSET_GPR, 0);
}

static void asm_xstore_(ASMState *as, IRIns *ir, int32_t ofs)
{
  if (ir-