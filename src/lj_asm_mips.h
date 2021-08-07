/*
** MIPS IR assembler (SSA IR -> machine code).
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

/* Allocate a register or RID_ZERO. */
static Reg ra_alloc1z(ASMState *as, IRRef ref, RegSet allow)
{
  Reg r = IR(ref)->r;
  if (ra_noreg(r)) {
    if (!(allow & RSET_FPR) && irref_isk(ref) && get_kval(as, ref) == 0)
      return RID_ZERO;
    r = ra_allocref(as, ref, allow);
  } else {
    ra_noweak(as, r);
  }
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
      right = ra_alloc1z(as, ir->op2, rset_exclude(allow, left));
    else
      ra_noweak(as, right);
  } else if (ra_hasreg(right)) {
    ra_noweak(as, right);
    left = ra_alloc1z(as, ir->op1, rset_exclude(allow, right));
  } else if (ra_hashint(right)) {
    right = ra_alloc1z(as, ir->op2, allow);
    left = ra_alloc1z(as, ir->op1, rset_exclude(allow, right));
  } else {
    left = ra_alloc1z(as, ir->op1, allow);
    right = ra_alloc1z(as, ir->op2, rset_exclude(allow, left));
  }
  return left | (right << 8);
}

/* -- Guard handling ------------------------------------------------------ */

/* Need some spare long-range jump slots, for out-of-range branches. */
#define MIPS_SPAREJUMP		4

/* Setup spare long-range jump slots per mcarea. */
static void asm_sparejump_setup(ASMState *as)
{
  MCode *mxp = as->mctop;
  if ((char *)mxp == (char *)as->J->mcarea + as->J->szmcarea) {
    mxp -= MIPS_SPAREJUMP*2;
    lj_assertA(MIPSI_NOP == 0, "bad NOP");
    memset(mxp, 0, MIPS_SPAREJUMP*2*sizeof(MCode));
    as->mctop = mxp;
  }
}

static MCode *asm_sparejump_use(MCode *mcarea, MCode tjump)
{
  MCode *mxp = (MCode *)((char *)mcarea + ((MCLink *)mcarea)->size);
  int slot = MIPS_SPAREJUMP;
  while (slot--) {
    mxp -= 2;
    if (*mxp == tjump) {
      return mxp;
    } else if (*mxp == MIPSI_NOP) {
      *mxp = tjump;
      return mxp;
    }
  }
  return NULL;
}

/* Setup exit stub after the end of each trace. */
static void asm_exitstub_setup(ASMState *as)
{
  MCode *mxp = as->mctop;
  /* sw TMP, 0(sp); j ->vm_exit_handler; li TMP, traceno */
  *--mxp = MIPSI_LI|MIPSF_T(RID_TMP)|as->T->traceno;
  *--mxp = MIPSI_J|((((uintptr_t)(void *)lj_vm_exit_handler)>>2)&0x03ffffffu);
  lj_assertA(((uintptr_t)mxp ^ (uintptr_t)(void *)lj_vm_exit_handler)>>28 == 0,
	     "branch target out of range");
  *--mxp = MIPSI_SW|MIPSF_T(RID_TMP)|MIPSF_S(RID_SP)|0;
  as->mctop = mxp;
}

/* Keep this in-sync with exitstub_trace_addr(). */
#define asm_exitstub_addr(as)	((as)->mctop)

