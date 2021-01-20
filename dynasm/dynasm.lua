------------------------------------------------------------------------------
-- DynASM. A dynamic assembler for code generation engines.
-- Originally designed and implemented for LuaJIT.
--
-- Copyright (C) 2005-2022 Mike Pall. All rights reserved.
-- See below for full copyright notice.
------------------------------------------------------------------------------

-- Application information.
local _info = {
  name =	"DynASM",
  description =	"A dynamic assembler for code generation engines",
  version =	"1.5.0",
  vernum =	 10500,
  release =	"2021-05-02",
  author =	"Mike Pall",
  url =		"https://luajit.org/dynasm.html",
  license =	"MIT",
  copyright =	[[
Copyright (C) 2005-2022 Mike Pall. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

[ MIT license: https://www.opensource.org/licenses/mit-license.php ]
]],
}

-- Cache library functions.
local type, pairs, ipairs = type, pairs, ipairs
local pcall, error, assert = pcall, error, assert
local _s = string
local sub, match, gmatch, gsub = _s.sub, _s.match, _s.gmatch, _s.gsub
local format, rep, upper = _s.format, _s.rep, _s.upper
local _t = table
local insert, remove, concat, sort = _t.insert, _t.remove, _t.concat, _t.sort
local exit = os.exit
local io = io
local stdin, stdout, stderr = io.stdin, io.stdout, io.stderr

------------------------------------------------------------------------------

-- Program options.
local g_opt = {}

-- Global state for current file.
local g_fname, g_curline, g_indent, g_lineno, g_synclineno, g_arch
local g_errcount = 0

-- Write buffer for output file.
local g_wbuffer, g_capbuffer

------------------------------------------------------------------------------

-- Write an output line (or callback function) to the buffer.
local function wline(line, needindent)
  local buf = g_capbuffer or g_wbuffer
  buf[#buf+1] = needindent and g_indent..line or line
  g_synclineno = g_synclineno + 1
end

-- Write assembler line as a comment, if requestd.
local function wcomment(aline)
  if g_opt.comment then
    wline(g_opt.comment..aline..g_opt.endcomment, true)
  end
end

-- Resync CPP line numbers.
local function wsync()
  if g_synclineno ~= g_lineno and g_opt.cpp then
    wline("#line "..g_lineno..' "'..g_fname..'"')
    g_synclineno = g_lineno
  end
end

-- Dummy action flush function. Replaced with arch-specific function later.
local function wflush(term)
end

-- Dump all buffered output lines.
local function wdumplines(out, buf)
  for _,line in ipairs(buf) do
    if type(line) == "string" then
      assert(out:write(line, "\n"))
    else
      -- Special callback to dynamically insert lines after end of processing.
      line(out)
    end
  end
end

------------------------------------------------------------------------------

-- Emit an error. Processing continues with next statement.
local function werror(msg)
  error(format("%s:%s: error: %s:\n%s", g_fname, g_lineno, msg, g_curline), 0)
end

-- Emit a fatal error. Processing stops.
local function wfatal(msg)
  g_errcount = "fatal"
  werror(msg)
end

-- Print a warning. Processing continues.
local function wwarn(msg)
  stderr:write(format("%s:%s: warning: %s:\n%s\n",
    g_fname, g_lineno, msg, g_curline))
end

-- Print caught error message. But suppress excessive errors.
local function wprinterr(...)
  if type(g_errcount) == "number" then
    -- Regular error.
    g_errcount = g_errcount + 1
    if g_errcount < 21 then -- Seems to be a reasonable limit.
      stderr:write(...)
    elseif g_errcount == 21 then
      stderr:write(g_fname,
	":*: warning: too many errors (suppressed further messages).\n")
    end
  else
    -- Fatal error.
    stderr:write(...)
    return true -- Stop processing.
  end
end

------------------------------------------------------------------------------

-- Map holding all option handlers.
local opt_map = {}
local opt_current

-- Print error and exit with error status.
local function opterror(...)
  stderr:write("dynasm.lua: ERROR: ", ...)
  stderr:write("\n")
  exit(1)
end

-- Get option parameter.
local function optparam(args)
  local argn = args.argn
  local p = args[argn]
  if not p then
    opterror("missing parameter for option `", opt_current, "'.")
  end
  args.argn = argn + 1
  return p
end

------------------------------------------------------------------------------

-- Core pseudo-opcodes.
local map_coreop = {}
-- Dummy opcode map. Replaced by arch-specific map.
local map_op = {}

-- Forward declarations.
local dostmt
local readfile

------------------------------------------------------------------------------

-- Map for defines (initially empty, chains to arch-specific map).
local map_def = {}

-- Pseudo-opcode to define a substitution.
map_coreop[".define_2"] = function(params, nparams)
  if not params then return nparams == 1 and "name" or "name, subst" end
  local name, def = params[1], params[2] or "1"
  if not match(name, "^[%a_][%w_]*$") then werror("bad or duplicate define") end
  map_def[name] = def
end
map_coreop[".define_1"] = map_coreop[".define_2"]

-- Define a substitution on the command line.
function opt_map.D(args)
  local namesubst = optparam(args)
  local name, subst = match(namesubst, "^([%a_][%w_]*)=(.*)$")
  if name then
    map_def[name] = subst
  elseif match(namesubst, "^[%a_][%w_]*$") then
    map_def[namesubst] = "1"
  else
    opterror("bad define")
  end
end

-- Undefine a substitution on the command line.
function opt_map.U(args)
  local name = optparam(args)
  if match(name, "^[%a_][%w_]*$") then
    map_def[name] = nil
  else
    opterror("bad define")
  end
end

-- Helper for definesubst.
local gotsubst

local function definesubst_one(word)
  local subst = map_def[word]
  if subst then gotsubst = word; return subst else return word end
end

-- Iteratively substitute defines.
local function definesubst(stmt)
  -- Limit number of iterations.
  for i=1,100 do
    gotsubst = false
    stmt = gsub(stmt, "#?[%w_]+", definesubst_one)
    if not gotsubst then break end
  end
  if gotsubst then wfatal("recursive define involving `"..gotsubst.."'") end
  return stmt
end

-- Dump all defines.
local function dumpdefines(out, lvl)
  local t = {}
  for name in pairs(map_def) do
    t[#t+1] = name
  end
  sort(t)
  out:write("Defines:\n")
  for _,name in ipairs(t) do
    local subst = map_def[name]
    if g_arch then subst = g_arch.revdef(subst) end
    out:write(format("  %-20s %s\n", name, subst))
  end
  out:write("\n")
end

------------------------------------------------------------------------------

-- Support variables for conditional assembly.
local condlevel = 0
local condstack = {}

-- Evaluate condition with a Lua expression. Substitutions already performed.
local function cond_eval(cond)
  local func, err
  if setfenv then
    func, err = loadstring("return "..cond, "=expr")
  else
    -- No globals. All unknown identifiers evaluate to nil.
    func, err = load("return "..cond, "=expr", "t", {})
  end
  if func then
    if setfenv then
      setfenv(func, {}) -- No globals. All unknown identifiers evaluate to nil.
    end
    local ok, res = pcall(func)
    if ok then
      if res == 0 then return false end -- Oh well.
      return not not res
    end
    err = res
  end
  wfatal("bad condition: "..err)
end

-- Skip statements until next conditional pseudo-opcode at the same level.
local function stmtskip()
  local dostmt_save = dostmt
  local lvl = 0
  dostmt = function(stmt)
    local op = match(stmt, "^%s*(%S+)")
    if op == ".if" then
      lvl = lvl + 1
    elseif lvl ~= 0 then
      if op == ".endif" then lvl = lvl - 1 end
    elseif op == ".elif" or op == ".else" or op == ".endif" then
      dostmt = dostmt_save
      dostmt(stmt)
    end
  end
end

-- Pseudo-opcodes for conditional assembly.
map_coreop[".if_1"] = function(params)
  if not params then return "condition" end
  local lvl = condlevel + 1
  local res = cond_eval(params[1])
  condlevel = lvl
  condstack[lvl] = res
  if not res then stmtskip() end
end

map_coreop[".elif_1"] = function(params)
  if not params then return "condition" end
  if condlevel == 0 then wfatal(".elif without .if") end
  local lvl = condlevel
  local res = condstack[lvl]
  if res then
    if res == "else" then wfatal(".elif after .else") end
  else
    res = cond_eval(params[1])
    if res then
      condstack[lvl] = res
      return
    end
  end
  stmtskip()
end

map_coreop[".else_0"] = function(params)
  if condlevel == 0 then wfatal(".else without .if") end
  local lvl = condlevel
  local res = condstack[lvl]
  condstack[lvl] = "else"
  if res then
    if res == "else" then wfatal(".else after .else") end
    stmtskip()
  end
end

map_coreop[".endif_0"] = function(params)
  local lvl = condlevel
  if lvl == 0 then wfatal(".endif without .if") end
  condlevel = lvl - 1
end

-- Check for unfinished conditionals.
local function checkconds()
  if g_errcount ~= "fatal" and condlevel ~= 0 then
    wprinterr(g_fname, ":*: error: unbalanced conditional\n")
  end
end

------------------------------------------------------------------------------

-- Search for a file in the given path and open it for reading.
local functi