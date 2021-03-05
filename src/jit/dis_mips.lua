
----------------------------------------------------------------------------
-- LuaJIT MIPS disassembler module.
--
-- Copyright (C) 2005-2022 Mike Pall. All rights reserved.
-- Released under the MIT/X license. See Copyright Notice in luajit.h
----------------------------------------------------------------------------
-- This is a helper module used by the LuaJIT machine code dumper module.
--
-- It disassembles all standard MIPS32R1/R2 instructions.
-- Default mode is big-endian, but see: dis_mipsel.lua
------------------------------------------------------------------------------

local type = type
local byte, format = string.byte, string.format
local match, gmatch = string.match, string.gmatch
local concat = table.concat
local bit = require("bit")
local band, bor, tohex = bit.band, bit.bor, bit.tohex
local lshift, rshift, arshift = bit.lshift, bit.rshift, bit.arshift

------------------------------------------------------------------------------
-- Extended opcode maps common to all MIPS releases
------------------------------------------------------------------------------

local map_srl = { shift = 21, mask = 1, [0] = "srlDTA", "rotrDTA", }
local map_srlv = { shift = 6, mask = 1, [0] = "srlvDTS", "rotrvDTS", }

local map_cop0 = {
  shift = 25, mask = 1,
  [0] = {
    shift = 21, mask = 15,
    [0] = "mfc0TDW", [4] = "mtc0TDW",
    [10] = "rdpgprDT",
    [11] = { shift = 5, mask = 1, [0] = "diT0", "eiT0", },
    [14] = "wrpgprDT",
  }, {
    shift = 0, mask = 63,
    [1] = "tlbr", [2] = "tlbwi", [6] = "tlbwr", [8] = "tlbp",
    [24] = "eret", [31] = "deret",
    [32] = "wait",
  },
}

------------------------------------------------------------------------------
-- Primary and extended opcode maps for MIPS R1-R5
------------------------------------------------------------------------------

local map_movci = { shift = 16, mask = 1, [0] = "movfDSC", "movtDSC", }

local map_special = {
  shift = 0, mask = 63,
  [0] = { shift = 0, mask = -1, [0] = "nop", _ = "sllDTA" },
  map_movci,	map_srl,	"sraDTA",
  "sllvDTS",	false,		map_srlv,	"sravDTS",
  "jrS",	"jalrD1S",	"movzDST",	"movnDST",
  "syscallY",	"breakY",	false,		"sync",
  "mfhiD",	"mthiS",	"mfloD",	"mtloS",
  "dsllvDST",	false,		"dsrlvDST",	"dsravDST",
  "multST",	"multuST",	"divST",	"divuST",
  "dmultST",	"dmultuST",	"ddivST",	"ddivuST",
  "addDST",	"addu|moveDST0", "subDST",	"subu|neguDS0T",
  "andDST",	"or|moveDST0",	"xorDST",	"nor|notDST0",
  false,	false,		"sltDST",	"sltuDST",
  "daddDST",	"dadduDST",	"dsubDST",	"dsubuDST",
  "tgeSTZ",	"tgeuSTZ",	"tltSTZ",	"tltuSTZ",
  "teqSTZ",	false,		"tneSTZ",	false,
  "dsllDTA",	false,		"dsrlDTA",	"dsraDTA",
  "dsll32DTA",	false,		"dsrl32DTA",	"dsra32DTA",
}

local map_special2 = {
  shift = 0, mask = 63,
  [0] = "maddST", "madduST",	"mulDST",	false,
  "msubST",	"msubuST",
  [32] = "clzDS", [33] = "cloDS",
  [63] = "sdbbpY",
}

local map_bshfl = {
  shift = 6, mask = 31,
  [2] = "wsbhDT",
  [16] = "sebDT",
  [24] = "sehDT",
}

local map_dbshfl = {
  shift = 6, mask = 31,
  [2] = "dsbhDT",
  [5] = "dshdDT",
}

local map_special3 = {
  shift = 0, mask = 63,
  [0]  = "extTSAK", [1]  = "dextmTSAP", [3]  = "dextTSAK",
  [4]  = "insTSAL", [6]  = "dinsuTSEQ", [7]  = "dinsTSAL",
  [32] = map_bshfl, [36] = map_dbshfl,  [59] = "rdhwrTD",
}

local map_regimm = {
  shift = 16, mask = 31,
  [0] = "bltzSB",	"bgezSB",	"bltzlSB",	"bgezlSB",
  false,	false,		false,		false,
  "tgeiSI",	"tgeiuSI",	"tltiSI",	"tltiuSI",
  "teqiSI",	false,		"tneiSI",	false,
  "bltzalSB",	"bgezalSB",	"bltzallSB",	"bgezallSB",
  false,	false,		false,		false,
  false,	false,		false,		false,
  false,	false,		false,		"synciSO",
}

