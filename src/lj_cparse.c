
/*
** C declaration parser.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

#include "lj_obj.h"

#if LJ_HASFFI

#include "lj_gc.h"
#include "lj_err.h"
#include "lj_buf.h"
#include "lj_ctype.h"
#include "lj_cparse.h"
#include "lj_frame.h"
#include "lj_vm.h"
#include "lj_char.h"
#include "lj_strscan.h"
#include "lj_strfmt.h"

/*
** Important note: this is NOT a validating C parser! This is a minimal
** C declaration parser, solely for use by the LuaJIT FFI.
**
** It ought to return correct results for properly formed C declarations,
** but it may accept some invalid declarations, too (and return nonsense).
** Also, it shows rather generic error messages to avoid unnecessary bloat.
** If in doubt, please check the input against your favorite C compiler.
*/

#ifdef LUA_USE_ASSERT
#define lj_assertCP(c, ...)	(lj_assertG_(G(cp->L), (c), __VA_ARGS__))
#else
#define lj_assertCP(c, ...)	((void)cp)
#endif

/* -- Miscellaneous ------------------------------------------------------- */

/* Match string against a C literal. */
#define cp_str_is(str, k) \
  ((str)->len == sizeof(k)-1 && !memcmp(strdata(str), k, sizeof(k)-1))

/* Check string against a linear list of matches. */
int lj_cparse_case(GCstr *str, const char *match)
{
  MSize len;
  int n;
  for  (n = 0; (len = (MSize)*match++); n++, match += len) {
    if (str->len == len && !memcmp(match, strdata(str), len))
      return n;
  }
  return -1;
}

/* -- C lexer ------------------------------------------------------------- */

/* C lexer token names. */
static const char *const ctoknames[] = {
#define CTOKSTR(name, str)	str,
CTOKDEF(CTOKSTR)
#undef CTOKSTR
  NULL
};

/* Forward declaration. */
LJ_NORET static void cp_err(CPState *cp, ErrMsg em);

static const char *cp_tok2str(CPState *cp, CPToken tok)
{
  lj_assertCP(tok < CTOK_FIRSTDECL, "bad CPToken %d", tok);
  if (tok > CTOK_OFS)
    return ctoknames[tok-CTOK_OFS-1];
  else if (!lj_char_iscntrl(tok))
    return lj_strfmt_pushf(cp->L, "%c", tok);
  else
    return lj_strfmt_pushf(cp->L, "char(%d)", tok);
}

/* End-of-line? */
static LJ_AINLINE int cp_iseol(CPChar c)
{
  return (c == '\n' || c == '\r');
}

/* Peek next raw character. */
static LJ_AINLINE CPChar cp_rawpeek(CPState *cp)
{
  return (CPChar)(uint8_t)(*cp->p);
}

static LJ_NOINLINE CPChar cp_get_bs(CPState *cp);

/* Get next character. */
static LJ_AINLINE CPChar cp_get(CPState *cp)
{
  cp->c = (CPChar)(uint8_t)(*cp->p++);
  if (LJ_LIKELY(cp->c != '\\')) return cp->c;
  return cp_get_bs(cp);
}

/* Transparently skip backslash-escaped line breaks. */
static LJ_NOINLINE CPChar cp_get_bs(CPState *cp)
{
  CPChar c2, c = cp_rawpeek(cp);
  if (!cp_iseol(c)) return cp->c;
  cp->p++;
  c2 = cp_rawpeek(cp);
  if (cp_iseol(c2) && c2 != c) cp->p++;
  cp->linenumber++;
  return cp_get(cp);
}

/* Save character in buffer. */
static LJ_AINLINE void cp_save(CPState *cp, CPChar c)
{
  lj_buf_putb(&cp->sb, c);
}

/* Skip line break. Handles "\n", "\r", "\r\n" or "\n\r". */
static void cp_newline(CPState *cp)
{
  CPChar c = cp_rawpeek(cp);
  if (cp_iseol(c) && c != cp->c) cp->p++;
  cp->linenumber++;
}

LJ_NORET static void cp_errmsg(CPState *cp, CPToken tok, ErrMsg em, ...)
{
  const char *msg, *tokstr;
  lua_State *L;
  va_list argp;
  if (tok == 0) {
    tokstr = NULL;
  } else if (tok == CTOK_IDENT || tok == CTOK_INTEGER || tok == CTOK_STRING ||
	     tok >= CTOK_FIRSTDECL) {
    if (cp->sb.w == cp->sb.b) cp_save(cp, '$');
    cp_save(cp, '\0');
    tokstr = cp->sb.b;
  } else {
    tokstr = cp_tok2str(cp, tok);
  }
  L = cp->L;
  va_start(argp, em);
  msg = lj_strfmt_pushvf(L, err2msg(em), argp);
  va_end(argp);
  if (tokstr)
    msg = lj_strfmt_pushf(L, err2msg(LJ_ERR_XNEAR), msg, tokstr);
  if (cp->linenumber > 1)
    msg = lj_strfmt_pushf(L, "%s at line %d", msg, cp->linenumber);
  lj_err_callermsg(L, msg);
}

