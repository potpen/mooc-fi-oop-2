
/*
** Trace recorder (bytecode -> SSA IR).
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

#define lj_record_c
#define LUA_CORE

#include "lj_obj.h"

#if LJ_HASJIT

#include "lj_err.h"
#include "lj_str.h"
#include "lj_tab.h"
#include "lj_meta.h"
#include "lj_frame.h"
#if LJ_HASFFI
#include "lj_ctype.h"
#endif
#include "lj_bc.h"
#include "lj_ff.h"
#if LJ_HASPROFILE
#include "lj_debug.h"
#endif
#include "lj_ir.h"
#include "lj_jit.h"
#include "lj_ircall.h"
#include "lj_iropt.h"
#include "lj_trace.h"
#include "lj_record.h"
#include "lj_ffrecord.h"
#include "lj_snap.h"
#include "lj_dispatch.h"
#include "lj_vm.h"
#include "lj_prng.h"

/* Some local macros to save typing. Undef'd at the end. */
#define IR(ref)			(&J->cur.ir[(ref)])

/* Pass IR on to next optimization in chain (FOLD). */
#define emitir(ot, a, b)	(lj_ir_set(J, (ot), (a), (b)), lj_opt_fold(J))

/* Emit raw IR without passing through optimizations. */
#define emitir_raw(ot, a, b)	(lj_ir_set(J, (ot), (a), (b)), lj_ir_emit(J))

/* -- Sanity checks ------------------------------------------------------- */

#ifdef LUA_USE_ASSERT
/* Sanity check the whole IR -- sloooow. */
static void rec_check_ir(jit_State *J)
{
  IRRef i, nins = J->cur.nins, nk = J->cur.nk;
  lj_assertJ(nk <= REF_BIAS && nins >= REF_BIAS && nins < 65536,
	     "inconsistent IR layout");
  for (i = nk; i < nins; i++) {
    IRIns *ir = IR(i);
    uint32_t mode = lj_ir_mode[ir->o];
    IRRef op1 = ir->op1;
    IRRef op2 = ir->op2;
    const char *err = NULL;
    switch (irm_op1(mode)) {
    case IRMnone:
      if (op1 != 0) err = "IRMnone op1 used";
      break;
    case IRMref:
      if (op1 < nk || (i >= REF_BIAS ? op1 >= i : op1 <= i))
	err = "IRMref op1 out of range";
      break;
    case IRMlit: break;
    case IRMcst:
      if (i >= REF_BIAS) { err = "constant in IR range"; break; }
      if (irt_is64(ir->t) && ir->o != IR_KNULL)
	i++;
      continue;
    }
    switch (irm_op2(mode)) {
    case IRMnone:
      if (op2) err = "IRMnone op2 used";
      break;
    case IRMref:
      if (op2 < nk || (i >= REF_BIAS ? op2 >= i : op2 <= i))
	err = "IRMref op2 out of range";
      break;
    case IRMlit: break;
    case IRMcst: err = "IRMcst op2"; break;
    }
    if (!err && ir->prev) {
      if (ir->prev < nk || (i >= REF_BIAS ? ir->prev >= i : ir->prev <= i))
	err = "chain out of range";
      else if (ir->o != IR_NOP && IR(ir->prev)->o != ir->o)
	err = "chain to different op";
    }
    lj_assertJ(!err, "bad IR %04d op %d(%04d,%04d): %s",
	       i-REF_BIAS,
	       ir->o,
	       irm_op1(mode) == IRMref ? op1-REF_BIAS : op1,
	       irm_op2(mode) == IRMref ? op2-REF_BIAS : op2,
	       err);
  }
}

