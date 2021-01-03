------------------------------------------------------------------------------
-- DynASM MIPS32/MIPS64 module.
--
-- Copyright (C) 2005-2022 Mike Pall. All rights reserved.
-- See dynasm.lua for full copyright notice.
------------------------------------------------------------------------------

local mips64 = mips64
local mipsr6 = _map_def.MIPSR6

-- Module information:
local _info = {
  arch =	mips64 and "mips64" or "mips",
  description =	"DynASM MIPS32/MIPS64 module",
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
local assert, setmetatable = assert, setmetatable
local _s = string
local sub, format, byte, char = _s.sub, _s.format, _s.byte, _s.char
local match, gmatch = _s.match, _s.gmatch
local concat, sort = table.concat, table.sort
local bit = bit or require("bit")
local band, shl, shr, sar = bit.band, bit.lshift, bit.rshift, bit.arshift
local tohex = bit.tohex

-- Inherited tables and callbacks.
local g_opt, g_arch
local wline, werror, wfatal, wwarn

-- Action name list.
-- CHECK: Keep this in sync with the C code!
local action_names = {
  "STOP", "SECTION", "ESC", "REL_EXT",
  "ALIGN", "REL_LG", "LABEL_LG",
  "REL_PC", "LABEL_PC", "IMM", "IMMS",
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
  wputxw(0xff000000 + w * 0x10000 + (val or 0))
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
  if n >= 0xff000000 then waction("ESC") end
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
local map_archdef = { sp="r29", ra="r31" } -- Ext. register name -> int. name.

local map_type = {}		-- Type name -> { ctype, reg }
local ctypenum = 0		-- Type number (for Dt... macros).

-- Reverse defines for registers.
function _M.revdef(s)
  if s == "r29" then return "sp"
  elseif s == "r31" then return "ra" end
  return s
end

------------------------------------------------------------------------------

-- Template strings for MIPS instructions.
local map_op = {
  -- First-level opcodes.
  j_1 =		"08000000J",
  jal_1 =	"0c000000J",
  b_1 =		"10000000B",
  beqz_2 =	"10000000SB",
  beq_3 =	"10000000STB",
  bnez_2 =	"14000000SB",
  bne_3 =	"14000000STB",
  blez_2 =	"18000000SB",
  bgtz_2 =	"1c000000SB",
  li_2 =	"24000000TI",
  addiu_3 =	"24000000TSI",
  slti_3 =	"28000000TSI",
  sltiu_3 =	"2c000000TSI",
  andi_3 =	"30000000TSU",
  lu_2 =	"34000000TU",
  ori_3 =	"34000000TSU",
  xori_3 =	"38000000TSU",
  lui_2 =	"3c000000TU",
  daddiu_3 =	mips64 and "64000000TSI",
  ldl_2 =	mips64 and "68000000TO",
  ldr_2 =	mips64 and "6c000000TO",
  lb_2 =	"80000000TO",
  lh_2 =	"84000000TO",
  lw_2 =	"8c000000TO",
  lbu_2 =	"90000000TO",
  lhu_2 =	"94000000TO",
  lwu_2 =	mips64 and "9c000000TO",
  sb_2 =	"a0000000TO",
  sh_2 =	"a4000000TO",
  sw_2 =	"ac000000TO",
  lwc1_2 =	"c4000000HO",
  ldc1_2 =	"d4000000HO",
  ld_2 =	mips64 and "dc000000TO",
  swc1_2 =	"e4000000HO",
  sdc1_2 =	"f4000000HO",
  sd_2 =	mips64 and "fc000000TO",

  -- Opcode SPECIAL.
  nop_0 =	"00000000",
  sll_3 =	"00000000DTA",
  sextw_2 =	"00000000DT",
  srl_3 =	"00000002DTA",
  rotr_3 =	"00200002DTA",
  sra_3 =	"00000003DTA",
  sllv_3 =	"00000004DTS",
  srlv_3 =	"00000006DTS",
  rotrv_3 =	"00000046DTS",
  drotrv_3 =	mips64 and "00000056DTS",
  srav_3 =	"00000007DTS",
  jalr_1 =	"0000f809S",
  jalr_2 =	"00000009DS",
  syscall_0 =	"0000000c",
  syscall_1 =	"0000000cY",
  break_0 =	"0000000d",
  break_1 =	"0000000dY",
  sync_0 =	"0000000f",
  dsllv_3 =	mips64 and "00000014DTS",
  dsrlv_3 =	mips64 and "00000016DTS",
  dsrav_3 =	mips64 and "00000017DTS",
  add_3 =	"00000020DST",
  move_2 =	mips64 and "00000025DS" or "00000021DS",
  addu_3 =	"00000021DST",
  sub_3 =	"00000022DST",
  negu_2 =	mips64 and "0000002fDT" or "00000023DT",
  subu_3 =	"00000023DST",
  and_3 =	"00000024DST",
  or_3 =	"00000025DST",
  xor_3 =	"00000026DST",
  not_2 =	"00000027DS",
  nor_3 =	"00000027DST",
  slt_3 =	"0000002aDST",
  sltu_3 =	"0000002bDST",
  dadd_3 =	mips64 and "0000002cDST",
  daddu_3 =	mips64 and "0000002dDST",
  dsub_3 =	mips64 and "0000002eDST",
  dsubu_3 =	mips64 and "0000002fDST",
  tge_2 =	"00000030ST",
  tge_3 =	"00000030STZ",
  tgeu_2 =	"00000031ST",
  tgeu_3 =	"00000031STZ",
  tlt_2 =	"00000032ST",
  tlt_3 =	"00000032STZ",
  tltu_2 =	"00000033ST",
  tltu_3 =	"00000033STZ",
  teq_2 =	"00000034ST",
  teq_3 =	"00000034STZ",
  tne_2 =	"00000036ST",
  tne_3 =	"00000036STZ",
  dsll_3 =	mips64 and "00000038DTa",
  dsrl_3 =	mips64 and "0000003aDTa",
  drotr_3 =	mips64 and "0020003aDTa",
  dsra_3 =	mips64 and "0000003bDTa",
  dsll32_3 =	mips64 and "0000003cDTA",
  dsrl32_3 =	mips64 and "0000003eDTA",
  drotr32_3 =	mips64 and "0020003eDTA",
  dsra32_3 =	mips64 and "0000003fDTA",

  -- Opcode REGIMM.
  bltz_2 =	"04000000SB",
  bgez_2 =	"04010000SB",
  bltzl_2 =	"04020000SB",
  bgezl_2 =	"04030000SB",
  bal_1 =	"04110000B",
  synci_1 =	"041f0000O",

  -- Opcode SPECIAL3.
  ext_4 =	"7c000000TSAM", -- Note: last arg is msbd = size-1
  dextm_4 =	mips64 and "7c000001TSAM", -- Args: pos    | size-1-32
  dextu_4 =	mips64 and "7c000002TSAM", -- Args: pos-32 | size-1
  dext_4 =	mips64 and "7c000003TSAM", -- Args: pos    | size-1
  zextw_2 =	mips64 and "7c00f803TS",
  ins_4 =	"7c000004TSAM", -- Note: last arg is msb = pos+size-1
  dinsm_4 =	mips64 and "7c000005TSAM", -- Args: pos    | pos+size-33
  dinsu_4 =	mips64 and "7c000006TSAM", -- Args: pos-32 | pos+size-33
  dins_4 =	mips64 and "7c000007TSAM", -- Args: pos    | pos+size-1
  wsbh_2 =	"7c0000a0DT",
  dsbh_2 =	mips64 and "7c0000a4DT",
  dshd_2 =	mips64 and "7c000164DT",
  seb_2 =	"7c000420DT",
  seh_2 =	"7c000620DT",
  rdhwr_2 =	"7c00003bTD",

  -- Opcode COP0.
  mfc0_2 =	"40000000TD",
  mfc0_3 =	"40000000TDW",
  dmfc0_2 =	mips64 and "40200000TD",
  dmfc0_3 =	mips64 and "40200000TDW",
  mtc0_2 =	"40800000TD",
  mtc0_3 =	"40800000TDW",
  dmtc0_2 =	mips64 and "40a00000TD",
  dmtc0_3 =	mips64 and "40a00000TDW",
  rdpgpr_2 =	"41400000DT",
  di_0 =	"41606000",
  di_1 =	"41606000T",
  ei_0 =	"41606020",
  ei_1 =	"41606020T",
  wrpgpr_2 =	"41c00000DT",
  tlbr_0 =	"42000001",
  tlbwi_0 =	"42000002",
  tlbwr_0 =	"42000006",
  tlbp_0 =	"42000008",
  eret_0 =	"42000018",
  deret_0 =	"4200001f",
  wait_0 =	"42000020",

  -- Opcode COP1.
  mfc1_2 =	"44000000TG",
  dmfc1_2 =	mips64 and "44200000TG",
  cfc1_2 =	"44400000TG",
  mfhc1_2 =	"44600000TG",
  mtc1_2 =	"44800000TG",
  dmtc1_2 =	mips64 and "44a00000TG",
  ctc1_2 =	"44c00000TG",
  mthc1_2 =	"44e00000TG",

  ["add.s_3"] =		"46000000FGH",
  ["sub.s_3"] =		"46000001FGH",
  ["mul.s_3"] =		"46000002FGH",
  ["div.s_3"] =		"46000003FGH",
  ["sqrt.s_2"] =	"46000004FG",
  ["abs.s_2"] =		"46000005FG",
  ["mov.s_2"] =		"46000006FG",
  ["neg.s_2"] =		"46000007FG",
  ["round.l.s_2"] =	"46000008FG",
  ["trunc.l.s_2"] =	"46000009FG",
  ["ceil.l.s_2"] =	"4600000aFG",
  ["floor.l.s_2"] =	"4600000bFG",
  ["round.w.s_2"] =	"4600000cFG",
  ["trunc.w.s_2"] =	"4600000dFG",
  ["ceil.w.s_2"] =	"4600000eFG",
  ["floor.w.s_2"] =	"4600000fFG",
  ["recip.s_2"] =	"46000015FG",
  ["rsqrt.s_2"] =	"46000016FG",
  ["cvt.d.s_2"] =	"46000021FG",
  ["cvt.w.s_2"] =	"46000024FG",
  ["cvt.l.s_2"] =	"46000025FG",
  ["add.d_3"] =		"46200000FGH",
  ["sub.d_3"] =		"46200001FGH",
  ["mul.d_3"] =		"46200002FGH",
  ["div.d_3"] =		"46200003FGH",
  ["sqrt.d_2"] =	"46200004FG",
  ["abs.d_2"] =		"46200005FG",
  ["mov.d_2"] =		"46200006FG",
  ["neg.d_2"] =		"46200007FG",
  ["round.l.d_2"] =	"46200008FG",
  ["trunc.l.d_2"] =	"46200009FG",
  ["ceil.l.d_2"] =	"4620000aFG",
  ["floor.l.d_2"] =	"4620000bFG",
  ["round.w.d_2"] =	"4620000cFG",
  ["trunc.w.d_2"] =	"4620000dFG",
  ["ceil.w.d_2"] =	"4620000eFG",
  ["floor.w.d_2"] =	"4620000fFG",
  ["recip.d_2"] =	"46200015FG",
  ["rsqrt.d_2"] =	"46200016FG",
  ["cvt.s.d_2"] =	"46200020FG",
  ["cvt.w.d_2"] =	"46200024FG",
  ["cvt.l.d_2"] =	"46200025FG",
  ["cvt.s.w_2"] =	"46800020FG",
  ["cvt.d.w_2"] =	"46800021FG",
  ["cvt.s.l_2"] =	"46a00020FG",
  ["cvt.d.l_2"] =	"46a00021FG",
}

if mipsr6 then -- Instructions added with MIPSR6.

  for k,v in pairs({

    -- Add immediate to upper bits.
    aui_3 =	"3c000000TSI",
    daui_3 =	mips64 and "74000000TSI",
    dahi_2 =	mips64 and "04060000SI",
    dati_2 =	mips64 and "041e0000SI",

    -- TODO: addiupc, auipc, aluipc, lwpc, lwupc, ldpc.

    -- Compact branches.
    blezalc_2 =	"18000000TB",	-- rt != 0.
    bgezalc_2 =	"18000000T=SB",	-- rt != 0.
    bgtzalc_2 =	"1c000000TB",	-- rt != 0.
    bltzalc_2 =	"1c000000T=SB",	-- rt != 0.

    blezc_2 =	"58000000TB",	-- rt != 0.
    bgezc_2 =	"58000000T=SB",	-- rt != 0.
    bgec_3 =	"58000000STB",	-- rs != rt.
    blec_3 =	"58000000TSB",	-- rt != rs.

    bgtzc_2 =	"5c000000TB",	-- rt != 0.
    bltzc_2 =	"5c000000T=SB",	-- rt != 0.
    bltc_3 =	"5c000000STB",	-- rs != rt.
    bgtc_3 =	"5c000000TSB",	-- rt != rs.

    bgeuc_3 =	"18000000STB",	-- rs != rt.
    bleuc_3 =	"18000000TSB",	-- rt != rs.
    bltuc_3 =	"1c000000STB",	-- rs != rt.
    bgtuc_3 =	"1c000000TSB",	-- rt != rs.

    beqzalc_2 =	"20000000TB",	-- rt != 0.
    bnezalc_2 =	"60000000TB",	-- rt != 0.
    beqc_3 =	"20000000STB",	-- rs < rt.
    bnec_3 =	"60000000STB",	-- rs < rt.
    bovc_3 =	"20000000STB",	-- rs >= rt.
    bnvc_3 =	"60000000STB",	-- rs >= rt.

    beqzc_2 =	"d8000000SK",	-- rs != 0.
    bnezc_2 =	"f8000000SK",	-- rs != 0.
    jic_2 =	"d8000000TI",
    jialc_2 =	"f8000000TI",
    bc_1 =	"c8000000L",
    balc_1 =	"e8000000L",

    -- Opcode SPECIAL.
    jr_1 =	"00000009S",
    sdbbp_0 =	"0000000e",
    sdbbp_1 =	"0000000eY",
    lsa_4 =	"00000005DSTA",
    dlsa_4 =	mips64 and "00000015DSTA",
    seleqz_3 =	"00000035DST",
    selnez_3 =	"00000037DST",
    clz_2 =	"00000050DS",
    clo_2 =	"00000051DS",
    dclz_2 =	mips64 and "00000052DS",
    dclo_2 =	mips64 and "00000053DS",
    mul_3 =	"00000098DST",
    muh_3 =	"000000d8DST",
    mulu_3 =	"00000099DST",
    muhu_3 =	"000000d9DST",
    div_3 =	"0000009aDST",
    mod_3 =	"000000daDST",
    divu_3 =	"0000009bDST",
    modu_3 =	"000000dbDST",
    dmul_3 =	mips64 and "0000009cDST",
    dmuh_3 =	mips64 and "000000dcDST",
    dmulu_3 =	mips64 and "0000009dDST",
    dmuhu_3 =	mips64 and "000000ddDST",
    ddiv_3 =	mips64 and "0000009eDST",
    dmod_3 =	mips64 and "000000deDST",
    ddivu_3 =	mips64 and "0000009fDST",
    dmodu_3 =	mips64 and "000000dfDST",

    -- Opcode SPECIAL3.
    align_4 =		"7c000220DSTA",
    dalign_4 =		mips64 and "7c000224DSTA",
    bitswap_2 =		"7c000020DT",
    dbitswap_2 =	mips64 and "7c000024DT",

    -- Opcode COP1.
    bc1eqz_2 =	"45200000HB",
    bc1nez_2 =	"45a00000HB",

    ["sel.s_3"] =	"46000010FGH",
    ["seleqz.s_3"] =	"46000014FGH",
    ["selnez.s_3"] =	"46000017FGH",
    ["maddf.s_3"] =	"46000018FGH",
    ["msubf.s_3"] =	"46000019FGH",
    ["rint.s_2"] =	"4600001aFG",
    ["class.s_2"] =	"4600001bFG",
    ["min.s_3"] =	"4600001cFGH",
    ["mina.s_3"] =	"4600001dFGH",
    ["max.s_3"] =	"4600001eFGH",
    ["maxa.s_3"] =	"4600001fFGH",
    ["cmp.af.s_3"] =	"46800000FGH",
    ["cmp.un.s_3"] =	"46800001FGH",
    ["cmp.or.s_3"] =	"46800011FGH",
    ["cmp.eq.s_3"] =	"46800002FGH",
    ["cmp.une.s_3"] =	"46800012FGH",
    ["cmp.ueq.s_3"] =	"46800003FGH",
    ["cmp.ne.s_3"] =	"46800013FGH",
    ["cmp.lt.s_3"] =	"46800004FGH",
    ["cmp.ult.s_3"] =	"46800005FGH",
    ["cmp.le.s_3"] =	"46800006FGH",
    ["cmp.ule.s_3"] =	"46800007FGH",
    ["cmp.saf.s_3"] =	"46800008FGH",
    ["cmp.sun.s_3"] =	"46800009FGH",
    ["cmp.sor.s_3"] =	"46800019FGH",
    ["cmp.seq.s_3"] =	"4680000aFGH",
    ["cmp.sune.s_3"] =	"4680001aFGH",
    ["cmp.sueq.s_3"] =	"4680000bFGH",
    ["cmp.sne.s_3"] =	"4680001bFGH",
    ["cmp.slt.s_3"] =	"4680000cFGH",
    ["cmp.sult.s_3"] =	"4680000dFGH",
    ["cmp.sle.s_3"] =	"4680000eFGH",
    ["cmp.sule.s_3"] =	"4680000fFGH",

    ["sel.d_3"] =	"46200010FGH",
    ["seleqz.d_3"] =	"46200014FGH",
    ["selnez.d_3"] =	"46200017FGH",
    ["maddf.d_3"] =	"46200018FGH",
    ["msubf.d_3"] =	"46200019FGH",
    ["rint.d_2"] =	"4620001aFG",
    ["class.d_2"] =	"4620001bFG",
    ["min.d_3"] =	"4620001cFGH",
    ["mina.d_3"] =	"4620001dFGH",
    ["max.d_3"] =	"4620001eFGH",
    ["maxa.d_3"] =	"4620001fFGH",
    ["cmp.af.d_3"] =	"46a00000FGH",
    ["cmp.un.d_3"] =	"46a00001FGH",
    ["cmp.or.d_3"] =	"46a00011FGH",
    ["cmp.eq.d_3"] =	"46a00002FGH",
    ["cmp.une.d_3"] =	"46a00012FGH",
    ["cmp.ueq.d_3"] =	"46a00003FGH",
    ["cmp.ne.d_3"] =	"46a00013FGH",
    ["cmp.lt.d_3"] =	"46a00004FGH",
    ["cmp.ult.d_3"] =	"46a00005FGH",
    ["cmp.le.d_3"] =	"46a00006FGH",
    ["cmp.ule.d_3"] =	"46a00007FGH",
    ["cmp.saf.d_3"] =	"46a00008FGH",
    ["cmp.sun.d_3"] =	"46a00009FGH",
    ["cmp.sor.d_3"] =	"46a00019FGH",
    ["cmp.seq.d_3"] =	"46a0000aFGH",
    ["cmp.sune.d_3"] =	"46a0001aFGH",
    ["cmp.sueq.d_3"] =	"46a0000bFGH",
    ["cmp.sne.d_3"] =	"46a0001bFGH",
    ["cmp.slt.d_3"] =	"46a0000cFGH",
    ["cmp.sult.d_3"] =	"46a0000dFGH",
    ["cmp.sle.d_3"] =	"46a0000eFGH",
    ["cmp.sule.d_3"] =	"46a0000fFGH",

  }) do map_op[k] = v end

else -- Instructions removed by MIPSR6.

  for k,v in pairs({
    -- Traps, don't use.
    addi_3 =	"20000000TSI",
    daddi_3 =	mips64 and "60000000TSI",

    -- Branch on likely, don't use.
    beqzl_2 =	"50000000SB",
    beql_3 =	"50000000STB",
    bnezl_2 =	"54000000SB",
    bnel_3 =	"54000000STB",
    blezl_2 =	"58000000SB",
    bgtzl_2 =	"5c000000SB",

    lwl_2 =	"88000000TO",
    lwr_2 =	"98000000TO",
    swl_2 =	"a8000000TO",
    sdl_2 =	mips64 and "b0000000TO",
    sdr_2 =	mips64 and "b1000000TO",
    swr_2 =	"b8000000TO",
    cache_2 =	"bc000000NO",
    ll_2 =	"c0000000TO",
    pref_2 =	"cc000000NO",
    sc_2 =	"e0000000TO",
    scd_2 =	mips64 and "f0000000TO",

    -- Opcode SPECIAL.
    movf_2 =	"00000001DS",
    movf_3 =	"00000001DSC",
    movt_2 =	"00010001DS",
    movt_3 =	"00010001DSC",
    jr_1 =	"00000008S",
    movz_3 =	"0000000aDST",
    movn_3 =	"0000000bDST",
    mfhi_1 =	"00000010D",
    mthi_1 =	"00000011S",
    mflo_1 =	"00000012D",
    mtlo_1 =	"00000013S",
    mult_2 =	"00000018ST",
    multu_2 =	"00000019ST",
    div_3 =	"0000001aST",
    divu_3 =	"0000001bST",
    ddiv_3 =	mips64 and "0000001eST",
    ddivu_3 =	mips64 and "0000001fST",
    dmult_2 =	mips64 and "0000001cST",
    dmultu_2 =	mips64 and "0000001dST",

    -- Opcode REGIMM.
    tgei_2 =	"04080000SI",
    tgeiu_2 =	"04090000SI",
    tlti_2 =	"040a0000SI",
    tltiu_2 =	"040b0000SI",
    teqi_2 =	"040c0000SI",
    tnei_2 =	"040e0000SI",
    bltzal_2 =	"04100000SB",
    bgezal_2 =	"04110000SB",
    bltzall_2 =	"04120000SB",
    bgezall_2 =	"04130000SB",

    -- Opcode SPECIAL2.
    madd_2 =	"70000000ST",
    maddu_2 =	"70000001ST",
    mul_3 =	"70000002DST",
    msub_2 =	"70000004ST",
    msubu_2 =	"70000005ST",
    clz_2 =	"70000020D=TS",
    clo_2 =	"70000021D=TS",
    dclz_2 =	mips64 and "70000024D=TS",
    dclo_2 =	mips64 and "70000025D=TS",
    sdbbp_0 =	"7000003f",
    sdbbp_1 =	"7000003fY",

    -- Opcode COP1.
    bc1f_1 =	"45000000B",
    bc1f_2 =	"45000000CB",
    bc1t_1 =	"45010000B",
    bc1t_2 =	"45010000CB",
    bc1fl_1 =	"45020000B",
    bc1fl_2 =	"45020000CB",
    bc1tl_1 =	"45030000B",
    bc1tl_2 =	"45030000CB",

    ["movf.s_2"] =	"46000011FG",
    ["movf.s_3"] =	"46000011FGC",
    ["movt.s_2"] =	"46010011FG",
    ["movt.s_3"] =	"46010011FGC",
    ["movz.s_3"] =	"46000012FGT",
    ["movn.s_3"] =	"46000013FGT",
    ["cvt.ps.s_3"] =	"46000026FGH",
    ["c.f.s_2"] =	"46000030GH",
    ["c.f.s_3"] =	"46000030VGH",
    ["c.un.s_2"] =	"46000031GH",
    ["c.un.s_3"] =	"46000031VGH",
    ["c.eq.s_2"] =	"46000032GH",
    ["c.eq.s_3"] =	"46000032VGH",
    ["c.ueq.s_2"] =	"46000033GH",
    ["c.ueq.s_3"] =	"46000033VGH",
    ["c.olt.s_2"] =	"46000034GH",
    ["c.olt.s_3"] =	"46000034VGH",
    ["c.ult.s_2"] =	"46000035GH",
    ["c.ult.s_3"] =	"46000035VGH",
    ["c.ole.s_2"] =	"46000036GH",
    ["c.ole.s_3"] =	"46000036VGH",
    ["c.ule.s_2"] =	"46000037GH",
    ["c.ule.s_3"] =	"46000037VGH",
    ["c.sf.s_2"] =	"46000038GH",
    ["c.sf.s_3"] =	"46000038VGH",
    ["c.ngle.s_2"] =	"46000039GH",
    ["c.ngle.s_3"] =	"46000039VGH",
    ["c.seq.s_2"] =	"4600003aGH",
    ["c.seq.s_3"] =	"4600003aVGH",
    ["c.ngl.s_2"] =	"4600003bGH",
    ["c.ngl.s_3"] =	"4600003bVGH",
    ["c.lt.s_2"] =	"4600003cGH",
    ["c.lt.s_3"] =	"4600003cVGH",
    ["c.nge.s_2"] =	"4600003dGH",
    ["c.nge.s_3"] =	"4600003dVGH",
    ["c.le.s_2"] =	"4600003eGH",
    ["c.le.s_3"] =	"4600003eVGH",
    ["c.ngt.s_2"] =	"4600003fGH",
    ["c.ngt.s_3"] =	"4600003fVGH",
    ["movf.d_2"] =	"46200011F