LJ_NORET LJ_NOINLINE static void cp_err_token(CPState *cp, CPToken tok)
{
  cp_errmsg(cp, cp->tok, LJ_ERR_XTOKEN, cp_tok2str(cp, tok));
}

LJ_NORET LJ_NOINLINE static void cp_err_badidx(CPState *cp, CType *ct)
{
  GCstr *s = lj_ctype_repr(cp->cts->L, ctype_typeid(cp->cts, ct), NULL);
  cp_errmsg(cp, 0, LJ_ERR_FFI_BADIDX, strdata(s));
}

LJ_NORET LJ_NOINLINE static void cp_err(CPState *cp, ErrMsg em)
{
  cp_errmsg(cp, 0, em);
}

/* -- Main lexical scanner ------------------------------------------------ */

/* Parse number literal. Only handles int32_t/uint32_t right now. */
static CPToken cp_number(CPState *cp)
{
  StrScanFmt fmt;
  TValue o;
  do { cp_save(cp, cp->c); } while (lj_char_isident(cp_get(cp)));
  cp_save(cp, '\0');
  fmt = lj_strscan_scan((const uint8_t *)(cp->sb.b), sbuflen(&cp->sb)-1,
			&o, STRSCAN_OPT_C);
  if (fmt == STRSCAN_INT) cp->val.id = CTID_INT32;
  else if (fmt == STRSCAN_U32) cp->val.id = CTID_UINT32;
  else if (!(cp->mode & CPARSE_MODE_SKIP))
    cp_errmsg(cp, CTOK_INTEGER, LJ_ERR_XNUMBER);
  cp->val.u32 = (uint32_t)o.i;
  return CTOK_INTEGER;
}

/* Parse identifier or keyword. */
static CPToken cp_ident(CPState *cp)
{
  do { cp_save(cp, cp->c); } while (lj_char_isident(cp_get(cp)));
  cp->str = lj_buf_str(cp->L, &cp->sb);
  cp->val.id = lj_ctype_getname(cp->cts, &cp->ct, cp->str, cp->tmask);
  if (ctype_type(cp->ct->info) == CT_KW)
    return ctype_cid(cp->ct->info);
  return CTOK_IDENT;
}

/* Parse parameter. */
static CPToken cp_param(CPState *cp)
{
  CPChar c = cp_get(cp);
  TValue *o = cp->param;
  if (lj_char_isident(c) || c == '$')  /* Reserve $xyz for future extensions. */
    cp_errmsg(cp, c, LJ_ERR_XSYNTAX);
  if (!o || o >= cp->L->top)
    cp_err(cp, LJ_ERR_FFI_NUMPARAM);
  cp->param = o+1;
  if (tvisstr(o)) {
    cp->str = strV(o);
    cp->val.id = 0;
    cp->ct = &cp->cts->tab[0];
    return CTOK_IDENT;
  } else if (tvisnumber(o)) {
    cp->val.i32 = numberVint(o);
    cp->val.id = CTID_INT32;
    return CTOK_INTEGER;
  } else {
    GCcdata *cd;
    if (!tviscdata(o))
      lj_err_argtype(cp->L, (int)(o-cp->L->base)+1, "type parameter");
    cd = cdataV(o);
    if (cd->ctypeid == CTID_CTYPEID)
      cp->val.id = *(CTypeID *)cdataptr(cd);
    else
      cp->val.id = cd->ctypeid;
    return '$';
  }
}

/* Parse string or character constant. */
static CPToken cp_string(CPState *cp)
{
  CPChar delim = cp->c;
  cp_get(cp);
  while (cp->c != delim) {
    CPChar c = cp->c;
    if (c == '\0') cp_errmsg(cp, CTOK_EOF, LJ_ERR_XSTR);
    if (c == '\\') {
      c = cp_get(cp);
      switch (c) {
      case '\0': cp_errmsg(cp, CTOK_EOF, LJ_ERR_XSTR); break;
      case 'a': c = '\a'; break;
      case 'b': c = '\b'; break;
      case 'f': c = '\f'; break;
      case 'n': c = '\n'; break;
      case 'r': c = '\r'; break;
      case 't': c = '\t'; break;
      case 'v': c = '\v'; break;
      case 'e': c = 27; break;
      case 'x':
	c = 0;
	while (lj_char_isxdigit(cp_get(cp)))
	  c = (c<<4) + (lj_char_isdigit(cp->c) ? cp->c-'0' : (cp->c&15)+9);
	cp_save(cp, (c & 0xff));
	continue;
      default:
	if (lj_char_isdigit(c)) {
	  c -= '0';
	  if (lj_char_isdigit(cp_get(cp))) {
	    c = c*8 + (cp->c - '0');
	    if (lj_char_isdigit(cp_get(cp))) {
	      c = c*8 + (cp->c - '0');
	      cp_get(cp);
	    }
	  }
	  cp_save(cp, (c & 0xff));
	  continue;
	}
	break;
      }
    }
    cp_save(cp, c);
    cp_get(cp);
  }
  cp_get(cp);
  if (delim == '"') {
    cp->str = lj_buf_str(cp->L, &cp->sb);
    return CTOK_STRING;
  } else {
    if (sbuflen(&cp->sb) != 1) cp_err_token(cp, '\'');
    cp->val.i32 = (int32_t)(char)*cp->sb.b;
    cp->val.id = CTID_INT32;
    return CTOK_INTEGER;
  }
}