/* Compare stack slots and frames of the recorder and the VM. */
static void rec_check_slots(jit_State *J)
{
  BCReg s, nslots = J->baseslot + J->maxslot;
  int32_t depth = 0;
  cTValue *base = J->L->base - J->baseslot;
  lj_assertJ(J->baseslot >= 1+LJ_FR2, "bad baseslot");
  lj_assertJ(J->baseslot == 1+LJ_FR2 || (J->slot[J->baseslot-1] & TREF_FRAME),
	     "baseslot does not point to frame");
  lj_assertJ(nslots <= LJ_MAX_JSLOTS, "slot overflow");
  for (s = 0; s < nslots; s++) {
    TRef tr = J->slot[s];
    if (tr) {
      cTValue *tv = &base[s];
      IRRef ref = tref_ref(tr);
      IRIns *ir = NULL;  /* Silence compiler. */
      if (!LJ_FR2 || ref || !(tr & (TREF_FRAME | TREF_CONT))) {
	lj_assertJ(ref >= J->cur.nk && ref < J->cur.nins,
		   "slot %d ref %04d out of range", s, ref - REF_BIAS);
	ir = IR(ref);
	lj_assertJ(irt_t(ir->t) == tref_t(tr), "slot %d IR type mismatch", s);
      }
      if (s == 0) {
	lj_assertJ(tref_isfunc(tr), "frame slot 0 is not a function");
#if LJ_FR2
      } else if (s == 1) {
	lj_assertJ((tr & ~TREF_FRAME) == 0, "bad frame slot 1");
#endif
      } else if ((tr & TREF_FRAME)) {
	GCfunc *fn = gco2func(frame_gc(tv));
	BCReg delta = (BCReg)(tv - frame_prev(tv));
#if LJ_FR2
	lj_assertJ(!ref || ir_knum(ir)->u64 == tv->u64,
		   "frame slot %d PC mismatch", s);
	tr = J->slot[s-1];
	ir = IR(tref_ref(tr));
#endif
	lj_assertJ(tref_isfunc(tr),
		   "frame slot %d is not a function", s-LJ_FR2);
	lj_assertJ(!tref_isk(tr) || fn == ir_kfunc(ir),
		   "frame slot %d function mismatch", s-LJ_FR2);
	lj_assertJ(s > delta + LJ_FR2 ? (J->slot[s-delta] & TREF_FRAME)
				      : (s == delta + LJ_FR2),
		   "frame slot %d broken chain", s-LJ_FR2);
	depth++;
      } else if ((tr & TREF_CONT)) {
#if LJ_FR2
	lj_assertJ(!ref || ir_knum(ir)->u64 == tv->u64,
		   "cont slot %d continuation mismatch", s);
#else
	lj_assertJ(ir_kptr(ir) == gcrefp(tv->gcr, void),
		   "cont slot %d continuation mismatch", s);
#endif
	lj_assertJ((J->slot[s+1+LJ_FR2] & TREF_FRAME),
		   "cont slot %d not followed by frame", s);
	depth++;
      } else if ((tr & TREF_KEYINDEX)) {
	lj_assertJ(tref_isint(tr), "keyindex slot %d bad type %d",
				   s, tref_type(tr));
      } else {
	/* Number repr. may differ, but other types must be the same. */
	lj_assertJ(tvisnumber(tv) ? tref_isnumber(tr) :
				    itype2irt(tv) == tref_type(tr),
		   "slot %d type mismatch: stack type %d vs IR type %d",
		   s, itypemap(tv), tref_type(tr));
	if (tref_isk(tr)) {  /* Compare constants. */
	  TValue tvk;
	  lj_ir_kvalue(J->L, &tvk, ir);
	  lj_assertJ((tvisnum(&tvk) && tvisnan(&tvk)) ?
		     (tvisnum(tv) && tvisnan(tv)) :
		     lj_obj_equal(tv, &tvk),
		     "slot %d const mismatch: stack %016llx vs IR %016llx",
		     s, tv->u64, tvk.u64);
	}
      }
    }
  }
  lj_assertJ(J->framedepth == depth,
	     "frame depth mismatch %d vs %d", J->framedepth, depth);
}
#endif

/* -- Type handling and specialization ------------------------------------ */

/* Note: these functions return tagged references (TRef). */

/* Specialize a slot to a specific type. Note: slot can be negative! */
static TRef sloadt(jit_State *J, int32_t slot, IRType t, int mode)
{
  /* Caller may set IRT_GUARD in t. */
  TRef ref = emitir_raw(IRT(IR_SLOAD, t), (int32_t)J->baseslot+slot, mode);
  J->base[slot] = ref;
  return ref;
}

/* Specialize a slot to the runtime type. Note: slot can be negative! */
static TRef sload(jit_State *J, int32_t slot)
{
  IRType t = itype2irt(&J->L->base[slot]);
  TRef ref = emitir_raw(IRTG(IR_SLOAD, t), (int32_t)J->baseslot+slot,
			IRSLOAD_TYPECHECK);
  if (irtype_ispri(t)) ref = TREF_PRI(t);  /* Canonicalize primitive refs. */
  J->base[slot] = ref;
  return ref;
}

/* Get TRef from slot. Load slot and specialize if not done already. */
#define getslot(J, s)	(J->base[(s)] ? J->base[(s)] : sload(J, (int32_t)(s)))

/* Get TRef for current function. */
static TRef getcurrf(jit_State *J)
{
  if (J->base[-1-LJ_FR2])
    return J->base[-1-LJ_FR2];
  /* Non-base frame functions ought to be loaded already. */
  lj_assertJ(J->baseslot == 1+LJ_FR2, "bad baseslot");
  return sloadt(J, -1-LJ_FR2, IRT_FUNC, IRSLOAD_READONLY);
}

/* Compare for raw object equality.
** Returns 0 if the objects are the same.
** Returns 1 if they are different, but the same type.
** Returns 2 for two different types.
** Comparisons between primitives always return 1 -- no caller cares about it.
*/
int lj_record_objcmp(jit_State *J, TRef a, TRef b, cTValue *av, cTValue *bv)
{
  int diff = !lj_obj_equal(av, bv);
  if (!tref_isk2(a, b)) {  /* Shortcut, also handles primitives. */
    IRType ta = tref_isinteger(a) ? IRT_INT : tref_type(a);
    IRType tb = tref_isinteger(b) ? IRT_INT : tref_type(b);
    if (ta != tb) {
      /* Widen mixed number/int comparisons to number/number comparison. */
      if (ta == IRT_INT && tb == IRT_NUM) {
	a = emitir(IRTN(IR_CONV), a, IRCONV_NUM_INT);
	ta = IRT_NUM;
      } else if (ta == IRT_NUM && tb == IRT_INT) {
	b = emitir(IRTN(IR_CONV), b, IRCONV_NUM_INT);
      } else {
	return 2;  /* Two different types are never equal. */
      }
    }
    emitir(IRTG(diff ? IR_NE : IR_EQ, ta), a, b);
  }
  return diff;
}

