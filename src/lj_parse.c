/*
** Lua parser (source code -> bytecode).
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
**
** Major portions taken verbatim or adapted from the Lua interpreter.
** Copyright (C) 1994-2008 Lua.org, PUC-Rio. See Copyright Notice in lua.h
*/

#define lj_parse_c
#define LUA_CORE

#include "lj_obj.h"
#include "lj_gc.h"
#include "lj_err.h"
#include "lj_debug.h"
#include "lj_buf.h"
#include "lj_str.h"
#include "lj_tab.h"
#include "lj_func.h"
#include "lj_state.h"
#include "lj_bc.h"
#if LJ_HASFFI
#include "lj_ctype.h"
#endif
#include "lj_strfmt.h"
#include "lj_lex.h"
#include "lj_parse.h"
#include "lj_vm.h"
#include "lj_vmevent.h"

/* -- Parser structures and definitions ----------------------------------- */

/* Expression kinds. */
typedef enum {
  /* Constant expressions must be first and in this order: */
  VKNIL,
  VKFALSE,
  VKTRUE,
  VKSTR,	/* sval = string value */
  VKNUM,	/* nval = number value */
  VKLAST = VKNUM,
  VKCDATA,	/* nval = cdata value, not treated as a constant expression */
  /* Non-constant expressions follow: */
  VLOCAL,	/* info = local register, aux = vstack index */
  VUPVAL,	/* info = upvalue index, aux = vstack index */
  VGLOBAL,	/* sval = string value */
  VINDEXED,	/* info = table register, aux = index reg/byte/string const */
  VJMP,		/* info = instruction PC */
  VRELOCABLE,	/* info = instruction PC */
  VNONRELOC,	/* info = result register */
  VCALL,	/* info = instruction PC, aux = base */
  VVOID
} ExpKind;

/* Expression descriptor. */
typedef struct ExpDesc {
  union {
    struct {
      uint32_t info;	/* Primary info. */
      uint32_t aux;	/* Secondary info. */
    } s;
    TValue nval;	/* Number value. */
    GCstr *sval;	/* String value. */
  } u;
  ExpKind k;
  BCPos t;		/* True condition jump list. */
  BCPos f;		/* False condition jump list. */
} ExpDesc;

/* Macros for expressions. */
#define expr_hasjump(e)		((e)->t != (e)->f)

#define expr_isk(e)		((e)->k <= VKLAST)
#define expr_isk_nojump(e)	(expr_isk(e) && !expr_hasjump(e))
#define expr_isnumk(e)		((e)->k == VKNUM)
#define expr_isnumk_nojump(e)	(expr_isnumk(e) && !expr_hasjump(e))
#define expr_isstrk(e)		((e)->k == VKSTR)

#define expr_numtv(e)		check_exp(expr_isnumk((e)), &(e)->u.nval)
#define expr_numberV(e)		numberVnum(expr_numtv((e)))

/* Initialize expression. */
static LJ_AINLINE void expr_init(ExpDesc *e, ExpKind k, uint32_t info)
{
  e->k = k;
  e->u.s.info = info;
  e->f = e->t = NO_JMP;
}

/* Check number constant for +-0. */
static int expr_numiszero(ExpDesc *e)
{
  TValue *o = expr_numtv(e);
  return tvisint(o) ? (intV(o) == 0) : tviszero(o);
}

/* Per-function linked list of scope blocks. */
typedef struct FuncScope {
  struct FuncScope *prev;	/* Link to outer scope. */
  MSize vstart;			/* Start of block-local variables. */
  uint8_t nactvar;		/* Number of active vars outside the scope. */
  uint8_t flags;		/* Scope flags. */
} FuncScope;

#define FSCOPE_LOOP		0x01	/* Scope is a (breakable) loop. */
#define FSCOPE_BREAK		0x02	/* Break used in scope. */
#define FSCOPE_GOLA		0x04	/* Goto or label used in scope. */
#define FSCOPE_UPVAL		0x08	/* Upvalue in scope. */
#define FSCOPE_NOCLOSE		0x10	/* Do not close upvalues. */

#define NAME_BREAK		((GCstr *)(uintptr_t)1)

/* Index into variable stack. */
typedef uint16_t VarIndex;
#define LJ_MAX_VSTACK		(65536 - LJ_MAX_UPVAL)

/* Variable/goto/label info. */
#define VSTACK_VAR_RW		0x01	/* R/W variable. */
#define VSTACK_GOTO		0x02	/* Pending goto. */
#define VSTACK_LABEL		0x04	/* Label. */

