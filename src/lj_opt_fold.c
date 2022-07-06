
/*
** FOLD: Constant Folding, Algebraic Simplifications and Reassociation.
** ABCelim: Array Bounds Check Elimination.
** CSE: Common-Subexpression Elimination.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

#define lj_opt_fold_c
#define LUA_CORE

#include <math.h>

#include "lj_obj.h"

#if LJ_HASJIT

#include "lj_buf.h"
#include "lj_str.h"
#include "lj_tab.h"
#include "lj_ir.h"
#include "lj_jit.h"
#include "lj_ircall.h"
#include "lj_iropt.h"
#include "lj_trace.h"
#if LJ_HASFFI
#include "lj_ctype.h"
#include "lj_carith.h"
#endif
#include "lj_vm.h"
#include "lj_strscan.h"
#include "lj_strfmt.h"

/* Here's a short description how the FOLD engine processes instructions:
**
** The FOLD engine receives a single instruction stored in fins (J->fold.ins).
** The instruction and its operands are used to select matching fold rules.
** These are applied iteratively until a fixed point is reached.
**
** The 8 bit opcode of the instruction itself plus the opcodes of the
** two instructions referenced by its operands form a 24 bit key
** 'ins left right' (unused operands -> 0, literals -> lowest 8 bits).
**
** This key is used for partial matching against the fold rules. The
** left/right operand fields of the key are successively masked with
** the 'any' wildcard, from most specific to least specific:
**
**   ins left right
**   ins any  right
**   ins left any
**   ins any  any
**
** The masked key is used to lookup a matching fold rule in a semi-perfect
** hash table. If a matching rule is found, the related fold function is run.
** Multiple rules can share the same fold function. A fold rule may return
** one of several special values:
**
** - NEXTFOLD means no folding was applied, because an additional test
**   inside the fold function failed. Matching continues against less
**   specific fold rules. Finally the instruction is passed on to CSE.
**
** - RETRYFOLD means the instruction was modified in-place. Folding is
**   retried as if this instruction had just been received.
**
** All other return values are terminal actions -- no further folding is
** applied:
**
** - INTFOLD(i) returns a reference to the integer constant i.
**
** - LEFTFOLD and RIGHTFOLD return the left/right operand reference
**   without emitting an instruction.
**
** - CSEFOLD and EMITFOLD pass the instruction directly to CSE or emit
**   it without passing through any further optimizations.
**
** - FAILFOLD, DROPFOLD and CONDFOLD only apply to instructions which have
**   no result (e.g. guarded assertions): FAILFOLD means the guard would
**   always fail, i.e. the current trace is pointless. DROPFOLD means
**   the guard is always true and has been eliminated. CONDFOLD is a
**   shortcut for FAILFOLD + cond (i.e. drop if true, otherwise fail).
**
** - Any other return value is interpreted as an IRRef or TRef. This
**   can be a reference to an existing or a newly created instruction.
**   Only the least-significant 16 bits (IRRef1) are used to form a TRef
**   which is finally returned to the caller.
**
** The FOLD engine receives instructions both from the trace recorder and
** substituted instructions from LOOP unrolling. This means all types
** of instructions may end up here, even though the recorder bypasses
** FOLD in some cases. Thus all loads, stores and allocations must have
** an any/any rule to avoid being passed on to CSE.
**
** Carefully read the following requirements before adding or modifying
** any fold rules:
**
** Requirement #1: All fold rules must preserve their destination type.
**
** Consistently use INTFOLD() (KINT result) or lj_ir_knum() (KNUM result).
** Never use lj_ir_knumint() which can have either a KINT or KNUM result.
**
** Requirement #2: Fold rules should not create *new* instructions which
** reference operands *across* PHIs.
**
** E.g. a RETRYFOLD with 'fins->op1 = fleft->op1' is invalid if the
** left operand is a PHI. Then fleft->op1 would point across the PHI
** frontier to an invariant instruction. Adding a PHI for this instruction
** would be counterproductive. The solution is to add a barrier which
** prevents folding across PHIs, i.e. 'PHIBARRIER(fleft)' in this case.
** The only exception is for recurrences with high latencies like
** repeated int->num->int conversions.
**
** One could relax this condition a bit if the referenced instruction is
** a PHI, too. But this often leads to worse code due to excessive
** register shuffling.
**
** Note: returning *existing* instructions (e.g. LEFTFOLD) is ok, though.
** Even returning fleft->op1 would be ok, because a new PHI will added,
** if needed. But again, this leads to excessive register shuffling and
** should be avoided.
**
** Requirement #3: The set of all fold rules must be monotonic to guarantee
** termination.
**
** The goal is optimization, so one primarily wants to add strength-reducing
** rules. This means eliminating an instruction or replacing an instruction
** with one or more simpler instructions. Don't add fold rules which point
** into the other direction.
**
** Some rules (like commutativity) do not directly reduce the strength of
** an instruction, but enable other fold rules (e.g. by moving constants
** to the right operand). These rules must be made unidirectional to avoid
** cycles.
**
** Rule of thumb: the trace recorder expands the IR and FOLD shrinks it.
*/