/* Constify a value. Returns 0 for non-representable object types. */
TRef lj_record_constify(jit_State *J, cTValue *o)
{
  if (tvisgcv(o))
    return lj_ir_kgc(J, gcV(o), itype2irt(o));
  else if (tvisint(o))
    return lj_ir_kint(J, intV(o));
  else if (tvisnum(o))
    return lj_ir_knumint(J, numV(o));
  else if (tvisbool(o))
    return TREF_PRI(itype2irt(o));
  else
    return 0;  /* Can't represent lightuserdata (pointless). */
}

/* Emit a VLOAD with the correct type. */
TRef lj_record_vload(jit_State *J, TRef ref, MSize idx, IRType t)
{
  TRef tr = emitir(IRTG(IR_VLOAD, t), ref, idx);
  if (irtype_ispri(t)) tr = TREF_PRI(t);  /* Canonicalize primitives. */
  return tr;
}

/* -- Record loop ops ----------------------------------------------------- */

/* Loop event. */
typedef enum {
  LOOPEV_LEAVE,		/* Loop is left or not entered. */
  LOOPEV_ENTERLO,	/* Loop is entered with a low iteration count left. */
  LOOPEV_ENTER		/* Loop is entered. */
} LoopEvent;

/* Canonicalize slots: convert integers to numbers. */
static void canonicalize_slots(jit_State *J)
{
  BCReg s;
  if (LJ_DUALNUM) return;
  for (s = J->baseslot+J->maxslot-1; s >= 1; s--) {
    TRef tr = J->slot[s];
    if (tref_isinteger(tr) && !(tr & TREF_KEYINDEX)) {
      IRIns *ir = IR(tref_ref(tr));
      if (!(ir->o == IR_SLOAD && (ir->op2 & (IRSLOAD_READONLY))))
	J->slot[s] = emitir(IRTN(IR_CONV), tr, IRCONV_NUM_INT);
    }
  }
}

/* Stop recording. */
void lj_record_stop(jit_State *J, TraceLink linktype, TraceNo lnk)
{
#ifdef LUAJIT_ENABLE_TABLE_BUMP
  if (J->retryrec)
    lj_trace_err(J, LJ_TRERR_RETRY);
#endif
  lj_trace_end(J);
  J->cur.linktype = (uint8_t)linktype;
  J->cur.link = (uint16_t)lnk;
  /* Looping back at the same stack level? */
  if (lnk == J->cur.traceno && J->framedepth + J->retdepth == 0) {
    if ((J->flags & JIT_F_OPT_LOOP))  /* Shall we try to create a loop? */
      goto nocanon;  /* Do not canonicalize or we lose the narrowing. */
    if (J->cur.root)  /* Otherwise ensure we always link to the root trace. */
      J->cur.link = J->cur.root;
  }
  canonicalize_slots(J);
nocanon:
  /* Note: all loop ops must set J->pc to the following instruction! */
  lj_snap_add(J);  /* Add loop snapshot. */
  J->needsnap = 0;
  J->mergesnap = 1;  /* In case recording continues. */
}

/* Search bytecode backwards for a int/num constant slot initializer. */
static TRef find_kinit(jit_State *J, const BCIns *endpc, BCReg slot, IRType t)
{
  /* This algorithm is rather simplistic and assumes quite a bit about
  ** how the bytecode is generated. It works fine for FORI initializers,
  ** but it won't necessarily work in other cases (e.g. iterator arguments).
  ** It doesn't do anything fancy, either (like backpropagating MOVs).
  */
  const BCIns *pc, *startpc = proto_bc(J->pt);
  for (pc = endpc-1; pc > startpc; pc--) {
    BCIns ins = *pc;
    BCOp op = bc_op(ins);
    /* First try to find the last instruction that stores to this slot. */
    if (bcmode_a(op) == BCMbase && bc_a(ins) <= slot) {
      return 0;  /* Multiple results, e.g. from a CALL or KNIL. */
    } else if (bcmode_a(op) == BCMdst && bc_a(ins) == slot) {
      if (op == BC_KSHORT || op == BC_KNUM) {  /* Found const. initializer. */
	/* Now try to verify there's no forward jump across it. */
	const BCIns *kpc = pc;
	for (; pc > startpc; pc--)
	  if (bc_op(*pc) == BC_JMP) {
	    const BCIns *target = pc+bc_j(*pc)+1;
	    if (target > kpc && target <= endpc)
	      return 0;  /* Conditional assignment. */
	  }
	if (op == BC_KSHORT) {
	  int32_t k = (int32_t)(int16_t)bc_d(ins);
	  return t == IRT_INT ? lj_ir_kint(J, k) : lj_ir_knum(J, (lua_Number)k);
	} else {
	  cTValue *tv = proto_knumtv(J->pt, bc_d(ins));
	  if (t == IRT_INT) {
	    int32_t k = numberVint(tv);
	    if (tvisint(tv) || numV(tv) == (lua_Number)k)  /* -0 is ok here. */
	      return lj_ir_kint(J, k);
	    return 0;  /* Type mismatch. */
	  } else {
	    return lj_ir_knum(J, numberVnum(tv));
	  }
	}
      }
      return 0;  /* Non-constant initializer. */
    }
  }
  return 0;  /* No assignment to this slot found? */
}

