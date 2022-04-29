/*
** ARM64 instruction emitter.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
**
** Contributed by Djordje Kovacevic and Stefan Pejic from RT-RK.com.
** Sponsored by Cisco Systems, Inc.
*/

/* -- Constant encoding --------------------------------------------------- */

static uint64_t get_k64val(ASMState *as, IRRef ref)
{
  IRIns *ir = IR(ref);
  if (ir->o == IR_KINT64) {
    return ir_kint64(ir)->u64;
  } else if (ir->o == IR_KGC) {
    return (uint64_t)ir_kgc(ir);
  } else if (ir->o == IR_KPTR || ir->o == IR_KKPTR) {
    return (uint64_t)ir_kptr(ir);
  } else {
    lj_assertA(ir->o == IR_KINT || ir->o == IR_KNULL,
	       "bad 64 bit const IR op %d", ir->o);
    return ir->i;  /* Sign-extended. */
  }
}

/* Encode constant in K12 format for data processing instructions. */
static uint32_t emit_isk12(int64_t n)
{
  uint64_t k = n < 0 ? ~(uint64_t)n+1u : (uint64_t)n;
  uint32_t m = n < 0 ? 0x40000000 : 0;
  if (k < 0x1000) {
    return A64I_K12|m|A64F_U12(k);
  } else if ((k & 0xfff000) == k) {
    return A64I_K12|m|0x400000|A64F_U12(k>>12);
  }
  return 0;
}

#define emit_clz64(n)	__builtin_clzll(n)
#define emit_ctz64(n)	__builtin_ctzll(n)

/* Encode constant in K13 format for logical data processing instructions. */
static uint32_t emit_isk13(uint64_t n, int is64)
{
  int inv = 0, w = 128, lz, tz;
  if (n & 1) { n = ~n; w = 64; inv = 1; }  /* Avoid wrap-around of ones. */
  if (!n) return 0;  /* Neither all-zero nor all-ones are allowed. */
  do {  /* Find the repeat width. */
    if (is64 && (uint32_t)(n^(n>>32))) break;
    n = (uint32_t)n;
    if (!n) return 0;  /* Ditto when passing n=0xffffffff and is64=0. */
    w = 32; if ((n^(n>>16)) & 0xffff) break;
    n = n & 0xffff; w = 16; if ((n^(n>>8)) & 0xff) break;
    n = n & 0xff; w = 8; if ((n^(n>>4)) & 0xf) break;
    n = n & 0xf; w = 4; if ((n^(n>>2)) & 0x3) break;
    n = n & 0x3; w = 2;
  } while (0);
  lz = emit_clz64(n);
  tz = emit_ctz64(n);
  if ((int64_t)(n << lz) >> (lz+tz) != -1ll) return 0; /* Non-contiguous? */
  if (inv)
    return A64I_K13 | (((lz-w) & 127) << 16) | (((lz+tz-w-1) & 63) << 10);
  else
    return A64I_K13 | ((w-tz) << 16) | (((63-lz-tz-w-w) & 63) << 10);
}

static uint32_t emit_isfpk64(uint64_t n)
{
  uint64_t etop9 = ((n >> 54) & 0x1ff);
  if ((n << 16) == 0 && (etop9 == 0x100 || etop9 == 0x0ff)) {
    return (uint32_t)(((n >> 48) & 0x7f) | ((n >> 56) & 0x80));
  }
  return ~0u;
}

/* -- Emit basic instructions --------------------------------------------- */

static void emit_dnma(ASMState *as, A64Ins ai, Reg rd, Reg rn, Reg rm, Reg ra)
{
  *--as->mcp = ai | A64F_D(rd) | A64F_N(rn) | A64F_M(rm) | A64F_A(ra);
}

static void emit_dnm(ASMState *as, A64Ins ai, Reg rd, Reg rn, Reg rm)
{
  *--as->mcp = ai | A64F_D(rd) | A64F_N(rn) | A64F_M(rm);
}

static void emit_dm(ASMState *as, A64Ins ai, Reg rd, Reg rm)
{
  *--as->mcp = ai | A64F_D(rd) | A64F_M(rm);
}

static void emit_dn(ASMState *as, A64Ins ai, Reg rd, Reg rn)
{
  *--as->mcp = ai | A64F_D(rd) | A64F_N(rn);
}

static void emit_nm(ASMState *as, A64Ins ai, Reg rn, Reg rm)
{
  *--as->mcp = ai | A64F_N(rn) | A64F_M(rm);
}

static void emit_d(ASMState *as, A64Ins ai, Reg rd)
{
  *--as->mcp = ai | A64F_D(rd);
}

