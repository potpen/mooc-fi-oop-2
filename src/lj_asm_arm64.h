
/*
** ARM64 IR assembler (SSA IR -> machine code).
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
**
** Contributed by Djordje Kovacevic and Stefan Pejic from RT-RK.com.
** Sponsored by Cisco Systems, Inc.
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

/* Setup all needed exit stubs. */
static void asm_exitstub_setup(ASMState *as, ExitNo nexits)
{
  ExitNo i;
  MCode *mxp = as->mctop;
  if (mxp - (nexits + 3 + MCLIM_REDZONE) < as->mclim)
    asm_mclimit(as);
  /* 1: str lr,[sp]; bl ->vm_exit_handler; movz w0,traceno; bl <1; bl <1; ... */
  for (i = nexits-1; (int32_t)i >= 0; i--)
    *--mxp = A64I_LE(A64I_BL | A64F_S26(-3-i));
  *--mxp = A64I_LE(A64I_MOVZw | A64F_U16(as->T->traceno));
  mxp--;
  *mxp = A64I_LE(A64I_BL | A64F_S26(((MCode *)(void *)lj_vm_exit_handler-mxp)));
  *--mxp = A64I_LE(A64I_STRx | A64F_D(RID_LR) | A64F_N(RID_SP));
  as->mctop = mxp;
}

static MCode *asm_exitstub_addr(ASMState *as, ExitNo exitno)
{
  /* Keep this in-sync with exitstub_trace_addr(). */
  return as->mctop + exitno + 3;
}

/* Emit conditional branch to exit for guard. */
static void asm_guardcc(ASMState *as, A64CC cc)
{
  MCode *target = asm_exitstub_addr(as, as->snapno);
  MCode *p = as->mcp;
  if (LJ_UNLIKELY(p == as->invmcp)) {
    as->loopinv = 1;
    *p = A64I_B | A64F_S26(target-p);
    emit_cond_branch(as, cc^1, p-1);
    return;
  }
  emit_cond_branch(as, cc, target);
}

/* Emit test and branch instruction to exit for guard. */
static void asm_guardtnb(ASMState *as, A64Ins ai, Reg r, uint32_t bit)
{
  MCode *target = asm_exitstub_addr(as, as->snapno);
  MCode *p = as->mcp;
  if (LJ_UNLIKELY(p == as->invmcp)) {
    as->loopinv = 1;
    *p = A64I_B | A64F_S26(target-p);
    emit_tnb(as, ai^0x01000000u, r, bit, p-1);
    return;
  }
  emit_tnb(as, ai, r, bit, target);
}

/* Emit compare and branch instruction to exit for guard. */
static void asm_guardcnb(ASMState *as, A64Ins ai, Reg r)
{
  MCode *target = asm_exitstub_addr(as, as->snapno);
  MCode *p = as->mcp;
  if (LJ_UNLIKELY(p == as->invmcp)) {
    as->loopinv = 1;
    *p = A64I_B | A64F_S26(target-p);
    emit_cnb(as, ai^0x01000000u, r, p-1);
    return;
  }
  emit_cnb(as, ai, r, target);
}

/* -- Operand fusion ------------------------------------------------------ */

/* Limit linear search to this distance. Avoids O(n^2) behavior. */
#define CONFLICT_SEARCH_LIM	31

static int asm_isk32(ASMState *as, IRRef ref, int32_t *k)
{
  if (irref_isk(ref)) {
    IRIns *ir = IR(ref);
    if (ir->o == IR_KNULL || !irt_is64(ir->t)) {
      *k = ir->i;
      return 1;
    } else if (checki32((int64_t)ir_k64(ir)->u64)) {
      *k = (int32_t)ir_k64(ir)->u64;
      return 1;
    }
  }
  return 0;
}

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

#define FUSE_REG	0x40000000

/* Fuse array/hash/upvalue reference into register+offset operand. */