/* Load and optionally convert a FORI argument from a slot. */
static TRef fori_load(jit_State *J, BCReg slot, IRType t, int mode)
{
  int conv = (tvisint(&J->L->base[slot]) != (t==IRT_INT)) ? IRSLOAD_CONVERT : 0;
  return sloadt(J, (int32_t)slot,
		t + (((mode & IRSLOAD_TYPECHECK) ||
		      (conv && t == IRT_INT && !(mode >> 16))) ?
		     IRT_GUARD : 0),
		mode + conv);
}

/* Peek before FORI to find a const initializer. Otherwise load from slot. */
static TRef fori_arg(jit_State *J, const BCIns *fori, BCReg slot,
		     IRType t, int mode)
{
  TRef tr = J->base[slot];
  if (!tr) {
    tr = find_kinit(J, fori, slot, t);
    if (!tr)
      tr = fori_load(J, slot, t, mode);
  }
  return tr;
}

/* Return the direction of the FOR loop iterator.
** It's important to exactly reproduce the semantics of the interpreter.
*/
static int rec_for_direction(cTValue *o)
{
  return (tvisint(o) ? intV(o) : (int32_t)o->u32.hi) >= 0;
}

/* Simulate the runtime behavior of the FOR loop iterator. */
static LoopEvent rec_for_iter(IROp *op, cTValue *o, int isforl)
{
  lua_Number stopv = numberVnum(&o[FORL_STOP]);
  lua_Number idxv = numberVnum(&o[FORL_IDX]);
  lua_Number stepv = numberVnum(&o[FORL_STEP]);
  if (isforl)
    idxv += stepv;
  if (rec_for_direction(&o[FORL_STEP])) {
    if (idxv <= stopv) {
      *op = IR_LE;
      return idxv + 2*stepv > stopv ? LOOPEV_ENTERLO : LOOPEV_ENTER;
    }
    *op = IR_GT; return LOOPEV_LEAVE;
  } else {
    if (stopv <= idxv) {
      *op = IR_GE;
      return idxv + 2*stepv < stopv ? LOOPEV_ENTERLO : LOOPEV_ENTER;
    }
    *op = IR_LT; return LOOPEV_LEAVE;
  }
}

/* Record checks for FOR loop overflow and step direction. */
static void rec_for_check(jit_State *J, IRType t, int dir,
			  TRef stop, TRef step, int init)
{
  if (!tref_isk(step)) {
    /* Non-constant step: need a guard for the direction. */
    TRef zero = (t == IRT_INT) ? lj_ir_kint(J, 0) : lj_ir_knum_zero(J);
    emitir(IRTG(dir ? IR_GE : IR_LT, t), step, zero);
    /* Add hoistable overflow checks for a narrowed FORL index. */
    if (init && t == IRT_INT) {
      if (tref_isk(stop)) {
	/* Constant stop: optimize check away or to a range check for step. */
	int32_t k = IR(tref_ref(stop))->i;
	if (dir) {
	  if (k > 0)
	    emitir(IRTGI(IR_LE), step, lj_ir_kint(J, (int32_t)0x7fffffff-k));
	} else {
	  if (k < 0)
	    emitir(IRTGI(IR_GE), step, lj_ir_kint(J, (int32_t)0x80000000-k));
	}
      } else {
	/* Stop+step variable: need full overflow check. */
	TRef tr = emitir(IRTGI(IR_ADDOV), step, stop);
	emitir(IRTI(IR_USE), tr, 0);  /* ADDOV is weak. Avoid dead result. */
      }
    }
  } else if (init && t == IRT_INT && !tref_isk(stop)) {
    /* Constant step: optimize overflow check to a range check for stop. */
    int32_t k = IR(tref_ref(step))->i;
    k = (int32_t)(dir ? 0x7fffffff : 0x80000000) - k;
    emitir(IRTGI(dir ? IR_LE : IR_GE), stop, lj_ir_kint(J, k));
  }
}

/* Record a FORL instruction. */
static void rec_for_loop(jit_State *J, const BCIns *fori, ScEvEntry *scev,
			 int init)
{
  BCReg ra = bc_a(*fori);
  cTValue *tv = &J->L->base[ra];
  TRef idx = J->base[ra+FORL_IDX];
  IRType t = idx ? tref_type(idx) :
	     (init || LJ_DUALNUM) ? lj_opt_narrow_forl(J, tv) : IRT_NUM;
  int mode = IRSLOAD_INHERIT +
    ((!LJ_DUALNUM || tvisint(tv) == (t == IRT_INT)) ? IRSLOAD_READONLY : 0);
  TRef stop = fori_arg(J, fori, ra+FORL_STOP, t, mode);
  TRef step = fori_arg(J, fori, ra+FORL_STEP, t, mode);
  int tc, dir = rec_for_direction(&tv[FORL_STEP]);
  lj_assertJ(bc_op(*fori) == BC_FORI || bc_op(*fori) == BC_JFORI,
	     "bad bytecode %d instead of FORI/JFORI", bc_op(*fori));
  scev->t.irt = t;
  scev->dir = dir;
  scev->stop = tref_ref(stop);
  scev->step = tref_ref(step);
  rec_for_check(J, t, dir, stop, step, init);
  scev->start = tref_ref(find_kinit(J, fori, ra+FORL_IDX, IRT_INT));
  tc = (LJ_DUALNUM &&
	!(scev->start && irref_isk(scev->stop) && irref_isk(scev->step) &&
	  tvisint(&tv[FORL_IDX]) == (t == IRT_INT))) ?
	IRSLOAD_TYPECHECK : 0;
  if (tc) {
    J->base[ra+FORL_STOP] = stop;
    J->base[ra+FORL_STEP] = step;
  }
  if (!idx)
    idx = fori_load(J, ra+FORL_IDX, t,
		    IRSLOAD_INHERIT + tc + (J->scev.start << 16));
  if (!init)
    J->base[ra+FORL_IDX] = idx = emitir(IRT(IR_ADD, t), idx, step);
  J->base[ra+FORL_EXT] = idx;
  scev->idx = tref_ref(idx);
  setmref(scev->pc, fori);
  J->maxslot = ra+FORL_EXT+1;
}