/* Skip C comment. */
static void cp_comment_c(CPState *cp)
{
  do {
    if (cp_get(cp) == '*') {
      do {
	if (cp_get(cp) == '/') { cp_get(cp); return; }
      } while (cp->c == '*');
    }
    if (cp_iseol(cp->c)) cp_newline(cp);
  } while (cp->c != '\0');
}

/* Skip C++ comment. */
static void cp_comment_cpp(CPState *cp)
{
  while (!cp_iseol(cp_get(cp)) && cp->c != '\0')
    ;
}

/* Lexical scanner for C. Only a minimal subset is implemented. */
static CPToken cp_next_(CPState *cp)
{
  lj_buf_reset(&cp->sb);
  for (;;) {
    if (lj_char_isident(cp->c))
      return lj_char_isdigit(cp->c) ? cp_number(cp) : cp_ident(cp);
    switch (cp->c) {
    case '\n': case '\r': cp_newline(cp);  /* fallthrough. */
    case ' ': case '\t': case '\v': case '\f': cp_get(cp); break;
    case '"': case '\'': return cp_string(cp);
    case '/':
      if (cp_get(cp) == '*') cp_comment_c(cp);
      else if (cp->c == '/') cp_comment_cpp(cp);
      else return '/';
      break;
    case '|':
      if (cp_get(cp) != '|') return '|';
      cp_get(cp); return CTOK_OROR;
    case '&':
      if (cp_get(cp) != '&') return '&';
      cp_get(cp); return CTOK_ANDAND;
    case '=':
      if (cp_get(cp) != '=') return '=';
      cp_get(cp); return CTOK_EQ;
    case '!':
      if (cp_get(cp) != '=') return '!';
      cp_get(cp); return CTOK_NE;
    case '<':
      if (cp_get(cp) == '=') { cp_get(cp); return CTOK_LE; }
      else if (cp->c == '<') { cp_get(cp); return CTOK_SHL; }
      return '<';
    case '>':
      if (cp_get(cp) == '=') { cp_get(cp); return CTOK_GE; }
      else if (cp->c == '>') { cp_get(cp); return CTOK_SHR; }
      return '>';
    case '-':
      if (cp_get(cp) != '>') return '-';
      cp_get(cp); return CTOK_DEREF;
    case '$':
      return cp_param(cp);
    case '\0': return CTOK_EOF;
    default: { CPToken c = cp->c; cp_get(cp); return c; }
    }
  }
}

static LJ_NOINLINE CPToken cp_next(CPState *cp)
{
  return (cp->tok = cp_next_(cp));
}

/* -- C parser ------------------------------------------------------------ */

/* Namespaces for resolving identifiers. */
#define CPNS_DEFAULT \
  ((1u<<CT_KW)|(1u<<CT_TYPEDEF)|(1u<<CT_FUNC)|(1u<<CT_EXTERN)|(1u<<CT_CONSTVAL))
#define CPNS_STRUCT	((1u<<CT_KW)|(1u<<CT_STRUCT)|(1u<<CT_ENUM))

typedef CTypeID CPDeclIdx;	/* Index into declaration stack. */
typedef uint32_t CPscl;		/* Storage class flags. */

/* Type declaration context. */
typedef struct CPDecl {
  CPDeclIdx top;	/* Top of declaration stack. */
  CPDeclIdx pos;	/* Insertion position in declaration chain. */
  CPDeclIdx specpos;	/* Saved position for declaration specifier. */
  uint32_t mode;	/* Declarator mode. */
  CPState *cp;		/* C parser state. */
  GCstr *name;		/* Name of declared identifier (if direct). */
  GCstr *redir;		/* Redirected symbol name. */
  CTypeID nameid;	/* Existing typedef for declared identifier. */
  CTInfo attr;		/* Attributes. */
  CTInfo fattr;		/* Function attributes. */
  CTInfo specattr;	/* Saved attributes. */
  CTInfo specfattr;	/* Saved function attributes. */
  CTSize bits;		/* Field size in bits (if any). */
  CType stack[CPARSE_MAX_DECLSTACK];  /* Type declaration stack. */
} CPDecl;

/* Forward declarations. */
static CPscl cp_decl_spec(CPState *cp, CPDecl *decl, CPscl scl);
static void cp_declarator(CPState *cp, CPDecl *decl);
static CTypeID cp_decl_abstract(CPState *cp);

/* Initialize C parser state. Caller must set up: L, p, srcname, mode. */
static void cp_init(CPState *cp)
{
  cp->linenumber = 1;
  cp->depth = 0;
  cp->curpack = 0;
  cp->packstack[0] = 255;
  lj_buf_init(cp->L, &cp->sb);
  lj_assertCP(cp->p != NULL, "uninitialized cp->p");
  cp_get(cp);  /* Read-ahead first char. */
  cp->tok = 0;
  cp->tmask = CPNS_DEFAULT;
  cp_next(cp);  /* Read-ahead first token. */
}

/* Cleanup C parser state. */
static void cp_cleanup(CPState *cp)
{
  global_State *g = G(cp->L);
  lj_buf_free(g, &cp->sb);
}

