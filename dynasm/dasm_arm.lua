
------------------------------------------------------------------------------
-- DynASM ARM module.
--
-- Copyright (C) 2005-2022 Mike Pall. All rights reserved.
-- See dynasm.lua for full copyright notice.
------------------------------------------------------------------------------

-- Module information:
local _info = {
  arch =	"arm",
  description =	"DynASM ARM module",
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
local sub, format, byte, char = _s.sub, _s.format, _s.byte, _s.char
local match, gmatch, gsub = _s.match, _s.gmatch, _s.gsub
local concat, sort, insert = table.concat, table.sort, table.insert
local bit = bit or require("bit")
local band, shl, shr, sar = bit.band, bit.lshift, bit.rshift, bit.arshift
local ror, tohex = bit.ror, bit.tohex

-- Inherited tables and callbacks.
local g_opt, g_arch
local wline, werror, wfatal, wwarn

-- Action name list.
-- CHECK: Keep this in sync with the C code!
local action_names = {
  "STOP", "SECTION", "ESC", "REL_EXT",
  "ALIGN", "REL_LG", "LABEL_LG",
  "REL_PC", "LABEL_PC", "IMM", "IMM12", "IMM16", "IMML8", "IMML12", "IMMV8",
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
local map_archdef = { sp = "r13", lr = "r14", pc = "r15", }

-- Int. register name -> ext. name.
local map_reg_rev = { r13 = "sp", r14 = "lr", r15 = "pc", }

local map_type = {}		-- Type name -> { ctype, reg }
local ctypenum = 0		-- Type number (for Dt... macros).

-- Reverse defines for registers.
function _M.revdef(s)
  return map_reg_rev[s] or s
end

local map_shift = { lsl = 0, lsr = 1, asr = 2, ror = 3, }

local map_cond = {
  eq = 0, ne = 1, cs = 2, cc = 3, mi = 4, pl = 5, vs = 6, vc = 7,
  hi = 8, ls = 9, ge = 10, lt = 11, gt = 12, le = 13, al = 14,
  hs = 2, lo = 3,
}

------------------------------------------------------------------------------

-- Template strings for ARM instructions.
local map_op = {
  -- Basic data processing instructions.
  and_3 = "e0000000DNPs",
  eor_3 = "e0200000DNPs",
  sub_3 = "e0400000DNPs",
  rsb_3 = "e0600000DNPs",
  add_3 = "e0800000DNPs",
  adc_3 = "e0a00000DNPs",
  sbc_3 = "e0c00000DNPs",
  rsc_3 = "e0e00000DNPs",
  tst_2 = "e1100000NP",
  teq_2 = "e1300000NP",
  cmp_2 = "e1500000NP",
  cmn_2 = "e1700000NP",
  orr_3 = "e1800000DNPs",
  mov_2 = "e1a00000DPs",
  bic_3 = "e1c00000DNPs",
  mvn_2 = "e1e00000DPs",

  and_4 = "e0000000DNMps",
  eor_4 = "e0200000DNMps",
  sub_4 = "e0400000DNMps",
  rsb_4 = "e0600000DNMps",
  add_4 = "e0800000DNMps",
  adc_4 = "e0a00000DNMps",
  sbc_4 = "e0c00000DNMps",
  rsc_4 = "e0e00000DNMps",
  tst_3 = "e1100000NMp",
  teq_3 = "e1300000NMp",
  cmp_3 = "e1500000NMp",
  cmn_3 = "e1700000NMp",
  orr_4 = "e1800000DNMps",
  mov_3 = "e1a00000DMps",
  bic_4 = "e1c00000DNMps",
  mvn_3 = "e1e00000DMps",

  lsl_3 = "e1a00000DMws",
  lsr_3 = "e1a00020DMws",
  asr_3 = "e1a00040DMws",
  ror_3 = "e1a00060DMws",
  rrx_2 = "e1a00060DMs",

  -- Multiply and multiply-accumulate.
  mul_3 = "e0000090NMSs",
  mla_4 = "e0200090NMSDs",
  umaal_4 = "e0400090DNMSs",	-- v6
  mls_4 = "e0600090DNMSs",	-- v6T2
  umull_4 = "e0800090DNMSs",
  umlal_4 = "e0a00090DNMSs",
  smull_4 = "e0c00090DNMSs",
  smlal_4 = "e0e00090DNMSs",

  -- Halfword multiply and multiply-accumulate.
  smlabb_4 = "e1000080NMSD",	-- v5TE
  smlatb_4 = "e10000a0NMSD",	-- v5TE
  smlabt_4 = "e10000c0NMSD",	-- v5TE
  smlatt_4 = "e10000e0NMSD",	-- v5TE
  smlawb_4 = "e1200080NMSD",	-- v5TE
  smulwb_3 = "e12000a0NMS",	-- v5TE
  smlawt_4 = "e12000c0NMSD",	-- v5TE
  smulwt_3 = "e12000e0NMS",	-- v5TE
  smlalbb_4 = "e1400080NMSD",	-- v5TE
  smlaltb_4 = "e14000a0NMSD",	-- v5TE
  smlalbt_4 = "e14000c0NMSD",	-- v5TE
  smlaltt_4 = "e14000e0NMSD",	-- v5TE
  smulbb_3 = "e1600080NMS",	-- v5TE
  smultb_3 = "e16000a0NMS",	-- v5TE
  smulbt_3 = "e16000c0NMS",	-- v5TE
  smultt_3 = "e16000e0NMS",	-- v5TE

  -- Miscellaneous data processing instructions.
  clz_2 = "e16f0f10DM", -- v5T
  rev_2 = "e6bf0f30DM", -- v6
  rev16_2 = "e6bf0fb0DM", -- v6
  revsh_2 = "e6ff0fb0DM", -- v6
  sel_3 = "e6800fb0DNM", -- v6
  usad8_3 = "e780f010NMS", -- v6
  usada8_4 = "e7800010NMSD", -- v6
  rbit_2 = "e6ff0f30DM", -- v6T2
  movw_2 = "e3000000DW", -- v6T2
  movt_2 = "e3400000DW", -- v6T2
  -- Note: the X encodes width-1, not width.
  sbfx_4 = "e7a00050DMvX", -- v6T2
  ubfx_4 = "e7e00050DMvX", -- v6T2
  -- Note: the X encodes the msb field, not the width.
  bfc_3 = "e7c0001fDvX", -- v6T2
  bfi_4 = "e7c00010DMvX", -- v6T2

  -- Packing and unpacking instructions.
  pkhbt_3 = "e6800010DNM", pkhbt_4 = "e6800010DNMv", -- v6
  pkhtb_3 = "e6800050DNM", pkhtb_4 = "e6800050DNMv", -- v6
  sxtab_3 = "e6a00070DNM", sxtab_4 = "e6a00070DNMv", -- v6
  sxtab16_3 = "e6800070DNM", sxtab16_4 = "e6800070DNMv", -- v6
  sxtah_3 = "e6b00070DNM", sxtah_4 = "e6b00070DNMv", -- v6
  sxtb_2 = "e6af0070DM", sxtb_3 = "e6af0070DMv", -- v6
  sxtb16_2 = "e68f0070DM", sxtb16_3 = "e68f0070DMv", -- v6
  sxth_2 = "e6bf0070DM", sxth_3 = "e6bf0070DMv", -- v6
  uxtab_3 = "e6e00070DNM", uxtab_4 = "e6e00070DNMv", -- v6
  uxtab16_3 = "e6c00070DNM", uxtab16_4 = "e6c00070DNMv", -- v6
  uxtah_3 = "e6f00070DNM", uxtah_4 = "e6f00070DNMv", -- v6
  uxtb_2 = "e6ef0070DM", uxtb_3 = "e6ef0070DMv", -- v6
  uxtb16_2 = "e6cf0070DM", uxtb16_3 = "e6cf0070DMv", -- v6
  uxth_2 = "e6ff0070DM", uxth_3 = "e6ff0070DMv", -- v6

  -- Saturating instructions.
  qadd_3 = "e1000050DMN",	-- v5TE
  qsub_3 = "e1200050DMN",	-- v5TE
  qdadd_3 = "e1400050DMN",	-- v5TE
  qdsub_3 = "e1600050DMN",	-- v5TE
  -- Note: the X for ssat* encodes sat_imm-1, not sat_imm.
  ssat_3 = "e6a00010DXM", ssat_4 = "e6a00010DXMp", -- v6
  usat_3 = "e6e00010DXM", usat_4 = "e6e00010DXMp", -- v6
  ssat16_3 = "e6a00f30DXM", -- v6
  usat16_3 = "e6e00f30DXM", -- v6

  -- Parallel addition and subtraction.
  sadd16_3 = "e6100f10DNM", -- v6
  sasx_3 = "e6100f30DNM", -- v6
  ssax_3 = "e6100f50DNM", -- v6
  ssub16_3 = "e6100f70DNM", -- v6
  sadd8_3 = "e6100f90DNM", -- v6
  ssub8_3 = "e6100ff0DNM", -- v6
  qadd16_3 = "e6200f10DNM", -- v6
  qasx_3 = "e6200f30DNM", -- v6
  qsax_3 = "e6200f50DNM", -- v6
  qsub16_3 = "e6200f70DNM", -- v6
  qadd8_3 = "e6200f90DNM", -- v6
  qsub8_3 = "e6200ff0DNM", -- v6
  shadd16_3 = "e6300f10DNM", -- v6
  shasx_3 = "e6300f30DNM", -- v6
  shsax_3 = "e6300f50DNM", -- v6
  shsub16_3 = "e6300f70DNM", -- v6
  shadd8_3 = "e6300f90DNM", -- v6
  shsub8_3 = "e6300ff0DNM", -- v6
  uadd16_3 = "e6500f10DNM", -- v6
  uasx_3 = "e6500f30DNM", -- v6
  usax_3 = "e6500f50DNM", -- v6
  usub16_3 = "e6500f70DNM", -- v6
  uadd8_3 = "e6500f90DNM", -- v6
  usub8_3 = "e6500ff0DNM", -- v6
  uqadd16_3 = "e6600f10DNM", -- v6
  uqasx_3 = "e6600f30DNM", -- v6
  uqsax_3 = "e6600f50DNM", -- v6
  uqsub16_3 = "e6600f70DNM", -- v6
  uqadd8_3 = "e6600f90DNM", -- v6
  uqsub8_3 = "e6600ff0DNM", -- v6
  uhadd16_3 = "e6700f10DNM", -- v6
  uhasx_3 = "e6700f30DNM", -- v6
  uhsax_3 = "e6700f50DNM", -- v6
  uhsub16_3 = "e6700f70DNM", -- v6
  uhadd8_3 = "e6700f90DNM", -- v6
  uhsub8_3 = "e6700ff0DNM", -- v6

  -- Load/store instructions.
  str_2 = "e4000000DL", str_3 = "e4000000DL", str_4 = "e4000000DL",
  strb_2 = "e4400000DL", strb_3 = "e4400000DL", strb_4 = "e4400000DL",
  ldr_2 = "e4100000DL", ldr_3 = "e4100000DL", ldr_4 = "e4100000DL",
  ldrb_2 = "e4500000DL", ldrb_3 = "e4500000DL", ldrb_4 = "e4500000DL",
  strh_2 = "e00000b0DL", strh_3 = "e00000b0DL",
  ldrh_2 = "e01000b0DL", ldrh_3 = "e01000b0DL",
  ldrd_2 = "e00000d0DL", ldrd_3 = "e00000d0DL", -- v5TE
  ldrsb_2 = "e01000d0DL", ldrsb_3 = "e01000d0DL",
  strd_2 = "e00000f0DL", strd_3 = "e00000f0DL", -- v5TE
  ldrsh_2 = "e01000f0DL", ldrsh_3 = "e01000f0DL",

  ldm_2 = "e8900000oR", ldmia_2 = "e8900000oR", ldmfd_2 = "e8900000oR",
  ldmda_2 = "e8100000oR", ldmfa_2 = "e8100000oR",
  ldmdb_2 = "e9100000oR", ldmea_2 = "e9100000oR",
  ldmib_2 = "e9900000oR", ldmed_2 = "e9900000oR",
  stm_2 = "e8800000oR", stmia_2 = "e8800000oR", stmfd_2 = "e8800000oR",
  stmda_2 = "e8000000oR", stmfa_2 = "e8000000oR",
  stmdb_2 = "e9000000oR", stmea_2 = "e9000000oR",
  stmib_2 = "e9800000oR", stmed_2 = "e9800000oR",
  pop_1 = "e8bd0000R", push_1 = "e92d0000R",

  -- Branch instructions.
  b_1 = "ea000000B",
  bl_1 = "eb000000B",
  blx_1 = "e12fff30C",
  bx_1 = "e12fff10M",

  -- Miscellaneous instructions.
  nop_0 = "e1a00000",
  mrs_1 = "e10f0000D",
  bkpt_1 = "e1200070K", -- v5T
  svc_1 = "ef000000T", swi_1 = "ef000000T",
  ud_0 = "e7f001f0",

  -- VFP instructions.
  ["vadd.f32_3"] = "ee300a00dnm",
  ["vadd.f64_3"] = "ee300b00Gdnm",
  ["vsub.f32_3"] = "ee300a40dnm",
  ["vsub.f64_3"] = "ee300b40Gdnm",
  ["vmul.f32_3"] = "ee200a00dnm",
  ["vmul.f64_3"] = "ee200b00Gdnm",
  ["vnmul.f32_3"] = "ee200a40dnm",
  ["vnmul.f64_3"] = "ee200b40Gdnm",
  ["vmla.f32_3"] = "ee000a00dnm",
  ["vmla.f64_3"] = "ee000b00Gdnm",
  ["vmls.f32_3"] = "ee000a40dnm",
  ["vmls.f64_3"] = "ee000b40Gdnm",
  ["vnmla.f32_3"] = "ee100a40dnm",
  ["vnmla.f64_3"] = "ee100b40Gdnm",
  ["vnmls.f32_3"] = "ee100a00dnm",
  ["vnmls.f64_3"] = "ee100b00Gdnm",
  ["vdiv.f32_3"] = "ee800a00dnm",
  ["vdiv.f64_3"] = "ee800b00Gdnm",

  ["vabs.f32_2"] = "eeb00ac0dm",
  ["vabs.f64_2"] = "eeb00bc0Gdm",
  ["vneg.f32_2"] = "eeb10a40dm",
  ["vneg.f64_2"] = "eeb10b40Gdm",
  ["vsqrt.f32_2"] = "eeb10ac0dm",
  ["vsqrt.f64_2"] = "eeb10bc0Gdm",
  ["vcmp.f32_2"] = "eeb40a40dm",
  ["vcmp.f64_2"] = "eeb40b40Gdm",
  ["vcmpe.f32_2"] = "eeb40ac0dm",
  ["vcmpe.f64_2"] = "eeb40bc0Gdm",
  ["vcmpz.f32_1"] = "eeb50a40d",
  ["vcmpz.f64_1"] = "eeb50b40Gd",
  ["vcmpze.f32_1"] = "eeb50ac0d",
  ["vcmpze.f64_1"] = "eeb50bc0Gd",

  vldr_2 = "ed100a00dl|ed100b00Gdl",
  vstr_2 = "ed000a00dl|ed000b00Gdl",
  vldm_2 = "ec900a00or",
  vldmia_2 = "ec900a00or",
  vldmdb_2 = "ed100a00or",
  vpop_1 = "ecbd0a00r",
  vstm_2 = "ec800a00or",
  vstmia_2 = "ec800a00or",
  vstmdb_2 = "ed000a00or",
  vpush_1 = "ed2d0a00r",

  ["vmov.f32_2"] = "eeb00a40dm|eeb00a00dY",	-- #imm is VFPv3 only
  ["vmov.f64_2"] = "eeb00b40Gdm|eeb00b00GdY",	-- #imm is VFPv3 only
  vmov_2 = "ee100a10Dn|ee000a10nD",
  vmov_3 = "ec500a10DNm|ec400a10mDN|ec500b10GDNm|ec400b10GmDN",

  vmrs_0 = "eef1fa10",
  vmrs_1 = "eef10a10D",
  vmsr_1 = "eee10a10D",

  ["vcvt.s32.f32_2"] = "eebd0ac0dm",
  ["vcvt.s32.f64_2"] = "eebd0bc0dGm",
  ["vcvt.u32.f32_2"] = "eebc0ac0dm",
  ["vcvt.u32.f64_2"] = "eebc0bc0dGm",
  ["vcvtr.s32.f32_2"] = "eebd0a40dm",
  ["vcvtr.s32.f64_2"] = "eebd0b40dGm",
  ["vcvtr.u32.f32_2"] = "eebc0a40dm",
  ["vcvtr.u32.f64_2"] = "eebc0b40dGm",
  ["vcvt.f32.s32_2"] = "eeb80ac0dm",
  ["vcvt.f64.s32_2"] = "eeb80bc0GdFm",
  ["vcvt.f32.u32_2"] = "eeb80a40dm",
  ["vcvt.f64.u32_2"] = "eeb80b40GdFm",
  ["vcvt.f32.f64_2"] = "eeb70bc0dGm",
  ["vcvt.f64.f32_2"] = "eeb70ac0GdFm",

  -- VFPv4 only:
  ["vfma.f32_3"] = "eea00a00dnm",
  ["vfma.f64_3"] = "eea00b00Gdnm",
  ["vfms.f32_3"] = "eea00a40dnm",
  ["vfms.f64_3"] = "eea00b40Gdnm",
  ["vfnma.f32_3"] = "ee900a40dnm",
  ["vfnma.f64_3"] = "ee900b40Gdnm",
  ["vfnms.f32_3"] = "ee900a00dnm",
  ["vfnms.f64_3"] = "ee900b00Gdnm",

  -- NYI: Advanced SIMD instructions.

  -- NYI: I have no need for these instructions right now:
  -- swp, swpb, strex, ldrex, strexd, ldrexd, strexb, ldrexb, strexh, ldrexh
  -- msr, nopv6, yield, wfe, wfi, sev, dbg, bxj, smc, srs, rfe
  -- cps, setend, pli, pld, pldw, clrex, dsb, dmb, isb
  -- stc, ldc, mcr, mcr2, mrc, mrc2, mcrr, mcrr2, mrrc, mrrc2, cdp, cdp2
}

-- Add mnemonics for "s" variants.
do
  local t = {}
  for k,v in pairs(map_op) do
    if sub(v, -1) == "s" then
      local v2 = sub(v, 1, 2)..char(byte(v, 3)+1)..sub(v, 4, -2)
      t[sub(k, 1, -3).."s"..sub(k, -2)] = v2
    end
  end
  for k,v in pairs(t) do
    map_op[k] = v
  end
end

------------------------------------------------------------------------------

local function parse_gpr(expr)
  local tname, ovreg = match(expr, "^([%w_]+):(r1?[0-9])$")
  local tp = map_type[tname or expr]
  if tp then
    local reg = ovreg or tp.reg
    if not reg then
      werror("type `"..(tname or expr).."' needs a register override")
    end
    expr = reg
  end
  local r = match(expr, "^r(1?[0-9])$")
  if r then
    r = tonumber(r)
    if r <= 15 then return r, tp end
  end
  werror("bad register name `"..expr.."'")
end

local function parse_gpr_pm(expr)
  local pm, expr2 = match(expr, "^([+-]?)(.*)$")
  return parse_gpr(expr2), (pm == "-")
end

local function parse_vr(expr, tp)
  local t, r = match(expr, "^([sd])([0-9]+)$")
  if t == tp then
    r = tonumber(r)
    if r <= 31 then
      if t == "s" then return shr(r, 1), band(r, 1) end
      return band(r, 15), shr(r, 4)
    end
  end
  werror("bad register name `"..expr.."'")
end

local function parse_reglist(reglist)
  reglist = match(reglist, "^{%s*([^}]*)}$")
  if not reglist then werror("register list expected") end
  local rr = 0
  for p in gmatch(reglist..",", "%s*([^,]*),") do
    local rbit = shl(1, parse_gpr(gsub(p, "%s+$", "")))
    if band(rr, rbit) ~= 0 then
      werror("duplicate register `"..p.."'")
    end
    rr = rr + rbit
  end
  return rr
end

local function parse_vrlist(reglist)
  local ta, ra, tb, rb = match(reglist,
			   "^{%s*([sd])([0-9]+)%s*%-%s*([sd])([0-9]+)%s*}$")
  ra, rb = tonumber(ra), tonumber(rb)
  if ta and ta == tb and ra and rb and ra <= 31 and rb <= 31 and ra <= rb then
    local nr = rb+1 - ra
    if ta == "s" then
      return shl(shr(ra,1),12)+shl(band(ra,1),22) + nr
    else
      return shl(band(ra,15),12)+shl(shr(ra,4),22) + nr*2 + 0x100
    end
  end
  werror("register list expected")
end

local function parse_imm(imm, bits, shift, scale, signed)
  imm = match(imm, "^#(.*)$")
  if not imm then werror("expected immediate operand") end
  local n = tonumber(imm)
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
  local n = tonumber(imm)
  if n then
    local m = band(n)
    for i=0,-15,-1 do
      if shr(m, 8) == 0 then return m + shl(band(i, 15), 8) end
      m = ror(m, 2)
    end
    werror("out of range immediate `"..imm.."'")
  else
    waction("IMM12", 0, imm)
    return 0
  end
end

local function parse_imm16(imm)
  imm = match(imm, "^#(.*)$")
  if not imm then werror("expected immediate operand") end
  local n = tonumber(imm)
  if n then
    if shr(n, 16) == 0 then return band(n, 0x0fff) + shl(band(n, 0xf000), 4) end
    werror("out of range immediate `"..imm.."'")