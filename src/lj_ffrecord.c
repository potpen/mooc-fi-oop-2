
/*
** Fast function call recorder.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

#define lj_ffrecord_c
#define LUA_CORE

#include "lj_obj.h"

#if LJ_HASJIT

#include "lj_err.h"
#include "lj_buf.h"
#include "lj_str.h"
#include "lj_tab.h"
#include "lj_frame.h"
#include "lj_bc.h"
#include "lj_ff.h"
#include "lj_ir.h"
#include "lj_jit.h"
#include "lj_ircall.h"
#include "lj_iropt.h"
#include "lj_trace.h"
#include "lj_record.h"
#include "lj_ffrecord.h"
#include "lj_crecord.h"
#include "lj_dispatch.h"
#include "lj_vm.h"
#include "lj_strscan.h"
#include "lj_strfmt.h"
#include "lj_serialize.h"

/* Some local macros to save typing. Undef'd at the end. */
#define IR(ref)			(&J->cur.ir[(ref)])

/* Pass IR on to next optimization in chain (FOLD). */
#define emitir(ot, a, b)	(lj_ir_set(J, (ot), (a), (b)), lj_opt_fold(J))

/* -- Fast function recording handlers ------------------------------------ */

/* Conventions for fast function call handlers:
**
** The argument slots start at J->base[0]. All of them are guaranteed to be
** valid and type-specialized references. J->base[J->maxslot] is set to 0
** as a sentinel. The runtime argument values start at rd->argv[0].
**
** In general fast functions should check for presence of all of their
** arguments and for the correct argument types. Some simplifications
** are allowed if the interpreter throws instead. But even if recording
** is aborted, the generated IR must be consistent (no zero-refs).
**
** The number of results in rd->nres is set to 1. Handlers that return
** a different number of results need to override it. A negative value
** prevents return processing (e.g. for pending calls).
**
** Results need to be stored starting at J->base[0]. Return processing
** moves them to the right slots later.
**
** The per-ffid auxiliary data is the value of the 2nd part of the
** LJLIB_REC() annotation. This allows handling similar functionality
** in a common handler.
*/

/* Type of handler to record a fast function. */
typedef void (LJ_FASTCALL *RecordFunc)(jit_State *J, RecordFFData *rd);

/* Get runtime value of int argument. */
static int32_t argv2int(jit_State *J, TValue *o)
{
  if (!lj_strscan_numberobj(o))
    lj_trace_err(J, LJ_TRERR_BADTYPE);
  return tvisint(o) ? intV(o) : lj_num2int(numV(o));
}

/* Get runtime value of string argument. */
static GCstr *argv2str(jit_State *J, TValue *o)
{
  if (LJ_LIKELY(tvisstr(o))) {
    return strV(o);
  } else {
    GCstr *s;
    if (!tvisnumber(o))
      lj_trace_err(J, LJ_TRERR_BADTYPE);
    s = lj_strfmt_number(J->L, o);
    setstrV(J->L, o, s);
    return s;
  }
}

/* Return number of results wanted by caller. */
static ptrdiff_t results_wanted(jit_State *J)
{
  TValue *frame = J->L->base-1;
  if (frame_islua(frame))
    return (ptrdiff_t)bc_b(frame_pc(frame)[-1]) - 1;
  else
    return -1;
}