/* Emit conditional branch to exit for guard. */
static void asm_guard(ASMState *as, MIPSIns mi, Reg rs, Reg rt)
{
  MCode *target = asm_exitstub_addr(as);
  MCode *p = as->mcp;
  if (LJ_UNLIKELY(p == as->invmcp)) {
    as->invmcp = NULL;
    as->loopinv = 1;
    as->mcp = p+1;
#if !LJ_TARGET_MIPSR6
    mi = mi ^ ((mi>>28) == 1 ? 0x04000000u : 0x00010000u);  /* Invert cond. */
#else
    mi = mi ^ ((mi>>28) == 1 ? 0x04000000u :
	       (mi>>28) == 4 ? 0x00800000u : 0x00010000u);  /* Invert cond. */
#endif
    target = p;  /* Patch target later in asm_loop_fixup. */
  }
  emit_ti(as, MIPSI_LI, RID_TMP, as->snapno);
  emit_branch(as, mi, rs, rt, target);
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
	intptr_t ofs = (intptr_t)&gcref(fn->l.uvptr[(ir->op2 >> 8)])->uv.tv;
	intptr_t jgl = (intptr_t)J2G(as->J);
	if ((uintptr_t)(ofs-jgl) < 65536) {
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
static void asm_fusexref(ASMState *as, MIPSIns mi, Reg rt, IRRef ref,
			 RegSet allow, int32_t ofs)
{
  IRIns *ir = IR(ref);
  Reg base;
  if (ra_noreg(ir->r) && canfuse(as, ir)) {
    if (ir->o == IR_ADD) {
      intptr_t ofs2;
      if (irref_isk(ir->op2) && (ofs2 = ofs + get_kval(as, ir->op2),
				 checki16(ofs2))) {
	ref = ir->op1;
	ofs = (int32_t)ofs2;
      }
    } else if (ir->o == IR_STRREF) {
      intptr_t ofs2 = 65536;
      lj_assertA(ofs == 0, "bad usage");
      ofs = (int32_t)sizeof(GCstr);
      if (irref_isk(ir->op2)) {
	ofs2 = ofs + get_kval(as, ir->op2);
	ref = ir->op1;
      } else if (irref_isk(ir->op1)) {
	ofs2 = ofs + get_kval(as, ir->op1);
	ref = ir->op2;
      }
      if (!checki16(ofs2)) {
	/* NYI: Fuse ADD with constant. */
	Reg right, left = ra_alloc2(as, ir, allow);
	right = (left >> 8); left &= 255;
	emit_hsi(as, mi, rt, RID_TMP, ofs);
	emit_dst(as, MIPSI_AADDU, RID_TMP, left, right);
	return;
      }
      ofs = ofs2;
    }
  }
  base = ra_alloc1(as, ref, allow);
  emit_hsi(as, mi, rt, base, ofs);
}

/* -- Calls --------------------------------------------------------------- */

/* Generate a call to a C function. */
static void asm_gencall(ASMState *as, const CCallInfo *ci, IRRef *args)
{
  uint32_t n, nargs = CCI_XNARGS(ci);
  int32_t ofs = LJ_32 ? 16 : 0;
#if LJ_SOFTFP
  Reg gpr = REGARG_FIRSTGPR;
#else
  Reg gpr, fpr = REGARG_FIRSTFPR;
#endif
  if ((void *)ci->func)
    emit_call(as, (void *)ci->func, 1);
#if !LJ_SOFTFP
  for (gpr = REGARG_FIRSTGPR; gpr <= REGARG_LASTGPR; gpr++)
    as->cost[gpr] = REGCOST(~0u, ASMREF_L);
  gpr = REGARG_FIRSTGPR;
#endif
  for (n = 0; n < nargs; n++) {  /* Setup args. */
    IRRef ref = args[n];
    if (ref) {
      IRIns *ir = IR(ref);
#if !LJ_SOFTFP
      if (irt_isfp(ir->t) && fpr <= REGARG_LASTFPR &&
	  !(ci->flags & CCI_VARARG)) {
	lj_assertA(rset_test(as->freeset, fpr),
		   "reg %d not free", fpr);  /* Already evicted. */
	ra_leftov(as, fpr, ref);
	fpr += LJ_32 ? 2 : 1;
	gpr += (LJ_32 && irt_isnum(ir->t)) ? 2 : 1;
      } else
#endif
      {
#if LJ_32 && !LJ_SOFTFP
	fpr = REGARG_LASTFPR+1;
#endif
	if (LJ_32 && irt_isnum(ir->t)) gpr = (gpr+1) & ~1;
	if (gpr <= REGARG_LASTGPR) {
	  lj_assertA(rset_test(as->freeset, gpr),
		     "reg %d not free", gpr);  /* Already evicted. */
#if !LJ_SOFTFP
	  if (irt_isfp(ir->t)) {
	    RegSet of = as->freeset;
	    Reg r;
	    /* Workaround to protect argument GPRs from being used for remat. */
	    as->freeset &= ~RSET_RANGE(REGARG_FIRSTGPR, REGARG_LASTGPR+1);
	    r = ra_alloc1(as, ref, RSET_FPR);
	    as->freeset |= (of & RSET_RANGE(REGARG_FIRSTGPR, REGARG_LASTGPR+1));
	    if (irt_isnum(ir->t)) {
#if LJ_32
	      emit_tg(as, MIPSI_MFC1, gpr+(LJ_BE?0:1), r+1);
	      emit_tg(as, MIPSI_MFC1, gpr+(LJ_BE?1:0), r);
	      lj_assertA(rset_test(as->freeset, gpr+1),
			 "reg %d not free", gpr+1);  /* Already evicted. */
	      gpr += 2;
#else
	      emit_tg(as, MIPSI_DMFC1, gpr, r);
	      gpr++; fpr++;
#endif
	    } else if (irt_isfloat(ir->t)) {
	      emit_tg(as, MIPSI_MFC1, gpr, r);
	      gpr++;
#if LJ_64
	      fpr++;
#endif
	    }
	  } else
#endif
	  {
	    ra_leftov(as, gpr, ref);
	    gpr++;
#if LJ_64 && !LJ_SOFTFP
	    fpr++;
#endif
	  }
	} else {
	  Reg r = ra_alloc1z(as, ref, !LJ_SOFTFP && irt_isfp(ir->t) ? RSET_FPR : RSET_GPR);
#if LJ_32
	  if (irt_isnum(ir->t)) ofs = (ofs + 4) & ~4;
	  emit_spstore(as, ir, r, ofs);
	  ofs += irt_isnum(ir->t) ? 8 : 4;
#else
	  emit_spstore(as, ir, r, ofs + ((LJ_BE && !irt_isfp(ir->t) && !irt_is64(ir->t)) ? 4 : 0));
	  ofs += 8;
#endif
	}
      }
    } else {
#if !LJ_SOFTFP
      fpr = REGARG_LASTFPR+1;
#endif
      if (gpr <= REGARG_LASTGPR) {
	gpr++;
#if LJ_64 && !LJ_SOFTFP
	fpr++;
#endif
      } else {
	ofs += LJ_32 ? 4 : 8;
      }
    }
    checkmclim(as);
  }
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
	int32_t ofs = sps_scale(ir->s);
	Reg dest = ir->r;
	if (ra_hasreg(dest)) {
	  ra_free(as, dest);
	  ra_modified(as, dest);
#if LJ_32
	  emit_tg(as, MIPSI_MTC1, RID_RETHI, dest+1);
	  emit_tg(as, MIPSI_MTC1, RID_RETLO, dest);
#else
	  emit_tg(as, MIPSI_DMTC1, RID_RET, dest);
#endif
	}
	if (ofs) {
#if LJ_32
	  emit_tsi(as, MIPSI_SW, RID_RETLO, RID_SP, ofs+(LJ_BE?4:0));
	  emit_tsi(as, MIPSI_SW, RID_RETHI, RID_SP, ofs+(LJ_BE?0:4));
#else
	  emit_tsi(as, MIPSI_SD, RID_RET, RID_SP, ofs);
#endif
	}
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
    ci.func = (ASMFunction)(void *)get_kval(as, func);
  } else {  /* Need specific register for indirect calls. */
    Reg r = ra_alloc1(as, func, RID2RSET(RID_CFUNCADDR));
    MCode *p = as->mcp;
    if (r == RID_CFUNCADDR)
      *--p = MIPSI_NOP;
    else
      *--p = MIPSI_MOVE | MIPSF_D(RID_CFUNCADDR) | MIPSF_S(r);
    *--p = MIPSI_JALR | MIPSF_S(r);
    as->mcp = p;
    ci.func = (ASMFunction)(void *)0;
  }
  asm_gencall(as, &ci, args);
}

#if !LJ_SOFTFP
static void asm_callround(ASMState *as, IRIns *ir, IRCallID id)
{
  /* The modified regs must match with the *.dasc implementation. */
  RegSet drop = RID2RSET(RID_R1)|RID2RSET(RID_R12)|RID2RSET(RID_FPRET)|
		RID2RSET(RID_F2)|RID2RSET(RID_F4)|RID2RSET(REGARG_FIRSTFPR)
#if LJ_TARGET_MIPSR6
		|RID2RSET(RID_F21)
#endif
		;
  if (ra_hasreg(ir->r)) rset_clear(drop, ir->r);
  ra_evictset(as, drop);
  ra_destreg(as, ir, RID_FPRET);
  emit_call(as, (void *)lj_ir_callinfo[id].func, 0);
  ra_leftov(as, REGARG_FIRSTFPR, ir->op1);
}
#endif

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
  asm_guard(as, MIPSI_BNE, RID_TMP,
	    ra_allock(as, igcptr(pc), rset_exclude(RSET_GPR, base)));
  emit_tsi(as, MIPSI_AL, RID_TMP, base, -8);
}

