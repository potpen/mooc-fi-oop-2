/*
** ARM instruction emitter.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

/* -- Constant encoding --------------------------------------------------- */

static uint8_t emit_invai[16] = {
  /* AND */ (ARMI_AND^ARMI_BIC) >> 21,
  /* EOR */ 0,
  /* SUB */ (ARMI_SUB^ARMI_ADD) >> 21,
  /* RSB */ 0,
  /* ADD */ (ARMI_ADD^ARMI_SUB) >> 21,
  /* ADC */ (ARMI_ADC^ARMI_SBC) >> 21,
  /* SBC */ (ARMI_SBC^ARMI_ADC) >> 21,
  /* RSC */ 0,
  /* TST */ 0,
  /* TEQ */ 0,
  /* CMP */ (ARMI_CMP^ARMI_CMN) >> 21,
  /* CMN */ (ARMI_CMN^ARMI_CMP) >> 21,
  /* ORR */ 0,
  /* MOV */ (ARMI_MOV^ARMI_MVN) >> 21,
  /* BIC */ (ARMI_BIC^ARMI_AND) >> 21,
  /* MVN */ (ARMI_MVN^ARMI_MOV) >> 21
};

/* Encode constant in K12 format for data processing instructions. */
static uint32_t emit_isk12(ARMIns ai, int32_t n)
{
  uint32_t invai, i, m = (uint32_t)n;
  /* K12: unsigned 8 bit value, rotated in steps of two bits. */
  for (i = 0; i < 4096; i += 256, m = lj_rol(m, 2))
    if (m <= 255) return ARMI_K12|m|i;
  /* Otherwise try negation/complement with the inverse instruction. */
  invai = emit_invai[((ai >> 21) & 15)];
  if (!invai) return 0;  /* Failed. No inverse instruction. */
  m = ~(uint32_t)n;
  if (invai == ((ARMI_SUB^ARMI_ADD) >> 21) ||
      invai == (ARMI_CMP^ARMI_CMN) >> 21) m++;
  for (i = 0; i < 4096; i += 256, m = lj_rol(m, 2))
    if (m <= 255) return ARMI_K12|(invai<<21)|m|i;
  return 0;  /* Failed. */
}

/* -- Emit basic instructions --------------------------------------------- */

static void emit_dnm(ASMState *as, ARMIns ai, Reg rd, Reg rn, Reg rm)
{
  *--as->mcp = ai | ARMF_D(rd) | ARMF_N(rn) | ARMF_M(rm);
}

static void emit_dm(ASMState *as, ARMIns ai, Reg rd, Reg rm)
{
  *--as->mcp = ai | ARMF_D(rd) | ARMF_M(rm);
}

static void emit_dn(ASMState *as, ARMIns ai, Reg rd, Reg rn)
{
  *--as->mcp = ai | ARMF_D(rd) | ARMF_N(rn);
}

static void emit_nm(ASMState *as, ARMIns ai, Reg rn, Reg rm)
{
  *--as->mcp = ai | ARMF_N(rn) | ARMF_M(rm);
}

static void emit_d(ASMState *as, ARMIns ai, Reg rd)
{
  *--as->mcp = ai | ARMF_D(rd);
}

static void emit_n(ASMState *as, ARMIns ai, Reg rn)
{
  *--as->mcp = ai | ARMF_N(rn);
}

static void emit_m(ASMState *as, ARMIns ai, Reg rm)
{
  *--as->mcp = ai | ARMF_M(rm);
}

static void emit_lsox(ASMState *as, ARMIns ai, Reg rd, Reg rn, int32_t ofs)
{
  lj_assertA(ofs >= -255 && ofs <= 255,
	     "load/store offset %d out of range", ofs);
  if (ofs < 0) ofs = -ofs; else ai |= ARMI_LS_U;
  *--as->mcp = ai | ARMI_LS_P | ARMI_LSX_I | ARMF_D(rd) | ARMF_N(rn) |
	       ((ofs & 0xf0) << 4) | (ofs & 0x0f);
}

static void emit_lso(ASMState *as, ARMIns ai, Reg rd, Reg rn, int32_t ofs)
{
  lj_assertA(ofs >= -4095 && ofs <= 4095,
	     "load/store offset %d out of range", ofs);
  /* Combine LDR/STR pairs to LDRD/STRD. */
  if (*as->mcp == (ai|ARMI_LS_P|ARMI_LS_U|ARMF_D(rd^1)|ARMF_N(rn)|(ofs^4)) &&
      (ai & ~(ARMI_LDR^ARMI_STR)) == ARMI_STR && rd != rn &&
      (uint32_t)ofs <= 252 && !(ofs & 3) && !((rd ^ (ofs >>2)) & 1) &&
      as->mcp != as->mcloop) {
    as->mcp++;
    emit_lsox(as, ai == ARMI_LDR ? ARMI_LDRD : ARMI_STRD, rd&~1, rn, ofs&~4);
    return;
  }
  if (ofs < 0) ofs = -ofs; else ai |= ARMI_LS_U;
  *--as->mcp = ai | ARMI_LS_P | ARMF_D(rd) | ARMF_N(rn) | ofs;
}

#if !LJ_SOFTFP
static void emit_vlso(ASMState *as, ARMIns ai, Reg rd, Reg rn, int32_t ofs)
{
  lj_assertA(ofs >= -1020 && ofs <= 1020 && (ofs&3) == 0,
	     "load/store offset %d out of range", ofs);
  if (ofs < 0) ofs = -ofs; else ai |= ARMI_LS_U;
  *--as->mcp = ai | ARMI_LS_P | ARMF_D(rd & 15) | ARMF_N(rn) | (ofs >> 2);
}
#endif

/* -- Emit loads/stores --------------------------------------------------- */

/* Prefer spills of BASE/L. */
#define emit_canremat(ref)	((ref) < ASMREF_L)

/* Try to find a one step delta relative to another constant. */
static int emit_kdelta1(ASMState *as, Reg d, int32_t i)
{
  RegSet work = ~as->freeset & RSET_GPR;
  while (work) {
    Reg r = rset_picktop(work);
    IRRef ref = regcost_ref(as->cost[r]);
    lj_assertA(r != d, "dest reg not free");
    if (emit_canremat(ref)) {
      int32_t delta = i - (ra_iskref(ref) ? ra_krefk(as, ref) : IR(ref)->i);
      uint32_t k = emit_isk12(ARMI_ADD, delta);
      if (k) {
	if (k == ARMI_K12)
	  emit_dm(as, ARMI_MOV, d, r);
	else
	  emit_dn(as, ARMI_ADD^k, d, r);
	return 1;
      }
    }
    rset_clear(work, r);
  }
  return 0;  /* Failed. */
}

/* Try to find a two step delta relative to another constant. */
static int emit_kdelta2(ASMState *as, Reg rd, int32_t i)
{
  RegSet work = ~as->freeset & RSET_GPR;
  while (work) {
    Reg r = rset_picktop(work);
    IRRef ref = regcost_ref(as->cost[r]);
    lj_assertA(r != rd, "dest reg %d not free", rd);
    if (emit_canremat(ref)) {
      int32_t other = ra_iskref(ref) ? ra_krefk(as, ref) : IR(ref)->i;
      if (other) {
	int