local map_cop1s = {
  shift = 0, mask = 63,
  [0] = "add.sFGH",	"sub.sFGH",	"mul.sFGH",	"div.sFGH",
  "sqrt.sFG",		"abs.sFG",	"mov.sFG",	"neg.sFG",
  "round.l.sFG",	"trunc.l.sFG",	"ceil.l.sFG",	"floor.l.sFG",
  "round.w.sFG",	"trunc.w.sFG",	"ceil.w.sFG",	"floor.w.sFG",
  false,
  { shift = 16, mask = 1, [0] = "movf.sFGC", "movt.sFGC" },
  "movz.sFGT",	"movn.sFGT",
  false,	"recip.sFG",	"rsqrt.sFG",	false,
  false,	false,		false,		false,
  false,	false,		false,		false,
  false,	"cvt.d.sFG",	false,		false,
  "cvt.w.sFG",	"cvt.l.sFG",	"cvt.ps.sFGH",	false,
  false,	false,		false,		false,
  false,	false,		false,		false,
  "c.f.sVGH",	"c.un.sVGH",	"c.eq.sVGH",	"c.ueq.sVGH",
  "c.olt.sVGH",	"c.ult.sVGH",	"c.ole.sVGH",	"c.ule.sVGH",
  "c.sf.sVGH",	"c.ngle.sVGH",	"c.seq.sVGH",	"c.ngl.sVGH",
  "c.lt.sVGH",	"c.nge.sVGH",	"c.le.sVGH",	"c.ngt.sVGH",
}

local map_cop1d = {
  shift = 0, mask = 63,
  [0] = "add.dFGH",	"sub.dFGH",	"mul.dFGH",	"div.dFGH",
  "sqrt.dFG",		"abs.dFG",	"mov.dFG",	"neg.dFG",
  "round.l.dFG",	"trunc.l.dFG",	"ceil.l.dFG",	"floor.l.dFG",
  "round.w.dFG",	"trunc.w.dFG",	"ceil.w.dFG",	"floor.w.dFG",
  false,
  { shift = 16, mask = 1, [0] = "movf.dFGC", "movt.dFGC" },
  "movz.dFGT",	"movn.dFGT",
  false,	"recip.dFG",	"rsqrt.dFG",	false,
  false,	false,		false,		false,
  false,	false,		false,		false,
  "cvt.s.dFG",	false,		false,		false,
  "cvt.w.dFG",	"cvt.l.dFG",	false,		false,
  false,	false,		false,		false,
  false,	false,		false,		false,
  "c.f.dVGH",	"c.un.dVGH",	"c.eq.dVGH",	"c.ueq.dVGH",
  "c.olt.dVGH",	"c.ult.dVGH",	"c.ole.dVGH",	"c.ule.dVGH",
  "c.df.dVGH",	"c.ngle.dVGH",	"c.deq.dVGH",	"c.ngl.dVGH",
  "c.lt.dVGH",	"c.nge.dVGH",	"c.le.dVGH",	"c.ngt.dVGH",
}

local map_cop1ps = {
  shift = 0, mask = 63,
  [0] = "add.psFGH",	"sub.psFGH",	"mul.psFGH",	false,
  false,		"abs.psFG",	"mov.psFG",	"neg.psFG",
  false,		false,		false,		false,
  false,		false,		false,		false,
  false,
  { shift = 16, mask = 1, [0] = "movf.psFGC", "movt.psFGC" },
  "movz.psFGT",	"movn.psFGT",
  false,	false,		false,		false,
  false,	false,		false,		false,
  false,	false,		false,		false,
  "cvt.s.puFG",	false,		false,		false,
  false,	false,		false,		false,
  "cvt.s.plFG",	false,		false,		false,
  "pll.psFGH",	"plu.psFGH",	"pul.psFGH",	"puu.psFGH",
  "c.f.psVGH",	"c.un.psVGH",	"c.eq.psVGH",	"c.ueq.psVGH",
  "c.olt.psVGH", "c.ult.psVGH",	"c.ole.psVGH",	"c.ule.psVGH",
  "c.psf.psVGH", "c.ngle.psVGH", "c.pseq.psVGH", "c.ngl.psVGH",
  "c.lt.psVGH",	"c.nge.psVGH",	"c.le.psVGH",	"c.ngt.psVGH",
}

local map_cop1w = {
  shift = 0, mask = 63,
  [32] = "cvt.s.wFG", [33] = "cvt.d.wFG",
}

local map_cop1l = {
  shift = 0, mask = 63,
  [32] = "cvt.s.lFG", [33] = "cvt.d.lFG",
}

local map_cop1bc = {
  shift = 16, mask = 3,
  [0] = "bc1fCB", "bc1tCB",	"bc1flCB",	"bc1tlCB",
}

