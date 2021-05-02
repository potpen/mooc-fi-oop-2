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
	/* NYI: Fuse ADD 