/* -- Buffer operations --------------------------------------------------- */

#if LJ_HASBUFFER
static void asm_bufhdr_write(ASMState *as, Reg sb)
{
  Reg tmp = ra_scratch(as, rset_exclude(RSET_GPR, sb));
  IRIns irgc;
  irgc.ot = IRT(0, IRT_PGC);  /* GC type. */
  emit_storeofs(as, &irgc, RID_TMP, sb, offsetof(SBuf, L));
  if ((as->flags & JIT_F_MIPSXXR2)) {
    emit_tsml(as, LJ_64 ? MIPSI_DINS : MIPSI_INS, RID_TMP, tmp,
	      lj_fls(SBUF_MASK_FLAG), 0);
  } else {
    emit_dst(as, MIPSI_OR, RID_TMP, RID_TMP, tmp);
    emit_tsi(as, MIPSI_ANDI, tmp, tmp, SBUF_MASK_FLAG);
  }
  emit_getgl(as, RID_TMP, cur_L);
  emit_loadofs(as, &irgc, tmp, sb, offsetof(SBuf, L));
}
#endif

/* -- Type conversions ---------------------------------------------------- */

#if !LJ_SOFTFP
static void asm_tointg(ASMState *as, IRIns *ir, Reg left)
{
  Reg tmp = ra_scratch(as, rset_exclude(RSET_FPR, left));
  Reg dest = ra_dest(as, ir, RSET_GPR);
#if !LJ_TARGET_MIPSR6
  asm_guard(as, MIPSI_BC1F, 0, 0);
  emit_fgh(as, MIPSI_C_EQ_D, 0, tmp, left);
#else
  asm_guard(as, MIPSI_BC1EQZ, 0, (tmp&31));
  emit_fgh(as, MIPSI_CMP_EQ_D, tmp, tmp, left);
#endif
  emit_fg(as, MIPSI_CVT_D_W, tmp, tmp);
  emit_tg(as, MIPSI_MFC1, dest, tmp);
  emit_fg(as, MIPSI_CVT_W_D, tmp, left);
}

