/*
** I/O library.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
**
** Major portions taken verbatim or adapted from the Lua interpreter.
** Copyright (C) 1994-2011 Lua.org, PUC-Rio. See Copyright Notice in lua.h
*/

#include <errno.h>
#include <stdio.h>

#define lib_io_c
#define LUA_LIB

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "lj_obj.h"
#include "lj_gc.h"
#include "lj_err.h"
#include "lj_buf.h"
#include "lj_str.h"
#include "lj_state.h"
#include "lj_strfmt.h"
#include "lj_ff.h"
#include "lj_lib.h"

/* Userdata payload for I/O file. */
typedef struct IOFileUD {
  FILE *fp;		/* File handle. */
  uint32_t type;	/* File type. */
} IOFileUD;

#define IOFILE_TYPE_FILE	0	/* Regular file. */
#define IOFILE_TYPE_PIPE	1	/* Pipe. */
#define IOFILE_TYPE_STDF	2	/* Standard file handle. */
#define IOFILE_TYPE_MASK	3

#define IOFILE_FLAG_CLOSE	4	/* Close after io.lines() iterator. */

#define IOSTDF_UD(L, id)	(&gcref(G(L)->gcroot[(id)])->ud)
#define IOSTDF_IOF(L, id)	((IOFileUD *)uddata(IOSTDF_UD(L, (id))))

/* -- Open/close helpers -------------------------------------------------- */

static IOFileUD *io_tofilep(lua_State *L)
{
  if (!(L->base < L->top && tvisudata(L->base) &&
	udataV(L->base)->udtype == UDTYPE_IO_FILE))
    lj_err_argtype(L, 1, "FILE*");
  return (IOFileUD *)uddata(udataV(L->base));
}

static IOFileUD *io_tofile(lua_State *L)
{
  IOFileUD *iof = io_tofilep(L);
  if (iof->fp == NULL)
    lj_err_caller(L, LJ_ERR_IOCLFL);
  return iof;
}

static IOFileUD *io_stdfile(lua_State *L, ptrdiff_t id)
{
  IOFileUD *iof = IOSTDF_IOF(L, id);
  if (iof->fp == NULL)
    lj_err_caller(L, LJ_ERR_IOSTDCL);
  return iof;
}

static IOFileUD *io_file_new(lua_State *L)
{
  IOFileUD *iof = (IOFileUD *)lua_newuserdata(L, sizeof(IOFileUD));
  GCudata *ud = udataV(L->top-1);
  ud->udtype = UDTYPE_IO_FILE;
  /* NOBARRIER: The GCudata is new (marked white). */
  setgcrefr(ud->metatable, curr_func(L)->c.env);
  iof->fp = NULL;
  iof->type = IOFILE_TYPE_FILE;
  return iof;
}

static IOFileUD *io_file_open(lua_State *L, const char *mode)
{
  const char *fname = strdata(lj_lib_checkstr(L, 1));
  IOFileUD *iof = io_file_new(L);
  iof->fp = fopen(fname, mode);
  if (iof->fp == NULL)
    luaL_argerror(L, 1, lj_strfmt_pushf(L, "%s: %s", fname, strerror(errno)));
  return iof;
}

static int io_file_close(lua_State *L, IOFileUD *iof)
{
  int ok;
  if ((iof->type & IOFILE_TYPE_MASK) == IOFILE_TYPE_FILE) {
    ok = (fclose(iof->fp) == 0);
  } else if ((iof->type & IOFILE_TYPE_MASK) == IOFILE_TYPE_PIPE) {
    int stat = -1;
#if LJ_TARGET_POSIX
    stat = pclose(iof->fp);
#elif LJ_TARGET_WINDOWS && !LJ_TARGET_XBOXONE && !LJ_TARGET_UWP
    stat = _pclose(iof->fp);
#endif
#if LJ_52
    iof->fp = NULL;
    return luaL_execresult(L, stat);
#else
    ok = (stat != -1);
#endif
  } else {
    lj_assertL((iof->type & IOFILE_TYPE_MASK) == IOFILE_TYPE_STDF,
	       "close of unknown FILE* type");
    setnilV(L->top++);
    lua_pushliteral(L, "cannot close standard file");
    return 2;
  }
  iof->fp = NULL;
  return luaL_fileresult(L, ok, NULL);
}

