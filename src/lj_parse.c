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