/* Record FORL/JFORL or FORI/JFORI. */
static LoopEvent rec_for(jit_State *J, const BCIns *fori, int isforl)
{
  BCReg ra = bc_a(*fori);
  TValue *tv = &J->L->base[ra];
  TRef *tr = &J->base[ra];
  IROp op;
  LoopEvent ev;
  TRef stop;
  IRType t;
  if (isforl) {  /* Handle FORL/JFORL opcodes. */
    TRef idx = tr[FORL_IDX];
    if (mref(J->scev.pc, const BCIns) == fori && tref_ref(idx) == J->scev.idx) {
      t = J->scev.t.irt;
      stop = J->scev.stop;
      idx = emitir(IRT(IR_ADD, t), idx, J->scev.step);
      tr[FORL_EXT] = tr[FORL_IDX] = idx;
    } else {
      ScEvEntry scev;
      rec_for_loop(J, fori, &scev, 0);
      t = scev.t.irt;
      stop = scev.stop;
    }
  } else {  /* Handle FORI/JFORI opcodes. */
    BCReg i;
    lj_meta_for(J->L, tv);
    t = (LJ_DUALNUM || tref_isint(tr[FORL_IDX])) ? lj_opt_narrow_forl(J, tv) :
						   IRT_NUM;
    for (i = FORL_IDX; i <= FORL_STEP; i++) {
      if (!tr[i]) sload(J, ra+i);
      lj_assertJ(tref_isnumber_str(tr[i]), "bad FORI argument type");
      if (tref_isstr(tr[i]))
	tr[i] = emitir(IRTG(IR_STRTO, IRT_NUM), tr[i], 0);
      if (t == IRT_INT) {
	if (!tref_isinteger(tr[i]))
	  tr[i] = emitir(IRTGI(IR_CONV), tr[i], IRCONV_INT_NUM|IRCONV_CHECK);
      } else {
	if (!tref_isnum(tr[i]))
	  tr[i] = emitir(IRTN(IR_CONV), tr[i], IRCONV_NUM_INT);
      }
    }
    tr[FORL_EXT] = tr[FORL_IDX];
    stop = tr[FORL_STOP];
    rec_for_check(J, t, rec_for_direction(&tv[FORL_STEP]),
		  stop, tr[FORL_STEP], 1);
  }

  ev = rec_for_iter(&op, tv, isforl);
  if (ev == LOOPEV_LEAVE) {
    J->maxslot = ra+FORL_EXT+1;
    J->pc = fori+1;
  } else {
    J->maxslot = ra;
    J->pc = fori+bc_j(*fori)+1;
  }
  lj_snap_add(J);

  emitir(IRTG(op, t), tr[FORL_IDX], stop);

  if (ev == LOOPEV_LEAVE) {
    J->maxslot = ra;
    J->pc = fori+bc_j(*fori)+1;
  } else {
    J->maxslot = ra+FORL_EXT+1;
    J->pc = fori+1;
  }
  J->needsnap = 1;
  return ev;
}

/* Record ITERL/JITERL. */
static LoopEvent rec_iterl(jit_State *J, const BCIns iterins)
{
  BCReg ra = bc_a(iterins);
  if (!tref_isnil(getslot(J, ra))) {  /* Looping back? */
    J->base[ra-1] = J->base[ra];  /* Copy result of ITERC to control var. */
    J->maxslot = ra-1+bc_b(J->pc[-1]);
    J->pc += bc_j(iterins)+1;
    return LOOPEV_ENTER;
  } else {
    J->maxslot = ra-3;
    J->pc++;
    return LOOPEV_LEAVE;
  }
}

/* Record LOOP/JLOOP. Now, that was easy. */
static LoopEvent rec_loop(jit_State *J, BCReg ra, int skip)
{
  if (ra < J->maxslot) J->maxslot = ra;
  J->pc += skip;
  return LOOPEV_ENTER;
}

/* Check if a loop repeatedly failed to trace because it didn't loop back. */
static int innerloopleft(jit_State *J, const BCIns *pc)
{
  ptrdiff_t i;
  for (i = 0; i < PENALTY_SLOTS; i++)
    if (mref(J->penalty[i].pc, const BCIns) == pc) {
      if ((J->penalty[i].reason == LJ_TRERR_LLEAVE ||
	   J->penalty[i].reason == LJ_TRERR_LINNER) &&
	  J->penalty[i].val >= 2*PENALTY_MIN)
	return 1;
      break;
    }
  return 0;
}