/* Trace stitching: add continuation below frame to start a new trace. */
static void recff_stitch(jit_State *J)
{
  ASMFunction cont = lj_cont_stitch;
  lua_State *L = J->L;
  TValue *base = L->base;
  BCReg nslot = J->maxslot + 1 + LJ_FR2;
  TValue *nframe = base + 1 + LJ_FR2;
  const BCIns *pc = frame_pc(base-1);
  TValue *pframe = frame_prevl(base-1);

  /* Check for this now. Throwing in lj_record_stop messes up the stack. */
  if (J->cur.nsnap >= (MSize)J->param[JIT_P_maxsnap])
    lj_trace_err(J, LJ_TRERR_SNAPOV);

  /* Move func + args up in Lua stack and insert continuation. */
  memmove(&base[1], &base[-1-LJ_FR2], sizeof(TValue)*nslot);
  setframe_ftsz(nframe, ((char *)nframe - (char *)pframe) + FRAME_CONT);
  setcont(base-LJ_FR2, cont);
  setframe_pc(base, pc);
  setnilV(base-1-LJ_FR2);  /* Incorrect, but rec_check_slots() won't run anymore. */
  L->base += 2 + LJ_FR2;
  L->top += 2 + LJ_FR2;

  /* Ditto for the IR. */
  memmove(&J->base[1], &J->base[-1-LJ_FR2], sizeof(TRef)*nslot);
#if LJ_FR2
  J->base[2] = TREF_FRAME;
  J->base[-1] = lj_ir_k64(J, IR_KNUM, u64ptr(contptr(cont)));
  J->base[0] = lj_ir_k64(J, IR_KNUM, u64ptr(pc)) | TREF_CONT;
#else
  J->base[0] = lj_ir_kptr(J, contptr(cont)) | TREF_CONT;
#endif
  J->ktrace = tref_ref((J->base[-1-LJ_FR2] = lj_ir_ktrace(J)));
  J->base += 2 + LJ_FR2;
  J->baseslot += 2 + LJ_FR2;
  J->framedepth++;

  lj_record_stop(J, LJ_TRLINK_STITCH, 0);

  /* Undo Lua stack changes. */
  memmove(&base[-1-LJ_FR2], &base[1], sizeof(TValue)*nslot);
  setframe_pc(base-1, pc);
  L->base -= 2 + LJ_FR2;
  L->top -= 2 + LJ_FR2;
}

/* Fallback handler for fast functions that are not recorded (yet). */
static void LJ_FASTCALL recff_nyi(jit_State *J, RecordFFData *rd)
{
  if (J->cur.nins < (IRRef)J->param[JIT_P_minstitch] + REF_BASE) {
    lj_trace_err_info(J, LJ_TRERR_TRACEUV);
  } else {
    /* Can only stitch from Lua call. */
    if (J->framedepth && frame_islua(J->L->base-1)) {
      BCOp op = bc_op(*frame_pc(J->L->base-1));
      /* Stitched trace cannot start with *M op with variable # of args. */
      if (!(op == BC_CALLM || op == BC_CALLMT ||
	    op == BC_RETM || op == BC_TSETM)) {
	switch (J->fn->c.ffid) {
	case FF_error:
	case FF_debug_sethook:
	case FF_jit_flush:
	  break;  /* Don't stitch across special builtins. */
	default:
	  recff_stitch(J);  /* Use trace stitching. */
	  rd->nres = -1;
	  return;
	}
      }
    }
    /* Otherwise stop trace and return to interpreter. */
    lj_record_stop(J, LJ_TRLINK_RETURN, 0);
    rd->nres = -1;
  }
}

/* Fallback handler for unsupported variants of fast functions. */
#define recff_nyiu	recff_nyi

/* Must stop the trace for classic C functions with arbitrary side-effects. */
#define recff_c		recff_nyi

/* Emit BUFHDR for the global temporary buffer. */
static TRef recff_bufhdr(jit_State *J)
{
  return emitir(IRT(IR_BUFHDR, IRT_PGC),
		lj_ir_kptr(J, &J2G(J)->tmpbuf), IRBUFHDR_RESET);
}

/* Emit TMPREF. */
static TRef recff_tmpref(jit_State *J, TRef tr, int mode)
{
  if (!LJ_DUALNUM && tref_isinteger(tr))
    tr = emitir(IRTN(IR_CONV), tr, IRCONV_NUM_INT);
  return emitir(IRT(IR_TMPREF, IRT_PGC), tr, mode);
}

/* -- Base library fast functions ----------------------------------------- */

static void LJ_FASTCALL recff_assert(jit_State *J, RecordFFData *rd)
{
  /* Arguments already specialized. The interpreter throws for nil/false. */
  rd->nres = J->maxslot;  /* Pass through all arguments. */
}

static void LJ_FASTCALL recff_type(jit_State *J, RecordFFData *rd)
{
  /* Arguments already specialized. Result is a constant string. Neat, huh? */
  uint32_t t;
  if (tvisnumber(&rd->argv[0]))
    t = ~LJ_TNUMX;
  else if (LJ_64 && !LJ_GC64 && tvislightud(&rd->argv[0]))
    t = ~LJ_TLIGHTUD;
  else
    t = ~itype(&rd->argv[0]);
  J->base[0] = lj_ir_kstr(J, strV(&J->fn->c.upvalue[t]));
  UNUSED(rd);
}