/* Some local macros to save typing. Undef'd at the end. */
#define IR(ref)		(&J->cur.ir[(ref)])
#define fins		(&J->fold.ins)
#define fleft		(J->fold.left)
#define fright		(J->fold.right)
#define knumleft	(ir_knum(fleft)->n)
#define knumright	(ir_knum(fright)->n)

/* Pass IR on to next optimization in chain (FOLD). */
#define emitir(ot, a, b)	(lj_ir_set(J, (ot), (a), (b)), lj_opt_fold(J))

/* Fold function type. Fastcall on x86 significantly reduces their size. */
typedef IRRef (LJ_FASTCALL *FoldFunc)(jit_State *J);

/* Macros for the fold specs, so buildvm can recognize them. */
#define LJFOLD(x)
#define LJFOLDX(x)
#define LJFOLDF(name)	static TRef LJ_FASTCALL fold_##name(jit_State *J)
/* Note: They must be at the start of a line or buildvm ignores them! */

/* Barrier to prevent using operands across PHIs. */
#define PHIBARRIER(ir)	if (irt_isphi((ir)->t)) return NEXTFOLD

/* Barrier to prevent folding across a GC step.
** GC steps can only happen at the head of a trace and at LOOP.
** And the GC is only driven forward if there's at least one allocation.
*/
#define gcstep_barrier(J, ref) \
  ((ref) < J->chain[IR_LOOP] && \
   (J->chain[IR_SNEW] || J->chain[IR_XSNEW] || \
    J->chain[IR_TNEW] || J->chain[IR_TDUP] || \
    J->chain[IR_CNEW] || J->chain[IR_CNEWI] || \
    J->chain[IR_BUFSTR] || J->chain[IR_TOSTR] || J->chain[IR_CALLA]))

/* -- Constant folding for FP numbers ------------------------------------- */

LJFOLD(ADD KNUM KNUM)
LJFOLD(SUB KNUM KNUM)
LJFOLD(MUL KNUM KNUM)
LJFOLD(DIV KNUM KNUM)
LJFOLD(LDEXP KNUM KNUM)
LJFOLD(MIN KNUM KNUM)
LJFOLD(MAX KNUM KNUM)
LJFOLDF(kfold_numarith)
{
  lua_Number a = knumleft;
  lua_Number b = knumright;
  lua_Number y = lj_vm_foldarith(a, b, fins->o - IR_ADD);
  return lj_ir_knum(J, y);
}

LJFOLD(NEG KNUM FLOAD)
LJFOLD(ABS KNUM FLOAD)
LJFOLDF(kfold_numabsneg)
{
  lua_Number a = knumleft;
  lua_Number y = lj_vm_foldarith(a, a, fins->o - IR_ADD);
  return lj_ir_knum(J, y);
}

