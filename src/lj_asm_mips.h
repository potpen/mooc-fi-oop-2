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
  while