static void LJ_FASTCALL recff_getmetatable(jit_State *J, RecordFFData *rd)
{
  TRef tr = J->base[0];
  if (tr) {
    RecordIndex ix;
    ix.tab = tr;
    copyTV(J->L, &ix.tabv, &rd->argv[0]);
    if (lj_record_mm_lookup(J, &ix, MM_metatable))
      J->base[0] = ix.mobj;
    else
      J->base[0] = ix.mt;
  }  /* else: Interpreter will throw. */
}

static void LJ_FASTCALL recff_setmetatable(jit_State *J, RecordFFData *rd)
{
  TRef tr = J->base[0];
  TRef mt = J->base[1];
  if (tref_istab(tr) && (tref_istab(mt) || (mt && tref_isnil(mt)))) {
    TRef fref, mtref;
    RecordIndex ix;
    ix.tab = tr;
    copyTV(J->L, &ix.tabv, &rd->argv[0]);
    lj_record_mm_lookup(J, &ix, MM_metatable); /* Guard for no __metatable. */
    fref = emitir(IRT(IR_FREF, IRT_PGC), tr, IRFL_TAB_META);
    mtref = tref_isnil(mt) ? lj_ir_knull(J, IRT_TAB) : mt;
    emitir(IRT(IR_FSTORE, IRT_TAB), fref, mtref);
    if (!tref_isnil(mt))
      emitir(IRT(IR_TBAR, IRT_TAB), tr, 0);
    J->base[0] = tr;
    J->needsnap = 1;
  }  /* else: Interpreter will throw. */
}

static void LJ_FASTCALL recff_rawget(jit_State *J, RecordFFData *rd)
{
  RecordIndex ix;
  ix.tab = J->base[0]; ix.key = J->base[1];
  if (tref_istab(ix.tab) && ix.key) {
    ix.val = 0; ix.idxchain = 0;
    settabV(J->L, &ix.tabv, tabV(&rd->argv[0]));
    copyTV(J->L, &ix.keyv, &rd->argv[1]);
    J->base[0] = lj_record_idx(J, &ix);
  }  /* else: Interpreter will throw. */
}

static void LJ_FASTCALL recff_rawset(jit_State *J, RecordFFData *rd)
{
  RecordIndex ix;
  ix.tab = J->base[0]; ix.key = J->base[1]; ix.val = J->base[2];
  if (tref_istab(ix.tab) && ix.key && ix.val) {
    ix.idxchain = 0;
    settabV(J->L, &ix.tabv, tabV(&rd->argv[0]));
    copyTV(J->L, &ix.keyv, &rd->argv[1]);
    copyTV(J->L, &ix.valv, &rd->argv[2]);
    lj_record_idx(J, &ix);
    /* Pass through table at J->base[0] as result. */
  }  /* else: Interpreter will throw. */
}

static void LJ_FASTCALL recff_rawequal(jit_State *J, RecordFFData *rd)
{
  TRef tra = J->base[0];
  TRef trb = J->base[1];
  if (tra && trb) {
    int diff = lj_record_objcmp(J, tra, trb, &rd->argv[0], &rd->argv[1]);
    J->base[0] = diff ? TREF_FALSE : TREF_TRUE;
  }  /* else: Interpreter will throw. */
}

#if LJ_52
static void LJ_FASTCALL recff_rawlen(jit_State *J, RecordFFData *rd)
{
  TRef tr = J->base[0];
  if (tref_isstr(tr))
    J->base[0] = emitir(IRTI(IR_FLOAD), tr, IRFL_STR_LEN);
  else if (tref_istab(tr))
    J->base[0] = emitir(IRTI(IR_ALEN), tr, TREF_NIL);
  /* else: Interpreter will throw. */
  UNUSED(rd);
}
#endif