/* Handle the case when an interpreted loop op is hit. */
static void rec_loop_interp(jit_State *J, const BCIns *pc, LoopEvent ev)
{
  if (J->parent == 0 && J->exitno == 0) {
    if (pc == J->startpc && J->framedepth + J->retdepth == 0) {
      if (bc_op(J->cur.startins) == BC_ITERN) return;  /* See rec_itern(). */
      /* Same loop? */
      if (ev == LOOPEV_LEAVE)  /* Must loop back to form a root trace. */
	lj_trace_err(J, LJ_TRERR_LLEAVE);
      lj_record_stop(J, LJ_TRLINK_LOOP, J->cur.traceno);  /* Looping trace. */
    } else if (ev != LOOPEV_LEAVE) {  /* Entering inner loop? */
      /* It's usually better to abort here and wait until the inner loop
      ** is traced. But if the inner loop repeatedly didn't loop back,
      ** this indicates a low trip count. In this case try unrolling
      ** an inner loop even in a root trace. But it's better to be a bit
      ** more conservative here and only do it for very short loops.
      */
      if (bc_j(*pc) != -1 && !innerloopleft(J, pc))
	lj_trace_err(J, LJ_TRERR_LINNER);  /* Root trace hit an inner loop. */
      if ((ev != LOOPEV_ENTERLO &&
	   J->loopref && J->cur.nins - J->loopref > 24) || --J->loopunroll < 0)
	lj_trace_err(J, LJ_TRERR_LUNROLL);  /* Limit loop unrolling. */
      J->loopref = J->cur.nins;
    }
  } else if (ev != LOOPEV_LEAVE) {  /* Side trace enters an inner loop. */
    J->loopref = J->cur.nins;
    if (--J->loopunroll < 0)
      lj_trace_err(J, LJ_TRERR_LUNROLL);  /* Limit loop unrolling. */
  }  /* Side trace continues across a loop that's left or not entered. */
}

/* Handle the case when an already compiled loop op is hit. */
static void rec_loop_jit(jit_State *J, TraceNo lnk, LoopEvent ev)
{
  if (J->parent == 0 && J->exitno == 0) {  /* Root trace hit an inner loop. */
    /* Better let the inner loop spawn a side trace back here. */
    lj_trace_err(J, LJ_TRERR_LINNER);
  } else if (ev != LOOPEV_LEAVE) {  /* Side trace enters a compiled loop. */
    J->instunroll = 0;  /* Cannot continue across a compiled loop op. */
    if (J->pc == J->startpc && J->framedepth + J->retdepth == 0)
      lj_record_stop(J, LJ_TRLINK_LOOP, J->cur.traceno);  /* Form extra loop. */
    else
      lj_record_stop(J, LJ_TRLINK_ROOT, lnk);  /* Link to the loop. */
  }  /* Side trace continues across a loop that's left or not entered. */
}

/* Record ITERN. */
static LoopEvent rec_itern(jit_State *J, BCReg ra, BCReg rb)
{
#if LJ_BE
  /* YAGNI: Disabled on big-endian due to issues with lj_vm_next,
  ** IR_HIOP, RID_RETLO/RID_RETHI and ra_destpair.
  */
  UNUSED(ra); UNUSED(rb);
  setintV(&J->errinfo, (int32_t)BC_ITERN);
  lj_trace_err_info(J, LJ_TRERR_NYIBC);
#else
  RecordIndex ix;
  /* Since ITERN is recorded at the start, we need our own loop detection. */
  if (J->pc == J->startpc &&
      J->framedepth + J->retdepth == 0 && J->parent == 0 && J->exitno == 0) {
    IRRef ref = REF_FIRST + LJ_HASPROFILE;
#ifdef LUAJIT_ENABLE_CHECKHOOK
    ref += 3;
#endif
    if (J->cur.nins > ref ||
       (LJ_HASPROFILE && J->cur.nins == ref && J->cur.ir[ref-1].o != IR_PROF)) {
      J->instunroll = 0;  /* Cannot continue unrolling across an ITERN. */
      lj_record_stop(J, LJ_TRLINK_LOOP, J->cur.traceno);  /* Looping trace. */
      return LOOPEV_ENTER;
    }
  }
  J->maxslot = ra;
  lj_snap_add(J);  /* Required to make JLOOP the first ins in a side-trace. */
  ix.tab = getslot(J, ra-2);
  ix.key = J->base[ra-1] ? J->base[ra-1] :
	   sloadt(J, (int32_t)(ra-1), IRT_GUARD|IRT_INT,
		  IRSLOAD_TYPECHECK|IRSLOAD_KEYINDEX);
  copyTV(J->L, &ix.tabv, &J->L->base[ra-2]);
  copyTV(J->L, &ix.keyv, &J->L->base[ra-1]);
  ix.idxchain = (rb < 3);  /* Omit value type check, if unused. */
  ix.mobj = 1;  /* We need the next index, too. */
  J->maxslot = ra + lj_record_next(J, &ix);
  J->needsnap = 1;
  if (!tref_isnil(ix.key)) {  /* Looping back? */
    J->base[ra-1] = ix.mobj | TREF_KEYINDEX;  /* Control var has next index. */
    J->base[ra] = ix.key;
    J->base[ra+1] = ix.val;
    J->pc += bc_j(J->pc[1])+2;
    return LOOPEV_ENTER;
  } else {
    J->maxslot = ra-3;
    J->pc += 2;
    return LOOPEV_LEAVE;
  }
#endif
}