/* -- Read/write helpers -------------------------------------------------- */

static int io_file_readnum(lua_State *L, FILE *fp)
{
  lua_Number d;
  if (fscanf(fp, LUA_NUMBER_SCAN, &d) == 1) {
    if (LJ_DUALNUM) {
      int32_t i = lj_num2int(d);
      if (d == (lua_Number)i && !tvismzero((cTValue *)&d)) {
	setintV(L->top++, i);
	return 1;
      }
    }
    setnumV(L->top++, d);
    return 1;
  } else {
    setnilV(L->top++);
    return 0;
  }
}

static int io_file_readline(lua_State *L, FILE *fp, MSize chop)
{
  MSize m = LUAL_BUFFERSIZE, n = 0, ok = 0;
  char *buf;
  for (;;) {
    buf = lj_buf_tmp(L, m);
    if (fgets(buf+n, m-n, fp) == NULL) break;
    n += (MSize)strlen(buf+n);
    ok |= n;
    if (n && buf[n-1] == '\n') { n -= chop; break; }
    if (n >= m - 64) m += m;
  }
  setstrV(L, L->top++, lj_str_new(L, buf, (size_t)n));
  lj_gc_check(L);
  return (int)ok;
}

static void io_file_readall(lua_State *L, FILE *fp)
{
  MSize m, n;
  for (m = LUAL_BUFFERSIZE, n = 0; ; m += m) {
    char *buf = lj_buf_tmp(L, m);
    n += (MSize)fread(buf+n, 1, m-n, fp);
    if (n != m) {
      setstrV(L, L->top++, lj_str_new(L, buf, (size_t)n));
      lj_gc_check(L);
      return;
    }
  }
}

static int io_file_readlen(lua_State *L, FILE *fp, MSize m)
{
  if (m) {
    char *buf = lj_buf_tmp(L, m);
    MSize n = (MSize)fread(buf, 1, m, fp);
    setstrV(L, L->top++, lj_str_new(L, buf, (size_t)n));
    lj_gc_check(L);
    return n > 0;
  } else {
    int c = getc(fp);
    ungetc(c, fp);
    setstrV(L, L->top++, &G(L)->strempty);
    return (c != EOF);
  }
}

static int io_file_read(lua_State *L, IOFileUD *iof, int start)
{
  FILE *fp = iof->fp;
  int ok, n, nargs = (int)(L->top - L->base) - start;
  clearerr(fp);
  if (nargs == 0) {
    ok = io_file_readline(L, fp, 1);
    n = start+1;  /* Return 1 result. */
  } else {
    /* The results plus the buffers go on top of the args. */
    luaL_checkstack(L, nargs+LUA_MINSTACK, "too many arguments");
    ok = 1;
    for (n = start; nargs-- && ok; n++) {
      if (tvisstr(L->base+n)) {
	const char *p = strVdata(L->base+n);
	if (p[0] == '*') p++;
	if (p[0] == 'n')
	  ok = io_file_readnum(L, fp);
	else if ((p[0] & ~0x20) == 'L')
	  ok = io_file_readline(L, fp, (p[0] == 'l'));
	else if (p[0] == 'a')
	  io_file_readall(L, fp);
	else
	  lj_err_arg(L, n+1, LJ_ERR_INVFMT);
      } else if (tvisnumber(L->base+n)) {
	ok = io_file_readlen(L, fp, (MSize)lj_lib_checkint(L, n+1));
      } else {
	lj_err_arg(L, n+1, LJ_ERR_INVOPT);
      }
    }
  }
  if (ferror(fp))
    return luaL_fileresult(L, 0, NULL);
  if (!ok)
    setnilV(L->top-1);  /* Replace last result with nil. */
  return n - start;
}

