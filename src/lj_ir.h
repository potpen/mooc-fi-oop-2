/*
** SSA IR (Intermediate Representation) format.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

#ifndef _LJ_IR_H
#define _LJ_IR_H

#include "lj_obj.h"

/* -- IR instructions ----------------------------------------------------- */

/* IR instruction definition. Order matters, see below. ORDER IR */
#define IRDEF(_) \
  /* Guarded assertions. */ \
  /* Must be properly aligned to flip opposites (^1) and (un)ordered (^4). */ \
  _(LT,		N , ref, ref) \
  _(GE,		N , ref, ref) \
  _(LE,		N , ref, ref) \
  _(GT,		N , ref, ref) \
  \
  _(ULT,	N , ref, ref) \
  _(UGE,	N , ref, ref) \
  _(ULE,	N , ref, ref) \
  _(UGT,	N , ref, ref) \
  \
  _(EQ,		C , ref, ref) \
  _(NE,		C , ref, ref) \
  \
  _(ABC,	N , ref, ref) \
  _(RETF,	S , ref, ref) \
  \
  /* Miscellaneous ops. */ \
  _(NOP,	N , ___, ___) \
  _(BASE,	N , lit, lit) \
  _(PVAL,	N , lit, ___) \
  _(GCSTEP,	S , ___, ___) \
  _(HIOP,	S , ref, ref) \
  _(LOOP,	S , ___, ___) \
  _(USE,	S , ref, ___) \
  _(PHI,	S , ref, ref) \
  _(RENAME,	S , ref, lit) \
  _(PROF,	S , ___, ___) \
  \
  /* Constants. */ \
  _(KPRI,	N , ___, ___) \
  _(KINT,	N , cst, ___) \
  _(KGC,	N , cst, ___) \
  _(KPTR,	N , cst, ___) \
  _(KKPTR,	N , cst, ___) \
  _(KNULL,	N , cst, ___) \
  _(KNUM,	N , cst, ___) \
  _(KINT64,	N , cst, ___) \
  _(KSLOT,	N , ref, lit) \
  \
  /* Bit ops. */ \
  _(BNOT,	N , ref, ___) \
  _(BSWAP,	N , ref, ___) \
  _(BAND,	C , ref, ref) \
  _(BOR,	C , ref, ref) \
  _(BXOR,	C , ref, ref) \
  _(BSHL,	N , ref, ref) \
  _(BSHR,	N , ref, ref) \
  _(BSAR,	N , ref, ref) \
  _(BROL,	N , ref, ref) \
  _(BROR,	N , ref, ref) \
  \
  /* Arithmetic ops. ORDER ARITH */ \
  _(ADD,	C , ref, ref) \
  _(SUB,	N , ref, ref) \
  _(MUL,	C , ref, ref) \
  _(DIV,	N , ref, ref) \
  _(MOD,	N , ref, ref) \
  _(POW,	N , ref, ref) \
  _(NEG,	N , ref, ref) \
  \
  _(ABS,	N , ref, ref) \
  _(LDEXP,	N , ref, ref) \
  _(MIN,	C , ref, ref) \
  _(MAX,	C , ref, ref) \
  _(FPMATH,	N , ref, lit) \
  \
  /* Overflow-checking arithmetic ops. */ \
  _(ADDOV,	CW, ref, ref) \
  _(SUBOV,	NW, ref, ref) \
  _(MULOV,	CW, ref, ref) \
  \
  /* Memory ops. A = array, H = hash, U = upvalue, F = field, S = stack. */ \
  \
  /* Memory references. */ \
  _(AREF,	R , ref, ref) \
  _(HREFK,	R , ref, ref) \
  _(HREF,	L , ref, ref) \
  _(NEWREF,	S , ref, ref) \
  _(UREFO,	LW, ref, lit) \
  _(UREFC,	LW, ref, lit) \
  _(FREF,	R , ref, lit) \
  _(TMPREF,	S , ref, lit) \
  _(STRREF,	N , ref, ref) \
  _(LREF,	L , ___, ___) \
  \
  /* Loads and Stores. These must be in the same order. */ \
  _(ALOAD,	L , ref, ___) \
  _(HLOAD,	L , ref, ___) \
  _(ULOAD,	L , ref, ___) \
  _(FLOAD,	L , ref, lit) \
  _(XLOAD,	L , ref, lit) \
  _(SLOAD,	L , lit, lit) \
  _(VLOAD,	L , ref, lit) \
  _(ALEN,	L , ref, ref) \
  \
  _(ASTORE,	S , ref, ref) \
  _(HSTORE,	S , ref, ref) \
  _(USTORE,	S , ref, ref) \
  _(FSTORE,	S , ref, ref) \
  _(XSTORE,	S , ref, ref) \
  \
  /* Allocations. */ \
  _(SNEW,	N , ref, ref)  /* CSE is ok, not marked as A. */ \
  _(XSNEW,	A , ref, ref) \
  _(TNEW,	AW, lit, lit) \
  _(TDUP,	AW, ref, ___) \
  _(CNEW,	AW, ref, ref) \
  _(CNEWI,	NW, ref, ref)  /* CSE is ok, not marked as A. */ \
  \
  /* Buffer operations. */ \
  _(BUFHDR,	L , ref, lit) \
  _(BUFPUT,	LW, ref, ref) \
  _(BUFSTR,	AW, ref, ref) \
  \
  /* Barriers. */ \
  _(TBAR,	S , ref, ___) \
  _(OBAR,	S , ref, ref) \
  _(XBAR,	S , ___, ___) \
  \
  /* Type conversions. */ \
  _(CONV,	N , ref, lit) \
  _(TOBIT,	N , ref, ref) \
  _(TOSTR,	N , ref, lit) \
  _(STRTO,	N , ref, ___) \
  \
  /* Calls. */ \
  _(CALLN,	NW, ref, lit) \
  _(CALLA,	AW, ref, lit) \
  _(CALLL,	LW, ref, lit) \
  _(CALLS,	S , ref, lit) \
  _(CALLXS,	S , ref, ref) \
  _(CARG,	N , ref, ref) \
  \
  /* End of list. */

