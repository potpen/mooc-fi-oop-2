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
s