/* Record ISNEXT. */
static void rec_isnext(jit_State *J, BCReg ra)
{
  cTValue *b = &J->L->base[ra-3];
  if (tvisfunc(b) && funcV(b)->c.ffid == FF_next &&
      tvistab(b+1) && tvisnil(b+2)) {
    /* These checks are folded away for a compiled pairs(). */
    TRef func = getslot(J, ra-3);
    TRef trid = emitir(IRT(IR_FLOAD, IRT_U8), func, IRFL_FUNC_FFID);
    emitir(IRTGI(IR_EQ), trid, lj_ir_kint(J, FF_next));
    (void)getslot(J, ra-2); /* Type check for table. */
    (void)getslot(J, ra-1); /* Type check for nil key. */
    J->base[ra-1] = lj_ir_kint(J, 0) | TREF_KEYINDEX;
    J->maxslot = ra;
  } else {  /* Abort trace. Interpreter will despecialize bytecode. */
    lj_trace_err(J, LJ_TRERR_RECERR);
  }
}

/* -- Record profiler hook checks ----------------------------------------- */

#if LJ_HASPROFILE

/* Need to insert profiler hook check? */
static int rec_profile_need(jit_State *J, GCproto *pt, const BCIns *pc)
{
  GCproto *ppt;
  lj_assertJ(J->prof_mode == 'f' || J->prof_mode == 'l',
	     "bad profiler mode %c", J->prof_mode);
  if (!pt)
    return 0;
  ppt = J->prev_pt;
  J->prev_pt = pt;
  if (pt != ppt && ppt) {
    J->prev_line = -1;
    return 1;
  }
  if (J->prof_mode == 'l') {
    BCLine line = lj_debug_line(pt, proto_bcpos(pt, pc));
    BCLine pline = J->prev_line;
    J->prev_line = line;
    if (pline != line)
      return 1;
  }
  return 0;
}

static void rec_profile_ins(jit_State *J, const BCIns *pc)
{
  if (J->prof_mode && rec_profile_need(J, J->pt, pc)) {
    emitir(IRTG(IR_PROF, IRT_NIL), 0, 0);
    lj_snap_add(J);
  }
}

static void rec_profile_ret(jit_State *J)
{
  if (J->prof_mode == 'f') {
    emitir(IRTG(IR_PROF, IRT_NIL), 0, 0);
    J->prev_pt = NULL;
    lj_snap_add(J);
  }
}

#endif

/* -- Record calls and returns -------------------------------------------- */

/* Specialize to the runtime value of the called function or its prototype. */
static TRef rec_call_specialize(jit_State *J, GCfunc *fn, TRef tr)
{
  TRef kfunc;
  if (isluafunc(fn)) {
    GCproto *pt = funcproto(fn);
    /* Too many closures created? Probably not a monomorphic function. */
    if (pt->flags >= PROTO_CLC_POLY) {  /* Specialize to prototype instead. */
      TRef trpt = emitir(IRT(IR_FLOAD, IRT_PGC), tr, IRFL_FUNC_PC);
      emitir(IRTG(IR_EQ, IRT_PGC), trpt, lj_ir_kptr(J, proto_bc(pt)));
      (void)lj_ir_kgc(J, obj2gco(pt), IRT_PROTO);  /* Prevent GC of proto. */
      return tr;
    }
  } else {
    /* Don't specialize to non-monomorphic builtins. */
    switch (fn->c.ffid) {
    case FF_coroutine_wrap_aux:
    case FF_string_gmatch_aux:
      /* NYI: io_file_iter doesn't have an ffid, yet. */
      {  /* Specialize to the ffid. */
	TRef trid = emitir(IRT(IR_FLOAD, IRT_U8), tr, IRFL_FUNC_FFID);
	emitir(IRTGI(IR_EQ), trid, lj_ir_kint(J, fn->c.ffid));
      }
      return tr;
    default:
      /* NYI: don't specialize to non-monomorphic C functions. */
      break;
    }
  }
  /* Otherwise specialize to the function (closure) value itself. */
  kfunc = lj_ir_kfunc(J, fn);
  emitir(IRTG(IR_EQ, IRT_FUNC), tr, kfunc);
  return kfunc;
}

/* Record call setup. */
static void rec_call_setup(jit_State *J, BCReg func, ptrdiff_t nargs)
{
  RecordIndex ix;
  TValue *functv = &J->L->base[func];
  TRef kfunc, *fbase = &J->base[func];
  ptrdiff_t i;
  (void)getslot(J, func); /* Ensure func has a reference. */
  for (i = 1; i <= nargs; i++)
    (void)getslot(J, func+LJ_FR2+i);  /* Ensure all args have a reference. */
  if (!tref_isfunc(fbase[0])) {  /* Resolve __call metamethod. */
    ix.tab = fbase[0];
    copyTV(J->L, &ix.tabv, functv);
    if (!lj_record_mm_lookup(J, &ix, MM_call) || !tref_isfunc(ix.mobj))
      lj_trace_err(J, LJ_TRERR_NOMM);
    for (i = ++nargs; i > LJ_FR2; i--)  /* Shift arguments up. */
      fbase[i+LJ_FR2] = fbase[i+LJ_FR2-1];
#if LJ_FR2
    fbase[2] = fbase[0];
#endif
    fbase[0] = ix.mobj;  /* Replace function. */
    functv = &ix.mobjv;
  }
  kfunc = rec_call_specialize(J, funcV(functv), fbase[0]);
#if LJ_FR2
  fbase[0] = kfunc;
  fbase[1] = TREF_FRAME;
#else
  fbase[0] = kfunc | TREF_FRAME;
#endif
  J->maxslot = (BCReg)nargs;
}