LJFOLD(LDEXP KNUM KINT)
LJFOLDF(kfold_ldexp)
{
#if LJ_TARGET_X86ORX64
  UNUSED(J);
  return NEXTFOLD;
#else
  return lj_ir_knum(J, ldexp(knumleft, fright->i));
#endif
}

LJFOLD(FPMATH KNUM any)
LJFOLDF(kfold_fpmath)
{
  lua_Number a = knumleft;
  lua_Number y = lj_vm_foldfpm(a, fins->op2);
  return lj_ir_knum(J, y);
}

LJFOLD(CALLN KNUM any)
LJFOLDF(kfold_fpcall1)
{
  const CCallInfo *ci = &lj_ir_callinfo[fins->op2];
  if (CCI_TYPE(ci) == IRT_NUM) {
    double y = ((double (*)(double))ci->func)(knumleft);
    return lj_ir_knum(J, y);
  }
  return NEXTFOLD;
}

LJFOLD(CALLN CARG IRCALL_atan2)
LJFOLDF(kfold_fpcall2)
{
  if (irref_isk(fleft->op1) && irref_isk(fleft->op2)) {
    const CCallInfo *ci = &lj_ir_callinfo[fins->op2];
    double a = ir_knum(IR(fleft->op1))->n;
    double b = ir_knum(IR(fleft->op2))->n;
    double y = ((double (*)(double, double))ci->func)(a, b);
    return lj_ir_knum(J, y);
  }
  return NEXTFOLD;
}

LJFOLD(POW KNUM KNUM)
LJFOLDF(kfold_numpow)
{
  return lj_ir_knum(J, lj_vm_foldarith(knumleft, knumright, IR_POW - IR_ADD));
}

/* Must not use kfold_kref for numbers (could be NaN). */
LJFOLD(EQ KNUM KNUM)
LJFOLD(NE KNUM KNUM)
LJFOLD(LT KNUM KNUM)
LJFOLD(GE KNUM KNUM)
LJFOLD(LE KNUM KNUM)
LJFOLD(GT KNUM KNUM)
LJFOLD(ULT KNUM KNUM)
LJFOLD(UGE KNUM KNUM)
LJFOLD(ULE KNUM KNUM)
LJFOLD(UGT KNUM KNUM)
LJFOLDF(kfold_numcomp)
{
  return CONDFOLD(lj_ir_numcmp(knumleft, knumright, (IROp)fins->o));
}

/* -- Constant folding for 32 bit integers -------------------------------- */

static int32_t kfold_intop(int32_t k1, int32_t k2, IROp op)
{
  switch (op) {
  case IR_ADD: k1 += k2; break;
  case IR_SUB: k1 -= k2; break;
  case IR_MUL: k1 *= k2; break;
  case IR_MOD: k1 = lj_vm_modi(k1, k2); break;
  case IR_NEG: k1 = (int32_t)(~(uint32_t)k1+1u); break;
  case IR_BAND: k1 &= k2; break;
  case IR_BOR: k1 |= k2; break;
  case IR_BXOR: k1 ^= k2; break;
  case IR_BSHL: k1 <<= (k2 & 31); break;
  case IR_BSHR: k1 = (int32_t)((uint32_t)k1 >> (k2 & 31)); break;
  case IR_BSAR: k1 >>= (k2 & 31); break;
  case IR_BROL: k1 = (int32_t)lj_rol((uint32_t)k1, (k2 & 31)); break;
  case IR_BROR: k1 = (int32_t)lj_ror((uint32_t)k1, (k2 & 31)); break;
  case IR_MIN: k1 = k1 < k2 ? k1 : k2; break;
  case IR_MAX: k1 = k1 > k2 ? k1 : k2; break;
  default: lj_assertX(0, "bad IR op %d", op); break;
  }
  return k1;
}

