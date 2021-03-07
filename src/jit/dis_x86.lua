
----------------------------------------------------------------------------
-- LuaJIT x86/x64 disassembler module.
--
-- Copyright (C) 2005-2022 Mike Pall. All rights reserved.
-- Released under the MIT license. See Copyright Notice in luajit.h
----------------------------------------------------------------------------
-- This is a helper module used by the LuaJIT machine code dumper module.
--
-- Sending small code snippets to an external disassembler and mixing the
-- output with our own stuff was too fragile. So I had to bite the bullet
-- and write yet another x86 disassembler. Oh well ...
--
-- The output format is very similar to what ndisasm generates. But it has
-- been developed independently by looking at the opcode tables from the
-- Intel and AMD manuals. The supported instruction set is quite extensive
-- and reflects what a current generation Intel or AMD CPU implements in
-- 32 bit and 64 bit mode. Yes, this includes MMX, SSE, SSE2, SSE3, SSSE3,
-- SSE4.1, SSE4.2, SSE4a, AVX, AVX2 and even privileged and hypervisor
-- (VMX/SVM) instructions.
--
-- Notes:
-- * The (useless) a16 prefix, 3DNow and pre-586 opcodes are unsupported.
-- * No attempt at optimization has been made -- it's fast enough for my needs.
------------------------------------------------------------------------------

local type = type
local sub, byte, format = string.sub, string.byte, string.format
local match, gmatch, gsub = string.match, string.gmatch, string.gsub
local lower, rep = string.lower, string.rep
local bit = require("bit")
local tohex = bit.tohex