/* IR opcodes (max. 256). */
typedef enum {
#define IRENUM(name, m, m1, m2)	IR_##name,
IRDEF(IRENUM)
#undef IRENUM
  IR__MAX
} IROp;

/* Stored opcode. */
typedef uint8_t IROp1;

LJ_STATIC_ASSERT(((int)IR_EQ^1) == (int)IR_NE);
LJ_STATIC_ASSERT(((int)IR_LT^1) == (int)IR_GE);
LJ_STATIC_ASSERT(((int)IR_LE^1) == (int)IR_GT);
LJ_STATIC_ASSERT(((int)IR_LT^3) == (int)IR_GT);
LJ_STATIC_ASSERT(((int)IR_LT^4) == (int)IR_ULT);

/* Delta between xLOAD and xSTORE. */
#define IRDELTA_L2S		((int)IR_ASTORE - (int)IR_ALOAD)

LJ_STATIC_ASSERT((int)IR_HLOAD + IRDELTA_L2S == (int)IR_HSTORE);
LJ_STATIC_ASSERT((int)IR_ULOAD + IRDELTA_L2S == (int)IR_USTORE);
LJ_STATIC_ASSERT((int)IR_FLOAD + IRDELTA_L2S == (int)IR_FSTORE);
LJ_STATIC_ASSERT((int)IR_XLOAD + IRDELTA_L2S == (int)IR_XSTORE);

/* -- Named IR literals --------------------------------------------------- */

