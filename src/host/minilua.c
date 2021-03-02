
/* This is a heavily customized and minimized copy of Lua 5.1.5. */
/* It's only used to build LuaJIT. It does NOT have all standard functions! */
/******************************************************************************
* Copyright (C) 1994-2012 Lua.org, PUC-Rio.  All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
******************************************************************************/
#ifdef _MSC_VER
typedef unsigned __int64 U64;
#else
typedef unsigned long long U64;
#endif
int _CRT_glob = 0;
#include <stddef.h>
#include <stdarg.h>
#include <limits.h>
#include <math.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <errno.h>
#include <time.h>
typedef enum{
TM_INDEX,
TM_NEWINDEX,
TM_GC,
TM_MODE,
TM_EQ,
TM_ADD,
TM_SUB,
TM_MUL,
TM_DIV,
TM_MOD,
TM_POW,
TM_UNM,
TM_LEN,
TM_LT,
TM_LE,
TM_CONCAT,
TM_CALL,
TM_N
}TMS;
enum OpMode{iABC,iABx,iAsBx};
typedef enum{
OP_MOVE,
OP_LOADK,
OP_LOADBOOL,
OP_LOADNIL,
OP_GETUPVAL,
OP_GETGLOBAL,
OP_GETTABLE,
OP_SETGLOBAL,
OP_SETUPVAL,
OP_SETTABLE,
OP_NEWTABLE,
OP_SELF,
OP_ADD,
OP_SUB,
OP_MUL,
OP_DIV,
OP_MOD,
OP_POW,
OP_UNM,
OP_NOT,
OP_LEN,
OP_CONCAT,
OP_JMP,
OP_EQ,
OP_LT,
OP_LE,
OP_TEST,
OP_TESTSET,
OP_CALL,
OP_TAILCALL,
OP_RETURN,
OP_FORLOOP,
OP_FORPREP,
OP_TFORLOOP,
OP_SETLIST,
OP_CLOSE,
OP_CLOSURE,
OP_VARARG
}OpCode;
enum OpArgMask{
OpArgN,
OpArgU,
OpArgR,
OpArgK
};
typedef enum{
VVOID,
VNIL,
VTRUE,
VFALSE,
VK,
VKNUM,
VLOCAL,
VUPVAL,
VGLOBAL,
VINDEXED,
VJMP,
VRELOCABLE,
VNONRELOC,
VCALL,
VVARARG
}expkind;
enum RESERVED{
TK_AND=257,TK_BREAK,
TK_DO,TK_ELSE,TK_ELSEIF,TK_END,TK_FALSE,TK_FOR,TK_FUNCTION,
TK_IF,TK_IN,TK_LOCAL,TK_NIL,TK_NOT,TK_OR,TK_REPEAT,
TK_RETURN,TK_THEN,TK_TRUE,TK_UNTIL,TK_WHILE,
TK_CONCAT,TK_DOTS,TK_EQ,TK_GE,TK_LE,TK_NE,TK_NUMBER,
TK_NAME,TK_STRING,TK_EOS
};
typedef enum BinOpr{
OPR_ADD,OPR_SUB,OPR_MUL,OPR_DIV,OPR_MOD,OPR_POW,
OPR_CONCAT,
OPR_NE,OPR_EQ,
OPR_LT,OPR_LE,OPR_GT,OPR_GE,
OPR_AND,OPR_OR,
OPR_NOBINOPR
}BinOpr;
typedef enum UnOpr{OPR_MINUS,OPR_NOT,OPR_LEN,OPR_NOUNOPR}UnOpr;
#define LUA_QL(x)"'"x"'"
#define luai_apicheck(L,o){(void)L;}
#define lua_number2str(s,n)sprintf((s),"%.14g",(n))
#define lua_str2number(s,p)strtod((s),(p))
#define luai_numadd(a,b)((a)+(b))
#define luai_numsub(a,b)((a)-(b))
#define luai_nummul(a,b)((a)*(b))
#define luai_numdiv(a,b)((a)/(b))
#define luai_nummod(a,b)((a)-floor((a)/(b))*(b))
#define luai_numpow(a,b)(pow(a,b))
#define luai_numunm(a)(-(a))
#define luai_numeq(a,b)((a)==(b))
#define luai_numlt(a,b)((a)<(b))
#define luai_numle(a,b)((a)<=(b))
#define luai_numisnan(a)(!luai_numeq((a),(a)))
#define lua_number2int(i,d)((i)=(int)(d))
#define lua_number2integer(i,d)((i)=(lua_Integer)(d))
#define LUAI_THROW(L,c)longjmp((c)->b,1)
#define LUAI_TRY(L,c,a)if(setjmp((c)->b)==0){a}
#define lua_pclose(L,file)((void)((void)L,file),0)
#define lua_upvalueindex(i)((-10002)-(i))
typedef struct lua_State lua_State;
typedef int(*lua_CFunction)(lua_State*L);
typedef const char*(*lua_Reader)(lua_State*L,void*ud,size_t*sz);
typedef void*(*lua_Alloc)(void*ud,void*ptr,size_t osize,size_t nsize);
typedef double lua_Number;
typedef ptrdiff_t lua_Integer;
static void lua_settop(lua_State*L,int idx);
static int lua_type(lua_State*L,int idx);
static const char* lua_tolstring(lua_State*L,int idx,size_t*len);
static size_t lua_objlen(lua_State*L,int idx);
static void lua_pushlstring(lua_State*L,const char*s,size_t l);
static void lua_pushcclosure(lua_State*L,lua_CFunction fn,int n);
static void lua_createtable(lua_State*L,int narr,int nrec);
static void lua_setfield(lua_State*L,int idx,const char*k);
#define lua_pop(L,n)lua_settop(L,-(n)-1)
#define lua_newtable(L)lua_createtable(L,0,0)
#define lua_pushcfunction(L,f)lua_pushcclosure(L,(f),0)
#define lua_strlen(L,i)lua_objlen(L,(i))
#define lua_isfunction(L,n)(lua_type(L,(n))==6)
#define lua_istable(L,n)(lua_type(L,(n))==5)
#define lua_isnil(L,n)(lua_type(L,(n))==0)
#define lua_isboolean(L,n)(lua_type(L,(n))==1)
#define lua_isnone(L,n)(lua_type(L,(n))==(-1))
#define lua_isnoneornil(L,n)(lua_type(L,(n))<=0)
#define lua_pushliteral(L,s)lua_pushlstring(L,""s,(sizeof(s)/sizeof(char))-1)
#define lua_setglobal(L,s)lua_setfield(L,(-10002),(s))
#define lua_tostring(L,i)lua_tolstring(L,(i),NULL)
typedef struct lua_Debug lua_Debug;
typedef void(*lua_Hook)(lua_State*L,lua_Debug*ar);
struct lua_Debug{
int event;
const char*name;
const char*namewhat;
const char*what;
const char*source;
int currentline;
int nups;
int linedefined;
int lastlinedefined;
char short_src[60];
int i_ci;
};
typedef unsigned int lu_int32;
typedef size_t lu_mem;
typedef ptrdiff_t l_mem;
typedef unsigned char lu_byte;
#define IntPoint(p)((unsigned int)(lu_mem)(p))
typedef union{double u;void*s;long l;}L_Umaxalign;
typedef double l_uacNumber;
#define check_exp(c,e)(e)
#define UNUSED(x)((void)(x))
#define cast(t,exp)((t)(exp))
#define cast_byte(i)cast(lu_byte,(i))
#define cast_num(i)cast(lua_Number,(i))
#define cast_int(i)cast(int,(i))
typedef lu_int32 Instruction;
#define condhardstacktests(x)((void)0)
typedef union GCObject GCObject;
typedef struct GCheader{
GCObject*next;lu_byte tt;lu_byte marked;
}GCheader;
typedef union{
GCObject*gc;
void*p;
lua_Number n;
int b;
}Value;
typedef struct lua_TValue{
Value value;int tt;
}TValue;
#define ttisnil(o)(ttype(o)==0)
#define ttisnumber(o)(ttype(o)==3)
#define ttisstring(o)(ttype(o)==4)
#define ttistable(o)(ttype(o)==5)
#define ttisfunction(o)(ttype(o)==6)
#define ttisboolean(o)(ttype(o)==1)
#define ttisuserdata(o)(ttype(o)==7)
#define ttisthread(o)(ttype(o)==8)
#define ttislightuserdata(o)(ttype(o)==2)
#define ttype(o)((o)->tt)
#define gcvalue(o)check_exp(iscollectable(o),(o)->value.gc)
#define pvalue(o)check_exp(ttislightuserdata(o),(o)->value.p)
#define nvalue(o)check_exp(ttisnumber(o),(o)->value.n)
#define rawtsvalue(o)check_exp(ttisstring(o),&(o)->value.gc->ts)
#define tsvalue(o)(&rawtsvalue(o)->tsv)
#define rawuvalue(o)check_exp(ttisuserdata(o),&(o)->value.gc->u)
#define uvalue(o)(&rawuvalue(o)->uv)
#define clvalue(o)check_exp(ttisfunction(o),&(o)->value.gc->cl)
#define hvalue(o)check_exp(ttistable(o),&(o)->value.gc->h)
#define bvalue(o)check_exp(ttisboolean(o),(o)->value.b)
#define thvalue(o)check_exp(ttisthread(o),&(o)->value.gc->th)
#define l_isfalse(o)(ttisnil(o)||(ttisboolean(o)&&bvalue(o)==0))
#define checkconsistency(obj)
#define checkliveness(g,obj)
#define setnilvalue(obj)((obj)->tt=0)
#define setnvalue(obj,x){TValue*i_o=(obj);i_o->value.n=(x);i_o->tt=3;}
#define setbvalue(obj,x){TValue*i_o=(obj);i_o->value.b=(x);i_o->tt=1;}
#define setsvalue(L,obj,x){TValue*i_o=(obj);i_o->value.gc=cast(GCObject*,(x));i_o->tt=4;checkliveness(G(L),i_o);}
#define setuvalue(L,obj,x){TValue*i_o=(obj);i_o->value.gc=cast(GCObject*,(x));i_o->tt=7;checkliveness(G(L),i_o);}
#define setthvalue(L,obj,x){TValue*i_o=(obj);i_o->value.gc=cast(GCObject*,(x));i_o->tt=8;checkliveness(G(L),i_o);}
#define setclvalue(L,obj,x){TValue*i_o=(obj);i_o->value.gc=cast(GCObject*,(x));i_o->tt=6;checkliveness(G(L),i_o);}
#define sethvalue(L,obj,x){TValue*i_o=(obj);i_o->value.gc=cast(GCObject*,(x));i_o->tt=5;checkliveness(G(L),i_o);}
#define setptvalue(L,obj,x){TValue*i_o=(obj);i_o->value.gc=cast(GCObject*,(x));i_o->tt=(8+1);checkliveness(G(L),i_o);}
#define setobj(L,obj1,obj2){const TValue*o2=(obj2);TValue*o1=(obj1);o1->value=o2->value;o1->tt=o2->tt;checkliveness(G(L),o1);}
#define setttype(obj,tt)(ttype(obj)=(tt))
#define iscollectable(o)(ttype(o)>=4)
typedef TValue*StkId;
typedef union TString{
L_Umaxalign dummy;
struct{
GCObject*next;lu_byte tt;lu_byte marked;
lu_byte reserved;
unsigned int hash;
size_t len;
}tsv;
}TString;
#define getstr(ts)cast(const char*,(ts)+1)
#define svalue(o)getstr(rawtsvalue(o))
typedef union Udata{
L_Umaxalign dummy;
struct{
GCObject*next;lu_byte tt;lu_byte marked;
struct Table*metatable;
struct Table*env;
size_t len;
}uv;
}Udata;
typedef struct Proto{
GCObject*next;lu_byte tt;lu_byte marked;
TValue*k;
Instruction*code;
struct Proto**p;
int*lineinfo;
struct LocVar*locvars;
TString**upvalues;
TString*source;
int sizeupvalues;
int sizek;
int sizecode;
int sizelineinfo;
int sizep;
int sizelocvars;
int linedefined;
int lastlinedefined;
GCObject*gclist;
lu_byte nups;
lu_byte numparams;
lu_byte is_vararg;
lu_byte maxstacksize;
}Proto;
typedef struct LocVar{
TString*varname;
int startpc;
int endpc;
}LocVar;
typedef struct UpVal{
GCObject*next;lu_byte tt;lu_byte marked;
TValue*v;
union{
TValue value;
struct{
struct UpVal*prev;
struct UpVal*next;
}l;
}u;
}UpVal;
typedef struct CClosure{
GCObject*next;lu_byte tt;lu_byte marked;lu_byte isC;lu_byte nupvalues;GCObject*gclist;struct Table*env;
lua_CFunction f;
TValue upvalue[1];
}CClosure;
typedef struct LClosure{
GCObject*next;lu_byte tt;lu_byte marked;lu_byte isC;lu_byte nupvalues;GCObject*gclist;struct Table*env;
struct Proto*p;
UpVal*upvals[1];
}LClosure;
typedef union Closure{
CClosure c;
LClosure l;
}Closure;
#define iscfunction(o)(ttype(o)==6&&clvalue(o)->c.isC)
typedef union TKey{
struct{
Value value;int tt;
struct Node*next;
}nk;
TValue tvk;
}TKey;
typedef struct Node{
TValue i_val;
TKey i_key;
}Node;
typedef struct Table{
GCObject*next;lu_byte tt;lu_byte marked;
lu_byte flags;
lu_byte lsizenode;
struct Table*metatable;
TValue*array;
Node*node;
Node*lastfree;
GCObject*gclist;
int sizearray;
}Table;
#define lmod(s,size)(check_exp((size&(size-1))==0,(cast(int,(s)&((size)-1)))))
#define twoto(x)((size_t)1<<(x))
#define sizenode(t)(twoto((t)->lsizenode))
static const TValue luaO_nilobject_;
#define ceillog2(x)(luaO_log2((x)-1)+1)
static int luaO_log2(unsigned int x);
#define gfasttm(g,et,e)((et)==NULL?NULL:((et)->flags&(1u<<(e)))?NULL:luaT_gettm(et,e,(g)->tmname[e]))
#define fasttm(l,et,e)gfasttm(G(l),et,e)
static const TValue*luaT_gettm(Table*events,TMS event,TString*ename);
#define luaM_reallocv(L,b,on,n,e)((cast(size_t,(n)+1)<=((size_t)(~(size_t)0)-2)/(e))?luaM_realloc_(L,(b),(on)*(e),(n)*(e)):luaM_toobig(L))
#define luaM_freemem(L,b,s)luaM_realloc_(L,(b),(s),0)
#define luaM_free(L,b)luaM_realloc_(L,(b),sizeof(*(b)),0)
#define luaM_freearray(L,b,n,t)luaM_reallocv(L,(b),n,0,sizeof(t))
#define luaM_malloc(L,t)luaM_realloc_(L,NULL,0,(t))
#define luaM_new(L,t)cast(t*,luaM_malloc(L,sizeof(t)))
#define luaM_newvector(L,n,t)cast(t*,luaM_reallocv(L,NULL,0,n,sizeof(t)))
#define luaM_growvector(L,v,nelems,size,t,limit,e)if((nelems)+1>(size))((v)=cast(t*,luaM_growaux_(L,v,&(size),sizeof(t),limit,e)))
#define luaM_reallocvector(L,v,oldn,n,t)((v)=cast(t*,luaM_reallocv(L,v,oldn,n,sizeof(t))))
static void*luaM_realloc_(lua_State*L,void*block,size_t oldsize,
size_t size);
static void*luaM_toobig(lua_State*L);
static void*luaM_growaux_(lua_State*L,void*block,int*size,
size_t size_elem,int limit,
const char*errormsg);
typedef struct Zio ZIO;
#define char2int(c)cast(int,cast(unsigned char,(c)))
#define zgetc(z)(((z)->n--)>0?char2int(*(z)->p++):luaZ_fill(z))
typedef struct Mbuffer{
char*buffer;
size_t n;
size_t buffsize;
}Mbuffer;
#define luaZ_initbuffer(L,buff)((buff)->buffer=NULL,(buff)->buffsize=0)
#define luaZ_buffer(buff)((buff)->buffer)
#define luaZ_sizebuffer(buff)((buff)->buffsize)
#define luaZ_bufflen(buff)((buff)->n)
#define luaZ_resetbuffer(buff)((buff)->n=0)
#define luaZ_resizebuffer(L,buff,size)(luaM_reallocvector(L,(buff)->buffer,(buff)->buffsize,size,char),(buff)->buffsize=size)
#define luaZ_freebuffer(L,buff)luaZ_resizebuffer(L,buff,0)
struct Zio{
size_t n;
const char*p;
lua_Reader reader;
void*data;
lua_State*L;
};
static int luaZ_fill(ZIO*z);
struct lua_longjmp;
#define gt(L)(&L->l_gt)
#define registry(L)(&G(L)->l_registry)
typedef struct stringtable{
GCObject**hash;
lu_int32 nuse;
int size;
}stringtable;
typedef struct CallInfo{
StkId base;
StkId func;
StkId top;
const Instruction*savedpc;
int nresults;
int tailcalls;
}CallInfo;
#define curr_func(L)(clvalue(L->ci->func))
#define ci_func(ci)(clvalue((ci)->func))
#define f_isLua(ci)(!ci_func(ci)->c.isC)
#define isLua(ci)(ttisfunction((ci)->func)&&f_isLua(ci))
typedef struct global_State{
stringtable strt;
lua_Alloc frealloc;
void*ud;
lu_byte currentwhite;
lu_byte gcstate;
int sweepstrgc;
GCObject*rootgc;
GCObject**sweepgc;
GCObject*gray;
GCObject*grayagain;
GCObject*weak;
GCObject*tmudata;
Mbuffer buff;
lu_mem GCthreshold;
lu_mem totalbytes;
lu_mem estimate;
lu_mem gcdept;
int gcpause;
int gcstepmul;
lua_CFunction panic;
TValue l_registry;
struct lua_State*mainthread;
UpVal uvhead;
struct Table*mt[(8+1)];
TString*tmname[TM_N];
}global_State;
struct lua_State{
GCObject*next;lu_byte tt;lu_byte marked;
lu_byte status;
StkId top;
StkId base;
global_State*l_G;
CallInfo*ci;
const Instruction*savedpc;
StkId stack_last;
StkId stack;
CallInfo*end_ci;
CallInfo*base_ci;
int stacksize;
int size_ci;
unsigned short nCcalls;
unsigned short baseCcalls;
lu_byte hookmask;
lu_byte allowhook;
int basehookcount;
int hookcount;
lua_Hook hook;
TValue l_gt;
TValue env;
GCObject*openupval;
GCObject*gclist;
struct lua_longjmp*errorJmp;
ptrdiff_t errfunc;
};
#define G(L)(L->l_G)
union GCObject{
GCheader gch;
union TString ts;
union Udata u;
union Closure cl;
struct Table h;
struct Proto p;
struct UpVal uv;
struct lua_State th;
};
#define rawgco2ts(o)check_exp((o)->gch.tt==4,&((o)->ts))
#define gco2ts(o)(&rawgco2ts(o)->tsv)
#define rawgco2u(o)check_exp((o)->gch.tt==7,&((o)->u))
#define gco2u(o)(&rawgco2u(o)->uv)
#define gco2cl(o)check_exp((o)->gch.tt==6,&((o)->cl))
#define gco2h(o)check_exp((o)->gch.tt==5,&((o)->h))
#define gco2p(o)check_exp((o)->gch.tt==(8+1),&((o)->p))
#define gco2uv(o)check_exp((o)->gch.tt==(8+2),&((o)->uv))
#define ngcotouv(o)check_exp((o)==NULL||(o)->gch.tt==(8+2),&((o)->uv))
#define gco2th(o)check_exp((o)->gch.tt==8,&((o)->th))
#define obj2gco(v)(cast(GCObject*,(v)))
static void luaE_freethread(lua_State*L,lua_State*L1);
#define pcRel(pc,p)(cast(int,(pc)-(p)->code)-1)
#define getline_(f,pc)(((f)->lineinfo)?(f)->lineinfo[pc]:0)
#define resethookcount(L)(L->hookcount=L->basehookcount)
static void luaG_typeerror(lua_State*L,const TValue*o,
const char*opname);
static void luaG_runerror(lua_State*L,const char*fmt,...);
#define luaD_checkstack(L,n)if((char*)L->stack_last-(char*)L->top<=(n)*(int)sizeof(TValue))luaD_growstack(L,n);else condhardstacktests(luaD_reallocstack(L,L->stacksize-5-1));
#define incr_top(L){luaD_checkstack(L,1);L->top++;}
#define savestack(L,p)((char*)(p)-(char*)L->stack)
#define restorestack(L,n)((TValue*)((char*)L->stack+(n)))
#define saveci(L,p)((char*)(p)-(char*)L->base_ci)
#define restoreci(L,n)((CallInfo*)((char*)L->base_ci+(n)))
typedef void(*Pfunc)(lua_State*L,void*ud);
static int luaD_poscall(lua_State*L,StkId firstResult);
static void luaD_reallocCI(lua_State*L,int newsize);
static void luaD_reallocstack(lua_State*L,int newsize);
static void luaD_growstack(lua_State*L,int n);
static void luaD_throw(lua_State*L,int errcode);
static void*luaM_growaux_(lua_State*L,void*block,int*size,size_t size_elems,
int limit,const char*errormsg){
void*newblock;
int newsize;
if(*size>=limit/2){
if(*size>=limit)
luaG_runerror(L,errormsg);
newsize=limit;
}
else{
newsize=(*size)*2;
if(newsize<4)
newsize=4;
}
newblock=luaM_reallocv(L,block,*size,newsize,size_elems);
*size=newsize;
return newblock;
}
static void*luaM_toobig(lua_State*L){
luaG_runerror(L,"memory allocation error: block too big");
return NULL;
}
static void*luaM_realloc_(lua_State*L,void*block,size_t osize,size_t nsize){
global_State*g=G(L);
block=(*g->frealloc)(g->ud,block,osize,nsize);
if(block==NULL&&nsize>0)
luaD_throw(L,4);
g->totalbytes=(g->totalbytes-osize)+nsize;
return block;
}
#define resetbits(x,m)((x)&=cast(lu_byte,~(m)))
#define setbits(x,m)((x)|=(m))
#define testbits(x,m)((x)&(m))
#define bitmask(b)(1<<(b))
#define bit2mask(b1,b2)(bitmask(b1)|bitmask(b2))
#define l_setbit(x,b)setbits(x,bitmask(b))
#define resetbit(x,b)resetbits(x,bitmask(b))
#define testbit(x,b)testbits(x,bitmask(b))
#define set2bits(x,b1,b2)setbits(x,(bit2mask(b1,b2)))
#define reset2bits(x,b1,b2)resetbits(x,(bit2mask(b1,b2)))
#define test2bits(x,b1,b2)testbits(x,(bit2mask(b1,b2)))
#define iswhite(x)test2bits((x)->gch.marked,0,1)
#define isblack(x)testbit((x)->gch.marked,2)
#define isgray(x)(!isblack(x)&&!iswhite(x))
#define otherwhite(g)(g->currentwhite^bit2mask(0,1))
#define isdead(g,v)((v)->gch.marked&otherwhite(g)&bit2mask(0,1))
#define changewhite(x)((x)->gch.marked^=bit2mask(0,1))
#define gray2black(x)l_setbit((x)->gch.marked,2)
#define valiswhite(x)(iscollectable(x)&&iswhite(gcvalue(x)))
#define luaC_white(g)cast(lu_byte,(g)->currentwhite&bit2mask(0,1))
#define luaC_checkGC(L){condhardstacktests(luaD_reallocstack(L,L->stacksize-5-1));if(G(L)->totalbytes>=G(L)->GCthreshold)luaC_step(L);}
#define luaC_barrier(L,p,v){if(valiswhite(v)&&isblack(obj2gco(p)))luaC_barrierf(L,obj2gco(p),gcvalue(v));}
#define luaC_barriert(L,t,v){if(valiswhite(v)&&isblack(obj2gco(t)))luaC_barrierback(L,t);}
#define luaC_objbarrier(L,p,o){if(iswhite(obj2gco(o))&&isblack(obj2gco(p)))luaC_barrierf(L,obj2gco(p),obj2gco(o));}
#define luaC_objbarriert(L,t,o){if(iswhite(obj2gco(o))&&isblack(obj2gco(t)))luaC_barrierback(L,t);}
static void luaC_step(lua_State*L);
static void luaC_link(lua_State*L,GCObject*o,lu_byte tt);
static void luaC_linkupval(lua_State*L,UpVal*uv);
static void luaC_barrierf(lua_State*L,GCObject*o,GCObject*v);
static void luaC_barrierback(lua_State*L,Table*t);
#define sizestring(s)(sizeof(union TString)+((s)->len+1)*sizeof(char))
#define sizeudata(u)(sizeof(union Udata)+(u)->len)
#define luaS_new(L,s)(luaS_newlstr(L,s,strlen(s)))
#define luaS_newliteral(L,s)(luaS_newlstr(L,""s,(sizeof(s)/sizeof(char))-1))
#define luaS_fix(s)l_setbit((s)->tsv.marked,5)
static TString*luaS_newlstr(lua_State*L,const char*str,size_t l);
#define tostring(L,o)((ttype(o)==4)||(luaV_tostring(L,o)))
#define tonumber(o,n)(ttype(o)==3||(((o)=luaV_tonumber(o,n))!=NULL))
#define equalobj(L,o1,o2)(ttype(o1)==ttype(o2)&&luaV_equalval(L,o1,o2))
static int luaV_equalval(lua_State*L,const TValue*t1,const TValue*t2);
static const TValue*luaV_tonumber(const TValue*obj,TValue*n);
static int luaV_tostring(lua_State*L,StkId obj);
static void luaV_execute(lua_State*L,int nexeccalls);
static void luaV_concat(lua_State*L,int total,int last);
static const TValue luaO_nilobject_={{NULL},0};
static int luaO_int2fb(unsigned int x){
int e=0;
while(x>=16){
x=(x+1)>>1;
e++;
}
if(x<8)return x;
else return((e+1)<<3)|(cast_int(x)-8);
}
static int luaO_fb2int(int x){
int e=(x>>3)&31;
if(e==0)return x;
else return((x&7)+8)<<(e-1);
}
static int luaO_log2(unsigned int x){
static const lu_byte log_2[256]={
0,1,2,2,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8
};
int l=-1;
while(x>=256){l+=8;x>>=8;}
return l+log_2[x];
}
static int luaO_rawequalObj(const TValue*t1,const TValue*t2){
if(ttype(t1)!=ttype(t2))return 0;
else switch(ttype(t1)){
case 0:
return 1;
case 3:
return luai_numeq(nvalue(t1),nvalue(t2));
case 1:
return bvalue(t1)==bvalue(t2);
case 2:
return pvalue(t1)==pvalue(t2);
default:
return gcvalue(t1)==gcvalue(t2);
}
}
static int luaO_str2d(const char*s,lua_Number*result){
char*endptr;
*result=lua_str2number(s,&endptr);
if(endptr==s)return 0;
if(*endptr=='x'||*endptr=='X')
*result=cast_num(strtoul(s,&endptr,16));
if(*endptr=='\0')return 1;
while(isspace(cast(unsigned char,*endptr)))endptr++;
if(*endptr!='\0')return 0;
return 1;
}
static void pushstr(lua_State*L,const char*str){
setsvalue(L,L->top,luaS_new(L,str));
incr_top(L);
}
static const char*luaO_pushvfstring(lua_State*L,const char*fmt,va_list argp){
int n=1;
pushstr(L,"");
for(;;){
const char*e=strchr(fmt,'%');
if(e==NULL)break;
setsvalue(L,L->top,luaS_newlstr(L,fmt,e-fmt));
incr_top(L);
switch(*(e+1)){
case's':{
const char*s=va_arg(argp,char*);
if(s==NULL)s="(null)";
pushstr(L,s);
break;
}
case'c':{
char buff[2];
buff[0]=cast(char,va_arg(argp,int));
buff[1]='\0';
pushstr(L,buff);
break;
}
case'd':{
setnvalue(L->top,cast_num(va_arg(argp,int)));
incr_top(L);
break;
}
case'f':{
setnvalue(L->top,cast_num(va_arg(argp,l_uacNumber)));
incr_top(L);
break;
}
case'p':{
char buff[4*sizeof(void*)+8];
sprintf(buff,"%p",va_arg(argp,void*));
pushstr(L,buff);
break;
}
case'%':{
pushstr(L,"%");
break;
}
default:{
char buff[3];
buff[0]='%';
buff[1]=*(e+1);
buff[2]='\0';
pushstr(L,buff);
break;
}
}
n+=2;
fmt=e+2;
}
pushstr(L,fmt);
luaV_concat(L,n+1,cast_int(L->top-L->base)-1);
L->top-=n;
return svalue(L->top-1);
}
static const char*luaO_pushfstring(lua_State*L,const char*fmt,...){
const char*msg;
va_list argp;
va_start(argp,fmt);
msg=luaO_pushvfstring(L,fmt,argp);
va_end(argp);
return msg;
}
static void luaO_chunkid(char*out,const char*source,size_t bufflen){
if(*source=='='){
strncpy(out,source+1,bufflen);
out[bufflen-1]='\0';
}
else{
if(*source=='@'){
size_t l;
source++;
bufflen-=sizeof(" '...' ");
l=strlen(source);
strcpy(out,"");
if(l>bufflen){
source+=(l-bufflen);
strcat(out,"...");
}
strcat(out,source);
}
else{
size_t len=strcspn(source,"\n\r");
bufflen-=sizeof(" [string \"...\"] ");
if(len>bufflen)len=bufflen;
strcpy(out,"[string \"");
if(source[len]!='\0'){
strncat(out,source,len);
strcat(out,"...");
}
else
strcat(out,source);
strcat(out,"\"]");
}
}
}
#define gnode(t,i)(&(t)->node[i])
#define gkey(n)(&(n)->i_key.nk)
#define gval(n)(&(n)->i_val)
#define gnext(n)((n)->i_key.nk.next)
#define key2tval(n)(&(n)->i_key.tvk)
static TValue*luaH_setnum(lua_State*L,Table*t,int key);
static const TValue*luaH_getstr(Table*t,TString*key);
static TValue*luaH_set(lua_State*L,Table*t,const TValue*key);
static const char*const luaT_typenames[]={
"nil","boolean","userdata","number",
"string","table","function","userdata","thread",
"proto","upval"
};
static void luaT_init(lua_State*L){
static const char*const luaT_eventname[]={
"__index","__newindex",
"__gc","__mode","__eq",
"__add","__sub","__mul","__div","__mod",
"__pow","__unm","__len","__lt","__le",
"__concat","__call"
};
int i;
for(i=0;i<TM_N;i++){
G(L)->tmname[i]=luaS_new(L,luaT_eventname[i]);
luaS_fix(G(L)->tmname[i]);
}
}
static const TValue*luaT_gettm(Table*events,TMS event,TString*ename){
const TValue*tm=luaH_getstr(events,ename);
if(ttisnil(tm)){
events->flags|=cast_byte(1u<<event);
return NULL;
}
else return tm;
}
static const TValue*luaT_gettmbyobj(lua_State*L,const TValue*o,TMS event){
Table*mt;
switch(ttype(o)){
case 5:
mt=hvalue(o)->metatable;
break;
case 7:
mt=uvalue(o)->metatable;
break;
default:
mt=G(L)->mt[ttype(o)];
}
return(mt?luaH_getstr(mt,G(L)->tmname[event]):(&luaO_nilobject_));
}
#define sizeCclosure(n)(cast(int,sizeof(CClosure))+cast(int,sizeof(TValue)*((n)-1)))
#define sizeLclosure(n)(cast(int,sizeof(LClosure))+cast(int,sizeof(TValue*)*((n)-1)))
static Closure*luaF_newCclosure(lua_State*L,int nelems,Table*e){
Closure*c=cast(Closure*,luaM_malloc(L,sizeCclosure(nelems)));
luaC_link(L,obj2gco(c),6);
c->c.isC=1;
c->c.env=e;
c->c.nupvalues=cast_byte(nelems);
return c;
}
static Closure*luaF_newLclosure(lua_State*L,int nelems,Table*e){
Closure*c=cast(Closure*,luaM_malloc(L,sizeLclosure(nelems)));
luaC_link(L,obj2gco(c),6);
c->l.isC=0;
c->l.env=e;
c->l.nupvalues=cast_byte(nelems);
while(nelems--)c->l.upvals[nelems]=NULL;
return c;
}
static UpVal*luaF_newupval(lua_State*L){
UpVal*uv=luaM_new(L,UpVal);
luaC_link(L,obj2gco(uv),(8+2));
uv->v=&uv->u.value;
setnilvalue(uv->v);
return uv;
}
static UpVal*luaF_findupval(lua_State*L,StkId level){
global_State*g=G(L);
GCObject**pp=&L->openupval;
UpVal*p;
UpVal*uv;
while(*pp!=NULL&&(p=ngcotouv(*pp))->v>=level){
if(p->v==level){
if(isdead(g,obj2gco(p)))
changewhite(obj2gco(p));
return p;
}
pp=&p->next;
}
uv=luaM_new(L,UpVal);
uv->tt=(8+2);
uv->marked=luaC_white(g);
uv->v=level;
uv->next=*pp;
*pp=obj2gco(uv);
uv->u.l.prev=&g->uvhead;
uv->u.l.next=g->uvhead.u.l.next;
uv->u.l.next->u.l.prev=uv;
g->uvhead.u.l.next=uv;
return uv;
}
static void unlinkupval(UpVal*uv){
uv->u.l.next->u.l.prev=uv->u.l.prev;
uv->u.l.prev->u.l.next=uv->u.l.next;
}
static void luaF_freeupval(lua_State*L,UpVal*uv){
if(uv->v!=&uv->u.value)
unlinkupval(uv);
luaM_free(L,uv);
}
static void luaF_close(lua_State*L,StkId level){
UpVal*uv;
global_State*g=G(L);
while(L->openupval!=NULL&&(uv=ngcotouv(L->openupval))->v>=level){
GCObject*o=obj2gco(uv);
L->openupval=uv->next;
if(isdead(g,o))
luaF_freeupval(L,uv);
else{
unlinkupval(uv);
setobj(L,&uv->u.value,uv->v);
uv->v=&uv->u.value;
luaC_linkupval(L,uv);
}
}
}
static Proto*luaF_newproto(lua_State*L){
Proto*f=luaM_new(L,Proto);
luaC_link(L,obj2gco(f),(8+1));
f->k=NULL;
f->sizek=0;
f->p=NULL;
f->sizep=0;
f->code=NULL;
f->sizecode=0;
f->sizelineinfo=0;
f->sizeupvalues=0;
f->nups=0;
f->upvalues=NULL;
f->numparams=0;
f->is_vararg=0;
f->maxstacksize=0;
f->lineinfo=NULL;
f->sizelocvars=0;
f->locvars=NULL;
f->linedefined=0;
f->lastlinedefined=0;
f->source=NULL;
return f;
}
static void luaF_freeproto(lua_State*L,Proto*f){
luaM_freearray(L,f->code,f->sizecode,Instruction);
luaM_freearray(L,f->p,f->sizep,Proto*);
luaM_freearray(L,f->k,f->sizek,TValue);
luaM_freearray(L,f->lineinfo,f->sizelineinfo,int);
luaM_freearray(L,f->locvars,f->sizelocvars,struct LocVar);
luaM_freearray(L,f->upvalues,f->sizeupvalues,TString*);
luaM_free(L,f);
}
static void luaF_freeclosure(lua_State*L,Closure*c){
int size=(c->c.isC)?sizeCclosure(c->c.nupvalues):
sizeLclosure(c->l.nupvalues);
luaM_freemem(L,c,size);
}
#define MASK1(n,p)((~((~(Instruction)0)<<n))<<p)
#define MASK0(n,p)(~MASK1(n,p))
#define GET_OPCODE(i)(cast(OpCode,((i)>>0)&MASK1(6,0)))
#define SET_OPCODE(i,o)((i)=(((i)&MASK0(6,0))|((cast(Instruction,o)<<0)&MASK1(6,0))))
#define GETARG_A(i)(cast(int,((i)>>(0+6))&MASK1(8,0)))
#define SETARG_A(i,u)((i)=(((i)&MASK0(8,(0+6)))|((cast(Instruction,u)<<(0+6))&MASK1(8,(0+6)))))
#define GETARG_B(i)(cast(int,((i)>>(((0+6)+8)+9))&MASK1(9,0)))
#define SETARG_B(i,b)((i)=(((i)&MASK0(9,(((0+6)+8)+9)))|((cast(Instruction,b)<<(((0+6)+8)+9))&MASK1(9,(((0+6)+8)+9)))))
#define GETARG_C(i)(cast(int,((i)>>((0+6)+8))&MASK1(9,0)))
#define SETARG_C(i,b)((i)=(((i)&MASK0(9,((0+6)+8)))|((cast(Instruction,b)<<((0+6)+8))&MASK1(9,((0+6)+8)))))
#define GETARG_Bx(i)(cast(int,((i)>>((0+6)+8))&MASK1((9+9),0)))
#define SETARG_Bx(i,b)((i)=(((i)&MASK0((9+9),((0+6)+8)))|((cast(Instruction,b)<<((0+6)+8))&MASK1((9+9),((0+6)+8)))))
#define GETARG_sBx(i)(GETARG_Bx(i)-(((1<<(9+9))-1)>>1))
#define SETARG_sBx(i,b)SETARG_Bx((i),cast(unsigned int,(b)+(((1<<(9+9))-1)>>1)))
#define CREATE_ABC(o,a,b,c)((cast(Instruction,o)<<0)|(cast(Instruction,a)<<(0+6))|(cast(Instruction,b)<<(((0+6)+8)+9))|(cast(Instruction,c)<<((0+6)+8)))
#define CREATE_ABx(o,a,bc)((cast(Instruction,o)<<0)|(cast(Instruction,a)<<(0+6))|(cast(Instruction,bc)<<((0+6)+8)))
#define ISK(x)((x)&(1<<(9-1)))
#define INDEXK(r)((int)(r)&~(1<<(9-1)))
#define RKASK(x)((x)|(1<<(9-1)))
static const lu_byte luaP_opmodes[(cast(int,OP_VARARG)+1)];
#define getBMode(m)(cast(enum OpArgMask,(luaP_opmodes[m]>>4)&3))
#define getCMode(m)(cast(enum OpArgMask,(luaP_opmodes[m]>>2)&3))
#define testTMode(m)(luaP_opmodes[m]&(1<<7))
typedef struct expdesc{
expkind k;
union{
struct{int info,aux;}s;
lua_Number nval;
}u;
int t;
int f;
}expdesc;
typedef struct upvaldesc{
lu_byte k;
lu_byte info;
}upvaldesc;
struct BlockCnt;
typedef struct FuncState{
Proto*f;
Table*h;
struct FuncState*prev;
struct LexState*ls;
struct lua_State*L;
struct BlockCnt*bl;
int pc;
int lasttarget;
int jpc;
int freereg;
int nk;
int np;
short nlocvars;
lu_byte nactvar;
upvaldesc upvalues[60];
unsigned short actvar[200];
}FuncState;
static Proto*luaY_parser(lua_State*L,ZIO*z,Mbuffer*buff,
const char*name);
struct lua_longjmp{
struct lua_longjmp*previous;
jmp_buf b;
volatile int status;
};
static void luaD_seterrorobj(lua_State*L,int errcode,StkId oldtop){
switch(errcode){
case 4:{
setsvalue(L,oldtop,luaS_newliteral(L,"not enough memory"));
break;
}
case 5:{
setsvalue(L,oldtop,luaS_newliteral(L,"error in error handling"));
break;
}
case 3:
case 2:{
setobj(L,oldtop,L->top-1);
break;
}
}
L->top=oldtop+1;
}
static void restore_stack_limit(lua_State*L){
if(L->size_ci>20000){
int inuse=cast_int(L->ci-L->base_ci);
if(inuse+1<20000)
luaD_reallocCI(L,20000);
}
}
static void resetstack(lua_State*L,int status){
L->ci=L->base_ci;
L->base=L->ci->base;
luaF_close(L,L->base);
luaD_seterrorobj(L,status,L->base);
L->nCcalls=L->baseCcalls;
L->allowhook=1;
restore_stack_limit(L);
L->errfunc=0;
L->errorJmp=NULL;
}
static void luaD_throw(lua_State*L,int errcode){
if(L->errorJmp){
L->errorJmp->status=errcode;
LUAI_THROW(L,L->errorJmp);
}
else{
L->status=cast_byte(errcode);
if(G(L)->panic){
resetstack(L,errcode);
G(L)->panic(L);
}
exit(EXIT_FAILURE);
}
}
static int luaD_rawrunprotected(lua_State*L,Pfunc f,void*ud){
struct lua_longjmp lj;
lj.status=0;
lj.previous=L->errorJmp;
L->errorJmp=&lj;
LUAI_TRY(L,&lj,
(*f)(L,ud);
);
L->errorJmp=lj.previous;
return lj.status;
}
static void correctstack(lua_State*L,TValue*oldstack){
CallInfo*ci;
GCObject*up;
L->top=(L->top-oldstack)+L->stack;
for(up=L->openupval;up!=NULL;up=up->gch.next)
gco2uv(up)->v=(gco2uv(up)->v-oldstack)+L->stack;
for(ci=L->base_ci;ci<=L->ci;ci++){
ci->top=(ci->top-oldstack)+L->stack;
ci->base=(ci->base-oldstack)+L->stack;
ci->func=(ci->func-oldstack)+L->stack;
}
L->base=(L->base-oldstack)+L->stack;
}
static void luaD_reallocstack(lua_State*L,int newsize){
TValue*oldstack=L->stack;
int realsize=newsize+1+5;
luaM_reallocvector(L,L->stack,L->stacksize,realsize,TValue);
L->stacksize=realsize;
L->stack_last=L->stack+newsize;
correctstack(L,oldstack);
}
static void luaD_reallocCI(lua_State*L,int newsize){
CallInfo*oldci=L->base_ci;
luaM_reallocvector(L,L->base_ci,L->size_ci,newsize,CallInfo);
L->size_ci=newsize;
L->ci=(L->ci-oldci)+L->base_ci;
L->end_ci=L->base_ci+L->size_ci-1;
}
static void luaD_growstack(lua_State*L,int n){
if(n<=L->stacksize)
luaD_reallocstack(L,2*L->stacksize);
else
luaD_reallocstack(L,L->stacksize+n);
}
static CallInfo*growCI(lua_State*L){
if(L->size_ci>20000)
luaD_throw(L,5);
else{
luaD_reallocCI(L,2*L->size_ci);
if(L->size_ci>20000)
luaG_runerror(L,"stack overflow");
}
return++L->ci;
}
static StkId adjust_varargs(lua_State*L,Proto*p,int actual){
int i;
int nfixargs=p->numparams;
Table*htab=NULL;
StkId base,fixed;
for(;actual<nfixargs;++actual)
setnilvalue(L->top++);
fixed=L->top-actual;
base=L->top;
for(i=0;i<nfixargs;i++){
setobj(L,L->top++,fixed+i);
setnilvalue(fixed+i);
}
if(htab){
sethvalue(L,L->top++,htab);
}
return base;
}
static StkId tryfuncTM(lua_State*L,StkId func){
const TValue*tm=luaT_gettmbyobj(L,func,TM_CALL);
StkId p;
ptrdiff_t funcr=savestack(L,func);
if(!ttisfunction(tm))
luaG_typeerror(L,func,"call");
for(p=L->top;p>func;p--)setobj(L,p,p-1);
incr_top(L);
func=restorestack(L,funcr);
setobj(L,func,tm);
return func;
}
#define inc_ci(L)((L->ci==L->end_ci)?growCI(L):(condhardstacktests(luaD_reallocCI(L,L->size_ci)),++L->ci))
static int luaD_precall(lua_State*L,StkId func,int nresults){
LClosure*cl;
ptrdiff_t funcr;
if(!ttisfunction(func))
func=tryfuncTM(L,func);
funcr=savestack(L,func);
cl=&clvalue(func)->l;
L->ci->savedpc=L->savedpc;
if(!cl->isC){
CallInfo*ci;
StkId st,base;
Proto*p=cl->p;
luaD_checkstack(L,p->maxstacksize+p->numparams);
func=restorestack(L,funcr);
if(!p->is_vararg){
base=func+1;
if(L->top>base+p->numparams)
L->top=base+p->numparams;
}
else{
int nargs=cast_int(L->top-func)-1;
base=adjust_varargs(L,p,nargs);
func=restorestack(L,funcr);
}
ci=inc_ci(L);
ci->func=func;
L->base=ci->base=base;
ci->top=L->base+p->maxstacksize;
L->savedpc=p->code;
ci->tailcalls=0;
ci->nresults=nresults;
for(st=L->top;st<ci->top;st++)
setnilvalue(st);
L->top=ci->top;
return 0;
}
else{
CallInfo*ci;
int n;
luaD_checkstack(L,20);
ci=inc_ci(L);
ci->func=restorestack(L,funcr);
L->base=ci->base=ci->func+1;
ci->top=L->top+20;
ci->nresults=nresults;
n=(*curr_func(L)->c.f)(L);
if(n<0)
return 2;
else{
luaD_poscall(L,L->top-n);
return 1;
}
}
}
static int luaD_poscall(lua_State*L,StkId firstResult){
StkId res;
int wanted,i;
CallInfo*ci;
ci=L->ci--;
res=ci->func;
wanted=ci->nresults;
L->base=(ci-1)->base;
L->savedpc=(ci-1)->savedpc;
for(i=wanted;i!=0&&firstResult<L->top;i--)
setobj(L,res++,firstResult++);
while(i-->0)
setnilvalue(res++);
L->top=res;
return(wanted-(-1));
}
static void luaD_call(lua_State*L,StkId func,int nResults){
if(++L->nCcalls>=200){
if(L->nCcalls==200)
luaG_runerror(L,"C stack overflow");
else if(L->nCcalls>=(200+(200>>3)))
luaD_throw(L,5);
}
if(luaD_precall(L,func,nResults)==0)
luaV_execute(L,1);
L->nCcalls--;
luaC_checkGC(L);
}
static int luaD_pcall(lua_State*L,Pfunc func,void*u,
ptrdiff_t old_top,ptrdiff_t ef){
int status;
unsigned short oldnCcalls=L->nCcalls;
ptrdiff_t old_ci=saveci(L,L->ci);
lu_byte old_allowhooks=L->allowhook;
ptrdiff_t old_errfunc=L->errfunc;
L->errfunc=ef;
status=luaD_rawrunprotected(L,func,u);
if(status!=0){
StkId oldtop=restorestack(L,old_top);
luaF_close(L,oldtop);
luaD_seterrorobj(L,status,oldtop);
L->nCcalls=oldnCcalls;
L->ci=restoreci(L,old_ci);
L->base=L->ci->base;
L->savedpc=L->ci->savedpc;
L->allowhook=old_allowhooks;
restore_stack_limit(L);
}
L->errfunc=old_errfunc;
return status;
}
struct SParser{
ZIO*z;
Mbuffer buff;
const char*name;
};
static void f_parser(lua_State*L,void*ud){
int i;
Proto*tf;
Closure*cl;
struct SParser*p=cast(struct SParser*,ud);
luaC_checkGC(L);
tf=luaY_parser(L,p->z,
&p->buff,p->name);
cl=luaF_newLclosure(L,tf->nups,hvalue(gt(L)));
cl->l.p=tf;
for(i=0;i<tf->nups;i++)
cl->l.upvals[i]=luaF_newupval(L);
setclvalue(L,L->top,cl);
incr_top(L);
}
static int luaD_protectedparser(lua_State*L,ZIO*z,const char*name){
struct SParser p;
int status;
p.z=z;p.name=name;
luaZ_initbuffer(L,&p.buff);
status=luaD_pcall(L,f_parser,&p,savestack(L,L->top),L->errfunc);
luaZ_freebuffer(L,&p.buff);
return status;
}
static void luaS_resize(lua_State*L,int newsize){
GCObject**newhash;
stringtable*tb;
int i;
if(G(L)->gcstate==2)
return;
newhash=luaM_newvector(L,newsize,GCObject*);
tb=&G(L)->strt;
for(i=0;i<newsize;i++)newhash[i]=NULL;
for(i=0;i<tb->size;i++){
GCObject*p=tb->hash[i];
while(p){
GCObject*next=p->gch.next;
unsigned int h=gco2ts(p)->hash;
int h1=lmod(h,newsize);
p->gch.next=newhash[h1];
newhash[h1]=p;
p=next;
}
}
luaM_freearray(L,tb->hash,tb->size,TString*);
tb->size=newsize;
tb->hash=newhash;
}
static TString*newlstr(lua_State*L,const char*str,size_t l,
unsigned int h){
TString*ts;
stringtable*tb;
if(l+1>(((size_t)(~(size_t)0)-2)-sizeof(TString))/sizeof(char))
luaM_toobig(L);
ts=cast(TString*,luaM_malloc(L,(l+1)*sizeof(char)+sizeof(TString)));
ts->tsv.len=l;
ts->tsv.hash=h;
ts->tsv.marked=luaC_white(G(L));
ts->tsv.tt=4;
ts->tsv.reserved=0;
memcpy(ts+1,str,l*sizeof(char));
((char*)(ts+1))[l]='\0';
tb=&G(L)->strt;
h=lmod(h,tb->size);
ts->tsv.next=tb->hash[h];
tb->hash[h]=obj2gco(ts);
tb->nuse++;
if(tb->nuse>cast(lu_int32,tb->size)&&tb->size<=(INT_MAX-2)/2)
luaS_resize(L,tb->size*2);
return ts;
}
static TString*luaS_newlstr(lua_State*L,const char*str,size_t l){
GCObject*o;
unsigned int h=cast(unsigned int,l);
size_t step=(l>>5)+1;
size_t l1;
for(l1=l;l1>=step;l1-=step)
h=h^((h<<5)+(h>>2)+cast(unsigned char,str[l1-1]));
for(o=G(L)->strt.hash[lmod(h,G(L)->strt.size)];
o!=NULL;
o=o->gch.next){
TString*ts=rawgco2ts(o);
if(ts->tsv.len==l&&(memcmp(str,getstr(ts),l)==0)){
if(isdead(G(L),o))changewhite(o);
return ts;
}
}
return newlstr(L,str,l,h);
}
static Udata*luaS_newudata(lua_State*L,size_t s,Table*e){
Udata*u;
if(s>((size_t)(~(size_t)0)-2)-sizeof(Udata))
luaM_toobig(L);
u=cast(Udata*,luaM_malloc(L,s+sizeof(Udata)));
u->uv.marked=luaC_white(G(L));
u->uv.tt=7;
u->uv.len=s;
u->uv.metatable=NULL;
u->uv.env=e;
u->uv.next=G(L)->mainthread->next;
G(L)->mainthread->next=obj2gco(u);
return u;
}
#define hashpow2(t,n)(gnode(t,lmod((n),sizenode(t))))
#define hashstr(t,str)hashpow2(t,(str)->tsv.hash)
#define hashboolean(t,p)hashpow2(t,p)
#define hashmod(t,n)(gnode(t,((n)%((sizenode(t)-1)|1))))
#define hashpointer(t,p)hashmod(t,IntPoint(p))
static const Node dummynode_={
{{NULL},0},
{{{NULL},0,NULL}}
};
static Node*hashnum(const Table*t,lua_Number n){
unsigned int a[cast_int(sizeof(lua_Number)/sizeof(int))];
int i;
if(luai_numeq(n,0))
return gnode(t,0);
memcpy(a,&n,sizeof(a));
for(i=1;i<cast_int(sizeof(lua_Number)/sizeof(int));i++)a[0]+=a[i];
return hashmod(t,a[0]);
}
static Node*mainposition(const Table*t,const TValue*key){
switch(ttype(key)){
case 3:
return hashnum(t,nvalue(key));
case 4:
return hashstr(t,rawtsvalue(key));
case 1:
return hashboolean(t,bvalue(key));
case 2:
return hashpointer(t,pvalue(key));
default:
return hashpointer(t,gcvalue(key));
}
}
static int arrayindex(const TValue*key){
if(ttisnumber(key)){
lua_Number n=nvalue(key);
int k;
lua_number2int(k,n);
if(luai_numeq(cast_num(k),n))
return k;
}
return-1;
}
static int findindex(lua_State*L,Table*t,StkId key){
int i;
if(ttisnil(key))return-1;
i=arrayindex(key);
if(0<i&&i<=t->sizearray)
return i-1;
else{
Node*n=mainposition(t,key);
do{
if(luaO_rawequalObj(key2tval(n),key)||
(ttype(gkey(n))==(8+3)&&iscollectable(key)&&
gcvalue(gkey(n))==gcvalue(key))){
i=cast_int(n-gnode(t,0));
return i+t->sizearray;
}
else n=gnext(n);
}while(n);
luaG_runerror(L,"invalid key to "LUA_QL("next"));
return 0;
}
}
static int luaH_next(lua_State*L,Table*t,StkId key){
int i=findindex(L,t,key);
for(i++;i<t->sizearray;i++){
if(!ttisnil(&t->array[i])){
setnvalue(key,cast_num(i+1));
setobj(L,key+1,&t->array[i]);
return 1;
}
}
for(i-=t->sizearray;i<(int)sizenode(t);i++){
if(!ttisnil(gval(gnode(t,i)))){
setobj(L,key,key2tval(gnode(t,i)));
setobj(L,key+1,gval(gnode(t,i)));
return 1;
}
}
return 0;
}
static int computesizes(int nums[],int*narray){
int i;
int twotoi;
int a=0;
int na=0;
int n=0;
for(i=0,twotoi=1;twotoi/2<*narray;i++,twotoi*=2){
if(nums[i]>0){
a+=nums[i];
if(a>twotoi/2){
n=twotoi;
na=a;
}
}
if(a==*narray)break;
}
*narray=n;
return na;
}
static int countint(const TValue*key,int*nums){
int k=arrayindex(key);
if(0<k&&k<=(1<<(32-2))){
nums[ceillog2(k)]++;
return 1;
}
else
return 0;
}
static int numusearray(const Table*t,int*nums){
int lg;
int ttlg;
int ause=0;
int i=1;
for(lg=0,ttlg=1;lg<=(32-2);lg++,ttlg*=2){
int lc=0;
int lim=ttlg;
if(lim>t->sizearray){
lim=t->sizearray;
if(i>lim)
break;
}
for(;i<=lim;i++){
if(!ttisnil(&t->array[i-1]))
lc++;
}
nums[lg]+=lc;
ause+=lc;
}
return ause;
}
static int numusehash(const Table*t,int*nums,int*pnasize){
int totaluse=0;
int ause=0;
int i=sizenode(t);
while(i--){
Node*n=&t->node[i];
if(!ttisnil(gval(n))){
ause+=countint(key2tval(n),nums);
totaluse++;
}
}
*pnasize+=ause;
return totaluse;
}
static void setarrayvector(lua_State*L,Table*t,int size){
int i;
luaM_reallocvector(L,t->array,t->sizearray,size,TValue);
for(i=t->sizearray;i<size;i++)
setnilvalue(&t->array[i]);
t->sizearray=size;
}
static void setnodevector(lua_State*L,Table*t,int size){
int lsize;
if(size==0){
t->node=cast(Node*,(&dummynode_));
lsize=0;
}
else{
int i;
lsize=ceillog2(size);
if(lsize>(32-2))
luaG_runerror(L,"table overflow");
size=twoto(lsize);
t->node=luaM_newvector(L,size,Node);
for(i=0;i<size;i++){
Node*n=gnode(t,i);
gnext(n)=NULL;
setnilvalue(gkey(n));
setnilvalue(gval(n));
}
}
t->lsizenode=cast_byte(lsize);
t->lastfree=gnode(t,size);
}
static void resize(lua_State*L,Table*t,int nasize,int nhsize){
int i;
int oldasize=t->sizearray;
int oldhsize=t->lsizenode;
Node*nold=t->node;
if(nasize>oldasize)
setarrayvector(L,t,nasize);
setnodevector(L,t,nhsize);
if(nasize<oldasize){
t->sizearray=nasize;
for(i=nasize;i<oldasize;i++){
if(!ttisnil(&t->array[i]))
setobj(L,luaH_setnum(L,t,i+1),&t->array[i]);
}
luaM_reallocvector(L,t->array,oldasize,nasize,TValue);
}
for(i=twoto(oldhsize)-1;i>=0;i--){
Node*old=nold+i;
if(!ttisnil(gval(old)))
setobj(L,luaH_set(L,t,key2tval(old)),gval(old));
}
if(nold!=(&dummynode_))
luaM_freearray(L,nold,twoto(oldhsize),Node);
}
static void luaH_resizearray(lua_State*L,Table*t,int nasize){
int nsize=(t->node==(&dummynode_))?0:sizenode(t);
resize(L,t,nasize,nsize);
}
static void rehash(lua_State*L,Table*t,const TValue*ek){
int nasize,na;
int nums[(32-2)+1];
int i;
int totaluse;
for(i=0;i<=(32-2);i++)nums[i]=0;
nasize=numusearray(t,nums);
totaluse=nasize;
totaluse+=numusehash(t,nums,&nasize);
nasize+=countint(ek,nums);
totaluse++;
na=computesizes(nums,&nasize);
resize(L,t,nasize,totaluse-na);
}
static Table*luaH_new(lua_State*L,int narray,int nhash){
Table*t=luaM_new(L,Table);
luaC_link(L,obj2gco(t),5);
t->metatable=NULL;
t->flags=cast_byte(~0);
t->array=NULL;
t->sizearray=0;
t->lsizenode=0;
t->node=cast(Node*,(&dummynode_));
setarrayvector(L,t,narray);
setnodevector(L,t,nhash);
return t;
}
static void luaH_free(lua_State*L,Table*t){
if(t->node!=(&dummynode_))
luaM_freearray(L,t->node,sizenode(t),Node);
luaM_freearray(L,t->array,t->sizearray,TValue);
luaM_free(L,t);
}
static Node*getfreepos(Table*t){
while(t->lastfree-->t->node){
if(ttisnil(gkey(t->lastfree)))
return t->lastfree;
}
return NULL;
}
static TValue*newkey(lua_State*L,Table*t,const TValue*key){
Node*mp=mainposition(t,key);
if(!ttisnil(gval(mp))||mp==(&dummynode_)){
Node*othern;
Node*n=getfreepos(t);
if(n==NULL){
rehash(L,t,key);
return luaH_set(L,t,key);
}
othern=mainposition(t,key2tval(mp));
if(othern!=mp){
while(gnext(othern)!=mp)othern=gnext(othern);
gnext(othern)=n;
*n=*mp;
gnext(mp)=NULL;
setnilvalue(gval(mp));
}
else{
gnext(n)=gnext(mp);
gnext(mp)=n;
mp=n;
}
}
gkey(mp)->value=key->value;gkey(mp)->tt=key->tt;
luaC_barriert(L,t,key);
return gval(mp);
}
static const TValue*luaH_getnum(Table*t,int key){
if(cast(unsigned int,key)-1<cast(unsigned int,t->sizearray))
return&t->array[key-1];
else{
lua_Number nk=cast_num(key);
Node*n=hashnum(t,nk);
do{
if(ttisnumber(gkey(n))&&luai_numeq(nvalue(gkey(n)),nk))
return gval(n);
else n=gnext(n);
}while(n);
return(&luaO_nilobject_);
}
}
static const TValue*luaH_getstr(Table*t,TString*key){
Node*n=hashstr(t,key);
do{
if(ttisstring(gkey(n))&&rawtsvalue(gkey(n))==key)
return gval(n);
else n=gnext(n);
}while(n);
return(&luaO_nilobject_);
}
static const TValue*luaH_get(Table*t,const TValue*key){
switch(ttype(key)){
case 0:return(&luaO_nilobject_);
case 4:return luaH_getstr(t,rawtsvalue(key));
case 3:{
int k;
lua_Number n=nvalue(key);
lua_number2int(k,n);
if(luai_numeq(cast_num(k),nvalue(key)))
return luaH_getnum(t,k);
}
/*fallthrough*/
default:{
Node*n=mainposition(t,key);
do{
if(luaO_rawequalObj(key2tval(n),key))
return gval(n);
else n=gnext(n);
}while(n);
return(&luaO_nilobject_);
}
}
}
static TValue*luaH_set(lua_State*L,Table*t,const TValue*key){
const TValue*p=luaH_get(t,key);
t->flags=0;
if(p!=(&luaO_nilobject_))
return cast(TValue*,p);
else{
if(ttisnil(key))luaG_runerror(L,"table index is nil");
else if(ttisnumber(key)&&luai_numisnan(nvalue(key)))
luaG_runerror(L,"table index is NaN");
return newkey(L,t,key);
}
}
static TValue*luaH_setnum(lua_State*L,Table*t,int key){
const TValue*p=luaH_getnum(t,key);
if(p!=(&luaO_nilobject_))
return cast(TValue*,p);
else{
TValue k;
setnvalue(&k,cast_num(key));
return newkey(L,t,&k);
}
}
static TValue*luaH_setstr(lua_State*L,Table*t,TString*key){
const TValue*p=luaH_getstr(t,key);
if(p!=(&luaO_nilobject_))
return cast(TValue*,p);
else{
TValue k;
setsvalue(L,&k,key);
return newkey(L,t,&k);
}
}
static int unbound_search(Table*t,unsigned int j){
unsigned int i=j;
j++;
while(!ttisnil(luaH_getnum(t,j))){
i=j;
j*=2;
if(j>cast(unsigned int,(INT_MAX-2))){
i=1;
while(!ttisnil(luaH_getnum(t,i)))i++;
return i-1;
}
}
while(j-i>1){
unsigned int m=(i+j)/2;
if(ttisnil(luaH_getnum(t,m)))j=m;
else i=m;
}
return i;
}
static int luaH_getn(Table*t){
unsigned int j=t->sizearray;
if(j>0&&ttisnil(&t->array[j-1])){
unsigned int i=0;
while(j-i>1){
unsigned int m=(i+j)/2;
if(ttisnil(&t->array[m-1]))j=m;
else i=m;
}
return i;
}
else if(t->node==(&dummynode_))
return j;
else return unbound_search(t,j);
}
#define makewhite(g,x)((x)->gch.marked=cast_byte(((x)->gch.marked&cast_byte(~(bitmask(2)|bit2mask(0,1))))|luaC_white(g)))
#define white2gray(x)reset2bits((x)->gch.marked,0,1)
#define black2gray(x)resetbit((x)->gch.marked,2)
#define stringmark(s)reset2bits((s)->tsv.marked,0,1)
#define isfinalized(u)testbit((u)->marked,3)
#define markfinalized(u)l_setbit((u)->marked,3)
#define markvalue(g,o){checkconsistency(o);if(iscollectable(o)&&iswhite(gcvalue(o)))reallymarkobject(g,gcvalue(o));}
#define markobject(g,t){if(iswhite(obj2gco(t)))reallymarkobject(g,obj2gco(t));}
#define setthreshold(g)(g->GCthreshold=(g->estimate/100)*g->gcpause)
static void removeentry(Node*n){
if(iscollectable(gkey(n)))
setttype(gkey(n),(8+3));
}
static void reallymarkobject(global_State*g,GCObject*o){
white2gray(o);
switch(o->gch.tt){
case 4:{
return;
}
case 7:{
Table*mt=gco2u(o)->metatable;
gray2black(o);
if(mt)markobject(g,mt);
markobject(g,gco2u(o)->env);
return;
}
case(8+2):{
UpVal*uv=gco2uv(o);
markvalue(g,uv->v);
if(uv->v==&uv->u.value)
gray2black(o);
return;
}
case 6:{
gco2cl(o)->c.gclist=g->gray;
g->gray=o;
break;
}
case 5:{
gco2h(o)->gclist=g->gray;
g->gray=o;
break;
}
case 8:{
gco2th(o)->gclist=g->gray;
g->gray=o;
break;
}
case(8+1):{
gco2p(o)->gclist=g->gray;
g->gray=o;
break;
}
default:;
}
}
static void marktmu(global_State*g){
GCObject*u=g->tmudata;
if(u){
do{
u=u->gch.next;
makewhite(g,u);
reallymarkobject(g,u);
}while(u!=g->tmudata);
}
}
static size_t luaC_separateudata(lua_State*L,int all){
global_State*g=G(L);
size_t deadmem=0;
GCObject**p=&g->mainthread->next;
GCObject*curr;
while((curr=*p)!=NULL){
if(!(iswhite(curr)||all)||isfinalized(gco2u(curr)))
p=&curr->gch.next;
else if(fasttm(L,gco2u(curr)->metatable,TM_GC)==NULL){
markfinalized(gco2u(curr));
p=&curr->gch.next;
}
else{
deadmem+=sizeudata(gco2u(curr));
markfinalized(gco2u(curr));
*p=curr->gch.next;
if(g->tmudata==NULL)
g->tmudata=curr->gch.next=curr;
else{
curr->gch.next=g->tmudata->gch.next;
g->tmudata->gch.next=curr;
g->tmudata=curr;
}
}
}
return deadmem;
}
static int traversetable(global_State*g,Table*h){
int i;
int weakkey=0;
int weakvalue=0;
const TValue*mode;
if(h->metatable)
markobject(g,h->metatable);
mode=gfasttm(g,h->metatable,TM_MODE);
if(mode&&ttisstring(mode)){
weakkey=(strchr(svalue(mode),'k')!=NULL);
weakvalue=(strchr(svalue(mode),'v')!=NULL);
if(weakkey||weakvalue){
h->marked&=~(bitmask(3)|bitmask(4));
h->marked|=cast_byte((weakkey<<3)|
(weakvalue<<4));
h->gclist=g->weak;
g->weak=obj2gco(h);
}
}
if(weakkey&&weakvalue)return 1;
if(!weakvalue){
i=h->sizearray;
while(i--)
markvalue(g,&h->array[i]);
}
i=sizenode(h);
while(i--){
Node*n=gnode(h,i);
if(ttisnil(gval(n)))
removeentry(n);
else{
if(!weakkey)markvalue(g,gkey(n));
if(!weakvalue)markvalue(g,gval(n));
}
}
return weakkey||weakvalue;
}
static void traverseproto(global_State*g,Proto*f){
int i;
if(f->source)stringmark(f->source);
for(i=0;i<f->sizek;i++)
markvalue(g,&f->k[i]);
for(i=0;i<f->sizeupvalues;i++){
if(f->upvalues[i])
stringmark(f->upvalues[i]);
}
for(i=0;i<f->sizep;i++){
if(f->p[i])
markobject(g,f->p[i]);
}
for(i=0;i<f->sizelocvars;i++){
if(f->locvars[i].varname)
stringmark(f->locvars[i].varname);
}
}
static void traverseclosure(global_State*g,Closure*cl){
markobject(g,cl->c.env);
if(cl->c.isC){
int i;
for(i=0;i<cl->c.nupvalues;i++)
markvalue(g,&cl->c.upvalue[i]);
}
else{
int i;
markobject(g,cl->l.p);
for(i=0;i<cl->l.nupvalues;i++)
markobject(g,cl->l.upvals[i]);
}
}
static void checkstacksizes(lua_State*L,StkId max){
int ci_used=cast_int(L->ci-L->base_ci);
int s_used=cast_int(max-L->stack);
if(L->size_ci>20000)
return;
if(4*ci_used<L->size_ci&&2*8<L->size_ci)
luaD_reallocCI(L,L->size_ci/2);
condhardstacktests(luaD_reallocCI(L,ci_used+1));
if(4*s_used<L->stacksize&&
2*((2*20)+5)<L->stacksize)
luaD_reallocstack(L,L->stacksize/2);
condhardstacktests(luaD_reallocstack(L,s_used));
}
static void traversestack(global_State*g,lua_State*l){
StkId o,lim;
CallInfo*ci;
markvalue(g,gt(l));
lim=l->top;
for(ci=l->base_ci;ci<=l->ci;ci++){
if(lim<ci->top)lim=ci->top;
}
for(o=l->stack;o<l->top;o++)
markvalue(g,o);
for(;o<=lim;o++)
setnilvalue(o);
checkstacksizes(l,lim);
}
static l_mem propagatemark(global_State*g){
GCObject*o=g->gray;
gray2black(o);
switch(o->gch.tt){
case 5:{
Table*h=gco2h(o);
g->gray=h->gclist;
if(traversetable(g,h))
black2gray(o);
return sizeof(Table)+sizeof(TValue)*h->sizearray+
sizeof(Node)*sizenode(h);
}
case 6:{
Closure*cl=gco2cl(o);
g->gray=cl->c.gclist;
traverseclosure(g,cl);
return(cl->c.isC)?sizeCclosure(cl->c.nupvalues):
sizeLclosure(cl->l.nupvalues);
}
case 8:{
lua_State*th=gco2th(o);
g->gray=th->gclist;
th->gclist=g->grayagain;
g->grayagain=o;
black2gray(o);
traversestack(g,th);
return sizeof(lua_State)+sizeof(TValue)*th->stacksize+
sizeof(CallInfo)*th->size_ci;
}
case(8+1):{
Proto*p=gco2p(o);
g->gray=p->gclist;
traverseproto(g,p);
return sizeof(Proto)+sizeof(Instruction)*p->sizecode+
sizeof(Proto*)*p->sizep+
sizeof(TValue)*p->sizek+
sizeof(int)*p->sizelineinfo+
sizeof(LocVar)*p->sizelocvars+
sizeof(TString*)*p->sizeupvalues;
}
default:return 0;
}
}
static size_t propagateall(global_State*g){
size_t m=0;
while(g->gray)m+=propagatemark(g);
return m;
}
static int iscleared(const TValue*o,int iskey){
if(!iscollectable(o))return 0;
if(ttisstring(o)){
stringmark(rawtsvalue(o));
return 0;
}
return iswhite(gcvalue(o))||
(ttisuserdata(o)&&(!iskey&&isfinalized(uvalue(o))));
}
static void cleartable(GCObject*l){
while(l){
Table*h=gco2h(l);
int i=h->sizearray;
if(testbit(h->marked,4)){
while(i--){
TValue*o=&h->array[i];
if(iscleared(o,0))
setnilvalue(o);
}
}
i=sizenode(h);
while(i--){
Node*n=gnode(h,i);
if(!ttisnil(gval(n))&&
(iscleared(key2tval(n),1)||iscleared(gval(n),0))){
setnilvalue(gval(n));
removeentry(n);
}
}
l=h->gclist;
}
}
static void freeobj(lua_State*L,GCObject*o){
switch(o->gch.tt){
case(8+1):luaF_freeproto(L,gco2p(o));break;
case 6:luaF_freeclosure(L,gco2cl(o));break;
case(8+2):luaF_freeupval(L,gco2uv(o));break;
case 5:luaH_free(L,gco2h(o));break;
case 8:{
luaE_freethread(L,gco2th(o));
break;
}
case 4:{
G(L)->strt.nuse--;
luaM_freemem(L,o,sizestring(gco2ts(o)));
break;
}
case 7:{
luaM_freemem(L,o,sizeudata(gco2u(o)));
break;
}
default:;
}
}
#define sweepwholelist(L,p)sweeplist(L,p,((lu_mem)(~(lu_mem)0)-2))
static GCObject**sweeplist(lua_State*L,GCObject**p,lu_mem count){
GCObject*curr;
global_State*g=G(L);
int deadmask=otherwhite(g);
while((curr=*p)!=NULL&&count-->0){
if(curr->gch.tt==8)
sweepwholelist(L,&gco2th(curr)->openupval);
if((curr->gch.marked^bit2mask(0,1))&deadmask){
makewhite(g,curr);
p=&curr->gch.next;
}
else{
*p=curr->gch.next;
if(curr==g->rootgc)
g->rootgc=curr->gch.next;
freeobj(L,curr);
}
}
return p;
}
static void checkSizes(lua_State*L){
global_State*g=G(L);
if(g->strt.nuse<cast(lu_int32,g->strt.size/4)&&
g->strt.size>32*2)
luaS_resize(L,g->strt.size/2);
if(luaZ_sizebuffer(&g->buff)>32*2){
size_t newsize=luaZ_sizebuffer(&g->buff)/2;
luaZ_resizebuffer(L,&g->buff,newsize);
}
}
static void GCTM(lua_State*L){
global_State*g=G(L);
GCObject*o=g->tmudata->gch.next;
Udata*udata=rawgco2u(o);
const TValue*tm;
if(o==g->tmudata)
g->tmudata=NULL;
else
g->tmudata->gch.next=udata->uv.next;
udata->uv.next=g->mainthread->next;
g->mainthread->next=o;
makewhite(g,o);
tm=fasttm(L,udata->uv.metatable,TM_GC);
if(tm!=NULL){
lu_byte oldah=L->allowhook;
lu_mem oldt=g->GCthreshold;
L->allowhook=0;
g->GCthreshold=2*g->totalbytes;
setobj(L,L->top,tm);
setuvalue(L,L->top+1,udata);
L->top+=2;
luaD_call(L,L->top-2,0);
L->allowhook=oldah;
g->GCthreshold=oldt;
}
}
static void luaC_callGCTM(lua_State*L){
while(G(L)->tmudata)
GCTM(L);
}
static void luaC_freeall(lua_State*L){
global_State*g=G(L);
int i;
g->currentwhite=bit2mask(0,1)|bitmask(6);
sweepwholelist(L,&g->rootgc);
for(i=0;i<g->strt.size;i++)
sweepwholelist(L,&g->strt.hash[i]);
}
static void markmt(global_State*g){
int i;
for(i=0;i<(8+1);i++)
if(g->mt[i])markobject(g,g->mt[i]);
}
static void markroot(lua_State*L){
global_State*g=G(L);
g->gray=NULL;
g->grayagain=NULL;
g->weak=NULL;
markobject(g,g->mainthread);
markvalue(g,gt(g->mainthread));
markvalue(g,registry(L));
markmt(g);
g->gcstate=1;
}
static void remarkupvals(global_State*g){
UpVal*uv;
for(uv=g->uvhead.u.l.next;uv!=&g->uvhead;uv=uv->u.l.next){
if(isgray(obj2gco(uv)))
markvalue(g,uv->v);
}
}
static void atomic(lua_State*L){
global_State*g=G(L);
size_t udsize;
remarkupvals(g);
propagateall(g);
g->gray=g->weak;
g->weak=NULL;
markobject(g,L);
markmt(g);
propagateall(g);
g->gray=g->grayagain;
g->grayagain=NULL;
propagateall(g);
udsize=luaC_separateudata(L,0);
marktmu(g);
udsize+=propagateall(g);
cleartable(g->weak);
g->currentwhite=cast_byte(otherwhite(g));
g->sweepstrgc=0;
g->sweepgc=&g->rootgc;
g->gcstate=2;
g->estimate=g->totalbytes-udsize;
}
static l_mem singlestep(lua_State*L){
global_State*g=G(L);
switch(g->gcstate){
case 0:{
markroot(L);
return 0;
}
case 1:{
if(g->gray)
return propagatemark(g);
else{
atomic(L);
return 0;
}
}
case 2:{
lu_mem old=g->totalbytes;
sweepwholelist(L,&g->strt.hash[g->sweepstrgc++]);
if(g->sweepstrgc>=g->strt.size)
g->gcstate=3;
g->estimate-=old-g->totalbytes;
return 10;
}
case 3:{
lu_mem old=g->totalbytes;
g->sweepgc=sweeplist(L,g->sweepgc,40);
if(*g->sweepgc==NULL){
checkSizes(L);
g->gcstate=4;
}
g->estimate-=old-g->totalbytes;
return 40*10;
}
case 4:{
if(g->tmudata){
GCTM(L);
if(g->estimate>100)
g->estimate-=100;
return 100;
}
else{
g->gcstate=0;
g->gcdept=0;
return 0;
}
}
default:return 0;
}
}
static void luaC_step(lua_State*L){
global_State*g=G(L);
l_mem lim=(1024u/100)*g->gcstepmul;
if(lim==0)
lim=(((lu_mem)(~(lu_mem)0)-2)-1)/2;
g->gcdept+=g->totalbytes-g->GCthreshold;
do{
lim-=singlestep(L);
if(g->gcstate==0)
break;
}while(lim>0);
if(g->gcstate!=0){
if(g->gcdept<1024u)
g->GCthreshold=g->totalbytes+1024u;
else{
g->gcdept-=1024u;
g->GCthreshold=g->totalbytes;
}
}
else{
setthreshold(g);
}
}
static void luaC_barrierf(lua_State*L,GCObject*o,GCObject*v){
global_State*g=G(L);
if(g->gcstate==1)
reallymarkobject(g,v);
else
makewhite(g,o);
}
static void luaC_barrierback(lua_State*L,Table*t){
global_State*g=G(L);
GCObject*o=obj2gco(t);
black2gray(o);
t->gclist=g->grayagain;
g->grayagain=o;
}
static void luaC_link(lua_State*L,GCObject*o,lu_byte tt){
global_State*g=G(L);
o->gch.next=g->rootgc;
g->rootgc=o;
o->gch.marked=luaC_white(g);
o->gch.tt=tt;
}
static void luaC_linkupval(lua_State*L,UpVal*uv){
global_State*g=G(L);
GCObject*o=obj2gco(uv);
o->gch.next=g->rootgc;
g->rootgc=o;
if(isgray(o)){
if(g->gcstate==1){
gray2black(o);
luaC_barrier(L,uv,uv->v);
}
else{
makewhite(g,o);
}
}
}
typedef union{
lua_Number r;
TString*ts;
}SemInfo;
typedef struct Token{
int token;
SemInfo seminfo;
}Token;
typedef struct LexState{
int current;
int linenumber;
int lastline;
Token t;
Token lookahead;
struct FuncState*fs;
struct lua_State*L;
ZIO*z;
Mbuffer*buff;
TString*source;
char decpoint;
}LexState;
static void luaX_init(lua_State*L);
static void luaX_lexerror(LexState*ls,const char*msg,int token);
#define state_size(x)(sizeof(x)+0)
#define fromstate(l)(cast(lu_byte*,(l))-0)
#define tostate(l)(cast(lua_State*,cast(lu_byte*,l)+0))
typedef struct LG{
lua_State l;
global_State g;
}LG;
static void stack_init(lua_State*L1,lua_State*L){
L1->base_ci=luaM_newvector(L,8,CallInfo);
L1->ci=L1->base_ci;
L1->size_ci=8;
L1->end_ci=L1->base_ci+L1->size_ci-1;
L1->stack=luaM_newvector(L,(2*20)+5,TValue);
L1->stacksize=(2*20)+5;
L1->top=L1->stack;
L1->stack_last=L1->stack+(L1->stacksize-5)-1;
L1->ci->func=L1->top;
setnilvalue(L1->top++);
L1->base=L1->ci->base=L1->top;
L1->ci->top=L1->top+20;
}
static void freestack(lua_State*L,lua_State*L1){