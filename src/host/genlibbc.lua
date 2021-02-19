----------------------------------------------------------------------------
-- Lua script to dump the bytecode of the library functions written in Lua.
-- The resulting 'buildvm_libbc.h' is used for the build process of LuaJIT.
----------------------------------------------------------------------------
-- Copyright (C) 2005-2022 Mike Pall. All rights reserved.
-- Released under the MIT license. See Copyright Notice in luajit.h
----------------------------------------------------------------------------

local ffi = require("ffi")
local bit = require("bit")
local vmdef = require("jit.vmdef")
local bcnames = vmdef.bcnames

local format = string.format

local isbe = (string.byte(string.dump(function() end), 5) % 2 == 1)

local function usage(arg)
  io.stderr:write("Usage: ", arg and arg[0] or "genli