/* Check and consume optional token. */
static int cp_opt(CPState *cp, CPToken tok)
{
  if (cp->tok == tok) { cp_next(cp); return 1; }
  return 0;
}

/* Check and consume token. */
static void cp_check(CPState *cp, CPToken tok)
{
  if (cp->tok != tok) cp_err_token(cp, tok);
  cp_next(cp);
}

/* Check if the next token may start a type declaration. */
static int cp_istypedecl(CPState *cp)
{
  if (cp->tok >= CTOK_FIRSTDECL && cp->tok <= CTOK_LASTDECL) return 1;
  if (cp->tok == CTOK_IDENT && ctype_istypedef(cp->ct->info)) return 1;
  if (cp->tok == '$') return 1;
  return 0;
}

/* -- Constant expression evaluator --------------------------------------- */

/* Forward declarations. */
static void cp_expr_unary(CPState *cp, CPValue *k);
static void cp_expr_sub(CPState *cp, CPValue *k, int pri);

/* Please note that type handling is very weak here. Most ops simply
** assume integer operands. Accessors are only needed to compute types and
** return synthetic values. The only purpose of the expression evaluator
** is to compute the values of constant expressions one would typically
** find in C header files. And again: this is NOT a validating C parser!
*/

/* Parse comma separated expression and return last result. */
static void cp_expr_comma(CPState *cp, CPValue *k)
{
  do { cp_expr_sub(cp, k, 0); } while (cp_opt(cp, ','));
}

/* Parse sizeof/alignof operator. */
static void cp_expr_sizeof(CPState *cp, CPValue *k, int wantsz)
{
  CTSize sz;
  CTInfo info;
  if (cp_opt(cp, '(')) {
    if (cp_istypedecl(cp))
      k->id = cp_decl_abstract(cp);
    else
      cp_expr_comma(cp, k);
    cp_check(cp, ')');
  } else {
    cp_expr_unary(cp, k);
  }
  info = lj_ctype_info_raw(cp->cts, k->id, &sz);
  if (wantsz) {
    if (sz != CTSIZE_INVALID)
      k->u32 = sz;
    else if (k->id != CTID_A_CCHAR)  /* Special case for sizeof("string"). */
      cp_err(cp, LJ_ERR_FFI_INVSIZE);
  } else {
    k->u32 = 1u << ctype_align(info);
  }
  k->id = CTID_UINT32;  /* Really size_t. */
}

/* Parse prefix operators. */
static void cp_expr_prefix(CPState *cp, CPValue *k)
{
  if (cp->tok == CTOK_INTEGER) {
    *k = cp->val; cp_next(cp);
  } else if (cp_opt(cp, '+')) {
    cp_expr_unary(cp, k);  /* Nothing to do (well, integer promotion). */
  } else if (cp_opt(cp, '-')) {
    cp_expr_unary(cp, k); k->i32 = (int32_t)(~(uint32_t)k->i32+1);
  } else if (cp_opt(cp, '~')) {
    cp_expr_unary(cp, k); k->i32 = ~k->i32;
  } else if (cp_opt(cp, '!')) {
    cp_expr_unary(cp, k); k->i32 = !k->i32; k->id = CTID_INT32;
  } else if (cp_opt(cp, '(')) {
    if (cp_istypedecl(cp)) {  /* Cast operator. */
      CTypeID id = cp_decl_abstract(cp);
      cp_check(cp, ')');
      cp_expr_unary(cp, k);
      k->id = id;  /* No conversion performed. */
    } else {  /* Sub-expression. */
      cp_expr_comma(cp, k);
      cp_check(cp, ')');
    }
  } else if (cp_opt(cp, '*')) {  /* Indirection. */
    CType *ct;
    cp_expr_unary(cp, k);
    ct = lj_ctype_rawref(cp->cts, k->id);
    if (!ctype_ispointer(ct->info))
      cp_err_badidx(cp, ct);
    k->u32 = 0; k->id = ctype_cid(ct->info);
  } else if (cp_opt(cp, '&')) {  /* Address operator. */
    cp_expr_unary(cp, k);
    k->id = lj_ctype_intern(cp->cts, CTINFO(CT_PTR, CTALIGN_PTR+k->id),
			    CTSIZE_PTR);
  } else if (cp_opt(cp, CTOK_SIZEOF)) {
    cp_expr_sizeof(cp, k, 1);
  } else if (cp_opt(cp, CTOK_ALIGNOF)) {
    cp_expr_sizeof(cp, k, 0);
  } else if (cp->tok == CTOK_IDENT) {
    if (ctype_type(cp->ct->info) == CT_CONSTVAL) {
      k->u32 = cp->ct->size; k->id = ctype_cid(cp->ct->info);
    } else if (ctype_type(cp->ct->info) == CT_EXTERN) {
      k->u32 = cp->val.id; k->id = ctype_cid(cp->ct->info);
    } else if (ctype_type(cp->ct->info) == CT_FUNC) {
      k->u32 = cp->val.id; k->id = cp->val.id;
    } else {
      goto err_expr;
    }
    cp_next(cp);
  } else if (cp->tok == CTOK_STRING) {
    CTSize sz = cp->str->len;
    while (cp_next(cp) == CTOK_STRING)
      sz += cp->str->len;
    k->u32 = sz + 1;
    k->id = CTID_A_CCHAR;
  } else {
  err_expr:
    cp_errmsg(cp, cp->tok, LJ_ERR_XSYMBOL);
  }
}

