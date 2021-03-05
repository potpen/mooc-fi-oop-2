
----------------------------------------------------------------------------
-- LuaJIT ARM64 disassembler module.
--
-- Copyright (C) 2005-2022 Mike Pall. All rights reserved.
-- Released under the MIT license. See Copyright Notice in luajit.h
--
-- Contributed by Djordje Kovacevic and Stefan Pejic from RT-RK.com.
-- Sponsored by Cisco Systems, Inc.
----------------------------------------------------------------------------
-- This is a helper module used by the LuaJIT machine code dumper module.
--
-- It disassembles most user-mode AArch64 instructions.
-- NYI: Advanced SIMD and VFP instructions.
------------------------------------------------------------------------------

local type = type
local sub, byte, format = string.sub, string.byte, string.format
local match, gmatch, gsub = string.match, string.gmatch, string.gsub
local concat = table.concat
local bit = require("bit")
local band, bor, bxor, tohex = bit.band, bit.bor, bit.bxor, bit.tohex
local lshift, rshift, arshift = bit.lshift, bit.rshift, bit.arshift
local ror = bit.ror

------------------------------------------------------------------------------
-- Opcode maps
------------------------------------------------------------------------------

local map_adr = { -- PC-relative addressing.
  shift = 31, mask = 1,
  [0] = "adrDBx", "adrpDBx"
}

local map_addsubi = { -- Add/subtract immediate.
  shift = 29, mask = 3,
  [0] = "add|movDNIg", "adds|cmnD0NIg", "subDNIg", "subs|cmpD0NIg",
}

local map_logi = { -- Logical immediate.
  shift = 31, mask = 1,
  [0] = {
    shift = 22, mask = 1,
    [0] = {
      shift = 29, mask = 3,
      [0] = "andDNig", "orr|movDN0ig", "eorDNig", "ands|tstD0Nig"
    },
    false -- unallocated
  },
  {
    shift = 29, mask = 3,
    [0] = "andDNig", "orr|movDN0ig", "eorDNig", "ands|tstD0Nig"
  }
}

local map_movwi = { -- Move wide immediate.
  shift = 31, mask = 1,
  [0] = {
    shift = 22, mask = 1,
    [0] = {
      shift = 29, mask = 3,
      [0] = "movnDWRg", false, "movz|movDYRg", "movkDWRg"
    }, false -- unallocated
  },
  {
    shift = 29, mask = 3,
    [0] = "movnDWRg", false, "movz|movDYRg", "movkDWRg"
  },
}

local map_bitf = { -- Bitfield.
  shift = 31, mask = 1,
  [0] = {
    shift = 22, mask = 1,
    [0] = {
      shift = 29, mask = 3,
      [0] = "sbfm|sbfiz|sbfx|asr|sxtw|sxth|sxtbDN12w",
      "bfm|bfi|bfxilDN13w",
      "ubfm|ubfiz|ubfx|lsr|lsl|uxth|uxtbDN12w"
    }
  },
  {
    shift = 22, mask = 1,
    {
      shift = 29, mask = 3,
      [0] = "sbfm|sbfiz|sbfx|asr|sxtw|sxth|sxtbDN12x",
      "bfm|bfi|bfxilDN13x",
      "ubfm|ubfiz|ubfx|lsr|lsl|uxth|uxtbDN12x"
    }
  }
}

local map_datai = { -- Data processing - immediate.
  shift = 23, mask = 7,
  [0] = map_adr, map_adr, map_addsubi, false,
  map_logi, map_movwi, map_bitf,
  {
    shift = 15, mask = 0x1c0c1,
    [0] = "extr|rorDNM4w", [0x10080] = "extr|rorDNM4x",
    [0x10081] = "extr|rorDNM4x"
  }
}

local map_logsr = { -- Logical, shifted register.
  shift = 31, mask = 1,
  [0] = {
    shift = 15, mask = 1,
    [0] = {
      shift = 29, mask = 3,
      [0] = {
	shift = 21, mask = 7,
	[0] = "andDNMSg", "bicDNMSg", "andDNMSg", "bicDNMSg",
	"andDNMSg", "bicDNMSg", "andDNMg", "bicDNMg"
      },
      {
	shift = 21, mask = 7,
	[0] ="orr|movDN0MSg", "orn|mvnDN0MSg", "orr|movDN0MSg", "orn|mvnDN0MSg",
	     "orr|movDN0MSg", "orn|mvnDN0MSg", "orr|movDN0Mg", "orn|mvnDN0Mg"
      },
      {
	shift = 21, mask = 7,
	[0] = "eorDNMSg", "eonDNMSg", "eorDNMSg", "eonDNMSg",
	"eorDNMSg", "eonDNMSg", "eorDNMg", "eonDNMg"
      },
      {
	shift = 21, mask = 7,
	[0] = "ands|tstD0NMSg", "bicsDNMSg", "ands|tstD0NMSg", "bicsDNMSg",
	"ands|tstD0NMSg", "bicsDNMSg", "ands|tstD0NMg", "bicsDNMg"
      }
    },
    false -- unallocated
  },
  {
    shift = 29, mask = 3,
    [0] = {
      shift = 21, mask = 7,
      [0] = "andDNMSg", "bicDNMSg", "andDNMSg", "bicDNMSg",
      "andDNMSg", "bicDNMSg", "andDNMg", "bicDNMg"
    },
    {
      shift = 21, mask = 7,
      [0] = "orr|movDN0MSg", "orn|mvnDN0MSg", "orr|movDN0MSg", "orn|mvnDN0MSg",