
/*
** PPC IR assembler (SSA IR -> machine code).
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

/* -- Guard handling ------------------------------------------------------ */

/* Setup exit stubs after the end of each trace. */
static void asm_exitstub_setup(ASMState *as, ExitNo nexits)
{
  ExitNo i;
  MCode *mxp = as->mctop;
  if (mxp - (nexits + 3 + MCLIM_REDZONE) < as->mclim)
    asm_mclimit(as);
  /* 1: mflr r0; bl ->vm_exit_handler; li r0, traceno; bl <1; bl <1; ... */
  for (i = nexits-1; (int32_t)i >= 0; i--)
    *--mxp = PPCI_BL|(((-3-i)&0x00ffffffu)<<2);
  *--mxp = PPCI_LI|PPCF_T(RID_TMP)|as->T->traceno;  /* Read by exit handler. */
  mxp--;
  *mxp = PPCI_BL|((((MCode *)(void *)lj_vm_exit_handler-mxp)&0x00ffffffu)<<2);
  *--mxp = PPCI_MFLR|PPCF_T(RID_TMP);
  as->mctop = mxp;
}

static MCode *asm_exitstub_addr(ASMState *as, ExitNo exitno)
{
  /* Keep this in-sync with exitstub_trace_addr(). */
  return as->mctop + exitno + 3;
}

/* Emit conditional branch to exit for guard. */
static void asm_guardcc(ASMState *as, PPCCC cc)
{
  MCode *target = asm_exitstub_addr(as, as->snapno);
  MCode *p = as->mcp;
  if (LJ_UNLIKELY(p == as->invmcp)) {
    as->loopinv = 1;
    *p = PPCI_B | (((target-p) & 0x00ffffffu) << 2);
    emit_condbranch(as, PPCI_BC, cc^4, p);
    return;
  }
  emit_condbranch(as, PPCI_BC, cc, target);
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

/* Indicates load/store indexed is ok. */
#define AHUREF_LSX	((int32_t)0x80000000)

/* Fuse array/hash/upvalue reference into register+offset operand. */
static Reg asm_fuseahuref(ASMState *as, IRRef ref, int32_t *ofsp, RegSet allow)
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
	  if (checki16(ofs)) {
	    *ofsp = ofs;
	    return ra_alloc1(as, refa, allow);
	  }
	}
	if (*ofsp == AHUREF_LSX) {
	  Reg base = ra_alloc1(as, ir->op1, allow);
	  Reg idx = ra_alloc1(as, ir->op2, rset_exclude(RSET_GPR, base));
	  return base | (idx << 8);
	}
      }
    } else if (ir->o == IR_HREFK) {
      if (mayfuse(as, ref)) {
	int32_t ofs = (int32_t)(IR(ir->op2)->op2 * sizeof(Node));
	if (checki16(ofs)) {
	  *ofsp = ofs;
	  return ra_alloc1(as, ir->op1, allow);
	}
      }
    } else if (ir->o == IR_UREFC) {
      if (irref_isk(ir->op1)) {
	GCfunc *fn = ir_kfunc(IR(ir->op1));
	int32_t ofs = i32ptr(&gcref(fn->l.uvptr[(ir->op2 >> 8)])->uv.tv);
	int32_t jgl = (intptr_t)J2G(as->J);
	if ((uint32_t)(ofs-jgl) < 65536) {
	  *ofsp = ofs-jgl-32768;
	  return RID_JGL;
	} else {
	  *ofsp = (int16_t)ofs;
	  return ra_allock(as, ofs-(int16_t)ofs, allow);
	}
      }
    } else if (ir->o == IR_TMPREF) {
      *ofsp = (int32_t)(offsetof(global_State, tmptv)-32768);
      return RID_JGL;
    }
  }
  *ofsp = 0;
  return ra_alloc1(as, ref, allow);
}

/* Fuse XLOAD/XSTORE reference into load/store operand. */
static void asm_fusexref(ASMState *as, PPCIns pi, Reg rt, IRRef ref,
			 RegSet allow, int32_t ofs)
{
  IRIns *ir = IR(ref);
  Reg base;
  if (ra_noreg(ir->r) && canfuse(as, ir)) {
    if (ir->o == IR_ADD) {
      int32_t ofs2;
      if (irref_isk(ir->op2) && (ofs2 = ofs + IR(ir->op2)->i, checki16(ofs2))) {
	ofs = ofs2;
	ref = ir->op1;
      } else if (ofs == 0) {
	Reg right, left = ra_alloc2(as, ir, allow);
	right = (left >> 8); left &= 255;
	emit_fab(as, PPCI_LWZX | ((pi >> 20) & 0x780), rt, left, right);
	return;
      }
    } else if (ir->o == IR_STRREF) {
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
	Reg tmp, right, left = ra_alloc2(as, ir, allow);
	right = (left >> 8); left &= 255;
	tmp = ra_scratch(as, rset_exclude(rset_exclude(allow, left), right));
	emit_fai(as, pi, rt, tmp, ofs);
	emit_tab(as, PPCI_ADD, tmp, left, right);
	return;
      }
      if (!checki16(ofs)) {
	Reg left = ra_alloc1(as, ref, allow);
	Reg right = ra_allock(as, ofs, rset_exclude(allow, left));
	emit_fab(as, PPCI_LWZX | ((pi >> 20) & 0x780), rt, left, right);
	return;
      }
    }
  }
  base = ra_alloc1(as, ref, allow);
  emit_fai(as, pi, rt, base, ofs);
}