/* Record call. */
void lj_record_call(jit_State *J, BCReg func, ptrdiff_t nargs)
{
  rec_call_setup(J, func, nargs);
  /* Bump frame. */
  J->framedepth++;
  J->base += func+1+LJ_FR2;
  J->baseslot += func+1+LJ_FR2;
  if (J->baseslot + J->maxslot >= LJ_MAX_JSLOTS)
    lj_trace_err(J, LJ_TRERR_STACKOV);
}

/* Record tail call. */
void lj_record_tailcall(jit_State *J, BCReg func, ptrdiff_t nargs)
{
  rec_call_setup(J, func, nargs);
  if (frame_isvarg(J->L->base - 1)) {
    BCReg cbase = (BCReg)frame_delta(J->L->base - 1);
    if (--J->framedepth < 0)
      lj_trace_err(J, LJ_TRERR_NYIRETL);
    J->baseslot -= (BCReg)cbase;
    J->base -= cbase;
    func += cbase;
  }
  /* Move func + args down. */
  if (LJ_FR2 && J->baseslot == 2)
    J->base[func+1] = TREF_FRAME;
  memmove(&J->base[-1-LJ_FR2], &J->base[func], sizeof(TRef)*(J->maxslot+1+LJ_FR2));
  /* Note: the new TREF_FRAME is now at J->base[-1] (even for slot #0). */
  /* Tailcalls can form a loop, so count towards the loop unroll limit. */
  if (++J->tailcalled > J->loopunroll)
    lj_trace_err(J, LJ_TRERR_LUNROLL);
}

/* Check unroll limits for down-recursion. */
static int check_downrec_unroll(jit_State *J, GCproto *pt)
{
  IRRef ptref;
  for (ptref = J->chain[IR_KGC]; ptref; ptref = IR(ptref)->prev)
    if (ir_kgc(IR(ptref)) == obj2gco(pt)) {
      int count = 0;
      IRRef ref;
      for (ref = J->chain[IR_RETF]; ref; ref = IR(ref)->prev)
	if (IR(ref)->op1 == ptref)
	  count++;
      if (count) {
	if (J->pc == J->startpc) {
	  if (count + J->tailcalled > J->param[JIT_P_recunroll])
	    return 1;
	} else {
	  lj_trace_err(J, LJ_TRERR_DOWNREC);
	}
      }
    }
  return 0;
}

static TRef rec_cat(jit_State *J, BCReg baseslot, BCReg topslot);

/* Record return. */
void lj_record_ret(jit_State *J, BCReg rbase, ptrdiff_t gotresults)
{
  TValue *frame = J->L->base - 1;
  ptrdiff_t i;
  for (i = 0; i < gotresults; i++)
    (void)getslot(J, rbase+i);  /* Ensure all results have a reference. */
  while (frame_ispcall(frame)) {  /* Immediately resolve pcall() returns. */
    BCReg cbase = (BCReg)frame_delta(frame);
    if (--J->framedepth <= 0)
      lj_trace_err(J, LJ_TRERR_NYIRETL);
    lj_assertJ(J->baseslot > 1+LJ_FR2, "bad baseslot for return");
    gotresults++;
    rbase += cbase;
    J->baseslot -= (BCReg)cbase;
    J->base -= cbase;
    J->base[--rbase] = TREF_TRUE;  /* Prepend true to results. */
    frame = frame_prevd(frame);
    J->needsnap = 1;  /* Stop catching on-trace errors. */
  }
  /* Return to lower frame via interpreter for unhandled cases. */
  if (J->framedepth == 0 && J->pt && bc_isret(bc_op(*J->pc)) &&
       (!frame_islua(frame) ||
	(J->parent == 0 && J->exitno == 0 &&
	 !bc_isret(bc_op(J->cur.startins))))) {
    /* NYI: specialize to frame type and return directly, not via RET*. */
    for (i = 0; i < (ptrdiff_t)rbase; i++)
      J->base[i] = 0;  /* Purge dead slots. */
    J->maxslot = rbase + (BCReg)gotresults;
    lj_record_stop(J, LJ_TRLINK_RETURN, 0);  /* Return to interpreter. */
    return;
  }
  if (frame_isvarg(frame)) {
    BCReg cbase = (BCReg)frame_delta(frame);
    if (--J->framedepth < 0)  /* NYI: return of vararg func to lower frame. */
      lj_trace_err(J, LJ_TRERR_NYIRETL);
    lj_assertJ(J->baseslot > 1+LJ_FR2, "bad baseslot for return");
    rbase += cbase;
    J->baseslot -= (BCReg)cbase;
    J->base -= cbase;
    frame = frame_prevd(frame);
  }
  if (frame_islua(frame)) {  /* Return to Lua frame. */
    BCIns callins = *(frame_pc(frame)-1);
    ptrdiff_t nresults = bc_b(callins) ? (ptrdiff_t)bc_b(callins)-1 :gotresults;
    BCReg cbase = bc_a(callins);
    GCproto *pt = funcproto(frame_func(frame - (cbase+1+LJ_FR2)));
    if ((pt->flags & PROTO_NOJIT))