-- Map for 1st opcode byte in 32 bit mode. Ugly? Well ... read on.
local map_opc1_32 = {
--0x
[0]="addBmr","addVmr","addBrm","addVrm","addBai","addVai","push es","pop es",
"orBmr","orVmr","orBrm","orVrm","orBai","orVai","push cs","opc2*",
--1x
"adcBmr","adcVmr","adcBrm","adcVrm","adcBai","adcVai","push ss","pop ss",
"sbbBmr","sbbVmr","sbbBrm","sbbVrm","sbbBai","sbbVai","push ds","pop ds",
--2x
"andBmr","andVmr","andBrm","andVrm","andBai","andVai","es:seg","daa",
"subBmr","subVmr","subBrm","subVrm","subBai","subVai","cs:seg","das",
--3x
"xorBmr","xorVmr","xorBrm","xorVrm","xorBai","xorVai","ss:seg","aaa",
"cmpBmr","cmpVmr","cmpBrm","cmpVrm","cmpBai","cmpVai","ds:seg","aas",
--4x
"incVR","incVR","incVR","incVR","incVR","incVR","incVR","incVR",
"decVR","decVR","decVR","decVR","decVR","decVR","decVR","decVR",
--5x
"pushUR","pushUR","pushUR","pushUR","pushUR","pushUR","pushUR","pushUR",
"popUR","popUR","popUR","popUR","popUR","popUR","popUR","popUR",
--6x
"sz*pushaw,pusha","sz*popaw,popa","boundVrm","arplWmr",
"fs:seg","gs:seg","o16:","a16",
"pushUi","imulVrmi","pushBs","imulVrms",
"insb","insVS","outsb","outsVS",
--7x
"joBj","jnoBj","jbBj","jnbBj","jzBj","jnzBj","jbeBj","jaBj",
"jsBj","jnsBj","jpeBj","jpoBj","jlBj","jgeBj","jleBj","jgBj",
--8x
"arith!Bmi","arith!Vmi","arith!Bmi","arith!Vms",
"testBmr","testVmr","xchgBrm","xchgVrm",
"movBmr","movVmr","movBrm","movVrm",
"movVmg","leaVrm","movWgm","popUm",
--9x
"nop*xchgVaR|pause|xchgWaR|repne nop","xchgVaR","xchgVaR","xchgVaR",
"xchgVaR","xchgVaR","xchgVaR","xchgVaR",
"sz*cbw,cwde,cdqe","sz*cwd,cdq,cqo","call farViw","wait",
"sz*pushfw,pushf","sz*popfw,popf","sahf","lahf",
--Ax
"movBao","movVao","movBoa","movVoa",
"movsb","movsVS","cmpsb","cmpsVS",
"testBai","testVai","stosb","stosVS",
"lodsb","lodsVS","scasb","scasVS",
--Bx
"movBRi","movBRi","movBRi","movBRi","movBRi","movBRi","movBRi","movBRi",
"movVRI","movVRI","movVRI","movVRI","movVRI","movVRI","movVRI","movVRI",
--Cx
"shift!Bmu","shift!Vmu","retBw","ret","vex*3$lesVrm","vex*2$ldsVrm","movBmi","movVmi",
"enterBwu","leave","retfBw","retf","int3","intBu","into","iretVS",
--Dx
"shift!Bm1","shift!Vm1","shift!Bmc","shift!Vmc","aamBu","aadBu","salc","xlatb",
"fp*0","fp*1","fp*2","fp*3","fp*4","fp*5","fp*6","fp*7",
--Ex
"loopneBj","loopeBj","loopBj","sz*jcxzBj,jecxzBj,jrcxzBj",
"inBau","inVau","outBua","outVua",
"callVj","jmpVj","jmp farViw","jmpBj","inBad","inVad","outBda","outVda",
--Fx
"lock:","int1","repne:rep","rep:","hlt","cmc","testb!Bm","testv!Vm",
"clc","stc","cli","sti","cld","std","incb!Bm","incd!Vm",
}
assert(#map_opc1_32 == 255)

-- Map for 1st opcode byte in 64 bit mode (overrides only).
local map_opc1_64 = setmetatable({
  [0x06]=false, [0x07]=false, [0x0e]=false,
  [0x16]=false, [0x17]=false, [0x1e]=false, [0x1f]=false,
  [0x27]=false, [0x2f]=false, [0x37]=false, [0x3f]=false,
  [0x60]=false, [0x61]=false, [0x62]=false, [0x63]="movsxdVrDmt", [0x67]="a32:",
  [0x40]="rex*",   [0x41]="rex*b",   [0x42]="rex*x",   [0x43]="rex*xb",
  [0x44]="rex*r",  [0x45]="rex*rb",  [0x46]="rex*rx",  [0x47]="rex*rxb",
  [0x48]="rex*w",  [0x49]="rex*wb",  [0x4a]="rex*wx",  [0x4b]="rex*wxb",
  [0x4c]="rex*wr", [0x4d]="rex*wrb", [0x4e]="rex*wrx", [0x4f]="rex*wrxb",
  [0x82]=false, [0x9a]=false, [0xc4]="vex*3", [0xc5]="vex*2", [0xce]=false,
  [0xd4]=false, [0xd5]=false, [0xd6]=false, [0xea]=false,
}, { __index = map_opc1_32 })

-- Map for 2nd opcode byte (0F xx). True CISC hell. Hey, I told you.
-- Prefix dependent MMX/SSE opcodes: (none)|rep|o16|repne, -|F3|66|F2
local map_opc2 = {
--0x
[0]="sldt!Dmp","sgdt!Ump","larVrm","lslVrm",nil,"syscall","clts","sysret",
"invd","wbinvd",nil,"ud1",nil,"$prefetch!Bm","femms","3dnowMrmu",
--1x
"movupsXrm|movssXrvm|movupdXrm|movsdXrvm",
"movupsXmr|movssXmvr|movupdXmr|movsdXmvr",
"movhlpsXrm$movlpsXrm|movsldupXrm|movlpdXrm|movddupXrm",
"movlpsXmr||movlpdXmr",
"unpcklpsXrvm||unpcklpdXrvm",
"unpckhpsXrvm||unpckhpdXrvm",
"movlhpsXrm$movhpsXrm|movshdupXrm|movhpdXrm",
"movhpsXmr||movhpdXmr",
"$prefetcht!Bm","hintnopVm","hintnopVm","hintnopVm",
"hintnopVm","hintnopVm","hintnopVm","hintnopVm",
--2x
"movUmx$","movUmy$","movUxm$","movUym$","movUmz$",nil,"movUzm$",nil,
"movapsXrm||movapdXrm",
"movapsXmr||movapdXmr",
"cvtpi2psXrMm|cvtsi2ssXrvVmt|cvtpi2pdXrMm|cvtsi2sdXrvVmt",
"movntpsXmr|movntssXmr|movntpdXmr|movntsdXmr",
"cvttps2piMrXm|cvttss2siVrXm|cvttpd2piMrXm|cvttsd2siVrXm",
"cvtps2piMrXm|cvtss2siVrXm|cvtpd2piMrXm|cvtsd2siVrXm",
"ucomissXrm||ucomisdXrm",
"comissXrm||comisdXrm",
--3x
"wrmsr","rdtsc","rdmsr","rdpmc","sysenter","sysexit",nil,"getsec",
"opc3*38",nil,"opc3*3a",nil,nil,nil,nil,nil,
--4x
"cmovoVrm","cmovnoVrm","cmovbVrm","cmovnbVrm",
"cmovzVrm","cmovnzVrm","cmovbeVrm","cmovaVrm",
"cmovsVrm","cmovnsVrm","cmovpeVrm","cmovpoVrm",
"cmovlVrm","cmovgeVrm","cmovleVrm","cmovgVrm",
--5x
"movmskpsVrXm$||movmskpdVrXm$","sqrtpsXrm|sqrtssXrm|sqrtpdXrm|sqrtsdXrm",
"rsqrtpsXrm|rsqrtssXrvm","rcppsXrm|rcpssXrvm",
"andpsXrvm||andpdXrvm","andnpsXrvm||andnpdXrvm",
"orpsXrvm||orpdXrvm","xorpsXrvm||xorpdXrvm",
"addpsXrvm|addssXrvm|addpdXrvm|addsdXrvm","mulpsXrvm|mulssXrvm|mulpdXrvm|mulsdXrvm",
"cvtps2pdXrm|cvtss2sdXrvm|cvtpd2psXrm|cvtsd2ssXrvm",
"cvtdq2psXrm|cvttps2dqXrm|cvtps2dqXrm",
"subpsXrvm|subssXrvm|subpdXrvm|subsdXrvm","minpsXrvm|minssXrvm|minpdXrvm|minsdXrvm",
"divpsXrvm|divssXrvm|divpdXrvm|divsdXrvm","maxpsXrvm|maxssXrvm|maxpdXrvm|maxsdXrvm",
--6x
"punpcklbwPrvm","punpcklwdPrvm","punpckldqPrvm","packsswbPrvm",
"pcmpgtbPrvm","pcmpgtwPrvm","pcmpgtdPrvm","packuswbPrvm",
"punpckhbwPrvm","punpckhwdPrvm","punpckhdqPrvm","packssdwPrvm",
"||punpcklqdqXrvm","||punpckhqdqXrvm",
"movPrVSm","movqMrm|movdquXrm|movdqaXrm",
--7x
"pshufwMrmu|pshufhwXrmu|pshufdXrmu|pshuflwXrmu","pshiftw!Pvmu",
"pshiftd!Pvmu","pshiftq!Mvmu||pshiftdq!Xvmu",
"pcmpeqbPrvm","pcmpeqwPrvm","pcmpeqdPrvm","emms*|",
"vmreadUmr||extrqXmuu$|insertqXrmuu$","vmwriteUrm||extrqXrm$|insertqXrm$",
nil,nil,
"||haddpdXrvm|haddpsXrvm","||hsubpdXrvm|hsubpsXrvm",
"movVSmMr|movqXrm|movVSmXr","movqMmr|movdquXmr|movdqaXmr",
--8x
"joVj","jnoVj","jbVj","jnbVj","jzVj","jnzVj","jbeVj","jaVj",
"jsVj","jnsVj","jpeVj","jpoVj","jlVj","jgeVj","jleVj","jgVj",
--9x
"setoBm","setnoBm","setbBm","setnbBm","setzBm","setnzBm","setbeBm","setaBm",
"setsBm","setnsBm","setpeBm","setpoBm","setlBm","setgeBm","setleBm","setgBm",
--Ax
"push fs","pop fs","cpuid","btVmr","shldVmru","shldVmrc",nil,nil,
"push gs","pop gs","rsm","btsVmr","shrdVmru","shrdVmrc","fxsave!Dmp","imulVrm",
--Bx
"cmpxchgBmr","cmpxchgVmr","$lssVrm","btrVmr",
"$lfsVrm","$lgsVrm","movzxVrBmt","movzxVrWmt",
"|popcntVrm","ud2Dp","bt!Vmu","btcVmr",
"bsfVrm","bsrVrm|lzcntVrm|bsrWrm","movsxVrBmt","movsxVrWmt",
--Cx
"xaddBmr","xaddVmr",
"cmppsXrvmu|cmpssXrvmu|cmppdXrvmu|cmpsdXrvmu","$movntiVmr|",
"pinsrwPrvWmu","pextrwDrPmu",
"shufpsXrvmu||shufpdXrvmu","$cmpxchg!Qmp",
"bswapVR","bswapVR","bswapVR","bswapVR","bswapVR","bswapVR","bswapVR","bswapVR",
--Dx
"||addsubpdXrvm|addsubpsXrvm","psrlwPrvm","psrldPrvm","psrlqPrvm",
"paddqPrvm","pmullwPrvm",
"|movq2dqXrMm|movqXmr|movdq2qMrXm$","pmovmskbVrMm||pmovmskbVrXm",
"psubusbPrvm","psubuswPrvm","pminubPrvm","pandPrvm",
"paddusbPrvm","padduswPrvm","pmaxubPrvm","pandnPrvm",
--Ex
"pavgbPrvm","psrawPrvm","psradPrvm","pavgwPrvm",
"pmulhuwPrvm","pmulhwPrvm",
"|cvtdq2pdXrm|cvttpd2dqXrm|cvtpd2dqXrm","$movntqMmr||$movntdqXmr",
"psubsbPrvm","psubswPrvm","pminswPrvm","porPrvm",
"paddsbPrvm","paddswPrvm","pmaxswPrvm","pxorPrvm",
--Fx
"|||lddquXrm","psllwPrvm","pslldPrvm","psllqPrvm",
"pmuludqPrvm","pmaddwdPrvm","psadbwPrvm","maskmovqMrm||maskmovdquXrm$",
"psubbPrvm","psubwPrvm","psubdPrvm","psubqPrvm",
"paddbPrvm","paddwPrvm","padddPrvm","ud",
}
assert(map_opc2[255] == "ud")

-- Map for three-byte opcodes. Can't wait for their next invention.
local map_opc3 = {
["38"] = { -- [66] 0f 38 xx
--0x
[0]="pshufbPrvm","phaddwPrvm","phadddPrvm","phaddswPrvm",
"pmaddubswPrvm","phsubwPrvm","phsubdPrvm","phsubswPrvm",
"psignbPrvm","psignwPrvm","psigndPrvm","pmulhrswPrvm",
"||permilpsXrvm","||permilpdXrvm",nil,nil,
--1x
"||pblendvbXrma",nil,nil,nil,
"||blendvpsXrma","||blendvpdXrma","||permpsXrvm","||ptestXrm",
"||broadcastssXrm","||broadcastsdXrm","||broadcastf128XrlXm",nil,
"pabsbPrm","pabswPrm","pabsdPrm",nil,
--2x
"||pmovsxbwXrm","||pmovsxbdXrm","||pmovsxbqXrm","||pmovsxwdXrm",
"||pmovsxwqXrm","||pmovsxdqXrm",nil,nil,
"||pmuldqXrvm","||pcmpeqqXrvm","||$movntdqaXrm","||packusdwXrvm",
"||maskmovpsXrvm","||maskmovpdXrvm","||maskmovpsXmvr","||maskmovpdXmvr",
--3x
"||pmovzxbwXrm","||pmovzxbdXrm","||pmovzxbqXrm","||pmovzxwdXrm",
"||pmovzxwqXrm","||pmovzxdqXrm","||permdXrvm","||pcmpgtqXrvm",
"||pminsbXrvm","||pminsdXrvm","||pminuwXrvm","||pminudXrvm",
"||pmaxsbXrvm","||pmaxsdXrvm","||pmaxuwXrvm","||pmaxudXrvm",
--4x
"||pmulddXrvm","||phminposuwXrm",nil,nil,
nil,"||psrlvVSXrvm","||psravdXrvm","||psllvVSXrvm",
--5x
[0x58] = "||pbroadcastdXrlXm",[0x59] = "||pbroadcastqXrlXm",
[0x5a] = "||broadcasti128XrlXm",
--7x
[0x78] = "||pbroadcastbXrlXm",[0x79] = "||pbroadcastwXrlXm",
--8x
[0x8c] = "||pmaskmovXrvVSm",
[0x8e] = "||pmaskmovVSmXvr",
--9x
[0x96] = "||fmaddsub132pHXrvm",[0x97] = "||fmsubadd132pHXrvm",
[0x98] = "||fmadd132pHXrvm",[0x99] = "||fmadd132sHXrvm",
[0x9a] = "||fmsub132pHXrvm",[0x9b] = "||fmsub132sHXrvm",
[0x9c] = "||fnmadd132pHXrvm",[0x9d] = "||fnmadd132sHXrvm",
[0x9e] = "||fnmsub132pHXrvm",[0x9f] = "||fnmsub132sHXrvm",
--Ax
[0xa6] = "||fmaddsub213pHXrvm",[0xa7] = "||fmsubadd213pHXrvm",
[0xa8] = "||fmadd213pHXrvm",[0xa9] = "||fmadd213sHXrvm",
[0xaa] = "||fmsub213pHXrvm",[0xab] = "||fmsub213sHXrvm",
[0xac] = "||fnmadd213pHXrvm",[0xad] = "||fnmadd213sHXrvm",
[0xae] = "||fnmsub213pHXrvm",[0xaf] = "||fnmsub213sHXrvm",
--Bx
[0xb6] = "||fmaddsub231pHXrvm",[0xb7] = "||fmsubadd231pHXrvm",
[0xb8] = "||fmadd231pHXrvm",[0xb9] = "||fmadd231sHXrvm",
[0xba] = "||fmsub231pHXrvm",[0xbb] = "||fmsub231sHXrvm",
[0xbc] = "||fnmadd231pHXrvm",[0xbd] = "||fnmadd231sHXrvm",
[0xbe] = "||fnmsub231pHXrvm",[0xbf] = "||fnmsub231sHXrvm",
--Dx
[0xdc] = "||aesencXrvm", [0xdd] = "||aesenclastXrvm",
[0xde] = "||aesdecXrvm", [0xdf] = "||aesdeclastXrvm",
--Fx
[0xf0] = "|||crc32TrBmt",[0xf1] = "|||crc32TrVmt",
[0xf7] = "| sarxVrmv| shlxVrmv| shrxVrmv",
},

["3a"] = { -- [66] 0f 3a xx
--0x
[0x00]="||permqXrmu","||permpdXrmu","||pblenddXrvmu",nil,
"||permilpsXrmu","||permilpdXrmu","||perm2f128Xrvmu",nil,
"||roundpsXrmu","||roundpdXrmu","||roundssXrvmu","||roundsdXrvmu",
"||blendpsXrvmu","||blendpdXrvmu","||pblendwXrvmu","palignrPrvmu",
--1x
nil,nil,nil,nil,
"||pextrbVmXru","||pextrwVmXru","||pextrVmSXru","||extractpsVmXru",
"||insertf128XrvlXmu","||extractf128XlXmYru",nil,nil,
nil,nil,nil,nil,
--2x
"||pinsrbXrvVmu","||insertpsXrvmu","||pinsrXrvVmuS",nil,
--3x
[0x38] = "||inserti128Xrvmu",[0x39] = "||extracti128XlXmYru",
--4x
[0x40] = "||dppsXrvmu",
[0x41] = "||dppdXrvmu",
[0x42] = "||mpsadbwXrvmu",
[0x44] = "||pclmulqdqXrvmu",
[0x46] = "||perm2i128Xrvmu",
[0x4a] = "||blendvpsXrvmb",[0x4b] = "||blendvpdXrvmb",
[0x4c] = "||pblendvbXrvmb",
--6x
[0x60] = "||pcmpestrmXrmu",[0x61] = "||pcmpestriXrmu",
[0x62] = "||pcmpistrmXrmu",[0x63] = "||pcmpistriXrmu",
[0xdf] = "||aeskeygenassistXrmu",
--Fx
[0xf0] = "||| rorxVrmu",
},
}

-- Map for VMX/SVM opcodes 0F 01 C0-FF (sgdt group with register operands).
local map_opcvm = {
[0xc1]="vmcall",[0xc2]="vmlaunch",[0xc3]="vmresume",[0xc4]="vmxoff",
[0xc8]="monitor",[0xc9]="mwait",
[0xd8]="vmrun",[0xd9]="vmmcall",[0xda]="vmload",[0xdb]="vmsave",
[0xdc]="stgi",[0xdd]="clgi",[0xde]="skinit",[0xdf]="invlpga",