static void emit_n(ASMState *as, A64Ins ai, Reg rn)
{
  *--as->mcp = ai | A64F_N(rn);
}

static int emit_checkofs(A64Ins ai, int64_t ofs)
{
  int scale = (ai >> 30) & 3;
  if (ofs < 0 || (ofs & ((1<<scale)-1))) {
    return (ofs >= -256 && ofs <= 255) ? -1 : 0;
  } else {
    return (ofs < (4096<<scale)) ? 1 : 0;
  }
}

static void emit_lso(ASMState *as, A64Ins ai, Reg rd, Reg rn, int64_t ofs)
{
  int ot = emit_checkofs(ai, ofs), sc = (ai >> 30) & 3;
  lj_assertA(ot, "load/store offset %d out of range", ofs);
  /* Combine LDR/STR pairs to LDP/STP. */
  if ((sc == 2 || sc == 3) &&
      (!(ai & 0x400000) || rd != rn) &&
      as->mcp != as->mcloop) {
    uint32_t prev = *as->mcp & ~A64F_D(31);
    int ofsm = ofs - (1<<sc), ofsp = ofs + (1<<sc);
    A64Ins aip;
    if (prev == (ai | A64F_N(rn) | A64F_U12(ofsm>>sc)) ||
	prev == ((ai^A64I_LS_U) | A64F_N(rn) | A64F_S9(ofsm&0x1ff))) {
      aip = (A64F_A(rd) | A64F_D(*as->mcp & 31));
    } else if (prev == (ai | A64F_N(rn) | A64F_U12(ofsp>>sc)) ||
	       prev == ((ai^A64I_LS_U) | A64F_N(rn) | A64F_S9(ofsp&0x1ff))) {
      aip = (A64F_D(rd) | A64F_A(*as->mcp & 31));
      ofsm = ofs;
    } else {
      goto nopair;
    }
    if (ofsm >= (int)((unsigned int)-64<<sc) && ofsm <= (63<<sc)) {
      *as->mcp = aip | A64F_N(rn) | ((ofsm >> sc) << 15) |
	(ai ^ ((ai == A64I_LDRx || ai == A64I_STRx) ? 0x50000000 : 0x90000000));
      return;
    }
  }
nopair:
  if (ot == 1)
    *--as->mcp = ai | A64F_D(rd) | A64F_N(rn) | A64F_U12(ofs >> sc);
  else
    *--as->mcp = (ai^A64I_LS_U) | A64F_D(rd) | A64F_N(rn) | A64F_S9(ofs & 0x1ff);
}

/* -- Emit loads/stores --------------------------------------------------- */

/* Prefer rematerialization of BASE/L from global_State over spills. */
#define emit_canremat(ref)	((ref) <= ASMREF_L)

/* Try to find an N-step delta relative to other consts with N < lim. */
static int emit_kdelta(ASMState *as, Reg rd, uint64_t k, int lim)
{
  RegSet work = (~as->freeset & RSET_GPR) | RID2RSET(RID_GL);
  if (lim <= 1) return 0;  /* Can't beat that. */
  while (work) {
    Reg r = rset_picktop(work);
    IRRef ref = regcost_ref(as->cost[r]);
    lj_assertA(r != rd, "dest reg %d not free", rd);
    if (ref < REF_TRUE) {
      uint64_t kx = ra_iskref(ref) ? (uint64_t)ra_krefk(as, ref) :
				     get_k64val(as, ref);
      int64_t delta = (int64_t)(k - kx);
      if (delta == 0) {
	emit_dm(as, A64I_MOVx, rd, r);
	return 1;
      } else {
	uint32_t k12 = emit_isk12(delta < 0 ? (int64_t)(~(uint64_t)delta+1u) : delta);
	if (k12) {
	  emit_dn(as, (delta < 0 ? A64I_SUBx : A64I_ADDx)^k12, rd, r);
	  return 1;
	}
	/* Do other ops or multi-step deltas pay off? Probably not.
	** E.g. XOR rarely helps with pointer consts.
	*/
      }
    }
    rset_clear(work, r);
  }
  return 0;  /* Failed. */
}

static void emit_loadk(ASMState *as, Reg rd, uint64_t u64, int is64)
{
  int i, zeros = 0, ones = 0, neg;
  if (!is64) u64 = (int64_t)(int32_t)u64;  /* Sign-extend. */
  /* Count homogeneous 16 bit fragments. */
  for (i = 0; i < 4; i++) {
    uint64_t frag = (u64 >> i*16) & 0xffff;
    zeros += (frag == 0);
    ones += (frag == 0xffff);
  }
  neg = ones > zeros;  /* Use MOVN if it pays off. */
  if ((neg ? ones : zeros) < 3) {  /* Need