local map_cop1 = {
  shift = 21, mask = 31,
  [0] = "mfc1TG", "dmfc1TG",	"cfc1TG",	"mfhc1TG",
  "mtc1TG",	"dmtc1TG",	"ctc1TG",	"mthc1TG",
  map_cop1bc,	false,		false,		false,
  false,	false,		false,		false,
  map_cop1s,	map_cop1d,	false,		false,
  map_cop1w,	map_cop1l,	map_cop1ps,
}

local map_cop1x = {
  shift = 0, mask = 63,
  [0] = "lwxc1FSX",	"ldxc1FSX",	false,		false,
  false,	"luxc1FSX",	false,		false,
  "swxc1FSX",	"sdxc1FSX",	false,		false,
  false,	"suxc1FSX",	false,		"prefxMSX",
  false,	false,		false,		false,
  false,	false,		false,		false,
  false,	false,		false,		false,
  false,	false,		"alnv.psFGHS",	false,
  "madd.sFRGH",	"madd.dFRGH",	false,		false,
  false,	false,		"madd.psFRGH",	false,
  "msub.sFRGH",	"msub.dFRGH",	false,		false,
  false,	false,		"msub.psFRGH",	false,
  "nmadd.sFRGH", "nmadd.dFRGH",	false,		false,
  false,	false,		"nmadd.psFRGH",	false,
  "nmsub.sFRGH", "nmsub.dFRGH",	false,		false,
  false,	false,		"nmsub.psFRGH",	false,
}

local map_pri = {
  [0] = map_special,	map_regimm,	"jJ",	"jalJ",
  "beq|beqz|bST00B",	"bne|bnezST0B",		"blezSB",	"bgtzSB",
  "addiTSI",	"addiu|liTS0I",	"sltiTSI",	"sltiuTSI",
  "andiTSU",	"ori|liTS0U",	"xoriTSU",	"luiTU",
  map_cop0,	map_cop1,	false,		map_cop1x,
  "beql|beqzlST0B",	"bnel|bnezlST0B",	"blezlSB",	"bgtzlSB",
  "daddiTSI",	"daddiuTSI",	false,		false,
  map_special2,	"jalxJ",	false,		map_special3,
  "lbTSO",	"lhTSO",	"lwlTSO",	"lwTSO",
  "lbuTSO",	"lhuTSO",	"lwrTSO",	false,
  "sbTSO",	"shTSO",	"swlTSO",	"swTSO",
  false,	false,		"swrTSO",	"cacheNSO",
  "llTSO",	"lwc1HSO",	"lwc2TSO",	"prefNSO",
  false,	"ldc1HSO",	"ldc2TSO",	"ldTSO",
  "scTSO",	"swc1HSO",	"swc2TSO",	false,
  false,	"sdc1HSO",	"sdc2TSO",	"sdTSO",
}

------------------------------------------------------------------------------
-- Primary and extended opcode maps for MIPS R6
------------------------------------------------------------------------------

local map_mul_r6 =   { shift = 6, mask = 3, [2] = "mulDST",   [3] = "muhDST" }
local map_mulu_r6 =  { shift = 6, mask = 3, [2] = "muluDST",  [3] = "muhuDST" }
local map_div_r6 =   { shift = 6, mask = 3, [2] = "divDST",   [3] = "modDST" }
local map_divu_r6 =  { shift = 6, mask = 3, [2] = "divuDST",  [3] = "moduDST" }
local map_dmul_r6 =  { shift = 6, mask = 3, [2] = "dmulDST",  [3] = "dmuhDST" }
local map_dmulu_r6 = { shift = 6, mask = 3, [2] = "dmuluDST", [3] = "dmuhuDST" }
local map_ddiv_r6 =  { shift = 6, mask = 3, [2] = "ddivDST",  [3] = "dmodDST" }
local map_ddivu_r6 = { shift = 6, mask = 3, [2] = "ddivuDST", [3] = "dmoduDST" }

local map_special_r6 = {
  shift = 0, mask = 63,
  [0] = { shift = 0, mask = -1, [0] = "nop", _ = "sllDTA" },
  false,	map_srl,	"sraDTA",
  "sllvDTS",	false,		map_srlv,	"sravDTS",
  "jrS",	"jalrD1S",	false,		false,
  "syscallY",	"breakY",	false,		"sync",
  "clzDS",	"cloDS",	"dclzDS",	"dcloDS",
  "dsllvDST",	"dlsaDSTA",	"dsrlvDST",	"dsravDST",
  map_mul_r6,	map_mulu_r6,	map_div_r6,	map_divu_r6,
  map_dmul_r6,	map_dmulu_r6,	map_ddiv_r6,	map_ddivu_r6,
  "addDST",	"addu|moveDST0", "subDST",	"subu|neguDS0T",
  "andDST",	"or|moveDST0",	"xorDST",	"nor|notDST0",
  false,	false,		"sltDST",	"sltuDST",
  "daddDST",	"dadduDST",	"dsubDST",	"dsubuDST",
  "tgeSTZ",	"tgeuSTZ",	"tltSTZ",	"tltuSTZ",
  "teqSTZ",	"seleqzDST",	"tneSTZ",	"selnezDST",
  "dsllDTA",	false,		"dsrlDTA",	"dsraDTA",
  "dsll32DTA",	false,		"dsrl32DTA",	"dsra32DTA",
}