/* Per-function state. */
typedef struct FuncState {
  GCtab *kt;			/* Hash table for constants. */
  LexState *ls;			/* Lexer state. */
  lua_State *L;			/* Lua state. */
  FuncScope *bl;		/* Current scope. */
  struct FuncState *prev;	/* Enclosing function. */
  BCPos pc;			/* Next bytecode position. */
  BCPos lasttarget;		/* Bytecode position of last jump target. */
  BCPos jpc;			/* Pending jump list to next bytecode. */
  BCReg freereg;		/* First free register. */
  BCReg nactvar;		/* Number of active local variables. */
  BCReg nkn, nkgc;		/* Number of lua_Number/GCobj constants */
  BCLine linedefined;		/* First line of the function definition. */
  BCInsLine *bcbase;		/* Base of bytecode stack. */
  BCPos bclim;			/* Limit of bytecode stack. */
  MSize vbase;			/* Base of variable stack for this function. */
  uint8_t flags;		/* Prototype flags. */
  uint8_t numparams;		/* Number of parameters. */
  uint8_t framesize;		/* Fixed frame size. */
  uint8_t nuv;			/* Number of upvalues */
  VarIndex varmap[LJ_MAX_LOCVAR];  /* Map from register to variable idx. */
  VarIndex uvmap[LJ_MAX_UPVAL];	/* Map from upvalue to variable idx. */
  VarIndex uvtmp[LJ_MAX_UPVAL];	/* Temporary upvalue map. */
} FuncState;

/* Binary and unary operators. ORDER OPR */
typedef enum BinOpr {
  OPR_ADD, OPR_SUB, OPR_MUL, OPR_DIV, OPR_MOD, OPR_POW,  /* ORDER ARITH */
  OPR_CONCAT,
  OPR_NE, OPR_EQ,
  OPR_LT, OPR_GE, OPR_LE, OPR_GT,
  OPR_AND, OPR_OR,
  OPR_NOBINOPR
} BinOpr;

LJ_STATIC_ASSERT((int)BC_ISGE-(int)BC_ISLT == (int)OPR_GE-(int)OPR_LT);
LJ_STATIC_ASSERT((int)BC_ISLE-(int)BC_ISLT == (int)OPR_LE-(int)OPR_LT);
LJ_STATIC_ASSERT((int)BC_ISGT-(int)BC_ISLT == (int)OPR_GT-(int)OPR_LT);
LJ_STATIC_ASSERT((int)BC_SUBVV-(int)BC_ADDVV == (int)OPR_SUB-(int)OPR_ADD);
LJ_STATIC_ASSERT((int)BC_MULVV-(int)BC_ADDVV == (int)OPR_MUL-(int)OPR_ADD);
LJ_STATIC_ASSERT((int)BC_DIVVV-(int)BC_ADDVV == (int)OPR_DIV-(int)OPR_ADD);
LJ_STATIC_ASSERT((int)BC_MODVV-(int)BC_ADDVV == (int)OPR_MOD-(int)OPR_ADD);

#ifdef LUA_USE_ASSERT
#define lj_assertFS(c, ...)	(lj_assertG_(G(fs->L), (c), __VA_ARGS__))
#else
#define lj_assertFS(c, ...)	((void)fs)
#endif

/* -- Error handling ------------------------------------------------------ */

LJ_NORET LJ_NOINLINE static void err_syntax(LexState *ls, ErrMsg em)
{
  lj_lex_error(ls, ls->tok, em);
}

LJ_NORET LJ_NOINLINE static void err_token(LexState *ls, LexToken tok)
{
  lj_lex_error(ls, ls->tok, LJ_ERR_XTOKEN, lj_lex_token2str(ls, tok));
}

LJ_NORET static void err_limit(FuncState *fs, uint32_t limit, const char *what)
{
  if (fs->linedefined == 0)
    lj_lex_error(fs->ls, 0, LJ_ERR_XLIMM, limit, what);
  else
    lj_lex_error(fs->ls, 0, LJ_ERR_XLIMF, fs->linedefined, limit, what);
}

#define checklimit(fs, v, l, m)		if ((v) >= (l)) err_limit(fs, l, m)
#define checklimitgt(fs, v, l, m)	if ((v) > (l)) err_limit(fs, l, m)
#define checkcond(ls, c, em)		{ if (!(c)) err_syntax(ls, em); }

