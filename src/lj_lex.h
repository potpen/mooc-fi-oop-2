/*
** Lexical analyzer.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

#ifndef _LJ_LEX_H
#define _LJ_LEX_H

#include <stdarg.h>

#include "lj_obj.h"
#include "lj_err.h"

/* Lua lexer tokens. */
#define TKDEF(_, __) \
  _(and) _(break) _(do) _(else) _(elseif) _(end) _(false) \
  _(for) _(function) _(goto) _(if) _(in) _(local) _(nil) _(not) _(or) \
  _(repeat) _(return) _(then) _(true) _(until) _(while) \
  __(concat, ..) __(dots, ...) __(eq, ==) __(ge, >=) __(le, <=) __(ne, ~=) \
  __(label, ::) __(number, <number>) __(name, <name>) __(string, <string>) \
  __(eof, <eof>)

enum {
  TK_OFS = 256,
#define TKENUM1(name)		TK_##name,
#define TKENUM2(name, sym)	TK_##name,
TKDEF(TKENUM1, TKENUM2)
#undef TKENUM1
#undef TKENUM2
  TK_RESERVED = TK_while - TK_OFS
};

typedef int LexChar;	/* Lexical character. Unsigned ext. from char. */
typedef int LexToken;	/* Lexical token. */

/* Combined bytecode ins/line. Only used during bytecode generation. */
typedef struct BCInsLine {
  BCIns ins;		/* Bytecode instruction. */
  BCLine line;		/* Line number for this bytecode. */
} BCInsLine;

/* Info for local variables. Only used during bytecode generation. */
typedef struct VarInfo {
  GCRef name;		/* Local variable name or goto/label name. */
  BCPos startpc;	/* First point where the local variable is active. */
  BCPos endpc;		/* First point where the local variable is dead. */
  uint8_t slot;		/* Variable slot. */
  uint8_t info;		/* Variable/goto/label info. */
} VarInfo;

/* Lua lexer state. */
typedef struct LexState {
  struct FuncState *fs;	/* Current FuncState. Defined in lj_parse.c. */
  struct lua_State *L;	/* Lua state. */
  TValue tokval;	/* Current token value. */
  TValue lookaheadval;	/* Lookahead token value. */
  const char *p;	/* Current position in input buffer. */
  const char *pe;	/* End of input buffer. */
  LexChar c;		/* Current character. */
  LexToken tok;		/* Current token. */
  LexToken lookahead;	/* Lookahead token. */
  SBuf sb;		/* String buffer for tokens. */
  lua_Reader rfunc;	/* Reader callback. */
  void *rdata;		/* Reader callback data. */
  BCLine linenumber;	/* I