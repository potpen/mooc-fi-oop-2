/* This is a generated file. DO NOT EDIT! */

static const int libbc_endian = 0;

static const uint8_t libbc_code[] = {
#if LJ_FR2
/* math.deg */ 0,1,2,0,0,1,2,BC_MULVN,1,0,0,BC_RET1,1,2,0,241,135,158,166,3,
220,203,178,130,4,
/* math.rad */ 0,1,2,0,0,1,2,BC_MULVN,1,0,0,BC_RET1,1,2,0,243,244,148,165,20,
198,190,199,252,3,
/* string.len */ 0,1,2,0,0,0,3,BC_ISTYPE,0,5,0,BC_LEN,1,0,0,BC_RET1,1,2,0,
/* table.foreachi */ 0,2,10,0,0,0,15,BC_ISTYPE,0,12,0,BC_ISTYPE,1,9,0,
BC_KSHORT,2,1,0,BC_LEN,3,0,0,BC_KSHORT,4,1,0,BC_FORI,2,8,128,BC_MOV,6,1,0,
BC_MOV,8,5,0,BC_TGETR,9,5,0,BC_CALL,6,3,2,BC_ISEQP,6,0,0,BC_JMP,7,1,128,
BC_RET1,6,2,0,BC_FORL,2,248,127,BC_RET0,0,1,0,
/* table.foreach */ 0,2,11,0,0,1,16,BC_ISTYPE,0,12,0,BC_ISTYPE,1,9,0,BC_KPRI,
2,0,0,BC_MOV,3,0,0,BC_KNUM,4,0,0,BC_JMP,5,7,128,BC_MOV,7,1,0,BC_MOV,9,5,0,
BC_MOV,10,6,0,BC_CALL,7,3,2,BC_ISEQP,7,0,0,BC_JMP,8,1,128,BC_RET1,7,2,0,
BC_ITERN,5,3,3,BC_ITERL,5,247,127,BC_RET0,0,1,0,1,255,255,249,255,15,
/* table.getn */ 0,1,2,0,0,0,3,BC_ISTYPE,0,12,0,BC_LEN,1,0,0,BC_RET1,1,2,0,
/* table.remove */ 0,2,10,0,0,2,30,BC_ISTYPE,0,12,0,BC_LEN,2,0,0,BC_ISNEP,1,0,
0,BC_JMP,3,7,128,BC_ISEQN,2,0,0,BC_JMP,3,23,128,BC_TGETR,3,2,0,BC_KPRI,4,0,0,
BC_TSETR,4,2,0,BC_RET1,3,2,0,BC_JMP,3,18,128,BC_ISTYPE,1,14,0,BC_KSHORT,3,1,0,
BC_ISGT,3,1,0,BC_JMP,3,14,128,BC_ISGT,1,2,0,BC_JMP,3,12,128,BC_TGETR,3,1,0,
BC_ADDVN,4,1,1,BC_MOV,5,2,0,BC_KSHORT,6,1,0,BC_FORI,4,4,128,BC_SUBVN,8,1,7,
BC_TGETR,9,7,0,BC_TSETR,9,8,0,BC_FORL,4,252,127,BC_KPRI,4