/* Determine mode of select() call. */
int32_t lj_ffrecord_select_mode(jit_State *J, TRef tr, TValue *tv)
{
  if (tref_isstr(tr) && *strVdata(tv) == '#') {  /* select('#', ...) */
    if (strV(tv)->len == 1) {
      emitir(IRTG(IR_EQ, IRT_STR), tr, lj_ir_kstr(J, strV(tv)));
    } else {
      TRef trptr = emitir(IRT(IR_STRREF, IRT_PGC), tr, lj_ir_kint(J, 0));
      TRef trchar = emitir(IRT(IR_XLOAD, IRT_U8), trptr, IRXLOAD_READONLY);
      emitir(IRTGI(IR_EQ), trchar, lj_ir_kint(J, '#'));
    }
    return 0;
  } else {  /* select(n, ...) */
    int32_t start = argv2int(J, tv);
    if (start == 0) lj_trace_err(J, LJ_TRERR_BADTYPE);  /* A bit misleading. */
    return start;
  }
}

static void LJ_FASTCALL recff_select(jit_State *J, RecordFFData *rd)
{
  TRef tr = J->base[0];
  if (tr) {
    ptrdiff_t start = lj_ffrecord_select_mode(J, tr, &rd->argv[0]);
    if (start == 0) {  /* select('#', ...) */
      J->base[0] = lj_ir_kint(J, J->maxslot - 1);
    } else if (tref_isk(tr)) {  /* select(k, ...) */
      ptrdiff_t n = (ptrdiff_t)J->maxslot;
      if (start < 0) start += n;
      else if (start > n) start = n;
      if (start >= 1) {
	ptrdiff_t i;
	rd->nres = n - start;
	for (i = 0; i < n - start; i++)
	  J->base[i] = J->base[start+i];
      }  /* else: Interpreter will throw. */
    } else {
      recff_nyiu(J, rd);
      return;
    }
  }  /* else: Interpreter will throw. */
}

static void LJ_FASTCALL recff_tonumber(jit_State *J, RecordFFData *rd)
{
  TRef tr = J->base[0];
  TRef base = J->base[1];
  if (tr && !tref_isnil(base)) {
    base = lj_opt_narrow_toint(J, base);
    if (!tref_isk(base) || IR(tref_ref(base))->i != 10) {
      recff_nyiu(J, rd);
      return;
    }
  }
  if (tref_isnumber_str(tr)) {
    if (tref_isstr(tr)) {
      TValue tmp;
      if (!lj_strscan_num(strV(&rd->argv[0]), &tmp)) {
	recff_nyiu(J, rd);  /* Would need an inverted STRTO for this case. */
	return;
      }
      tr = emitir(IRTG(IR_STRTO, IRT_NUM), tr, 0);
    }
#if LJ_HASFFI
  } else if (tref_iscdata(tr)) {
    lj_crecord_tonumber(J, rd);
    return;
#endif
  } else {
    tr = TREF_NIL;
  }
  J->base[0] = tr;
  UNUSED(rd);
}

static TValue *recff_metacall_cp(lua_State *L, lua_CFunction dummy, void *ud)
{
  jit_State *J = (jit_State *)ud;
  lj_record_tailcall(J, 0, 1);
  UNUSED(L); UNUSED(dummy);
  return NULL;
}

static int recff_metacall(jit_State *J, RecordFFData *rd, MMS mm)
{
  RecordIndex ix;
  ix.tab = J->base[0];
  copyTV(J->L, &ix.tabv, &rd->argv[0]);
  if (lj_record_mm_lookup(J, &ix, mm)) {  /* Has metamethod? */
    int errcode;
    TValue argv0;
    /* Temporarily insert metamethod below object. */
    J->base[1+LJ_FR2] = J->base[0];
    J->base[0] = ix.mobj;
    copyTV(J->L, &argv0, &rd->argv[0]);
    copyTV(J->L, &rd->argv[1+LJ_FR2], &rd->argv[0]);
    copyTV(J->L, &rd->argv[0], &ix.mobjv);
    /* Need to protect lj_record_tailcall because it may throw. */
    errcode = lj_vm_cpcall(J->L, NULL, J, recff_metacall_cp);
    /* Always undo Lua stack changes to avoid confusing the interpreter. */
    copyTV(J->L, &rd->argv[0], &argv0);
    if (errcode)
      lj_err_throw(J->L, errcode);  /* Propagate errors. */
    rd->nres = -1;  /* Pending call. */
    return 1;  /* Tailcalled to metamethod. */
  }
  return 0;
}