/* FPMATH sub-functions. ORDER FPM. */
#define IRFPMDEF(_) \
  _(FLOOR) _(CEIL) _(TRUNC)  /* Must be first and in this order. */ \
  _(SQRT) _(LOG) _(LOG2) \
  _(OTHER)

typedef enum {
#define FPMENUM(name)		IRFPM_##name,
IRFPMDEF(FPMENUM)
#undef FPMENUM
  IRFPM__MAX
} IRFPMathOp;

/* FLOAD fields. */
#define IRFLDEF(_) \
  _(STR_LEN,	offsetof(GCstr, len)) \
  _(FUNC_ENV,	offsetof(GCfunc, l.env)) \
  _(FUNC_PC,	offsetof(GCfunc, l.pc)) \
  _(FUNC_FFID,	offsetof(GCfunc, l.ffid)) \
  _(THREAD_ENV,	offsetof(lua_State, env)) \
  _(TAB_META,	offsetof(GCtab, metatable)) \
  _(TAB_ARRAY,	offsetof(GCtab, array)) \
  _(TAB_NODE,	offsetof(GCtab, node)) \
  _(TAB_ASIZE,	offsetof(GCtab, asize)) \
  _(TAB_HMASK,	offsetof(GCtab, hmask)) \
  _(TAB_NOMM,	offsetof(GCtab, nomm)) \
  _(UDATA_META,	offsetof(GCudata, metatable)) \
  _(UDATA_UDTYPE, offsetof(GCudata, udtype)) \
  _(UDATA_FILE,	sizeof(GCudata)) \
  _(SBUF_W,	sizeof(GCudata) + offsetof(SBufExt, w)) \
  _(SBUF_E,	sizeof(GCudata) + offsetof(SBufExt, e)) \
  _(SBUF_B,	sizeof(GCudata) + offsetof(SBufExt, b)) \
  _(SBUF_L,	sizeof(GCudata) + offsetof(SBufExt, L)) \
  _(SBUF_REF,	sizeof(GCudata) + offsetof(SBufExt, cowref)) \
  _(SBUF_R,	sizeof(GCudata) + offsetof(SBufExt, r)) \
  _(CDATA_CTYPEID, offsetof(GCcdata, ctypeid)) \
  _(CDATA_PTR,	sizeof(GCcdata)) \
  _(CDATA_INT,	sizeof(GCcdata)) \
  _(CDATA_INT64, sizeof(GCcdata)) \
  _(CDATA_INT64_4, sizeof(GCcdata) + 4)

typedef enum {
#define FLENUM(name, ofs)	IRFL_##name,
IRFLDEF(FLENUM)
#undef FLENUM
  IRFL__MAX
} IRFieldID;

/* TMPREF mode bits, stored in op2. */
#define IRTMPREF_IN1		0x01	/* First input value. */
#define IRTMPREF_OUT1		0x02	/* First output value. */
#define IRTMPREF_OUT2		0x04	/* Second output value. */

/* SLOAD mode bits, stored in op2. */
#define IRSLOAD_PARENT		0x01	/* Coalesce with parent trace. */
#define IRSLOAD_FRAME		0x02	/* Load 32 bits of ftsz. */
#define IRSLOAD_TYPECHECK	0x04	/* Needs type check. */
#define IRSLOAD_CONVERT		0x08	/* Number to integer conversion. */
#define IRSLOAD_READONLY	0x10	/* Read-only, omit slot store. */
#define IRSLOAD_INHERIT		0x20	/* Inherited by exits/side traces. */
#define IRSLOAD_KEYINDEX	0x40	/* Table traversal key index. */

/* XLOAD mode bits, stored in op2. */
#define IRXLOAD_READONLY	0x01	/* Load from read-only data. */
#define IRXLOAD_VOLATILE	0x02	/* Load from volatile data. */
#define IRXLOAD_UNALIGNED	0x04	/* Unaligned load. */