/* Parse postfix operators. */
static void cp_expr_postfix(CPState *cp, CPValue *k)
{
  for (;;) {
    CType *ct;
    if (cp_opt(cp, '[')) {  /* Array/pointer index. */
      CPValue k2;
      cp_expr_comma(cp, &k2);
      ct = lj_ctype_rawref(cp->cts, k->id);
      if (!ctype_ispointer(ct->info)) {
	ct = lj_ctype_rawref(cp->cts, k2.id);
	if (!ctype_ispointer(ct->info))
	  cp_err_badidx(cp, ct);
      }
      cp_check(cp, ']');
      k->u32 = 0;
    } else if (cp->tok == '.' || cp->tok == CTOK_DEREF) {  /* Struct deref. */
      CTSize ofs;
      CType *fct;
      ct = lj_ctype_rawref(cp->cts, k->id);
      if (cp->tok == CTOK_DEREF) {
	if (!ctype_ispointer(ct->info))
	  cp_err_badidx(cp, ct);
	ct = lj_ctype_rawref(cp->cts, ctype_cid(ct->info));
      }
      cp_next(cp);
      if (cp->tok != CTOK_IDENT) cp_err_token(cp, CTOK_IDENT);
      if (!ctype_isstruct(ct->info) || ct->size == CTSIZE_INVALID ||
	  !(fct = lj_ctype_getfield(cp->cts, ct, cp->str, &ofs)) ||
	  ctype_isbitfield(fct->info)) {
	GCstr *s = lj_ctype_repr(cp->cts->L, ctype_typeid(cp->cts, ct), NULL);
	cp_errmsg(cp, 0, LJ_ERR_FFI_BADMEMBER, strdata(s), strdata(cp->str));
      }
      ct = fct;
      k->u32 = ctype_isconstval(ct->info) ? ct->size : 0;
      cp_next(cp);
    } else {
      return;
    }
    k->id = ctype_cid(ct->info);
  }
}