/* -- Management of constants --------------------------------------------- */

/* Return bytecode encoding for primitive constant. */
#define const_pri(e)		check_exp((e)->k <= VKTRUE, (e)->k)

#define tvhaskslot(o)	((o)->u32.hi == 0)
#define tvkslot(o)	((o)->u32.lo)

/* Add a number constant. */
static BCReg const_num(FuncState *fs, ExpDesc *e)
{
  lua_State *L = fs->L;
  TValue *o;
  lj_assertFS(expr_isnumk(e), "bad usage");
  o = lj_tab_set(L, fs->kt, &e->u.nval);
  if (tvhaskslot(o))
    return tvkslot(o);
  o->u64 = fs->nkn;
  return fs->nkn++;
}

/* Add a GC object constant. */
static BCReg const_gc(FuncState *fs, GCobj *gc, uint32_t itype)
{
  lua_State *L = fs->L;
  TValue key, *o;
  setgcV(L, &key, gc, itype);
  /* NOBARRIER: the key is new or kept alive. */
  o = lj_tab_set(L, fs->kt, &key);
  if (tvhaskslot(o))
    return tvkslot(o);
  o->u64 = fs->nkgc;
  return fs->nkgc++;
}

/* Add a string constant. */
static BCReg const_str(FuncState *fs, ExpDesc *e)
{
  lj_assertFS(expr_isstrk(e) || e->k == VGLOBAL, "bad usage");
  return const_gc(fs, obj2gco(e->u.sval), LJ_TSTR);
}

/* Anchor string constant to avoid GC. */
GCstr *lj_parse_keepstr(LexState *ls, const char *str, size_t len)
{
  /* NOBARRIER: the key is new or kept alive. */
  lua_State *L = ls->L;
  GCstr *s = lj_str_new(L, str, len);
  TValue *tv = lj_tab_setstr(L, ls->fs->kt, s);
  if (tvisnil(tv)) setboolV(tv, 1);
  lj_gc_check(L);
  return s;
}

#if LJ_HASFFI
/* Anchor cdata to avoid GC. */
void lj_parse_keepcdata(LexState *ls, TValue *tv, GCcdata *cd)
{
  /* NOBARRIER: the key is new or kept alive. */
  lua_State *L = ls->L;
  setcdataV(L, tv, cd);
  setboolV(lj_tab_set(L, ls->fs->kt, tv), 1);
}
#endif

/* -- Jump list handling -------------------------------------------------- */

/* Get next element in jump list. */
static BCPos jmp_next(FuncState *fs, BCPos pc)
{
  ptrdiff_t delta = bc_j(fs->bcbase[pc].ins);
  if ((BCPos)delta == NO_JMP)
    return NO_JMP;
  else
    return (BCPos)(((ptrdiff_t)pc+1)+delta);
}

/* Check if any of the instructions on the jump list produce no value. */
static int jmp_novalue(FuncState *fs, BCPos list)
{
  for (; list != NO_JMP; list = jmp_next(fs, list)) {
    BCIns p = fs->bcbase[list >= 1 ? list-1 : list].ins;
    if (!(bc_op(p) == BC_ISTC || bc_op(p) == BC_ISFC || bc_a(p) == NO_REG))
      return 1;
  }
  return 0;
}

/* Patch register of test instructions. */
static int jmp_patchtestreg(FuncState *fs, BCPos pc, BCReg reg)
{
  BCInsLine *ilp = &fs->bcbase[pc >= 1 ? pc-1 : pc];
  BCOp op = bc_op(ilp->ins);
  if (op == BC_ISTC || op == BC_ISFC) {
    if (reg != NO_REG && reg != bc_d(ilp->ins)) {
      setbc_a(&ilp->ins, reg);
    } else {  /* Nothing to store or already in the right register. */
      setbc_op(&ilp->ins, op+(BC_IST-BC_ISTC));
      setbc_a(&ilp->ins, 0);
    }
  } else if (bc_a(ilp->ins) == NO_REG) {
    if (reg == NO_REG) {
      ilp->ins = BCINS_AJ(BC_JMP, bc_a(fs->bcbase[pc].ins), 0);
    } else {
      setbc_a(&ilp->ins, reg);
      if (reg >= bc_a(ilp[1].ins))
	setbc_a(&ilp[1].ins, reg+1);
    }
  } else {
    return 0;  /* Cannot patch other instructions. */
  }
  return 1;
}