/* BUFHDR mode, stored in op2. */
#define IRBUFHDR_RESET		0	/* Reset buffer. */
#define IRBUFHDR_APPEND		1	/* Append to buffer. */
#define IRBUFHDR_WRITE		2	/* Write to string buffer. */

/* CONV mode, stored in op2. */
#define IRCONV_SRCMASK		0x001f	/* Source IRType. */
#define IRCONV_DSTMASK		0x03e0	/* Dest. IRType (also in ir->t). */
#define IRCONV_DSH		5
#define IRCONV_NUM_INT		((IRT_NUM<<IRCONV_DSH)|IRT_INT)
#define IRCONV_INT_NUM		((IRT_INT<<IRCONV_DSH)|IRT_NUM)
#define IRCONV_SEXT		0x0800	/* Sign-extend integer to integer. */
#define IRCONV_MODEMASK		0x0fff
#define IRCONV_CONVMASK		0xf000
#define IRCONV_CSH		12
/* Number to integer conversion mode. Ordered by strength of the checks. */
#define IRCONV_TOBIT  (0<<IRCONV_CSH)	/* None. Cache only: TOBIT conv. */
#define IRCONV_ANY    (1<<IRCONV_CSH)	/* Any FP number is ok. */
#define IRCONV_INDEX  (2<<IRCONV_CSH)	/* Check + special backprop rules. */
#define IRCONV_CHECK  (3<<IRCONV_CSH)	/* Number checked for integerness. */
#define IRCONV_NONE   IRCONV_ANY	/* INT|*64 no conv, but change type. */

/* TOSTR mode, stored in op2. */
#define IRTOSTR_INT		0	/* Convert integer to string. */
#define IRTOSTR_NUM		1	/* Convert number to string. */
#define IRTOSTR_CHAR		2	/* Convert char value to string. */

/* -- IR operands --------------------------------------------------------- */

/* IR operand mode (2 bit). */
typedef enum {
  IRMref,		/* IR reference. */
  IRMlit,		/* 16 bit unsigned literal. */
  IRMcst,		/* Constant literal: i, gcr or ptr. */
  IRMnone		/* Unused operand. */
} IRMode;
#define IRM___		IRMnone

/* Mode bits: Commutative, {Normal/Ref, Alloc, Load, Store}, Non-weak guard. */
#define IRM_C			0x10

#define IRM_N			0x00
#define IRM_R			IRM_N
#define IRM_A			0x20
#define IRM_L			0x40
#define IRM_S			0x60

#define IRM_W			0x80

#define IRM_NW			(IRM_N|IRM_W)
#define IRM_CW			(IRM_C|IRM_W)
#define IRM_AW			(IRM_A|IRM_W)
#define IRM_LW			(IRM_L|IRM_W)

#define irm_op1(m)		((IRMode)((m)&3))
#define irm_op2(m)		((IRMode)(((m)>>2)&3))
#define irm_iscomm(m)		((m) & IRM_C)
#define irm_kind(m)		((m) & IRM_S)

#define IRMODE(name, m, m1, m2)	(((IRM##m1)|((IRM##m2)<<2)|(IRM_##m))^IRM_W),

LJ_DATA const uint8_t lj_ir_mode[IR__MAX+1];

/* -- IR instruction types ------------------------------------------------ */

#define IRTSIZE_PGC		(LJ_GC64 ? 8 : 4)

/* Map of itypes to non-negative numbers and their sizes. ORDER LJ_T.
** LJ_TUPVAL/LJ_TTRACE never appear in a TValue. Use these itypes for
** IRT_P32 and IRT_P64, which never escape the IR.
** The various integers are only used in the IR and can only escape to
** a TValue after implicit or explicit conversion. Their types must be
** contiguous and next to IRT_NUM (see the typerange macros below).
*/
#define IRTDEF(_) \
  _(NIL, 4) _(FALSE, 4) _(TRUE, 4) _(LIGHTUD, LJ_64 ? 8 : 4) \
  _(S