/* Fuse XLOAD/XSTORE reference into indexed-only load/store operand. */
static void asm_fusexrefx(ASMState *as, PPCIns pi, Reg rt, IRRef ref,
			  RegSet allow)
{
  IRIns *ira = IR(ref);
  Reg right, left;
  if (canfuse(as, ira) && ira->o == IR_ADD && ra_noreg(ira->r)) {
    left = ra_alloc2(as, ira, allow);
    right = (left >> 8); left &= 255;
  } else {
    right = ra_alloc1(as, ref, allow);
    left = RID_R0;
  }
  emit_tab(as, pi, rt, left, right);
}

#if !LJ_SOFTFP
/* Fuse to multiply-add/sub instruction. */
static int asm_fusemadd(ASMState *as, IRIns *ir, PPCIns pi, PPCIns pir)
{
  IRRef lref = ir->op1, rref = ir->op2;
  IRIns *irm;
  if ((as->flags & JIT_F_OPT_FMA) &&
      lref != rref &&
      ((mayfuse(as, lref) && (irm = IR(lref), irm->o == IR_MUL) &&
	ra_noreg(irm->r)) ||
       (mayfuse(as, rref) && (irm = IR(rref), irm->o == IR_MUL) &&
	(rref = lref, pi = pir, ra_noreg(irm->r))))) {
    Reg dest = ra_dest(as, ir, RSET_FPR);
    Reg add = ra_alloc1(as, rref, RSET_FPR);
    Reg right, left = ra_alloc2(as, irm, rset_exclude(RSET_FPR, add));
    right = (left >> 8); left &= 255;
    emit_facb(as, pi, dest, left, right, add);
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
  int32_t ofs = 8;
  Reg gpr = REGARG_FIRSTGPR;
#if !LJ_SOFTFP
  Reg fpr = REGARG_FIRSTFPR;
#endif
  if ((void *)ci->func)
    emit_call(as, (void *)ci->func);
  for (n = 0; n < nargs; n++) {  /* Setup args. */
    IRRef ref = args[n];
    if (ref) {
      IRIns *ir = IR(ref);
#if !LJ_SOFTFP
      if (irt_isfp(ir->t)) {
	if (fpr <= REGARG_LASTFPR) {
	  lj_assertA(rset_test(as->freeset, fpr),
		     "reg %d not free", fpr);  /* Already evicted. */
	  ra_leftov(as, fpr, ref);
	  fpr++;
	} else {
	  Reg r = ra_alloc1(as, ref, RSET_FPR);
	  if (irt_isnum(ir->t)) ofs = (ofs + 4) & ~4;
	  emit_spstore(as, ir, r, ofs);
	  ofs += irt_isnum(ir->t) ? 8 : 4;
	}
      } else
#endif
      {
	if (gpr <= REGARG_LASTGPR) {
	  lj_assertA(rset_test(as->freeset, gpr),
		     "reg %d not free", gpr);  /* Already evicted. */
	  ra_leftov(as, gpr, ref);
	  gpr++;
	} else {
	  Reg r = ra_alloc1(as, ref, RSET_GPR);
	  emit_spstore(as, ir, r, ofs);
	  ofs += 4;
	}
      }
    } else {
      if (gpr <= REGARG_LASTGPR)
	gpr++;
      else
	ofs += 4;
    }
    checkmclim(as);
  }
#if !LJ_SOFTFP
  if ((ci->flags & CCI_VARARG))  /* Vararg calls need to know about FPR use. */
    emit_tab(as, fpr == REGARG_FIRSTFPR ? PPCI_CRXOR : PPCI_CREQV, 6, 6, 6);
#endif
}

/* Setup result reg/sp for call. Evict scratch regs. */
static void asm_setupresult(ASMState *as, IRIns *ir, const CCallInfo *ci)
{
  RegSet drop = RSET_SCRATCH;
  int hiop = ((ir+1)->o == IR_HIOP && !irt_isnil((ir+1)->t));
#if !LJ_SOFTFP
  if ((ci->flags & CCI_NOFPRCLOBBER))
    drop &= ~RSET_FPR;
#endif
  if (ra_hasreg(ir->r))
    rset_clear(drop, ir->r);  /* Dest reg handled below. */
  if (hiop && ra_hasreg((ir+1)->r))
    rset_clear(drop, (ir+1)->r);  /* Dest reg handled below. */
  ra_evictset(as, drop);  /* Evictions must be performed first. */
  if (ra_used(ir)) {
    lj_assertA(!irt_ispri(ir->t), "PRI dest");
    if (!LJ_SOFTFP && irt_isfp(ir->t)) {
      if ((ci->flags & CCI_CASTU64)) {
	/* Use spill slot or temp slots. */
	int32_t ofs = ir->s ? sps_scale(ir->s) : SPOFS_TMP;
	Reg dest = ir->r;
	if (ra_hasreg(dest)) {
	  ra_free(as, dest);
	  ra_modified(as, dest);
	  emit_fai(as, PPCI_LFD, dest, RID_SP, ofs);
	}
	emit_tai(as, PPCI_STW, RID_RETHI, RID_SP, ofs);
	emit_tai(as, PPCI_STW, RID_RETLO, RID_SP, ofs+4);
      } else {
	ra_destreg(as, ir, RID_FPRET);
      }
    } else if (hiop) {
      ra_destpair(as, ir);
    } else {
      ra_destreg(as, ir, RID_RET);
    }
  }
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
    ci.func = (ASMFunction)(void *)(intptr_t)(irf->i);
  } else {  /* Need a non-argument register for indirect calls. */
    RegSet allow = RSET_GPR & ~RSET_RANGE(RID_R0, REGARG_LASTGPR+1);
    Reg freg = ra_alloc1(as, func, allow);
    *--as->mcp = PPCI_BCTRL;
    *--as->mcp = PPCI_MTCTR | PPCF_T(freg);
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
  emit_setgl(as, base, jit_base);
  emit_addptr(as, base, -8*delta);
  asm_guardcc(as, CC_NE);
  emit_ab(as, PPCI_CMPW, RID_TMP,
	  ra_allock(as, i32ptr(pc), rset_exclude(RSET_GPR, base)));
  emit_tai(as, PPCI_LWZ, RID_TMP, base, -8);
}

/* -- Buffer operations --------------------------------------------------- */

#if LJ_HASBUFFER
static void asm_bufhdr_write(ASMState *as, Reg sb)
{
  Reg tmp = ra_scratch(as, rset_exclude(RSET_GPR, sb));
  IRIns irgc;
  irgc.ot = IRT(0, IRT_PGC);  /* GC type. */
  emit_storeofs(as, &irgc, RID_TMP, sb, offsetof(SBuf, L));
  emit_rot(as, PPCI_RLWIMI, RID_TMP, tmp, 0, 31-lj_fls(SBUF_MASK_FLAG), 31);
  emit_getgl(as, RID_TMP, cur_L);
  emit_loadofs(as, &irgc, tmp, sb, offsetof(SBuf, L));
}
#endif

/* -- Type conversions ---------------------------------------------------- */

#if !LJ_SOFTFP
static void asm_tointg(ASMState *as, IRIns *ir, Reg left)
{
  RegSet allow = RSET_FPR;
  Reg tmp = ra_scratch(as, rset_clear(allow, left));
  Reg fbias = ra_scratch(as, rset_clear(allow, tmp));
  Reg dest = ra_dest(as, ir, RSET_GPR);
  Reg hibias = ra_allock(as, 0x43300000, rset_exclude(RSET_GPR, dest));
  asm_guardcc(as, CC_NE);
  emit_fab(as, PPCI_FCMPU, 0, tmp, left);
  emit_fab(as, PPCI_FSUB, tmp, tmp, fbias);
  emit_fai(as, PPCI_LFD, tmp, RID_SP, SPOFS_TMP);
  emit_tai(as, PPCI_STW, RID_TMP, RID_SP, SPOFS_TMPLO);
  emit_tai(as, PPCI_STW, hibias, RID_SP, SPOFS_TMPHI);
  emit_asi(as, PPCI_XORIS, RID_TMP, dest, 0x8000);
  emit_tai(as, PPCI_LWZ, dest, RID_SP, SPOFS_TMPLO);
  emit_lsptr(as, PPCI_LFS, (fbias & 31),
	     (void *)&as->J->k32[LJ_K32_2P52_2P31], RSET_GPR);
  emit_fai(as, PPCI_STFD, tmp, RID_SP, SPOFS_TMP);
  emit_fb(as, PPCI_FCTIWZ, tmp, left);
}

static void asm_tobit(ASMState *as, IRIns *ir)
{
  RegSet allow = RSET_FPR;
  Reg dest = ra_dest(as, ir, RSET_GPR);
  Reg left = ra_alloc1(as, ir->op1, allow);
  Reg right = ra_alloc1(as, ir->op2, rset_clear(allow, left));
  Reg tmp = ra_scratch(as, rset_clear(allow, right));
  emit_tai(as, PPCI_LWZ, dest, RID_SP, SPOFS_TMPLO);
  emit_fai(as, PPCI_STFD, tmp, RID_SP, SPOFS_TMP);
  emit_fab(as, PPCI_FADD, tmp, left, right);
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
  lj_assertA(!(irt_isint64(ir->t) || (st == IRT_I64 || st == IRT_U64)),
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
      if (st == IRT_NUM)  /* double -> float conversion. */
	emit_fb(as, PPCI_FRSP, dest, ra_alloc1(as, lref, RSET_FPR));
      else  /* float -> double conversion is a no-op on PPC. */
	ra_leftov(as, dest, lref);  /* Do nothing, but may need to move regs. */
    } else {  /* Integer to FP conversion. */
      /* IRT_INT: Flip hibit, bias with 2^52, subtract 2^52+2^31. */