/* Parse infix operators. */
static void cp_expr_infix(CPState *cp, CPValue *k, int pri)
{
  CPValue k2;
  k2.u32 = 0; k2.id = 0;  /* Silence the compiler. */
  for (;;) {
    switch (pri) {
    case 0:
      if (cp_opt(cp, '?')) {
	CPValue k3;
	cp_expr_comma(cp, &k2);  /* Right-associative. */
	cp_check(cp, ':');
	cp_expr_sub(cp, &k3, 0);
	k->u32 = k->u32 ? k2.u32 : k3.u32;
	k->id = k2.id > k3.id ? k2.id : k3.id;
	continue;
      }
      /* fallthrough */
    case 1:
      if (cp_opt(cp, CTOK_OROR)) {
	cp_expr_sub(cp, &k2, 2); k->i32 = k->u32 || k2.u32; k->id = CTID_INT32;
	continue;
      }
      /* fallthrough */
    case 2:
      if (cp_opt(cp, CTOK_ANDAND)) {
	cp_expr_sub(cp, &k2, 3); k->i32 = k->u32 && k2.u32; k->id = CTID_INT32;
	continue;
      }
      /* fallthrough */
    case 3:
      if (cp_opt(cp, '|')) {
	cp_expr_sub(cp, &k2, 4); k->u32 = k->u32 | k2.u32; goto arith_result;
      }
      /* fallthrough */
    case 4:
      if (cp_opt(cp, '^')) {
	cp_expr_sub(cp, &k2, 5); k->u32 = k->u32 ^ k2.u32; goto arith_result;
      }
      /* fallthrough */
    case 5:
      if (cp_opt(cp, '&')) {
	cp_expr_sub(cp, &k2, 6); k->u32 = k->u32 & k2.u32; goto arith_result;
      }
      /* fallthrough */
    case 6:
      if (cp_opt(cp, CTOK_EQ)) {
	cp_expr_sub(cp, &k2, 7); k->i32 = k->u32 == k2.u32; k->id = CTID_INT32;
	continue;
      } else if (cp_opt(cp, CTOK_NE)) {
	cp_expr_sub(cp, &k2, 7); k->i32 = k->u32 != k2.u32; k->id = CTID_INT32;
	continue;
      }
      /* fallthrough */
    case 7:
      if (cp_opt(cp, '<')) {
	cp_expr_sub(cp, &k2, 8);
	if (k->id == CTID_INT32 && k2.id == CTID_INT32)
	  k->i32 = k->i32 < k2.i32;
	else
	  k->i32 = k->u32 < k2.u32;
	k->id = CTID_INT32;
	continue;
      } else if (cp_opt(cp, '>')) {
	cp_expr_sub(cp, &k2, 8);
	if (k->id == CTID_INT32 && k2.id == CTID_INT32)
	  k->i32 = k->i32 > k2.i32;
	else
	  k->i32 = k->u32 > k2.u32;
	k->id = CTID_INT32;
	continue;
      } else if (cp_opt(cp, CTOK_LE)) {
	cp_expr_sub(cp, &k2, 8);
	if (k->id == CTID_INT32 && k2.id == CTID_INT32)
	  k->i32 = k->i32 <= k2.i32;
	else
	  k->i32 = k->u32 <= k2.u32;
	k->id = CTID_INT32;
	continue;
      } else if (cp_opt(cp, CTOK_GE)) {
	cp_expr_sub(cp, &k2, 8);
	if (k->id == CTID_INT32 && k2.id == CTID_INT32)
	  k->i32 = k->i32 >= k2.i32;
	else
	  k->i32 = k->u32 >= k2.u32;
	k->id = CTID_INT32;
	continue;
      }
      /* fallthrough */
    case 8:
      if (cp_opt(cp, CTOK_SHL)) {
	cp_expr_sub(cp, &k2, 9); k->u32 = k->u32 << k2.u32;
	continue;
      } else if (cp_opt(cp, CTOK_SHR)) {
	cp_expr_sub(cp, &k2, 9);
	if (k->id == CTID_INT32)
	  k->i32 = k->i32 >> k2.i32;
	else
	  k->u32 = k->u32 >> k2.u32;
	continue;
      }
      /* fallthrough */
    case 9:
      if (cp_opt(cp, '+')) {
	cp_expr_sub(cp, &k2, 10); k->u32 = k->u32 + k2.u32;
      arith_result:
	if (k2.id > k->id) k->id = k2.id;  /* Trivial promotion to unsigned. */
	continue;
      } else if (cp_opt(cp, '-')) {
	cp_expr_sub(cp, &k2, 10); k->u32 = k->u32 - k2.u32; goto arith_result;
      }
      /* fallthrough */
    case 10:
      if (cp_opt(cp, '*')) {
	cp_expr_unary(cp, &k2); k->u32 = k->u32 * k2.u32; goto arith_result;
      } else if (cp_opt(cp, '/')) {
	cp_expr_unary(cp, &k2);
	if (k2.id > k->id) k->id = k2.id;  /* Trivial promotion to unsigned. */
	if (k2.u32 == 0 ||
	    (k->id == CTID_INT32 && k->u32 == 0x80000000u && k2.i32 == -1))
	  cp_err(cp, LJ_ERR_BADVAL);
	if (k->id == CTID_INT32)
	  k->i32 = k->i32 / k2.i32;
	else
	  k->u32 = k->u32 / k2.u32;
	continue;
      } else if (cp_opt(cp, '%')) {
	cp_expr_unary(cp, &k2);
	if (k2.id > k->id) k->id = k2.id;  /* Trivial promotion to unsigned. */
	if (k2.u32 == 0 ||
	    (k->id == CTID_INT32 && k->u32 == 0x80000000u && k2.i32 == -1))
	  cp_err(cp, LJ_ERR_BADVAL);
	if (k->id == CTID_INT32)
	  k->i32 = k->i32 % k2.i32;
	else
	  k->u32 = k->u32 % k2.u32;
	continue;
      }
    default:
      return;
    }
  }
}

/* Parse and evaluate unary expression. */
static void cp_expr_unary(CPState *cp, CPValue *k)
{
  if (++cp->depth > CPARSE_MAX_DECLDEPTH) cp_err(cp, LJ_ERR_XLEVELS);
  cp_expr_prefix(cp, k);
  cp_expr_postfix(cp, k);
  cp->depth--;
}

/* Parse and evaluate sub-expression. */
static void cp_expr_sub(CPState *cp, CPValue *k, int pri)
{
  cp_expr_unary(cp, k);
  cp_expr_infix(cp, k, pri);
}

/* Parse constant integer expression. */
static void cp_expr_kint(CPState *cp, CPValue *k)
{
  CType *ct;
  cp_expr_sub(cp, k, 0);
  ct = ctype_raw(cp->cts, k->id);
  if (!ctype_isinteger(ct->info)) cp_err(cp, LJ_ERR_BADVAL);
}

/* Parse (non-negative) size expression. */
static CTSize cp_expr_ksize(CPState *cp)
{
  CPValue k;
  cp_expr_kint(cp, &k);
  if (k.u32 >= 0x80000000u) cp_err(cp, LJ_ERR_FFI_INVSIZE);
  return k.u32;
}

/* -- Type declaration stack management ----------------------------------- */

/* Add declaration element behind the insertion position. */
static CPDeclIdx cp_add(CPDecl *decl, CTInfo info, CTSize size)
{
  CPDeclIdx top = decl->top;
  if (top >= CPARSE_MAX_DECLSTACK) cp_err(decl->cp, LJ_ERR_XLEVELS);
  decl->stack[top].info = info;
  decl->stack[top].size = size;
  decl->stack[top].sib = 0;
  setgcrefnull(decl->stack[top].name);
  decl->stack[top].next = decl->stack[decl->pos].next;
  decl->stack[decl->pos].next = (CTypeID1)top;
  decl->top = top+1;
  return top;
}

/* Push declaration element before the insertion position. */
static CPDeclIdx cp_push(CPDecl *decl, CTInfo info, CTSize size)
{
  return (decl->pos = cp_add(decl, info, size));
}