static int io_file_write(lua_State *L, IOFileUD *iof, int start)
{
  FILE *fp = iof->fp;
  cTValue *tv;
  int status = 1;
  for (tv = L->base+start; tv < L->top; tv++) {
    MSize len;
    const char *p = lj_strfmt_wstrnum(L, tv, &len);
    if (!p)
      lj_err_argt(L, (int)(tv - L->base) + 1, LUA_TSTRING);
    status = status && (fwrite(p, 1, len, fp) == len);
  }
  if (LJ_52 && status) {
    L->top = L->base+1;
    if (start == 0)
      setudataV(L, L->base, IOSTDF_UD(L, GCROOT_IO_OUTPUT));
    return 1;
  }
  return luaL_fileresult(L, status, NULL);
}

static int io_file_iter(lua_State *L)
{
  GCfunc *fn = curr_func(L);
  IOFileUD *iof = uddata(udataV(&fn->c.upvalue[0]));
  int n = fn->c.nupvalues - 1;
  if (iof->fp == NULL)
    lj_err_caller(L, LJ_ERR_IOCLFL);
  L->top = L->base;
  if (n) {  /* Copy upvalues with options to stack. */
    lj_state_checkstack(L, (MSize)n);
    memcpy(L->top, &fn->c.upvalue[1], n*sizeof(TValue));
    L->top += n;
  }
  n = io_file_read(L, iof, 0);
  if (ferror(iof->fp))
    lj_err_callermsg(L, strVdata(L->top-2));
  if (tvisnil(L->base) && (iof->type & IOFILE_FLAG_CLOSE)) {
    io_file_close(L, iof);  /* Return values are ignored. */
    return 0;
  }
  return n;
}

static int io_file_lines(lua_State *L)
{
  int n = (int)(L->top - L->base);
  if (n > LJ_MAX_UPVAL)
    lj_err_caller(L, LJ_ERR_UNPACK);
  lua_pushcclosure(L, io_file_iter, n);
  return 1;
}

/* -- I/O file methods ---------------------------------------------------- */

#define LJLIB_MODULE_io_method

LJLIB_CF(io_method_close)
{
  IOFileUD *iof;
  if (L->base < L->top) {
    iof = io_tofile(L);
  } else {
    iof = IOSTDF_IOF(L, GCROOT_IO_OUTPUT);
    if (iof->fp == NULL)
      lj_err_caller(L, LJ_ERR_IOCLFL);
  }
  return io_file_close(L, iof);
}

LJLIB_CF(io_method_read)
{
  return io_file_read(L, io_tofile(L), 1);
}

LJLIB_CF(io_method_write)		LJLIB_REC(io_write 0)
{
  return io_file_write(L, io_tofile(L), 1);
}

LJLIB_CF(io_method_flush)		LJLIB_REC(io_flush 0)
{
  return luaL_fileresult(L, fflush(io_tofile(L)->fp) == 0, NULL);
}

#if LJ_32 && defined(__ANDROID__) && __ANDROID_API__ < 24
/* The Android NDK is such an unmatched marvel of engineering. */
extern int fseeko32(FILE *, long int, int) __asm__("fseeko");
extern long int ftello32(FILE *) __asm__("ftello");
#define fseeko(fp, pos, whence)	(fseeko32((fp), (pos), (whence)))
#define ftello(fp)		(ftello32((fp)))
#endif

LJLIB_CF(io_method_seek)
{
  FILE *fp = io_tofile(L)->fp;
  int opt = lj_lib_checkopt(L, 2, 1, "\3set\3cur\3end");
  int64_t ofs = 0;
  cTValue *o;
  int res;
  if (opt == 0) opt = SEEK_SET;
  else if (opt == 1) opt = SEEK_CUR;
  else if (opt == 2) opt = SEEK_END;
  o = L->base+2;
  if (o < L->top) {
    if (tvisint(o))
      ofs = (int64_t)intV(