LJFOLD(ADD KINT KINT)
LJFOLD(SUB KINT KINT)
LJFOLD(MUL KINT KINT)
LJFOLD(MOD KINT KINT)
LJFOLD(NEG KINT KINT)
LJFOLD(BAND KINT KINT)
LJFOLD(BOR KINT KINT)
LJFOLD(BXOR KINT KINT)
LJFOLD(BSHL KINT KINT)
LJFOLD(BSHR KINT KINT)
LJFOLD(BSAR KINT KINT)
LJFOLD(BROL KINT KINT)
LJFOLD(BROR KINT KINT)
LJFOLD(MIN KINT KINT)
LJFOLD(MAX KINT KINT)
LJFOLDF(kfold_intarith)
{
  return INTFOLD(kfold_intop(fleft->i, fright->i, (IROp)fins->o));
}

LJFOLD(ADDOV KINT KINT)
LJFOLD(SUBOV KINT KINT)
LJFOLD(MULOV KINT KINT)
LJFOLDF(kfold_intovarith)
{
  lua_Number n = lj_vm_foldarith((lua_Number)fleft->i, (lua_Number)fright->i,
				 fins->o - IR_ADDOV);
  int32_t k = lj_num2int(n);
  if (n != (lua_Number)k)
    return FAILFOLD;
  return INTFOLD(k);
}

LJFOLD(BNOT KINT)
LJFOLDF(kfold_bnot)
{
  return INTFOLD(~fleft->i);
}

LJFOLD(BSWAP KINT)
LJFOLDF(kfold_bswap)
{
  return INTFOLD((int32_t)lj_bswap((uint32_t)fleft->i));
}

LJFOLD(LT KINT KINT)
LJFOLD(GE KINT KINT)
LJFOLD(LE KINT KINT)
LJFOLD(GT KINT KINT)
LJFOLD(ULT KINT KINT)
LJFOLD(UGE KINT KINT)
LJFOLD(ULE KINT KINT)
LJFOLD(UGT KINT KINT)
LJFOLD(ABC KINT KINT)
LJFOLDF(kfold_intcomp)
{
  int32_t a = fleft->i, b = fright->i;
  switch ((IROp)fins->o) {
  case IR_LT: return CONDFOLD(a < b);
  case IR_GE: return CONDFOLD(a >= b);
  case IR_LE: return CONDFOLD(a <= b);
  case IR_GT: return CONDFOLD(a > b);
  case IR_ULT: return CONDFOLD((uint32_t)a < (uint32_t)b);
  case IR_UGE: return CONDFOLD((uint32_t)a >= (uint32_t)b);
  case IR_ULE: return CONDFOLD((uint32_t)a <= (uint32_t)b);
  case IR_ABC:
  case IR_UGT: return CONDFOLD((uint32_t)a > (uint32_t)b);
  default: lj_assertJ(0, "bad IR op %d", fins->o); return FAILFOLD;
  }
}

LJFOLD(UGE any KINT)
LJFOLDF(kfold_intcomp0)
{
  if (fright->i == 0)
    return DROPFOLD;
  return NEXTFOLD;
}

/* -- Constant folding for 64 bit integers -------------------------------- */

static uint64_t kfold_int64arith(jit_State *J, uint64_t k1, uint64_t k2,
				 IROp op)
{
  UNUSED(J);
#if LJ_HASFFI
  switch (op) {
  case IR_ADD: k1 += k2; break;
  case IR_SUB: k1 -= k2; break;
  case IR_MUL: k1 *= k2; break;
  case IR_BAND: k1 &= k2; break;
  case IR_BOR: k1 |= k2; break;
  case IR_BXOR: k1 ^= k2; break;
  case IR_BSHL: k1 <<= (k2 & 63); break;
  case IR_BSHR: k1 = (int32_t)((uint32_t)k1 >> (k2 & 63)); break;
  case IR_BSAR: k1 >>= (k2 & 63); break;
  case IR_BROL: k1 = (int32_t)lj_rol((uint32_t)k1, (k2 & 63)); break;
  case IR_BROR: k1 = (int32_t)lj_ror((uint32_t)k1, (k2 & 63)); break;
  default: lj_assertJ(0, "bad IR op %d", op); break;
  }
#else
  UNUSED(k2); UNUSED(op);
  lj_assertJ(0, "FFI IR op without FFI");
#endif
  return k1;
}

