------------------------------------------------------------------------------
-- DynASM ARM64 module.
--
-- Copyright (C) 2005-2022 Mike Pall. All rights reserved.
-- See dynasm.lua for full copyright notice.
------------------------------------------------------------------------------

-- Module information:
local _info = {
  arch =	"arm",
  description =	"DynASM ARM64 module",
  version =	"1.5.0",
  vernum =	 10500,
  release =	"2021-05-02",
  author =	"Mike Pall",
  license =	"MIT",
}

-- Exported glue functions for the arch-specific module.
local _M = { _info = _info }

-- Cache library functions.
local type, tonumber, pairs, ipairs = type, tonumber, pairs, ipairs
local assert, setmetatable, rawget = assert, setmetatable, rawget
local _s = string
local format, byte, char = _s.format, _s.byte, _s.char
local match, gmatch, gsub = _s.match, _s.gmatch, _s.gsub
local concat, sort, insert = table.concat, table.sort, table.insert
local bit = bit or require("bit")
local band, shl, shr, sar = bit.band, bit.lshift, bit.rshift, bit.arshift
local ror, tohex, tobit = bit.ror, bit.tohex, bit.tobit

-- Inherited tables and callbacks.
local g_opt, g_arch
local wline, werror, wfatal, wwarn

-- Action name list.
-- CHECK: Keep this in sync with the C code!
local action_names = {
  "STOP", "SECTION", "ESC", "REL_EXT",
  "ALIGN", "REL_LG", "LABEL_LG",
  "REL_PC", "LABEL_PC", "REL_A",
  "IMM", "IMM6", "IMM12", "IMM13W", "IMM13X", "IMML", "IMMV",
  "VREG",
}

-- Maximum number of section buffer positions for dasm_put().
-- CHECK: Keep this in sync with the C code!
local maxsecpos = 25 -- Keep this low, to avoid excessively long C lines.

-- Action name -> action number.
local map_action = {}
for n,name in ipairs(action_names) do
  map_action[name] = n-1
end

-- Action list buffer.
local actlist = {}

-- Argument list for next dasm_put(). Start with offset 0 into action list.
local actargs = { 0 }

-- Current number of section buffer positions for dasm_put().
local secpos = 1

------------------------------------------------------------------------------

-- Dump action names and numbers.
local function dumpactions(out)
  out:write("DynASM encoding engine action codes:\n")
  for n,name in ipairs(action_names) do
    local num = map_action[name]
    out:write(format("  %-10s %02X  %d\n", name, num, num))
  end
  out:write("\n")
end

-- Write action list buffer as a huge static C array.
local function writeactions(out, name)
  local nn = #actlist
  if nn == 0 then nn = 1; actlist[0] = map_action.STOP end
  out:write("static const unsigned int ", name, "[", nn, "] = {\n")
  for i = 1,nn-1 do
    assert(out:write("0x", tohex(actlist[i]), ",\n"))
  end
  assert(out:write("0x", tohex(actlist[nn]), "\n};\n\n"))
end

------------------------------------------------------------------------------

