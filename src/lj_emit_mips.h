
/*
** MIPS instruction emitter.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

#if LJ_64
static intptr_t get_k64val(ASMState *as, IRRef ref)
{
  IRIns *ir = IR(ref);
  if (ir->o == IR_KINT64) {
    return (intptr_t)ir_kint64(ir)->u64;
  } else if (ir->o == IR_KGC) {
    return (intptr_t)ir_kgc(ir);
  } else if (ir->o == IR_KPTR || ir->o == IR_KKPTR) {
    return (intptr_t)ir_kptr(ir);
  } else if (LJ_SOFTFP && ir->o == IR_KNUM) {
    return (intptr_t)ir_knum(ir)->u64;
  } else {
    lj_assertA(ir->o == IR_KINT || ir->o == IR_KNULL,
	       "bad 64 bit const IR op %d", ir->o);
    return ir->i;  /* Sign-extended. */
  }
}
#endif

#if LJ_64
#define get_kval(as, ref)	get_k64val(as, ref)
#else
#define get_kval(as, ref)	(IR((ref))->i)
#endif

/* -- Emit basic instructions --------------------------------------------- */

static void emit_dst(ASMState *as, MIPSIns mi, Reg rd, Reg rs, Reg rt)
{
  *--as->mcp = mi | MIPSF_D(rd) | MIPSF_S(rs) | MIPSF_T(rt);
}

static void emit_dta(ASMState *as, MIPSIns mi, Reg rd, Reg rt, uint32_t a)
{
  *--as->mcp = mi | MIPSF_D(rd) | MIPSF_T(rt) | MIPSF_A(a);
}

#define emit_ds(as, mi, rd, rs)		emit_dst(as, (mi), (rd), (rs), 0)
#define emit_tg(as, mi, rt, rg)		emit_dst(as, (mi), (rg)&31, 0, (rt))

static void emit_tsi(ASMState *as, MIPSIns mi, Reg rt, Reg rs, int32_t i)
{
  *--as->mcp = mi | MIPSF_T(rt) | MIPSF_S(rs) | (i & 0xffff);
}

#define emit_ti(as, mi, rt, i)		emit_tsi(as, (mi), (rt), 0, (i))
#define emit_hsi(as, mi, rh, rs, i)	emit_tsi(as, (mi), (rh) & 31, (rs), (i))

static void emit_fgh(ASMState *as, MIPSIns mi, Reg rf, Reg rg, Reg rh)
{
  *--as->mcp = mi | MIPSF_F(rf&31) | MIPSF_G(rg&31) | MIPSF_H(rh&31);
}

#define emit_fg(as, mi, rf, rg)		emit_fgh(as, (mi), (rf), (rg), 0)

static void emit_rotr(ASMState *as, Reg dest, Reg src, Reg tmp, uint32_t shift)
{
  if (LJ_64 || (as->flags & JIT_F_MIPSXXR2)) {
    emit_dta(as, MIPSI_ROTR, dest, src, shift);
  } else {
    emit_dst(as, MIPSI_OR, dest, dest, tmp);
    emit_dta(as, MIPSI_SLL, dest, src, (-shift)&31);
    emit_dta(as, MIPSI_SRL, tmp, src, shift);
  }
}

#if LJ_64 || LJ_HASBUFFER
static void emit_tsml(ASMState *as, MIPSIns mi, Reg rt, Reg rs, uint32_t msb,
		      uint32_t lsb)
{
  *--as->mcp = mi | MIPSF_T(rt) | MIPSF_S(rs) | MIPSF_M(msb) | MIPSF_L(lsb);
}
#endif

/* -- Emit loads/stores --------------------------------------------------- */

/* Prefer rematerialization of BASE/L from global_State over spills. */
#define emit_canremat(ref)	((ref) <= REF_BASE)