LJFOLD(ADD KINT64 KINT64)
LJFOLD(SUB KINT64 KINT64)
LJFOLD(MUL KINT64 KINT64)
LJFOLD(BAND KINT64 KINT64)
LJFOLD(BOR KINT64 KINT64)
LJFOLD(BXOR KINT64 KINT64)
LJFOLDF(kfold_int64arith)
{
  return INT64FOLD(kfold_int64arith(J, ir_k64(fleft)->u64,
				    ir_k64(fright)->u64, (IROp)fins->o));
}

LJFOLD(DIV KINT64 KINT64)
LJFOLD(MOD KINT64 KINT64)
LJFOLD(POW KINT64 KINT64)
LJFOLDF(kfold_int64arith2)
{
#if LJ_HASFFI
  uint64_t k1 = ir_k64(fleft)->u64, k2 = ir_k64(fright)->u64;
  if (irt_isi64(fins->t)) {
    k1 = fins->o == IR_DIV ? lj_carith_divi64((int64_t)k1, (int64_t)k2) :
	 fins->o == IR_MOD ? lj_carith_modi64((int64_t)k1, (int64_t)k2) :
			     lj_carith_powi64((int64_t)k1, (int64_t)k2);
  } else {
    k1 = fins->o == IR_DIV ? lj_carith_divu64(k1, k2) :
	 fins->o == IR_MOD ? lj_carith_modu64(k1, k2) :
			     lj_carith_powu64(k1, k2);
  }
  return INT64FOLD(k1);
#else
  UNUSED(J); lj_assertJ(0, "FFI IR op without FFI"); return FAILFOLD;
#endif
}

LJFOLD(BSHL KINT64 KINT)
LJFOLD(BSHR KINT64 KINT)
LJFOLD(BSAR KINT64 KINT)
LJFOLD(BROL KINT64 KINT)
LJFOLD(BROR KINT64 KINT)
LJFOLDF(kfold_int64shift)
{
#if LJ_HASFFI
  uint64_t k = ir_k64(fleft)->u64;
  int32_t sh = (fright->i & 63);
  return INT64FOLD(lj_carith_shift64(k, sh, fins->o - IR_BSHL));
#else
  UNUSED(J); lj_assertJ(0, "FFI IR op without FFI"); return FAILFOLD;
#endif
}

LJFOLD(BNOT KINT64)
LJFOLDF(kfold_bnot64)
{
#if LJ_HASFFI
  return INT64FOLD(~ir_k64(fleft)->u64);
#else
  UNUSED(J); lj_assertJ(0, "FFI IR op without FFI"); return FAILFOLD;
#endif
}

LJFOLD(BSWAP KINT64)
LJFOLDF(kfold_bswap64)
{
#if LJ_HASFFI
  return INT64FOLD(lj_bswap64(ir_k64(fleft)->u64));
#else
  UNUSED(J); lj_assertJ(0, "FFI IR op without FFI"); return FAILFOLD;
#endif
}

