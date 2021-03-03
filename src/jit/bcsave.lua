
----------------------------------------------------------------------------
-- LuaJIT module to save/list bytecode.
--
-- Copyright (C) 2005-2022 Mike Pall. All rights reserved.
-- Released under the MIT license. See Copyright Notice in luajit.h
----------------------------------------------------------------------------
--
-- This module saves or lists the bytecode for an input file.
-- It's run by the -b command line option.
--
------------------------------------------------------------------------------

local jit = require("jit")
assert(jit.version_num == 20100, "LuaJIT core/library version mismatch")
local bit = require("bit")

-- Symbol name prefix for LuaJIT bytecode.
local LJBC_PREFIX = "luaJIT_BC_"

local type, assert = type, assert
local format = string.format
local tremove, tconcat = table.remove, table.concat

------------------------------------------------------------------------------

local function usage()
  io.stderr:write[[
Save LuaJIT bytecode: luajit -b[options] input output
  -l        Only list bytecode.
  -s        Strip debug info (default).
  -g        Keep debug info.
  -n name   Set module name (default: auto-detect from input name).
  -t type   Set output file type (default: auto-detect from output name).
  -a arch   Override architecture for object files (default: native).
  -o os     Override OS for object files (default: native).
  -F name   Override filename (default: input filename).
  -e chunk  Use chunk string as input.
  --        Stop handling options.
  -         Use stdin as input and/or stdout as output.

File types: c h obj o raw (default)
]]
  os.exit(1)
end

local function check(ok, ...)
  if ok then return ok, ... end
  io.stderr:write("luajit: ", ...)
  io.stderr:write("\n")
  os.exit(1)
end

local function readfile(ctx, input)
  if type(input) == "function" then return input end
  if ctx.filename then
    local data
    if input == "-" then
      data = io.stdin:read("*a")
    else
      local fp = assert(io.open(input, "rb"))
      data = assert(fp:read("*a"))
      assert(fp:close())
    end
    return check(load(data, ctx.filename))
  else
    if input == "-" then input = nil end
    return check(loadfile(input))
  end
end

local function savefile(name, mode)
  if name == "-" then return io.stdout end
  return check(io.open(name, mode))
end

local function set_stdout_binary(ffi)
  ffi.cdef[[int _setmode(int fd, int mode);]]
  ffi.C._setmode(1, 0x8000)
end