static void asm_tobit(ASMState *as, IRIns *ir)
{
  RegSet allow = RSET_FPR;
  Reg dest = ra_dest(as, ir, RSET_GPR);
  Reg left = ra_alloc1(as, ir->op1, allow);
  Reg right = ra_alloc1(as, ir->op2, rset_clear(allow, left));
  Reg tmp = ra_scratch(as, rset_clear(allow, right));
  emit_tg(as, MIPSI_MFC1, dest, tmp);
  emit_fgh(as, MIPSI_ADD_D, tmp, left, right);
}
#elif LJ_64  /* && LJ_SOFTFP */
static void asm_tointg(ASMState *as, IRIns *ir, Reg r)
{
  /* The modified regs must match with the *.dasc implementation. */
  RegSet drop = RID2RSET(REGARG_FIRSTGPR)|RID2RSET(RID_RET)|RID2RSET(RID_RET+1)|
		RID2RSET(RID_R1)|RID2RSET(RID_R12);
  if (ra_hasreg(ir->r)) rset_clear(drop, ir->r);
  ra_evictset(as, drop);
  /* Return values are in RID_RET (converted value) and RID_RET+1 (status). */
  ra_destreg(as, ir, RID_RET);
  asm_guard(as, MIPSI_BNE, RID_RET+1, RID_ZERO);
  emit_call(as, (void *)lj_ir_callinfo[IRCALL_lj_vm_tointg].func, 0);
  if (r == RID_NONE)
    ra_leftov(as, REGARG_FIRSTGPR, ir->op1);
  else if (r != REGARG_FIRSTGPR)
    emit_move(as, REGARG_FIRSTGPR, r);
}