-- Add word to action list.
local function wputxw(n)
  assert(n >= 0 and n <= 0xffffffff and n % 1 == 0, "word out of range")
  actlist[#actlist+1] = n
end

-- Add action to list with optional arg. Advance buffer pos, too.
local function waction(action, val, a, num)
  local w = assert(map_action[action], "bad action name `"..action.."'")
  wputxw(w * 0x10000 + (val or 0))
  if a then actargs[#actargs+1] = a end
  if a or num then secpos = secpos + (num or 1) end
end

-- Flush action list (intervening C code or buffer pos overflow).
local function wflush(term)
  if #actlist == actargs[1] then return end -- Nothing to flush.
  if not term then waction("STOP") end -- Terminate action list.
  wline(format("dasm_put(Dst, %s);", concat(actargs, ", ")), true)
  actargs = { #actlist } -- Actionlist offset is 1st arg to next dasm_put().
  secpos = 1 -- The actionlist offset occupies a buffer position, too.
end

-- Put escaped word.
local function wputw(n)
  if n <= 0x000fffff then waction("ESC") end
  wputxw(n)
end

-- Reserve position for word.
local function wpos()
  local pos = #actlist+1
  actlist[pos] = ""
  return pos
end

-- Store word to reserved position.
local function wputpos(pos, n)
  assert(n >= 0 and n <= 0xffffffff and n % 1 == 0, "word out of range")
  if n <= 0x000fffff then
    insert(actlist, pos+1, n)
    n = map_action.ESC * 0x10000
  end
  actlist[pos] = n
end

------------------------------------------------------------------------------

-- Global label name -> global label number. With auto assignment on 1st use.
local next_global = 20
local map_global = setmetatable({}, { __index = function(t, name)
  if not match(name, "^[%a_][%w_]*$") then werror("bad global label") end
  local n = next_global
  if n > 2047 then werror("too many global labels") end
  next_global = n + 1
  t[name] = n
  return n
end})

-- Dump global labels.
local function dumpglobals(out, lvl)
  local t = {}
  for name, n in pairs(map_global) do t[n] = name end
  out:write("Global labels:\n")
  for i=20,next_global-1 do
    out:write(format("  %s\n", t[i]))
  end
  out:write("\n")
end

-- Write global label enum.
local function writeglobals(out, prefix)
  local t = {}
  for name, n in pairs(map_global) do t[n] = name end
  out:write("enum {\n")
  for i=20,next_global-1 do
    out:write("  ", prefix, t[i], ",\n")
  end
  out:write("  ", prefix, "_MAX\n};\n")
end

-- Write global label names.
local function writeglobalnames(out, name)
  local t = {}
  for name, n in pairs(map_global) do t[n] = name end
  out:write("static const char *const ", name, "[] = {\n")
  for i=20,next_global-1 do
    out:write("  \"", t[i], "\",\n")
  end
  out:write("  (const char *)0\n};\n")
end

------------------------------------------------------------------------------

-- Extern label name -> extern label number. With auto assignment on 1st use.
local next_extern = 0
local map_extern_ = {}
local map_extern = setmetatable({}, { __index = function(t, name)
  -- No restrictions on the name for now.
  local n = next_extern
  if n > 2047 then werror("too many extern labels") end
  next_extern = n + 1
  t[name] = n
  map_extern_[n] = name
  return n
end})

-- Dump extern labels.
local function dumpexterns(out, lvl)
  out:write("Extern labels:\n")
  for i=0,next_extern-1 do
    out:write(format("  %s\n", map_extern_[i]))
  end
  out:write("\n")
end

-- Write extern label names.
local function writeexternnames(out, name)
  out:write("static const char *const ", name, "[] = {\n")
  for i=0,next_extern-1 do
    out:write("  \"", map_extern_[i], "\",\n")
  end
  out:write("  (const char *)0\n};\n")
end

------------------------------------------------------------------------------

-- Arch-specific maps.

-- Ext. register name -> int. name.
local map_archdef = { xzr = "@x31", wzr = "@w31", lr = "x30", }

-- Int. register name -> ext. name.
local map_reg_rev = { ["@x31"] = "xzr", ["@w31"] = "wzr", x30 = "lr", }

local map_type = {}		-- Type name -> { ctype, reg }
local ctypenum = 0		-- Type number (for Dt... macros).

-- Reverse defines for registers.
function _M.revdef(s)
  return map_reg_rev[s] or s
end

local map_shift = { lsl = 0, lsr = 1, asr = 2, }

local map_extend = {
  uxtb = 0, uxth = 1, uxtw = 2, uxtx = 3,
  sxtb = 4, sxth = 5, sxtw = 6, sxtx = 7,
}

local map_cond = {
  eq = 0, ne = 1, cs = 2, cc = 3, mi = 4, pl = 5, vs = 6, vc = 7,
  hi = 8, ls = 9, ge = 10, lt = 11, gt = 12, le = 13, al = 14,
  hs = 2, lo = 3,
}

------------------------------------------------------------------------------

local parse_reg_type

local function parse_reg(expr, shift, no_vreg)
  if not expr then werror("expected register name") end
  local tname, ovreg = match(expr, "^([%w_]+):(@?%l%d+)$")
  if not tname then
    tname, ovreg = match(expr, "^([%w_]+):(R[xwqdshb]%b())$")
  end
  local tp = map_type[tname or expr]
  if tp then
    local reg = ovreg or tp.reg
    if not reg then
      werror("type `"..(tname or expr).."' needs a register override")
    end
    expr = reg
  end
  local ok31, rt, r = match(expr, "^(@?)([xwqdshb])([123]?[0-9])$")
  if r then
    r = tonumber(r)
    if r <= 30 or (r == 31 and ok31 ~= "" or (rt ~= "w" and rt ~= "x")) then
      if not parse_reg_type then
	parse_reg_type = rt
      elseif parse_reg_type ~= rt then
	werror("register size mismatch")
      end
      return shl(r, shift), tp
    end
  end
  local vrt, vreg = match(expr, "^R([xwqdshb])(%b())$")
  if vreg then
    if not parse_reg_type then
      parse_reg_type = vrt
    elseif parse_reg_type ~= vrt then
      werror("register size mismatch")
    end
    if not no_vreg then waction("VREG", shift, vreg) end
    return 0
  end
  werror("bad register name `"..expr.."'")
end

local function parse_reg_base(expr)
  if expr == "sp" then return 0x3e0 end
  local base, tp = parse_reg(expr, 5)
  if parse_reg_type ~= "x" then werror("bad register type") end
  parse_reg_type = false
  return base, tp
end

local parse_ctx = {}

local loadenv = setfenv and function(s)
  local code = loadstring(s, "")
  if code then setfenv(code, parse_ctx) end
  return code
end or function(s)
  return load(s, "", nil, parse_ctx)
end

-- Try to parse simple arithmetic, too, since some basic ops are aliases.
local function parse_number(n)
  local x = tonumber(n)
  if x then return x end
  local code = loadenv("return "..n)
  if code then
    local ok, y = pcall(code)
    if ok and type(y) == "number" then return y end
  end
  return nil
end

local function parse_imm(imm, bits, shift, scale, signed)
  imm = match(imm, "^#(.*)$")
  if not imm then werror("expected immediate operand") end
  local n = parse_number(imm)
  if n then
    local m = sar(n, scale)
    if shl(m, scale) == n then
      if signed then
	local s = sar(m, bits-1)
	if s == 0 then return shl(m, shift)
	elseif s == -1 then return shl(m + shl(1, bits), shift) end
      else
	if sar(m, bits) == 0 then return shl(m, shift) end
      end
    end
    werror("out of range immediate `"..imm.."'")
  else
    waction("IMM", (signed and 32768 or 0)+scale*1024+bits*32+shift, imm)
    return 0
  end
end

local function parse_imm12(imm)
  imm = match(imm, "^#(.*)$")
  if not imm then werror("expected immediate operand") end
  local n = parse_number(imm)
  if n then
    if shr(n, 12) == 0 then
      return shl(n, 10)
    elseif band(n, 0xff000fff) == 0 then
      return shr(n, 2) + 0x00400000
    end
    werror("out of range immediate `"..imm.."'")
  else
    waction("IMM12", 0, imm)
    return 0
  end
end

local function parse_imm13(imm)
  imm = match(imm, "^#(.*)$")
  if not imm then werror("expected immediate operand") end
  local n = parse_number(imm)
  local r64 = parse_reg_type == "x"
  if n and n % 1 == 0 and n >= 0 and n <= 0xffffffff then
    local inv = false
    if band(n, 1) == 1 then n = bit.bnot(n); inv = true end
    local t = {}
    for i=1,32 do t[i] = band(n, 1); n = shr(n, 1) end
    local b = table.concat(t)
    b = b..(r64 and (inv and "1" or "0"):rep(32) or b)
    local p0, p1, p0a, p1a = b:match("^(0+)(1+)(0*)(1*)")
    if p0 then
      local w = p1a == "" and (r64 and 64 or 32) or #p1+#p0a
      if band(w, w-1) == 0 and b == b:sub(1, w):rep(64/w) then
	local s = band(-2*w, 0x3f) - 1
	if w == 64 then s = s + 0x1000 end
	if inv then
	  return shl(w-#p1-#p0, 16) + shl(s+w-#p1, 10)
	else
	  return shl(w-#p0, 16) + shl(s+#p1, 10)
	end
      end
    end
    werror("out of range immediate `"..imm.."'")
  elseif r64 then
    waction("IMM13X", 0, format("(unsigned int)(%s)", imm))
    actargs[#actargs+1] = format("(unsigned int)((unsigned long long)(%s)>>32)", imm)
    return 0
  else
    waction("IMM13W", 0, imm)
    return 0
  end
end

local function parse_imm6(imm)
  imm = match(imm, "^#(.*)$")
  if not imm then werror("expected immediate operand") end
  local n = parse_number(imm)
  if n then
    if n >= 0 and n <= 63 then
      return shl(band(n, 0x1f), 19) + (n >= 32 and 0x80000000 or 0)
    end
    werror("out of range immediate `"..imm.."'")
  else
    waction("IMM6", 0, imm)
    return 0
  end
end

local function parse_imm_load(imm, scale)
  local n = parse_number(imm)
  if n then
    local m = sar(n, scale)
    if shl(m, scale) == n and m >= 0 and m < 0x1000 then
      return shl(m, 10) + 0x01000000 -- Scaled, unsigned 12 bit offset.
    elseif n >= -256 and n < 256 then
      return shl(band(n, 511), 12) -- Unscaled, signed 9 bit offset.
    end
    werror("out of range immediate `"..imm.."'")
  else
    waction("IMML", scale, imm)
    return 0
  end
end

local function parse_fpimm(imm)
  imm = match(imm, "^#(.*)$")
  if not imm then werror("expected immediate operand") end
  local n = parse_number(imm)
  if n then
    local m, e = math.frexp(n)
    local s, e2 = 0, band(e-2, 7)
    if m < 0 then m = -m; s = 0x00100000 end
    m = m*32-16
    if m % 1 == 0 and m >= 0 and m <= 15 and sar(shl(e2, 29), 29)+2 == e then
      return s + shl(e2, 17) + shl(m, 13)
    end
    werror("out of range immediate `"..imm.."'")
  else
    werror("NYI fpimm action")
  end
end

local function parse_shift(expr)
  local s, s2 = match(expr, "^(%S+)%s*(.*)$")
  s = map_shift[s]
  if not s then werror("expected shift operand") end
  return parse_imm(s2, 6, 10, 0, false) + shl(s, 22)
end

local function parse_lslx16(expr)
  local n = match(expr, "^lsl%s*#(%d+)$")
  n = tonumber(n)
  if not n then werror("expected shift operand") end
  if band(n, parse_reg_type == "x" and 0xffffffcf or 0xffffffef) ~= 0 then
    werror("bad shift amount")
  end
  return shl(n, 17)
end

local function parse_extend(expr)
  local s, s2 = match(expr, "^(%S+)%s*(.*)$")
  if s == "lsl" then
    s = parse_reg_type == "x" and 3 or 2
  else
    s = map_extend[s]
  end
  if not s then werror("expected extend operand") end
  return (s2 == "" and 0 or parse_imm(s2, 3, 10, 0, false)) + shl(s, 13)
end

local function parse_cond(expr, inv)
  local c = map_cond[expr]
  if not c then werror("expected condition operand") end
  return shl(bit.bxor(c, inv), 12)
end

local function parse_load(params, nparams, n, op)
  if params[n+2] then werror("too many operands") end
  local scale = shr(op, 30)
  local pn, p2 = params[n], params[n+1]
  local p1, wb = match(pn, "^%[%s*(.-)%s*%](!?)$")
  if not p1 then
    if not p2 then
      local reg, tailr = match(pn, "^([%w_:]+)%s*(.*)$")
      if reg and tailr ~= "" then
	local base, tp = parse_reg_base(reg)
	if tp then
	  waction("IMML", scale, format(tp.ctypefmt, tailr))
	  return op + base
	end
      end
    end
    werror("expected address operand")
  end
  if p2 then
    if wb == "!" then werror("bad use of '!'") end
    op = op + parse_reg_base(p1) + parse_imm(p2, 9, 12, 0, true) + 0x400
  elseif wb == "!" then
    local p1a, p2a = match(p1, "^([^,%s]*)%s*,%s*(.*)$")
    if not p1a then werror("bad use of '!'") end
    op = op + parse_reg_base(p1a) + parse_imm(p2a, 9, 12, 0, true) + 0xc00
  else
    local p1a, p2a = match(p1, "^([^,%s]*)%s*(.*)$")
    op = op + parse_reg_base(p1a)
    if p2a ~= "" then
      local imm = match(p2a, "^,%s*#(.*)$")
      if imm then
	op = op + parse_imm_load(imm, scale)
      else
	local p2b, p3b, p3s = match(p2a, "^,%s*([^,%s]*)%s*,?%s*(%S*)%s*(.*)$")
	op = op + parse_reg(p2b, 16) + 0x00200800
	if parse_reg_type ~= "x" and parse_reg_type ~= "w" then
	  werror("bad index register type")
	end
	if p3b == "" then
	  if parse_reg_type ~= "x" then werror("bad index register type") end
	  op = op + 0x6000
	else
	  if p3s == "" or p3s == "#0" then
	  elseif p3s == "#"..scale then
	    op = op + 0x1000
	  else
	    werror("bad scale")
	  end
	  if parse_reg_type == "x" then
	    if p3b == "lsl" and p3s ~= "" then op = op + 0x6000
	    elseif p3b == "sxtx" then op = op + 0xe000
	    else
	      werror("bad extend/shift specifier")
	    end
	  else
	    if p3b == "uxtw" then op = op + 0x4000
	    elseif p3b == "sxtw" then op = op + 0xc000
	   