static void LJ_FASTCALL recff_tostring(jit_State *J, RecordFFData *rd)
{
  TRef tr = J->base[0];
  if (tref_isstr(tr)) {
    /* Ignore __tostring in the string base metatable. */
    /* Pass on result in J->base[0]. */
  } else if (tr && !recff_metacall(J, rd, MM_tostring)) {
    if (tref_isnumber(tr)) {
      J->base[0] = emitir(IRT(IR_TOSTR, IRT_STR), tr,
			  tref_isnum(tr) ? IRTOSTR_NUM : IRTOSTR_INT);
    } else if (tref_ispri(tr)) {
      J->base[0] = lj_ir_kstr(J, lj_strfmt_obj(J->L, &rd->argv[0]));
    } else {
      recff_nyiu(J, rd);
      return;
    }
  }
}

static void LJ_FASTCALL recff_ipairs_aux(jit_State *J, RecordFFData *rd)
{
  RecordIndex ix;
  ix.tab = J->base[0];
  if (tref_istab(ix.tab)) {
    if (!tvisnumber(&rd->argv[1]))  /* No support for string coercion. */
      lj_trace_err(J, LJ_TRERR_BADTYPE);
    setintV(&ix.keyv, numberVint(&rd->argv[1])+1);
    settabV(J->L, &ix.tabv, tabV(&rd->argv[0]));
    ix.val = 0; ix.idxchain = 0;
    ix.key = lj_opt_narrow_toint(J, J->base[1]);
    J->base[0] = ix.key = emitir(IRTI(IR_ADD), ix.key, lj_ir_kint(J, 1));
    J->base[1] = lj_record_idx(J, &ix);
    rd->nres = tref_isnil(J->base[1]) ? 0 : 2;
  }  /* else: Interpreter will throw. */
}

static void LJ_FASTCALL recff_xpairs(jit_State *J, RecordFFData *rd)
{
  TRef tr = J->base[0];
  if (!((LJ_52 || (LJ_HASFFI && tref_iscdata(tr))) &&
	recff_metacall(J, rd, MM_pairs + rd->data))) {
    if (tref_istab(tr)) {
      J->base[0] = lj_ir_kfunc(J, funcV(&J->fn->c.upvalue[0]));
      J->base[1] = tr;
      J->base[2] = rd->data ? lj_ir_kint(J, 0) : TREF_NIL;
      rd->nres = 3;
    }  /* else: Interpreter will throw. */
  }
}

static void LJ_FASTCALL recff_pcall(jit_State *J, RecordFFData *rd)
{
  if (J->maxslot >= 1) {
#if LJ_FR2
    /* Shift function arguments up. */
    memmove(J->base + 1, J->base, sizeof(TRef) * J->maxslot);
#endif
    lj_record_call(J, 0, J->maxslot - 1);
    rd->nres = -1;  /* Pending call. */
    J->needsnap = 1;  /* Start catching on-trace errors. */
  }  /* else: Interpreter will throw. */
}

static TValue *recff_xpcall_cp(lua_State *L, lua_CFunction dummy, void *ud)
{
  jit_State *J = (jit_State *)ud;
  lj_record_call(J, 1, J->maxslot - 2);
  UNUSED(L); UNUSED(dummy);
  return NULL;
}

static void LJ_FASTCALL recff_xpcall(jit_State *J, RecordFFData *rd)
{
  if (J->maxslot >= 2) {
    TValue argv0, argv1;
    TRef tmp;
    int errcode;
    /* Swap function and traceback. */
    tmp = J->base[0]; J->base[0] = J->base[1]; J->base[1] = tmp;
    copyTV(J->L, &argv0, &rd->argv[0]);
    copyTV(J->L, &argv1, &rd->argv[1]);
    copyTV(J->L, &rd->argv[0], &argv1);
    copyTV(J->L, &rd->argv[1], &argv0);
#if LJ_FR2
    /* Shift function arguments up. */
    memmove(J->base + 2, J->base + 1, sizeof(TRef) * (J->maxslot-1));
#endif
    /* Need to protect lj_record_call because it may throw. */
    errcode = lj_vm_cpcall(J->L, NULL, J, recff_xpcall_cp);
    /* Always undo Lua stack swap to avoid confusing the interpreter. */
    copyTV(J->L, &rd->argv[0], &argv0);
    copyTV(J->L, &rd->argv[1], &argv1);
    if (errcode)
      lj_err_throw(J->L, errcode);  /* Propagate errors. */
    rd->nres = -1;  /* Pending call. */
    J->needsnap = 1;  /* Start catching on-trace errors. */
  }  /* else: Interpreter will throw. */
}

