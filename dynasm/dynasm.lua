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
    