LJFOLD(LT KINT64 KINT64)
LJFOLD(GE KINT64 KINT64)
LJFOLD(LE KINT64 KINT64)
LJFOLD(GT KINT64 KINT64)
LJFOLD(ULT KINT64 KINT64)
LJFOLD(UGE KINT64 KINT64)
LJFOLD(ULE KINT64 KINT64)
LJFOLD(UGT KINT64 KINT64)
LJFOLDF(kfold_int64comp)
{
#if LJ_HASFFI
  uint64_t a = ir_k64(fleft)->u64, b = ir_k64(fright)->u64;
  switch ((IROp)fins->o) {
  case IR_LT: return CONDFOLD((int64_t)a < (int64_t)b);
  case IR_GE: return CONDFOLD((int64_t)a >= (int64_t)b);
  case IR_LE: return CONDFOLD((int64_t)a <= (int64_t)b);
  case IR_GT: return CONDFOLD((int64_t)a > (int64_t)b);
  case IR_ULT: return CONDFOLD(a < b);
  case IR_UGE: return CONDFOLD(a >= b);
  case IR_ULE: return CONDFOLD(a <= b);
  case IR_UGT: return CONDFOLD(a > b);
  default: lj_assertJ(0, "bad IR op %d", fins->o); return FAILFOLD;
  }
#else
  UNUSED(J); lj_assertJ(0, "FFI IR op without FFI"); return FAILFOLD;
#endif
}

LJFOLD(UGE any KINT64)
LJFOLDF(kfold_int64comp0)
{
#if LJ_HASFFI
  if (ir_k64(fright)->u64 == 0)
    return DROPFOLD;
  return NEXTFOLD;
#else
  UNUSED(J); lj_assertJ(0, "FFI IR op without FFI"); return FAILFOLD;
#endif
}

/* -- Constant folding for strings ---------------------------------------- */

LJFOLD(SNEW KKPTR KINT)
LJFOLDF(kfold_snew_kptr)
{
  GCstr *s = lj_str_new(J->L, (const char *)ir_kptr(fleft), (size_t)fright->i);
  return lj_ir_kstr(J, s);
}

LJFOLD(SNEW any KINT)
LJFOLD(XSNEW any KINT)
LJFOLDF(kfold_snew_empty)
{
  if (fright->i == 0)
    return lj_ir_kstr(J, &J2G(J)->strempty);
  return NEXTFOLD;
}

LJFOLD(STRREF KGC KINT)
LJFOLDF(kfold_strref)
{
  GCstr *str = ir_kstr(fleft);
  lj_assertJ((MSize)fright->i <= str->len, "bad string ref");
  return lj_ir_kkptr(J, (char *)strdata(str) + fright->i);
}

LJFOLD(STRREF SNEW any)
LJFOLDF(kfold_strref_snew)
{
  PHIBARRIER(fleft);
  if (irref_isk(fins->op2) && fright->i == 0) {
    return fleft->op1;  /* strref(snew(ptr, len), 0) ==> ptr */
  } else {
    /* Reassociate: strref(snew(strref(str, a), len), b) ==> strref(str, a+b) */
    IRIns *ir = IR(fleft->op1);
    if (ir->o == IR_STRREF) {
      IRRef1 str = ir->op1;  /* IRIns * is not valid across emitir. */
      PHIBARRIER(ir);
      fins->op2 = emitir(IRTI(IR_ADD), ir->op2, fins->op2); /* Clobbers fins! */
      fins->op1 = str;
      fins->ot = IRT(IR_STRREF, IRT_PGC);
      return RETRYFOLD;
    }
  }
  return NEXTFOLD;
}

LJFOLD(CALLN CARG IRCALL_lj_str_cmp)
LJFOLDF(kfold_strcmp)
{
  if (irref_isk(fleft->op1) && irref_isk(fleft->op2)) {
    GCstr *a = ir_kstr(IR(fleft->op1));
    GCstr *b = ir_kstr(IR(fleft->op2));
    return INTFOLD(lj_str_cmp(a, b));
  }
  return NEXTFOLD;
}

/* -- Constant folding and forwarding for buffers ------------------------- */

/*
** Buffer ops perform stores, but their effect is limited to the buffer
** itself. Also, buffer ops are chained: a use of an op implies a use of
** all other ops up the chain. Conversely, if an op is unused, all ops
** up the chain can go unsed. This largely eliminates the need to treat
** them as stores.
**
** Alas, treating them as normal (IRM_N) ops doesn't work, because they
** cannot be CSEd in isolation. CSE for IRM_N is implicitly done in LOOP
** or if FOLD is disabled.
**
** The compromise is to declare them as loads, emit them like stores and
** CSE whole chains manually when the BUFSTR is to be emitted. Any chain
** fragments left over from CSE are eliminated by DCE.
**
** The string buffer methods emit a USE instead of a BUFSTR to keep the
** chain alive.
*/