static void LJ_FASTCALL recff_getfenv(jit_State *J, RecordFFData *rd)
{
  TRef tr = J->base[0];
  /* Only support getfenv(0) for now. */
  if (tref_isint(tr) && tref_isk(tr) && IR(tref_ref(tr))->i == 0) {
    TRef trl = emitir(IRT(IR_LREF, IRT_THREAD), 0, 0);
    J->base[0] = emitir(IRT(IR_FLOAD, IRT_TAB), trl, IRFL_THREAD_ENV);
    return;
  }
  recff_nyiu(J, rd);
}

static void LJ_FASTCALL recff_next(jit_State *J, RecordFFData *rd)
{
#if LJ_BE
  /* YAGNI: Disabled on big-endian due to issues with lj_vm_next,
  ** IR_HIOP, RID_RETLO/RID_RETHI and ra_destpair.
  */
  recff_nyi(J, rd);
#else
  TRef tab = J->base[0];
  if (tref_istab(tab)) {
    RecordIndex ix;
    cTValue *keyv;
    ix.tab = tab;
    if (tref_isnil(J->base[1])) {  /* Shortcut for start of traversal. */
      ix.key = lj_ir_kint(J, 0);
      keyv = niltvg(J2G(J));
    } else {
      TRef tmp = recff_tmpref(J, J->base[1], IRTMPREF_IN1);
      ix.key = lj_ir_call(J, IRCALL_lj_tab_keyindex, tab, tmp);
      keyv = &rd->argv[1];
    }
    copyTV(J->L, &ix.tabv, &rd->argv[0]);
    ix.keyv.u32.lo = lj_tab_keyindex(tabV(&ix.tabv), keyv);
    /* Omit the value, if not used by the caller. */
    ix.idxchain = (J->framedepth && frame_islua(J->L->base-1) &&
		   bc_b(frame_pc(J->L->base-1)[-1])-1 < 2);
    ix.mobj = 0;  /* We don't need the next index. */
    rd->nres = lj_record_next(J, &ix);
    J->base[0] = ix.key;
    J->base[1] = ix.val;
  }  /* else: Interpreter will throw. */
#endif
}

/* -- Math library fast functions ----------------------------------------- */

static void LJ_FASTCALL recff_math_abs(jit_State *J, RecordFFData *rd)
{
  TRef tr = lj_ir_tonum(J, J->base[0]);
  J->base[0] = emitir(IRTN(IR_ABS), tr, lj_ir_ksimd(J, LJ_KSIMD_ABS));
  UNUSED(rd);
}

/* Record rounding functions math.floor and math.ceil. */
static void LJ_FASTCALL recff_math_round(jit_State *J, RecordFFData *rd)
{
  TRef tr = J->base[0];
  if (!tref_isinteger(tr)) {  /* Pass through integers unmodified. */
    tr = emitir(IRTN(IR_FPMATH), lj_ir_tonum(J, tr), rd->data);
    /* Result is integral (or NaN/Inf), but may not fit an int32_t. */
    if (LJ_DUALNUM) {  /* Try to narrow using a guarded conversion to int. */
      lua_Number n = lj_vm_foldfpm(numberVnum(&rd->argv[0]), rd->data);
      if (n == (lua_Number)lj_num2int(n))
	tr = emitir(IRTGI(IR_CONV), tr, IRCONV_INT_NUM|IRCONV_CHECK);
    }
    J->base[0] = tr;
  }
}

/* Record unary math.* functions, mapped to IR_FPMATH opcode. */
static void LJ_FASTCALL recff_math_unary(jit_State *J, RecordFFData *rd)
{
  J->base[0] = emitir(IRTN(IR_FPMATH), lj_ir_tonum(J, J->base[0]), rd->data);
}

