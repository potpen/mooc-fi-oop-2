----------------------------------------------------------------------------
-- Lua script to generate a customized, minified version of Lua.
-- The resulting 'minilua' is used for the build process of LuaJIT.
----------------------------------------------------------------------------
-- Copyright (C) 2005-2022 Mike Pall. All rights reserved.
-- Released under the MIT license. See Copyright Notice in luajit.h
----------------------------------------------------------------------------

local sub, match, gsub = string.sub, string.match, string.gsub

local LUA_VERSION = "5.1.5"
local LUA_SOURCE

local function usage()
  io.stderr:write("Usage: ", arg and arg[0] or "genminilua",
		  " lua-", LUA_VERSION, "-source-dir\n")
  os.exit(1)
end

local function find_sources()
  LUA_SOURCE = arg and arg[1]
  if not LUA_SOURCE then usage() end
  if sub(LUA_SOURCE, -1) ~= "/" then LUA_SOURCE = LUA_SOURCE.."/" end
  local fp = io.open(LUA_SOURCE .. "lua.h")
  if not fp then
    LUA_SOURCE = LUA_SOURCE.."src/"
    fp = io.open(LUA_SOURCE .. "lua.h")
    if not fp then usage() end
  end
  local all = fp:read("*a")
  fp:close()
  if not match(all, 'LUA_RELEASE%s*"Lua '..LUA_VERSION..'"') then
    io.stderr:write("Error: version mismatch\n")
    usage()
  end
end

local LUA_FILES = {
"lmem.c", "lobject.c", "ltm.c", "lfunc.c", "ldo.c", "lstring.c", "ltable.c",
"lgc.c", "lstate.c", "ldebug.c", "lzio.c", "lopcodes.c",
"llex.c", "lcode.c", "lparser.c", "lvm.c", "lapi.c", "lauxlib.c",
"lbaselib.c", "ltablib.c", "liolib.c", "loslib.c", "lstrlib.c", "linit.c",
}

local REMOVE_LIB = {}
gsub([[
collectgarbage dofile gcinfo getfenv getmetatable load print rawequal rawset
select tostring xpcall
foreach foreachi getn maxn setn
popen tmpfile seek setvbuf __tostring
clock date difftime execute getenv rename setlocale time tmpname
dump gfind len reverse
LUA_LOADLIBNAME LUA_MATHLIBNAME LUA_DBLIBNAME
]], "%S+", function(name)
  REMOVE_LIB[name] = true
end)

local REMOVE_EXTINC = { ["<assert.h>"] = true, ["<locale.h>"] = true, }

local CUSTOM_MAIN = [[
typedef unsigned int UB;
static UB barg(lua_State *L,int idx){
union{lua_Number n;U64 b;}bn;
bn.n=lua_tonumber(L,idx)+6755399441055744.0;
if (bn.n==0.0&&!lua_isnumber(L,idx))luaL_typerror(L,idx,"number");
return(UB)bn.b;
}
#define BRET(b) lua_pushnumber(L,(lua_Number)(int)(b));return 1;
static int tobit(lua_State *L){
BRET(barg(L,1))}
static int bnot(lua_State *L){
BRET(~barg(L,1))}
static int band(lua_State *L){
int i;UB b=barg(L,1);for(i=lua_gettop(L);i>1;i--)b&=barg(L,i);BRET(b)}
static int bor(lua_State *L){
int i;UB b=barg(L,1);for(i=lua_gettop(L);i>1;i--)b|=barg(L,i);BRET(b)}
static int bxor(lua_State *L){
int i;UB b=barg(L,1);for(i=lua_gettop(L);i>1;i--)b^=barg(L,i);BRET(b)}
static int lshift(lua_State *L){
UB b=barg(L,1),n=barg(L,2)&31;BRET(b<<n)}
static int rshift(lua_State *L){
UB b=barg(L,1),n=barg(L,2)&31;BRET(b>>n)}
static int arshift(lua_State *L){
UB b=barg(L,1),n=barg(L,2)&31;BRET((int)b>>n)}
static int rol(lua_State *L){
UB b=barg(L,1),n=barg(L,2)&31;BRET((b<<n)|(b>>(32-n)))}
static int ror(lua_State *L){
UB b=barg(L,1),n=barg(L,2)&31;BRET((b>>n)|(b<<(32-n)))}
static int bswap(lua_State *L){
UB b=barg(L,1);b=(b>>24)|((b>>8)&0xff00)|((b&0xff00)<<8)|(b<<24);BRET(b)}
static int tohex(lua_State *L){
UB b=barg(L,1);
int n=lua_isnone(L,2)?8:(int)barg(L,2);
const char *hexdigits="0123456789abcdef";
char buf[8];
int i;
if(n<0){n=-n;hexdigits="0123456789ABCDEF";}
if(n>8)