/* Drop values for all instructions on jump list. */
static void jmp_dropval(FuncState *fs, BCPos list)
{
  for (; list != NO_JMP; list = jmp_next(fs, list))
    jmp_patchtestreg(fs, list, NO_REG);
}

/* Patch jump instruction to target. */
static void jmp_patchins(FuncState *fs, BCPos pc, BCPos dest)
{
  BCIns *jmp = &fs->bcbase[pc].ins;
  BCPos offset = dest-(pc+1)+BCBIAS_J;
  lj_assertFS(dest != NO_JMP, "uninitialized jump target");
  if (offset > BCMAX_D)
    err_syntax(fs->ls, LJ_ERR_XJUMP);
  setbc_d(jmp, offset);
}

/* Append to jump list. */
static void jmp_append(FuncState *fs, BCPos *l1, BCPos l2)
{
  if (l2 == NO_JMP) {
    return;
  } else if (*l1 == NO_JMP) {
    *l1 = l2;
  } else {
    BCPos list = *l1;
    BCPos next;
    while ((next = jmp_next(fs, list)) != NO_JMP)  /* Find last element. */
      list = next;
    jmp_patchins(fs, list, l2);
  }
}

/* Patch jump list and preserve produced values. */
static void jmp_patchval(FuncState *fs, BCPos list, BCPos vtarget,
			 BCReg reg, BCPos dtarget)
{
  while (list != NO_JMP) {
    BCPos next = jmp_next(fs, list);
    if (jmp_patchtestreg(fs, list, reg))
      jmp_patchins(fs, list, vtarget);  /* Jump to target with value. */
    else
      jmp_patchins(fs, list, dtarget);  /* Jump to default target. */
    list = next;
  }
}

/* Jump to following instruction. Append to list of pending jumps. */
static void jmp_tohere(FuncState *fs, BCPos list)
{
  fs->lasttarget = fs->pc;
  jmp_append(fs, &fs->jpc, list);
}

/* Patch jump list to target. */
static void jmp_patch(FuncState *fs, BCPos list, BCPos target)
{
  if (target == fs->pc) {
    jmp_tohere(fs, list);
  } else {
    lj_assertFS(target < fs->pc, "bad jump target");
    jmp_patchval(fs, list, target, NO_REG, target);
  }
}

/* -- Bytecode register allocator ----------------------------------------- */

/* Bump frame size. */
static void bcreg_bump(FuncState *fs, BCReg n)
{
  BCReg sz = fs->freereg + n;
  if (sz > fs->framesize) {
    if (sz >= LJ_MAX_SLOTS)
      err_syntax(fs->ls, LJ_ERR_XSLOTS);
    fs->framesize = (uint8_t)sz;
  }
}

/* Reserve registers. */
static void bcreg_reserve(FuncState *fs, BCReg n)
{
  bcreg_bump(fs, n);
  fs->freereg += n;
}

/* Free register. */
static void bcreg_free(FuncState *fs, BCReg reg)
{
  if (reg >= fs->nactvar) {
    fs->freereg--;
    lj_assertFS(reg == fs->freereg, "bad regfree");
  }
}

/* Free register for expression. */
static void expr_free(FuncState *fs, ExpDesc *e)
{
  if (e->k == VNONRELOC)
    bcreg_free(fs, e->u.s.info);
}

/* -- Bytecode emitter ---------------------------------------------------- */

/* Emit bytecode instruction. */
static BCPos bcemit_INS(FuncState *fs, BCIns ins)
{
  BCPos pc = fs->pc;
  LexState *ls = fs->ls;
  jmp_patchval(fs, fs->jpc, pc, NO_REG, pc);
  fs->jpc = NO_JMP;
  if (LJ_UNLIKELY(pc >= fs->bclim)) {
    ptrdiff_t base = fs->bcbase - ls->bcstack;
    checklimit(fs, ls->sizebcstack, LJ_MAX_BCINS, "bytecode instructions");
    lj_mem_growvec(fs->L, ls->bcstack, ls->sizebcstack, LJ_MAX_BCINS,BCInsLine);
    fs->bclim = (BCPos)(ls->sizebcstack - base);
    fs->bcbase = ls->bcstack + base;
  }
  fs->bcbase[pc].ins = ins;
  fs->bcbase[pc].line = ls->lastline;
  fs->pc = pc+1;
  return pc;
}