LJFOLD(BUFHDR any any)
LJFOLDF(bufhdr_merge)
{
  return fins->op2 == IRBUFHDR_WRITE ? CSEFOLD : EMITFOLD;
}

LJFOLD(BUFPUT any BUFSTR)
LJFOLDF(bufput_bufstr)
{
  if ((J->flags & JIT_F_OPT_FWD)) {
    IRRef hdr = fright->op2;
    /* New buffer, no other buffer op inbetween and same buffer? */
    if (fleft->o == IR_BUFHDR && fleft->op2 == IRBUFHDR_RESET &&
	fleft->prev == hdr &&
	fleft->op1 == IR(hdr)->op1 &&
	!(irt_isphi(fright->t) && IR(hdr)->prev) &&
	(!LJ_HASBUFFER || J->chain[IR_CALLA] < hdr)) {
      IRRef ref = fins->op1;
      IR(ref)->op2 = IRBUFHDR_APPEND;  /* Modify BUFHDR. */
      IR(ref)->op1 = fright->op1;
      return ref;
    }
    /* Replay puts to global temporary buffer. */
    if (IR(hdr)->op2 == IRBUFHDR_RESET && !irt_isphi(fright->t)) {
      IRIns *ir = IR(fright->op1);
      /* For now only handle single string.reverse .lower .upper .rep. */
      if (ir->o == IR_CALLL &&
	  ir->op2 >= IRCALL_lj_buf_putstr_reverse &&
	  ir->op2 <= IRCALL_lj_buf_putstr_rep) {
	IRIns *carg1 = IR(ir->op1);
	if (ir->op2 == IRCALL_lj_buf_putstr_rep) {
	  IRIns *carg2 = IR(carg1->op1);
	  if (carg2->op1 == hdr) {
	    return lj_ir_call(J, ir->op2, fins->op1, carg2->op2, carg1->op2);
	  }
	} else if (carg1->op1 == hdr) {
	  return lj_ir_call(J, ir->op2, fins->op1, carg1->op2);
	}
      }
    }
  }
  return EMITFOLD;  /* Always emit, CSE later. */
}

LJFOLD(BUFPUT any any)
LJFOLDF(bufput_kgc)
{
  if (LJ_LIKELY(J->flags & JIT_F_OPT_FOLD) && fright->o == IR_KGC) {
    GCstr *s2 = ir_kstr(fright);
    if (s2->len == 0) {  /* Empty string? */
      return LEFTFOLD;
    } else {
      if (fleft->o == IR_BUFPUT && irref_isk(fleft->op2) &&
	  !irt_isphi(fleft->t)) {  /* Join two constant string puts in a row. */
	GCstr *s1 = ir_kstr(IR(fleft->op2));
	IRRef kref = lj_ir_kstr(J, lj_buf_cat2str(J->L, s1, s2));
	/* lj_ir_kstr() may realloc the IR and invalidates any IRIns *. */
	IR(fins->op1)->op2 = kref;  /* Modify previous BUFPUT. */
	return fins->op1;
      }
    }
  }
  return EMITFOLD;  /* Always emit, CSE later. */
}

LJFOLD(BUFSTR any any)
LJFOLDF(bufstr_kfold_cse)
{
  lj_assertJ(fleft->o == IR_BUFHDR || fleft->o == IR_BUFPUT ||
	     fleft->o == IR_CALLL,
	     "bad buffer constructor IR op %d", fleft->o);
  if (LJ_LIKELY(J->flags & JIT_F_OPT_FOLD)) {
    if (fleft->o == IR_BUFHDR) {  /* No put operations? */
      if (fleft->op2 == IRBUFHDR_RESET)  /* Empty buffer? */