/* Record math.log. */
static void LJ_FASTCALL recff_math_log(jit_State *J, RecordFFData *rd)
{
  TRef tr = lj_ir_tonum(J, J->base[0]);
  if (J->base[1]) {
#ifdef LUAJIT_NO_LOG2
    uint32_t fpm = IRFPM_LOG;
#else
    uint32_t fpm = IRFPM_LOG2;
#endif
    TRef trb = lj_ir_tonum(J, J->base[1]);
    tr = emitir(IRTN(IR_FPMATH), tr, fpm);
    trb = emitir(IRTN(IR_FPMATH), trb, fpm);
    trb = emitir(IRTN(IR_DIV), lj_ir_knum_one(J), trb);
    tr = emitir(IRTN(IR_MUL), tr, trb);
  } else {
    tr = emitir(IRTN(IR_FPMATH), tr, IRFPM_LOG);
  }
  J->base[0] = tr;
  UNUSED(rd);
}

/* Record math.atan2. */
static void LJ_FASTCALL recff_math_atan2(jit_State *J, RecordFFData *rd)
{
  TRef tr = lj_ir_tonum(J, J->base[0]);
  TRef tr2 = lj_ir_tonum(J, J->base[1]);
  J->base[0] = lj_ir_call(J, IRCALL_atan2, tr, tr2);
  UNUSED(rd);
}

/* Record math.ldexp. */
static void LJ_FASTCALL recff_math_ldexp(jit_State *J, RecordFFData *rd)
{
  TRef tr = lj_ir_tonum(J, J->base[0]);
#if LJ_TARGET_X86ORX64
  TRef tr2 = lj_ir_tonum(J, J->base[1]);
#else
  TRef tr2 = lj_opt_narrow_toint(J, J->base[1]);
#endif
  J->base[0] = emitir(IRTN(IR_LDEXP), tr, tr2);
  UNUSED(rd);
}

static void LJ_FASTCALL recff_math_call(jit_State *J, RecordFFData *rd)
{
  TRef tr = lj_ir_tonum(J, J->base[0]);
  J->base[0] = emitir(IRTN(IR_CALLN), tr, rd->data);
}

static void LJ_FASTCALL recff_math_pow(jit_State *J, RecordFFData *rd)
{
  J->base[0] = lj_opt_narrow_arith(J, J->base[0], J->base[1],
				   &rd->argv[0], &rd->argv[1], IR_POW);
  UNUSED(rd);
}

static void LJ_FASTCALL recff_math_minmax(jit_State *J, RecordFFData *rd)
{
  TRef tr = lj_ir_tonumber(J, J->base[0]);
  uint32_t op = rd->data;
  BCReg i;
  for (i = 1; J->base[i] != 0; i++) {
    TRef tr2 = lj_ir_tonumber(J, J->base[i]);
    IRType t = IRT_INT;
    if (!(tref_isinteger(tr) && tref_isinteger(tr2))) {
      if (tref_isinteger(tr)) tr = emitir(IRTN(IR_CONV), tr, IRCONV_NUM_INT);
      if (tref_isinteger(tr2)) tr2 = emitir(IRTN(IR_CONV), tr2, IRCONV_NUM_INT);
      t = IRT_NUM;
    }
    tr = emitir(IRT(op, t), tr, tr2);
  }
  J->base[0] = tr;
}

static void LJ_FASTCALL recff_math_random(jit_State *J, RecordFFData *rd)
{
  GCudata *ud = udataV(&J->fn->c.upvalue[0]);
  TRef tr, one;
  lj_ir_kgc(J, obj2gco(ud), IRT_UDATA);  /* Prevent collection. */
  tr = lj_ir_call(J, IRCALL_lj_prng_u64d, lj_ir_kptr(J, uddata(ud)));
  one = lj_ir_knum_one(J);
  tr = emitir(IRTN(IR_SUB), tr, one);
  if (J->base[0]) {
    TRef tr1 = lj_ir_tonum(J, J->base[0]);
    if (J->base[1]) {  /* d = floor(d*(r2-r1+1.0)) + r1 */
      TRef tr2 = lj_ir_tonum(J, J->base[1]);
      tr2 = emitir(IRTN(IR_SUB), tr2, tr1);
      tr2 = emitir(IRTN(IR_ADD), tr2, one);
      tr = emitir(IRTN(IR_MUL), tr, tr2);
      tr = emitir(IRTN(IR_FPMATH), tr, IRFPM_FLOOR);
      tr = emitir(IRTN(IR_ADD), tr, tr1);
    } else {  /* d = floor(d*r1) + 1.0 */
      tr = emitir(IRTN(IR_MUL), tr, tr1);
      tr = emitir(IRTN(IR_FPMATH), tr, IRFPM_FLOOR);
      tr = emitir(IRTN(IR_ADD), tr, one);
    }
  }
  J->base[0] = tr;
  UNUSED(rd);
}