local map_bshfl_r6 = {
  shift = 9, mask = 3,
  [1] = "alignDSTa",
  _ = {
    shift = 6, mask = 31,
    [0] = "bitswapDT",
    [2] = "wsbhDT",
    [16] = "sebDT",
    [24] = "sehDT",
  }
}

local map_dbshfl_r6 = {
  shift = 9, mask = 3,
  [1] = "dalignDSTa",
  _ = {
    shift = 6, mask = 31,
    [0] = "dbitswapDT",
    [2] = "dsbhDT",
    [5] = "dshdDT",
  }
}

local map_special3_r6 = {
  shift = 0, mask = 63,
  [0]  = "extTSAK", [1]  = "dextmTSAP", [3]  = "dextTSAK",
  [4]  = "insTSAL", [6]  = "dinsuTSEQ", [7]  = "dinsTSAL",
  [32] = map_bshfl_r6, [36] = map_dbshfl_r6,  [59] = "rdhwrTD",
}

local map_regimm_r6 = {
  shift = 16, mask = 31,
  [0] = "bltzSB", [1] = "bgezSB",
  [6] = "dahiSI", [30] = "datiSI",
  [23] = "sigrieI", [31] = "synciSO",
}

local map_pcrel_r6 = {
  shift = 19, mask = 3,
  [0] = "addiupcS2", "lwpcS2", "lwupcS2", {
    shift = 18, mask = 1,
    [0] = "ldpcS3", { shift = 16, mask = 3, [2] = "auipcSI", [3] = "aluipcSI" }
  }
}

local map_cop1s_r6 = {
  shift = 0, mask = 63,
  [0] = "add.sFGH",	"sub.sFGH",	"mul.sFGH",	"div.sFGH",
  "sqrt.sFG",		"abs.sFG",	"mov.sFG",	"neg.sFG",
  "round.l.sFG",	"trunc.l.sFG",	"ceil.l.sFG",	"floor.l.sFG",
  "round.w.sFG",	"trunc.w.sFG",	"ceil.w.sFG",	"floor.w.sFG",
  "sel.sFGH",		false,		false,		false,
  "seleqz.sFGH",	"recip.sFG",	"rsqrt.sFG",	"selnez.sFGH",
  "maddf.sFGH",		"msubf.sFGH",	"rint.sFG",	"class.sFG",
  "min.sFGH",		"mina.sFGH",	"max.sFGH",	"maxa.sFGH",
  false,		"cvt.d.sFG",	false,		false,
  "cvt.w.sFG",		"cvt.l.sFG",
}

local map_cop1d_r6 = {
  shift = 0, mask = 63,
  [0] = "add.dFGH",	"sub.dFGH",	"mul.dFGH",	"div.dFGH",
  "sqrt.dFG",		"abs.dFG",	"mov.dFG",	"neg.dFG",
  "round.l.dFG",	"trunc.l.dFG",	"ceil.l.dFG",	"floor.l.dFG",
  "round.w.dFG",	"trunc.w.dFG",	"ceil.w.dFG",	"floor.w.dFG",
  "sel.dFGH",		false,		false,		false,
  "seleqz.dFGH",	"recip.dFG",	"rsqrt.dFG",	"selnez.dFGH",
  "maddf.dFGH",		"msubf.dFGH",	"rint.dFG",	"class.dFG",
  "min.dFGH",		"mina.dFGH",	"max.dFGH",	"maxa.dFGH",
  "cvt.s.dFG",		false,		false,		false,
  "cvt.w.dFG",		"cvt.l.dFG",
}

local map_cop1w_r6 = {
  shift = 0, mask = 63,
  [0] = "cmp.af.sFGH",	"cmp.un.sFGH",	"cmp.eq.sFGH",	"cmp.ueq.sFGH",
  "cmp.lt.sFGH",	"cmp.ult.sFGH",	"cmp.le.sFGH",	"cmp.ule.sFGH",
  "cmp.saf.sFGH",	"cmp.sun.sFGH",	"cmp.seq.sFGH",	"cmp.sueq.sFGH",
  "cmp.slt.sFGH",	"cmp.sult.sFGH",	"cmp.sle.sFGH",	"cmp.sule.sFGH",
  false,		"cmp.or.sFGH",	"cmp.une.sFGH",	"cmp.ne.sFGH",