#define bcemit_ABC(fs, o, a, b, c)	bcemit_INS(fs, BCINS_ABC(o, a, b, c))
#define bcemit_AD(fs, o, a, d)		bcemit_INS(fs, BCINS_AD(o, a, d))
#define bcemit_AJ(fs, o, a, j)		bcemit_INS(fs, BCINS_AJ(o, a, j))

#define bcptr(fs, e)			(&(fs)->bcbase[(e)->u.s.info].ins)

/* -- Bytecode emitter for expressions ------------------------------------ */

/* Discharge non-constant expression to any register. */
static void expr_discharge(FuncState *fs, ExpDesc *e)
{
  BCIns ins;
  if (e->k == VUPVAL) {
    ins = BCINS_AD(BC_UGET, 0, e->u.s.info);
  } else if (e->k == VGLOBAL) {
    ins = BCINS_AD(BC_GGET, 0, const_str(fs, e));
  } else if (e->k == VINDEXED) {
    BCReg rc = e->u.s.aux;
    if ((int32_t)rc < 0) {
      ins = BCINS_ABC(BC_TGETS, 0, e->u.s.info, ~rc);
    } else if (rc > BCMAX_C) {
      ins = BCINS_ABC(BC_TGETB, 0, e->u.s.info, rc-(BCMAX_C+1));
    } else {
      bcreg_free(fs, rc);
      ins = BCINS_ABC(BC_TGETV, 0, e->u.s.info, rc);
    }
    bcreg_free(fs, e->u.s.info);
  } else if (e->k == VCALL) {
    e->u.s.info = e->u.s.aux;
    e->k = VNONRELOC;
    return;
  } else if (e->k == VLOCAL) {
    e->k = VNONRELOC;
    return;
  } else {
    return;
  }
  e->u.s.info = bcemit_INS(fs, ins);
  e->k = VRELOCABLE;
}

/* Emit bytecode to set a range of registers to nil. */
static void bcemit_nil(FuncState *fs, BCReg from, BCReg n)
{
  if (fs->pc > fs->lasttarget) {  /* No jumps to current position? */
    BCIns *ip = &fs->bcbase[fs->pc-1].ins;
    BCReg pto, pfrom = bc_a(*ip);
    switch (bc_op(*ip)) {  /* Try to merge with the previous instruction. */
    case BC_KPRI:
      if (bc_d(*ip) != ~LJ_TNIL) break;
      if (from == pfrom) {
	if (n == 1) return;
      } else if (from == pfrom+1) {
	from = pfrom;
	n++;
      } else {
	break;
      }
      *ip = BCINS_AD(BC_KNIL, from, from+n-1);  /* Replace KPRI. */
      return;
    case BC_KNIL:
      pto = bc_d(*ip);
      if (pfrom <= from && from <= pto+1) {  /* Can we connect both ranges? */
	if (from+n-1 > pto)
	  setbc_d(ip, from+n-1);  /* Patch previous instruction range. */
	return;
      }
      break;
    default:
      break;
    }
  }
  /* Emit new instruction or replace old instruction. */
  bcemit_INS(fs, n == 1 ? BCINS_AD(BC_KPRI, from, VKNIL) :
			  BCINS_AD(BC_KNIL, from, from+n-1));
}

/* Discharge an expression to a specific register. Ignore branches. */
static void expr_toreg_nobranch(FuncState *fs, ExpDesc *e, BCReg reg)
{
  BCIns ins;
  expr_discharge(fs, e);
  if (e->k == VKSTR) {
    ins = BCINS_AD(BC_KSTR, reg, const_str(fs, e));
  } else if (e->k == VKNUM) {
#if LJ_DUALNUM
    cTValue *tv = expr_numtv(e);
    if (tvisint(tv) && checki16(intV(tv)))
      ins = BCINS_AD(BC_KSHORT, reg, (BCReg)(uint16_t)intV(tv));
    else
#else
    lua_Number n = expr_numberV(e);
    int32_t k = lj_num2int(n);
    if (checki16(k) && n == (lua_Number)k)
      ins = BCINS_AD(BC_KSHORT, reg, (BCReg)(uint16_t)k);
    else
#endif
      ins = BCINS_AD(BC_KNUM, reg, const_num(fs, e));
#if LJ_HASFFI
  } else if (e->k == VKCDATA) {
    fs->flags |= PROTO_FFI;
    ins = BCINS_AD(BC_KCDATA, reg,
		   const_gc(fs, obj2gco(cdataV(&e->u.nval)