/* Push or merge attributes. */
static void cp_push_attributes(CPDecl *decl)
{
  CType *ct = &decl->stack[decl->pos];
  if (ctype_isfunc(ct->info)) {  /* Ok to modify in-place. */
#if LJ_TARGET_X86
    if ((decl->fattr & CTFP_CCONV))
      ct->info = (ct->info & (CTMASK_NUM|CTF_VARARG|CTMASK_CID)) +
		 (decl->fattr & ~CTMASK_CID);
#endif
  } else {
    if ((decl->attr & CTFP_ALIGNED) && !(decl->mode & CPARSE_MODE_FIELD))
      cp_push(decl, CTINFO(CT_ATTRIB, CTATTRIB(CTA_ALIGN)),
	      ctype_align(decl->attr));
  }
}

/* Push unrolled type to declaration stack and merge qualifiers. */
static void cp_push_type(CPDecl *decl, CTypeID id)
{
  CType *ct = ctype_get(decl->cp->cts, id);
  CTInfo info = ct->info;
  CTSize size = ct->size;
  switch (ctype_type(info)) {
  case CT_STRUCT: case CT_ENUM:
    cp_push(decl, CTINFO(CT_TYPEDEF, id), 0);  /* Don't copy unique types. */
    if ((decl->attr & CTF_QUAL)) {  /* Push unmerged qualifiers. */
      cp_push(decl, CTINFO(CT_ATTRIB, CTATTRIB(CTA_QUAL)),
	      (decl->attr & CTF_QUAL));
      decl->attr &= ~CTF_QUAL;
    }
    break;
  case CT_ATTRIB:
    if (ctype_isxattrib(info, CTA_QUAL))
      decl->attr &= ~size;  /* Remove redundant qualifiers. */
    cp_push_type(decl, ctype_cid(info));  /* Unroll. */
    cp_push(decl, info & ~CTMASK_CID, size);  /* Copy type. */
    break;
  case CT_ARRAY:
    if ((ct->info & (CTF_VECTOR|CTF_COMPLEX))) {
      info |= (decl->attr & CTF_QUAL);
      decl->attr &= ~CTF_QUAL;
    }
    cp_push_type(decl, ctype_cid(info));  /* Unroll. */
    cp_push(decl, info & ~CTMASK_CID, size);  /* Copy type. */
    decl->stack[decl->pos].sib = 1;  /* Mark as already checked and sized. */
    /* Note: this is not copied to the ct->sib in the C type table. */
    break;
  case CT_FUNC:
    /* Copy type, link parameters (shared). */
    decl->stack[cp_push(decl, info, size)].sib = ct->sib;
    break;
  default:
    /* Copy type, merge common qualifiers. */
    cp_push(decl, info|(decl->attr & CTF_QUAL), size);
    decl->attr &= ~CTF_QUAL;
    break;
  }
}