static void asm_tobit(ASMState *as, IRIns *ir)
{
  Reg dest = ra_dest(as, ir, RSET_GPR);
  emit_dta(as, MIPSI_SLL, dest, dest, 0);
  asm_callid(as, ir, IRCALL_lj_vm_tobit);
}
#endif

static void asm_conv(ASMState *as, IRIns *ir)
{
  IRType st = (IRType)(ir->op2 & IRCONV_SRCMASK);
#if !LJ_SOFTFP32
  int stfp = (st == IRT_NUM || st == IRT_FLOAT);
#endif
#if LJ_64
  int st64 = (st == IRT_I64 || st == IRT_U64 || st == IRT_P64);
#endif
  IRRef lref = ir->op1;
#if LJ_32
  /* 64 bit integer conversions are handled by SPLIT. */
  lj_assertA(!(irt_isint64(ir->t) || (st == IRT_I64 || st == IRT_U64)),
	     "IR %04d has unsplit 64 bit type",
	     (int)(ir - as->ir) - REF_BIAS);
#endif
#if LJ_SOFTFP32
  /* FP conversions are handled by SPLIT. */
  lj_assertA(!irt_isfp(ir->t) && !(st == IRT_NUM || st == IRT_FLOAT),
	     "IR %04d has FP type",
	     (int)(ir - as->ir) - REF_BIAS);
  /* Can't check for same types: SPLIT uses CONV int.int + BXOR for sfp NEG. */
#else
  lj_assertA(irt_type(ir->t) != st, "inconsistent types for CONV");
#if !LJ_SOFTFP
  if (irt_isfp(ir->t)) {
    Reg dest = ra_dest(as, ir, RSET_FPR);
    if (stfp) {  /* FP to FP conversion. */
      emit_fg(as, st == IRT_NUM ? MIPSI_CVT_S_D : MIPSI_CVT_D_S,
	      dest, ra_alloc1(as, lref, RSET_FPR));
    } else if (st == IRT_U32) {  /* U32 to FP conversion. */
      /* y = (x ^ 0x8000000) + 2147483648.0 */
      Reg left = ra_alloc1(as, lref, RSET_GPR);
      Reg tmp = ra_scratch(as, rset_exclude(RSET_FPR, dest));
      if (irt_isfloat(ir->t))
	emit_fg(as, MIPSI_CVT_S_D, dest, dest);
      /* Must perform arithmetic with doubles to keep the precision. */
      emit_fgh(as, MIPSI_ADD_D, dest, dest, tmp);
      emit_fg(as, MIPSI_CVT_D_W, dest, dest);
      emit_lsptr(as, MIPSI_LDC1, (tmp & 31),
		 (void *)&as->J->k64[LJ_K64_2P31], RSET_GPR);
      emit_tg(as, MIPSI_MTC1, RID_TMP, dest);
      emit_dst(as, MIPSI_XOR, RID_TMP, RID_TMP, left);
      emit_ti(as, MIPSI_LUI, RID_TMP, 0x8000);
#if LJ_64
    } else if(st == IRT_U64) {  /* U64 to FP conversion. */
      /* if (x >= 1u<<63) y = (double)(int64_t)(x&(1u<<63)-1) + pow(2.0, 63) */
      Reg left = ra_alloc1(as, lref, RSET_GPR);
      Reg tmp = ra_scratch(as, rset_exclude(RSET_FPR, dest));
      MCLabel l_end = emit_label(as);
      if (irt_isfloat(ir->t)) {
	emit_fgh(as, MIPSI_ADD_S, dest, dest, tmp);
	emit_lsptr(as, MIPSI_LWC1, (tmp & 3