/* -- Bit library fast functions ------------------------------------------ */

/* Record bit.tobit. */
static void LJ_FASTCALL recff_bit_tobit(jit_State *J, RecordFFData *rd)
{
  TRef tr = J->base[0];
#if LJ_HASFFI
  if (tref_iscdata(tr)) { recff_bit64_tobit(J, rd); return; }
#endif
  J->base[0] = lj_opt_narrow_tobit(J, tr);
  UNUSED(rd);
}

/* Record unary bit.bnot, bit.bswap. */
static void LJ_FASTCALL recff_bit_unary(jit_State *J, RecordFFData *rd)
{
#if LJ_HASFFI
  if (recff_bit64_unary(J, rd))
    return;
#endif
  J->base[0] = emitir(IRTI(rd->data), lj_opt_narrow_tobit(J, J->base[0]), 0);
}

/* Record N-ary bit.band, bit.bor, bit.bxor. */
static void LJ_FASTCALL recff_bit_nary(jit_State *J, RecordFFData *rd)
{
#if LJ_HASFFI
  if (recff_bit64_nary(J, rd))
    return;
#endif
  {
    TRef tr = lj_opt_narrow_tobit(J, J->base[0]);
    uint32_t ot = IRTI(rd->data);
    BCReg i;
    for (i = 1; J->base[i] != 0; i++)
      tr = emitir(ot, tr, lj_opt_narrow_tobit(J, J->base[i]));
    J->base[0] = tr;
  }
}

/* Record bit shifts. */
static void LJ_FASTCALL recff_bit_shift(jit_State *J, RecordFFData *rd)
{
#if LJ_HASFFI
  if (recff_bit64_shift(J, rd))
    return;
#endif
  {
    TRef tr = lj_opt_narrow_tobit(J, J->base[0]);
    TRef tsh = lj_opt_narrow_tobit(J, J->base[1]);
    IROp op = (IROp)rd->data;
    if (!(op < IR_BROL ? LJ_TARGET_MASKSHIFT : LJ_TARGET_MASKROT) &&
	!tref_isk(tsh))
      tsh = emitir(IRTI(IR_BAND), tsh, lj_ir_kint(J, 31));
#ifdef LJ_TARGET_UNIFYROT
    if (op == (LJ_TARGET_UNIFYROT == 1 ? IR_BROR : IR_BROL)) {
      op = LJ_TARGET_UNIFYROT == 1 ? IR_BROL : IR_BROR;
      tsh = emitir(IRTI(IR_NEG), tsh, tsh);
    }
#endif
    J->base[0] = emitir(IRTI(op), tr, tsh);
  }
}

static void LJ_FASTCALL recff_bit_tohex(jit_State *J, RecordFFData *rd)
{
#if LJ_HASFFI
  TRef hdr = recff_bufhdr(J);
  TRef tr = recff_bit64_tohex(J, rd, hdr);
  J->base[0] = emitir(IRTG(IR_BUFSTR, IRT_STR), tr, hdr);
#else
  recff_nyiu(J, rd);  /* Don't bother working around this NYI. */
#endif
}

/* -- String library fast functions --------------------------------------- */

/* Specialize to relative starting position for string. */
static TRef recff_string_start(jit_State *J, GCstr *s, int32_t *st, TRef tr,
			       TRef trlen, TRef tr0)
{
  int32_t start = *st;
  if (start < 0) {
    emitir(IRTGI(IR_LT), tr, tr0);
    tr = emitir(IRTI(IR_ADD), trlen, tr);
    start = start + (int32_t)s->len;
    emitir(start < 0 ? IRTGI(IR_LT) : IRTGI(IR_GE), tr, tr0);
    if (start < 0) {
      tr = tr0;
      start = 0;
    }
  } else if (start == 0) {
    emitir(IRTGI(IR_EQ), tr, tr0);
    tr = tr0;
  } else {
    tr = emitir(IRTI(IR_ADD), tr, lj_ir_kint(J, -1));
    emitir(IRTGI(IR_GE), tr, tr0);
    start--;
  }
  *st = start;
  return tr;