/* Consume the declaration element chain and intern the C type. */
static CTypeID cp_decl_intern(CPState *cp, CPDecl *decl)
{
  CTypeID id = 0;
  CPDeclIdx idx = 0;
  CTSize csize = CTSIZE_INVALID;
  CTSize cinfo = 0;
  do {
    CType *ct = &decl->stack[idx];
    CTInfo info = ct->info;
    CTInfo size = ct->size;
    /* The cid is already part of info for copies of pointers/functions. */
    idx = ct->next;
    if (ctype_istypedef(info)) {
      lj_assertCP(id == 0, "typedef not at toplevel");
      id = ctype_cid(info);
      /* Always refetch info/size, since struct/enum may have been completed. */
      cinfo = ctype_get(cp->cts, id)->info;
      csize = ctype_get(cp->cts, id)->size;
      lj_assertCP(ctype_isstruct(cinfo) || ctype_isenum(cinfo),
		  "typedef of bad type");
    } else if (ctype_isfunc(info)) {  /* Intern function. */
      CType *fct;
      CTypeID fid;
      CTypeID sib;
      if (id) {
	CType *refct = ctype_raw(cp->cts, id);
	/* Reject function or refarray return types. */
	if (ctype_isfunc(refct->info) || ctype_isrefarray(refct->info))
	  cp_err(cp, LJ_ERR_FFI_INVTYPE);
      }
      /* No intervening attributes allowed, skip forward. */
      while (idx) {
	CType *ctn = &decl->stack[idx];
	if (!ctype_isattrib(ctn->info)) break;
	idx = ctn->next;  /* Skip attribute. */
      }
      sib = ct->sib;  /* Next line may reallocate the C type table. */
      fid = lj_ctype_new(cp->cts, &fct);
      csize = CTSIZE_INVALID;
      fct->info = cinfo = info + id;
      fct->size = size;
      fct->sib = sib;
      id = fid;
    } else if (ctype_isattrib(info)) {
      if (ctype_isxattrib(info, CTA_QUAL))
	cinfo |= size;
      else if (ctype_isxattrib(info, CTA_ALIGN))
	CTF_INSERT(cinfo, ALIGN, size);
      id = lj_ctype_intern(cp->cts, info+id, size);
      /* Inherit csize/cinfo from original type. */
    } else {
      if (ctype_isnum(info)) {  /* Handle mode/vector-size attributes. */
	lj_assertCP(id == 0, "number not at toplevel");
	if (!(info & CTF_BOOL)) {
	  CTSize msize = ctype_msizeP(decl->attr);
	  CTSize vsize = ctype_vsizeP(decl->attr);
	  if (msize && (!(info & CTF_FP) || (msize == 4 || msize == 8))) {
	    CTSize malign = lj_fls(msize);
	    if (malign > 4) malign = 4;  /* Limit alignment. */
	    CTF_INSERT(info, ALIGN, malign);
	    size = msize;  /* Override size via mode. */
	  }
	  if (vsize) {  /* Vector size set? */
	    CTSize esize = lj_fls(size);
	    if (vsize >= esize) {
	      /* Intern the element type first. */
	      id = lj_ctype_intern(cp->cts, info, size);
	      /* Then create a vector (array) with vsize alignment. */
	      size = (1u << vsize);
	      if (vsize > 4) vsize = 4;  /* Limit alignment. */
	      if (ctype_align(info) > vsize) vsize = ctype_align(info);
	      info = CTINFO(CT_ARRAY, (info & CTF_QUAL) + CTF_VECTOR +
				      CTALIGN(vsize));
	    }
	  }
	}
      } else if (ctype_isptr(info)) {
	/* Reject pointer/ref to ref. */
	if (id && ctype_isref(ctype_raw(cp->cts, id)->info))
	  cp_err(cp, LJ_ERR_FFI_INVTYPE);
	if (ctype_isref(info)) {
	  info &= ~CTF_VOLATILE;  /* Refs are always const, never volatile. */
	  /* No intervening attributes allowed, skip forward. */
	  while (idx) {
	    CType *ctn = &decl->stack[idx];
	    if (!ctype_isattrib(ctn->info)) break;
	    idx = ctn->next;  /* Skip attribute. */
	  }
	}
      } else if (ctype_isarray(info)) {  /* Check for valid array size etc. */
	if (ct->sib == 0) {  /* Only check/size arrays not copied by unroll. */
	  if (ctype_isref(cinfo))  /* Reject arrays of refs. */
	    cp_err(cp, LJ_ERR_FFI_INVTYPE);
	  /* Reject VLS or unknown-sized types. */
	  if (ctype_isvltype(cinfo) || csize == CTSIZE_INVALID)
	    cp_err(cp, LJ_ERR_FFI_INVSIZE);
	  /* a[] and a[?] keep their invalid size. */
	  if (size != CTSIZE_INVALID) {
	    uint64_t xsz = (uint64_t)size * csize;
	    if (xsz >= 0x80000000u) cp_err(cp, LJ_ERR_FFI_INVSIZE);
	    size = (CTSize)xsz;
	  }
	}
	if ((cinfo & CTF_ALIGN) > (info & CTF_ALIGN))  /* Find max. align. */
	  info = (info & ~CTF_ALIGN) | (cinfo & CTF_ALIGN);
	info |= (cinfo & CTF_QUAL);  /* Inherit qual. */
      } else {
	lj_assertCP(ctype_isvoid(info), "bad ctype %08x", info);
      }
      csize = size;
      cinfo = info+id;
      id = lj_ctype_intern(cp->cts, info+id, size);
    }
  } while (idx);
  return id;
}

/* -- C declaration parser ------------------------------------------------ */

/* Reset declaration state to declaration specifier. */
static void cp_decl_reset(CPDecl *decl)
{
  decl->pos = decl->specpos;
  decl->top = decl->specpos+1;
  decl->stack[decl->specpos].next = 0;
  decl->attr = decl->specattr;
  decl->fattr = decl->specfattr;
  decl->name = NULL;
  decl->redir = NULL;
}

/* Parse constant initializer. */
/* NYI: FP constants and strings as initializers. */
static CTypeID cp_decl_constinit(CPState *cp, CType **ctp, CTypeID ctypeid)
{
  CType *ctt = ctype_get(cp->cts, ctypeid);
  CTInfo info;
  CTSize size;
  CPValue k;
  CTypeID constid;
  while (ctype_isattrib(ctt->info)) {  /* Skip attributes. */
    ctypeid = ctype_cid(ctt->info);  /* Update ID, too. */
    ctt = ctype_get(cp->cts, ctypeid);
  }
  info = ctt->info;
  size = ctt->size;
  if (!ctype_isinteger(info) || !(info & CTF_CONST) || size > 4)
    cp_err(cp, LJ_ERR_FFI_INVTYPE);
  cp_check(cp, '=');
  cp_expr_sub(cp, &k, 0);
  constid = lj_ctype_new(cp->cts, ctp);
  (*ctp)->info = CTINFO(CT_CONSTVAL, CTF_CONST|ctypeid);
  k.u32 <<= 8*(4-size);
  if ((info & CTF_UNSIGNED))
    k.u32 >>= 8*(4-size);
  else
    k.u32 = (uint32_t)((int32_t)k.u32 >> 8*(4-size));
  (*ctp)->size = k.u32;
  return constid;
}

/* Parse size in parentheses as part of attribute. */
static CTSize cp_decl_sizeattr(CPState *cp)
{