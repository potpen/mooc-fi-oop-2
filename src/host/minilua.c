
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
luaM_freearray(L,L1->base_ci,L1->size_ci,CallInfo);
luaM_freearray(L,L1->stack,L1->stacksize,TValue);
}
static void f_luaopen(lua_State*L,void*ud){
global_State*g=G(L);
UNUSED(ud);
stack_init(L,L);
sethvalue(L,gt(L),luaH_new(L,0,2));
sethvalue(L,registry(L),luaH_new(L,0,2));
luaS_resize(L,32);
luaT_init(L);
luaX_init(L);
luaS_fix(luaS_newliteral(L,"not enough memory"));
g->GCthreshold=4*g->totalbytes;
}
static void preinit_state(lua_State*L,global_State*g){
G(L)=g;
L->stack=NULL;
L->stacksize=0;
L->errorJmp=NULL;
L->hook=NULL;
L->hookmask=0;
L->basehookcount=0;
L->allowhook=1;
resethookcount(L);
L->openupval=NULL;
L->size_ci=0;
L->nCcalls=L->baseCcalls=0;
L->status=0;
L->base_ci=L->ci=NULL;
L->savedpc=NULL;
L->errfunc=0;
setnilvalue(gt(L));
}
static void close_state(lua_State*L){
global_State*g=G(L);
luaF_close(L,L->stack);
luaC_freeall(L);
luaM_freearray(L,G(L)->strt.hash,G(L)->strt.size,TString*);
luaZ_freebuffer(L,&g->buff);
freestack(L,L);
(*g->frealloc)(g->ud,fromstate(L),state_size(LG),0);
}
static void luaE_freethread(lua_State*L,lua_State*L1){
luaF_close(L1,L1->stack);
freestack(L,L1);
luaM_freemem(L,fromstate(L1),state_size(lua_State));
}
static lua_State*lua_newstate(lua_Alloc f,void*ud){
int i;
lua_State*L;
global_State*g;
void*l=(*f)(ud,NULL,0,state_size(LG));
if(l==NULL)return NULL;
L=tostate(l);
g=&((LG*)L)->g;
L->next=NULL;
L->tt=8;
g->currentwhite=bit2mask(0,5);
L->marked=luaC_white(g);
set2bits(L->marked,5,6);
preinit_state(L,g);
g->frealloc=f;
g->ud=ud;
g->mainthread=L;
g->uvhead.u.l.prev=&g->uvhead;
g->uvhead.u.l.next=&g->uvhead;
g->GCthreshold=0;
g->strt.size=0;
g->strt.nuse=0;
g->strt.hash=NULL;
setnilvalue(registry(L));
luaZ_initbuffer(L,&g->buff);
g->panic=NULL;
g->gcstate=0;
g->rootgc=obj2gco(L);
g->sweepstrgc=0;
g->sweepgc=&g->rootgc;
g->gray=NULL;
g->grayagain=NULL;
g->weak=NULL;
g->tmudata=NULL;
g->totalbytes=sizeof(LG);
g->gcpause=200;
g->gcstepmul=200;
g->gcdept=0;
for(i=0;i<(8+1);i++)g->mt[i]=NULL;
if(luaD_rawrunprotected(L,f_luaopen,NULL)!=0){
close_state(L);
L=NULL;
}
else
{}
return L;
}
static void callallgcTM(lua_State*L,void*ud){
UNUSED(ud);
luaC_callGCTM(L);
}
static void lua_close(lua_State*L){
L=G(L)->mainthread;
luaF_close(L,L->stack);
luaC_separateudata(L,1);
L->errfunc=0;
do{
L->ci=L->base_ci;
L->base=L->top=L->ci->base;
L->nCcalls=L->baseCcalls=0;
}while(luaD_rawrunprotected(L,callallgcTM,NULL)!=0);
close_state(L);
}
#define getcode(fs,e)((fs)->f->code[(e)->u.s.info])
#define luaK_codeAsBx(fs,o,A,sBx)luaK_codeABx(fs,o,A,(sBx)+(((1<<(9+9))-1)>>1))
#define luaK_setmultret(fs,e)luaK_setreturns(fs,e,(-1))
static int luaK_codeABx(FuncState*fs,OpCode o,int A,unsigned int Bx);
static int luaK_codeABC(FuncState*fs,OpCode o,int A,int B,int C);
static void luaK_setreturns(FuncState*fs,expdesc*e,int nresults);
static void luaK_patchtohere(FuncState*fs,int list);
static void luaK_concat(FuncState*fs,int*l1,int l2);
static int currentpc(lua_State*L,CallInfo*ci){
if(!isLua(ci))return-1;
if(ci==L->ci)
ci->savedpc=L->savedpc;
return pcRel(ci->savedpc,ci_func(ci)->l.p);
}
static int currentline(lua_State*L,CallInfo*ci){
int pc=currentpc(L,ci);
if(pc<0)
return-1;
else
return getline_(ci_func(ci)->l.p,pc);
}
static int lua_getstack(lua_State*L,int level,lua_Debug*ar){
int status;
CallInfo*ci;
for(ci=L->ci;level>0&&ci>L->base_ci;ci--){
level--;
if(f_isLua(ci))
level-=ci->tailcalls;
}
if(level==0&&ci>L->base_ci){
status=1;
ar->i_ci=cast_int(ci-L->base_ci);
}
else if(level<0){
status=1;
ar->i_ci=0;
}
else status=0;
return status;
}
static Proto*getluaproto(CallInfo*ci){
return(isLua(ci)?ci_func(ci)->l.p:NULL);
}
static void funcinfo(lua_Debug*ar,Closure*cl){
if(cl->c.isC){
ar->source="=[C]";
ar->linedefined=-1;
ar->lastlinedefined=-1;
ar->what="C";
}
else{
ar->source=getstr(cl->l.p->source);
ar->linedefined=cl->l.p->linedefined;
ar->lastlinedefined=cl->l.p->lastlinedefined;
ar->what=(ar->linedefined==0)?"main":"Lua";
}
luaO_chunkid(ar->short_src,ar->source,60);
}
static void info_tailcall(lua_Debug*ar){
ar->name=ar->namewhat="";
ar->what="tail";
ar->lastlinedefined=ar->linedefined=ar->currentline=-1;
ar->source="=(tail call)";
luaO_chunkid(ar->short_src,ar->source,60);
ar->nups=0;
}
static void collectvalidlines(lua_State*L,Closure*f){
if(f==NULL||f->c.isC){
setnilvalue(L->top);
}
else{
Table*t=luaH_new(L,0,0);
int*lineinfo=f->l.p->lineinfo;
int i;
for(i=0;i<f->l.p->sizelineinfo;i++)
setbvalue(luaH_setnum(L,t,lineinfo[i]),1);
sethvalue(L,L->top,t);
}
incr_top(L);
}
static int auxgetinfo(lua_State*L,const char*what,lua_Debug*ar,
Closure*f,CallInfo*ci){
int status=1;
if(f==NULL){
info_tailcall(ar);
return status;
}
for(;*what;what++){
switch(*what){
case'S':{
funcinfo(ar,f);
break;
}
case'l':{
ar->currentline=(ci)?currentline(L,ci):-1;
break;
}
case'u':{
ar->nups=f->c.nupvalues;
break;
}
case'n':{
ar->namewhat=(ci)?NULL:NULL;
if(ar->namewhat==NULL){
ar->namewhat="";
ar->name=NULL;
}
break;
}
case'L':
case'f':
break;
default:status=0;
}
}
return status;
}
static int lua_getinfo(lua_State*L,const char*what,lua_Debug*ar){
int status;
Closure*f=NULL;
CallInfo*ci=NULL;
if(*what=='>'){
StkId func=L->top-1;
luai_apicheck(L,ttisfunction(func));
what++;
f=clvalue(func);
L->top--;
}
else if(ar->i_ci!=0){
ci=L->base_ci+ar->i_ci;
f=clvalue(ci->func);
}
status=auxgetinfo(L,what,ar,f,ci);
if(strchr(what,'f')){
if(f==NULL)setnilvalue(L->top);
else setclvalue(L,L->top,f);
incr_top(L);
}
if(strchr(what,'L'))
collectvalidlines(L,f);
return status;
}
static int isinstack(CallInfo*ci,const TValue*o){
StkId p;
for(p=ci->base;p<ci->top;p++)
if(o==p)return 1;
return 0;
}
static void luaG_typeerror(lua_State*L,const TValue*o,const char*op){
const char*name=NULL;
const char*t=luaT_typenames[ttype(o)];
const char*kind=(isinstack(L->ci,o))?
NULL:
NULL;
if(kind)
luaG_runerror(L,"attempt to %s %s "LUA_QL("%s")" (a %s value)",
op,kind,name,t);
else
luaG_runerror(L,"attempt to %s a %s value",op,t);
}
static void luaG_concaterror(lua_State*L,StkId p1,StkId p2){
if(ttisstring(p1)||ttisnumber(p1))p1=p2;
luaG_typeerror(L,p1,"concatenate");
}
static void luaG_aritherror(lua_State*L,const TValue*p1,const TValue*p2){
TValue temp;
if(luaV_tonumber(p1,&temp)==NULL)
p2=p1;
luaG_typeerror(L,p2,"perform arithmetic on");
}
static int luaG_ordererror(lua_State*L,const TValue*p1,const TValue*p2){
const char*t1=luaT_typenames[ttype(p1)];
const char*t2=luaT_typenames[ttype(p2)];
if(t1[2]==t2[2])
luaG_runerror(L,"attempt to compare two %s values",t1);
else
luaG_runerror(L,"attempt to compare %s with %s",t1,t2);
return 0;
}
static void addinfo(lua_State*L,const char*msg){
CallInfo*ci=L->ci;
if(isLua(ci)){
char buff[60];
int line=currentline(L,ci);
luaO_chunkid(buff,getstr(getluaproto(ci)->source),60);
luaO_pushfstring(L,"%s:%d: %s",buff,line,msg);
}
}
static void luaG_errormsg(lua_State*L){
if(L->errfunc!=0){
StkId errfunc=restorestack(L,L->errfunc);
if(!ttisfunction(errfunc))luaD_throw(L,5);
setobj(L,L->top,L->top-1);
setobj(L,L->top-1,errfunc);
incr_top(L);
luaD_call(L,L->top-2,1);
}
luaD_throw(L,2);
}
static void luaG_runerror(lua_State*L,const char*fmt,...){
va_list argp;
va_start(argp,fmt);
addinfo(L,luaO_pushvfstring(L,fmt,argp));
va_end(argp);
luaG_errormsg(L);
}
static int luaZ_fill(ZIO*z){
size_t size;
lua_State*L=z->L;
const char*buff;
buff=z->reader(L,z->data,&size);
if(buff==NULL||size==0)return(-1);
z->n=size-1;
z->p=buff;
return char2int(*(z->p++));
}
static void luaZ_init(lua_State*L,ZIO*z,lua_Reader reader,void*data){
z->L=L;
z->reader=reader;
z->data=data;
z->n=0;
z->p=NULL;
}
static char*luaZ_openspace(lua_State*L,Mbuffer*buff,size_t n){
if(n>buff->buffsize){
if(n<32)n=32;
luaZ_resizebuffer(L,buff,n);
}
return buff->buffer;
}
#define opmode(t,a,b,c,m)(((t)<<7)|((a)<<6)|((b)<<4)|((c)<<2)|(m))
static const lu_byte luaP_opmodes[(cast(int,OP_VARARG)+1)]={
opmode(0,1,OpArgR,OpArgN,iABC)
,opmode(0,1,OpArgK,OpArgN,iABx)
,opmode(0,1,OpArgU,OpArgU,iABC)
,opmode(0,1,OpArgR,OpArgN,iABC)
,opmode(0,1,OpArgU,OpArgN,iABC)
,opmode(0,1,OpArgK,OpArgN,iABx)
,opmode(0,1,OpArgR,OpArgK,iABC)
,opmode(0,0,OpArgK,OpArgN,iABx)
,opmode(0,0,OpArgU,OpArgN,iABC)
,opmode(0,0,OpArgK,OpArgK,iABC)
,opmode(0,1,OpArgU,OpArgU,iABC)
,opmode(0,1,OpArgR,OpArgK,iABC)
,opmode(0,1,OpArgK,OpArgK,iABC)
,opmode(0,1,OpArgK,OpArgK,iABC)
,opmode(0,1,OpArgK,OpArgK,iABC)
,opmode(0,1,OpArgK,OpArgK,iABC)
,opmode(0,1,OpArgK,OpArgK,iABC)
,opmode(0,1,OpArgK,OpArgK,iABC)
,opmode(0,1,OpArgR,OpArgN,iABC)
,opmode(0,1,OpArgR,OpArgN,iABC)
,opmode(0,1,OpArgR,OpArgN,iABC)
,opmode(0,1,OpArgR,OpArgR,iABC)
,opmode(0,0,OpArgR,OpArgN,iAsBx)
,opmode(1,0,OpArgK,OpArgK,iABC)
,opmode(1,0,OpArgK,OpArgK,iABC)
,opmode(1,0,OpArgK,OpArgK,iABC)
,opmode(1,1,OpArgR,OpArgU,iABC)
,opmode(1,1,OpArgR,OpArgU,iABC)
,opmode(0,1,OpArgU,OpArgU,iABC)
,opmode(0,1,OpArgU,OpArgU,iABC)
,opmode(0,0,OpArgU,OpArgN,iABC)
,opmode(0,1,OpArgR,OpArgN,iAsBx)
,opmode(0,1,OpArgR,OpArgN,iAsBx)
,opmode(1,0,OpArgN,OpArgU,iABC)
,opmode(0,0,OpArgU,OpArgU,iABC)
,opmode(0,0,OpArgN,OpArgN,iABC)
,opmode(0,1,OpArgU,OpArgN,iABx)
,opmode(0,1,OpArgU,OpArgN,iABC)
};
#define next(ls)(ls->current=zgetc(ls->z))
#define currIsNewline(ls)(ls->current=='\n'||ls->current=='\r')
static const char*const luaX_tokens[]={
"and","break","do","else","elseif",
"end","false","for","function","if",
"in","local","nil","not","or","repeat",
"return","then","true","until","while",
"..","...","==",">=","<=","~=",
"<number>","<name>","<string>","<eof>",
NULL
};
#define save_and_next(ls)(save(ls,ls->current),next(ls))
static void save(LexState*ls,int c){
Mbuffer*b=ls->buff;
if(b->n+1>b->buffsize){
size_t newsize;
if(b->buffsize>=((size_t)(~(size_t)0)-2)/2)
luaX_lexerror(ls,"lexical element too long",0);
newsize=b->buffsize*2;
luaZ_resizebuffer(ls->L,b,newsize);
}
b->buffer[b->n++]=cast(char,c);
}
static void luaX_init(lua_State*L){
int i;
for(i=0;i<(cast(int,TK_WHILE-257+1));i++){
TString*ts=luaS_new(L,luaX_tokens[i]);
luaS_fix(ts);
ts->tsv.reserved=cast_byte(i+1);
}
}
static const char*luaX_token2str(LexState*ls,int token){
if(token<257){
return(iscntrl(token))?luaO_pushfstring(ls->L,"char(%d)",token):
luaO_pushfstring(ls->L,"%c",token);
}
else
return luaX_tokens[token-257];
}
static const char*txtToken(LexState*ls,int token){
switch(token){
case TK_NAME:
case TK_STRING:
case TK_NUMBER:
save(ls,'\0');
return luaZ_buffer(ls->buff);
default:
return luaX_token2str(ls,token);
}
}
static void luaX_lexerror(LexState*ls,const char*msg,int token){
char buff[80];
luaO_chunkid(buff,getstr(ls->source),80);
msg=luaO_pushfstring(ls->L,"%s:%d: %s",buff,ls->linenumber,msg);
if(token)
luaO_pushfstring(ls->L,"%s near "LUA_QL("%s"),msg,txtToken(ls,token));
luaD_throw(ls->L,3);
}
static void luaX_syntaxerror(LexState*ls,const char*msg){
luaX_lexerror(ls,msg,ls->t.token);
}
static TString*luaX_newstring(LexState*ls,const char*str,size_t l){
lua_State*L=ls->L;
TString*ts=luaS_newlstr(L,str,l);
TValue*o=luaH_setstr(L,ls->fs->h,ts);
if(ttisnil(o)){
setbvalue(o,1);
luaC_checkGC(L);
}
return ts;
}
static void inclinenumber(LexState*ls){
int old=ls->current;
next(ls);
if(currIsNewline(ls)&&ls->current!=old)
next(ls);
if(++ls->linenumber>=(INT_MAX-2))
luaX_syntaxerror(ls,"chunk has too many lines");
}
static void luaX_setinput(lua_State*L,LexState*ls,ZIO*z,TString*source){
ls->decpoint='.';
ls->L=L;
ls->lookahead.token=TK_EOS;
ls->z=z;
ls->fs=NULL;
ls->linenumber=1;
ls->lastline=1;
ls->source=source;
luaZ_resizebuffer(ls->L,ls->buff,32);
next(ls);
}
static int check_next(LexState*ls,const char*set){
if(!strchr(set,ls->current))
return 0;
save_and_next(ls);
return 1;
}
static void buffreplace(LexState*ls,char from,char to){
size_t n=luaZ_bufflen(ls->buff);
char*p=luaZ_buffer(ls->buff);
while(n--)
if(p[n]==from)p[n]=to;
}
static void read_numeral(LexState*ls,SemInfo*seminfo){
do{
save_and_next(ls);
}while(isdigit(ls->current)||ls->current=='.');
if(check_next(ls,"Ee"))
check_next(ls,"+-");
while(isalnum(ls->current)||ls->current=='_')
save_and_next(ls);
save(ls,'\0');
buffreplace(ls,'.',ls->decpoint);
if(!luaO_str2d(luaZ_buffer(ls->buff),&seminfo->r))
luaX_lexerror(ls,"malformed number",TK_NUMBER);
}
static int skip_sep(LexState*ls){
int count=0;
int s=ls->current;
save_and_next(ls);
while(ls->current=='='){
save_and_next(ls);
count++;
}
return(ls->current==s)?count:(-count)-1;
}
static void read_long_string(LexState*ls,SemInfo*seminfo,int sep){
int cont=0;
(void)(cont);
save_and_next(ls);
if(currIsNewline(ls))
inclinenumber(ls);
for(;;){
switch(ls->current){
case(-1):
luaX_lexerror(ls,(seminfo)?"unfinished long string":
"unfinished long comment",TK_EOS);
break;
case']':{
if(skip_sep(ls)==sep){
save_and_next(ls);
goto endloop;
}
break;
}
case'\n':
case'\r':{
save(ls,'\n');
inclinenumber(ls);
if(!seminfo)luaZ_resetbuffer(ls->buff);
break;
}
default:{
if(seminfo)save_and_next(ls);
else next(ls);
}
}
}endloop:
if(seminfo)
seminfo->ts=luaX_newstring(ls,luaZ_buffer(ls->buff)+(2+sep),
luaZ_bufflen(ls->buff)-2*(2+sep));
}
static void read_string(LexState*ls,int del,SemInfo*seminfo){
save_and_next(ls);
while(ls->current!=del){
switch(ls->current){
case(-1):
luaX_lexerror(ls,"unfinished string",TK_EOS);
continue;
case'\n':
case'\r':
luaX_lexerror(ls,"unfinished string",TK_STRING);
continue;
case'\\':{
int c;
next(ls);
switch(ls->current){
case'a':c='\a';break;
case'b':c='\b';break;
case'f':c='\f';break;
case'n':c='\n';break;
case'r':c='\r';break;
case't':c='\t';break;
case'v':c='\v';break;
case'\n':
case'\r':save(ls,'\n');inclinenumber(ls);continue;
case(-1):continue;
default:{
if(!isdigit(ls->current))
save_and_next(ls);
else{
int i=0;
c=0;
do{
c=10*c+(ls->current-'0');
next(ls);
}while(++i<3&&isdigit(ls->current));
if(c>UCHAR_MAX)
luaX_lexerror(ls,"escape sequence too large",TK_STRING);
save(ls,c);
}
continue;
}
}
save(ls,c);
next(ls);
continue;
}
default:
save_and_next(ls);
}
}
save_and_next(ls);
seminfo->ts=luaX_newstring(ls,luaZ_buffer(ls->buff)+1,
luaZ_bufflen(ls->buff)-2);
}
static int llex(LexState*ls,SemInfo*seminfo){
luaZ_resetbuffer(ls->buff);
for(;;){
switch(ls->current){
case'\n':
case'\r':{
inclinenumber(ls);
continue;
}
case'-':{
next(ls);
if(ls->current!='-')return'-';
next(ls);
if(ls->current=='['){
int sep=skip_sep(ls);
luaZ_resetbuffer(ls->buff);
if(sep>=0){
read_long_string(ls,NULL,sep);
luaZ_resetbuffer(ls->buff);
continue;
}
}
while(!currIsNewline(ls)&&ls->current!=(-1))
next(ls);
continue;
}
case'[':{
int sep=skip_sep(ls);
if(sep>=0){
read_long_string(ls,seminfo,sep);
return TK_STRING;
}
else if (sep!=-1)luaX_lexerror(ls,"invalid long string delimiter",TK_STRING);
return'[';
}
case'=':{
next(ls);
if(ls->current!='=')return'=';
else{next(ls);return TK_EQ;}
}
case'<':{
next(ls);
if(ls->current!='=')return'<';
else{next(ls);return TK_LE;}
}
case'>':{
next(ls);
if(ls->current!='=')return'>';
else{next(ls);return TK_GE;}
}
case'~':{
next(ls);
if(ls->current!='=')return'~';
else{next(ls);return TK_NE;}
}
case'"':
case'\'':{
read_string(ls,ls->current,seminfo);
return TK_STRING;
}
case'.':{
save_and_next(ls);
if(check_next(ls,".")){
if(check_next(ls,"."))
return TK_DOTS;
else return TK_CONCAT;
}
else if(!isdigit(ls->current))return'.';
else{
read_numeral(ls,seminfo);
return TK_NUMBER;
}
}
case(-1):{
return TK_EOS;
}
default:{
if(isspace(ls->current)){
next(ls);
continue;
}
else if(isdigit(ls->current)){
read_numeral(ls,seminfo);
return TK_NUMBER;
}
else if(isalpha(ls->current)||ls->current=='_'){
TString*ts;
do{
save_and_next(ls);
}while(isalnum(ls->current)||ls->current=='_');
ts=luaX_newstring(ls,luaZ_buffer(ls->buff),
luaZ_bufflen(ls->buff));
if(ts->tsv.reserved>0)
return ts->tsv.reserved-1+257;
else{
seminfo->ts=ts;
return TK_NAME;
}
}
else{
int c=ls->current;
next(ls);
return c;
}
}
}
}
}
static void luaX_next(LexState*ls){
ls->lastline=ls->linenumber;
if(ls->lookahead.token!=TK_EOS){
ls->t=ls->lookahead;
ls->lookahead.token=TK_EOS;
}
else
ls->t.token=llex(ls,&ls->t.seminfo);
}
static void luaX_lookahead(LexState*ls){
ls->lookahead.token=llex(ls,&ls->lookahead.seminfo);
}
#define hasjumps(e)((e)->t!=(e)->f)
static int isnumeral(expdesc*e){
return(e->k==VKNUM&&e->t==(-1)&&e->f==(-1));
}
static void luaK_nil(FuncState*fs,int from,int n){
Instruction*previous;
if(fs->pc>fs->lasttarget){
if(fs->pc==0){
if(from>=fs->nactvar)
return;
}
else{
previous=&fs->f->code[fs->pc-1];
if(GET_OPCODE(*previous)==OP_LOADNIL){
int pfrom=GETARG_A(*previous);
int pto=GETARG_B(*previous);
if(pfrom<=from&&from<=pto+1){
if(from+n-1>pto)
SETARG_B(*previous,from+n-1);
return;
}
}
}
}
luaK_codeABC(fs,OP_LOADNIL,from,from+n-1,0);
}
static int luaK_jump(FuncState*fs){
int jpc=fs->jpc;
int j;
fs->jpc=(-1);
j=luaK_codeAsBx(fs,OP_JMP,0,(-1));
luaK_concat(fs,&j,jpc);
return j;
}
static void luaK_ret(FuncState*fs,int first,int nret){
luaK_codeABC(fs,OP_RETURN,first,nret+1,0);
}
static int condjump(FuncState*fs,OpCode op,int A,int B,int C){
luaK_codeABC(fs,op,A,B,C);
return luaK_jump(fs);
}
static void fixjump(FuncState*fs,int pc,int dest){
Instruction*jmp=&fs->f->code[pc];
int offset=dest-(pc+1);
if(abs(offset)>(((1<<(9+9))-1)>>1))
luaX_syntaxerror(fs->ls,"control structure too long");
SETARG_sBx(*jmp,offset);
}
static int luaK_getlabel(FuncState*fs){
fs->lasttarget=fs->pc;
return fs->pc;
}
static int getjump(FuncState*fs,int pc){
int offset=GETARG_sBx(fs->f->code[pc]);
if(offset==(-1))
return(-1);
else
return(pc+1)+offset;
}
static Instruction*getjumpcontrol(FuncState*fs,int pc){
Instruction*pi=&fs->f->code[pc];
if(pc>=1&&testTMode(GET_OPCODE(*(pi-1))))
return pi-1;
else
return pi;
}
static int need_value(FuncState*fs,int list){
for(;list!=(-1);list=getjump(fs,list)){
Instruction i=*getjumpcontrol(fs,list);
if(GET_OPCODE(i)!=OP_TESTSET)return 1;
}
return 0;
}
static int patchtestreg(FuncState*fs,int node,int reg){
Instruction*i=getjumpcontrol(fs,node);
if(GET_OPCODE(*i)!=OP_TESTSET)
return 0;
if(reg!=((1<<8)-1)&&reg!=GETARG_B(*i))
SETARG_A(*i,reg);
else
*i=CREATE_ABC(OP_TEST,GETARG_B(*i),0,GETARG_C(*i));
return 1;
}
static void removevalues(FuncState*fs,int list){
for(;list!=(-1);list=getjump(fs,list))
patchtestreg(fs,list,((1<<8)-1));
}
static void patchlistaux(FuncState*fs,int list,int vtarget,int reg,
int dtarget){
while(list!=(-1)){
int next=getjump(fs,list);
if(patchtestreg(fs,list,reg))
fixjump(fs,list,vtarget);
else
fixjump(fs,list,dtarget);
list=next;
}
}
static void dischargejpc(FuncState*fs){
patchlistaux(fs,fs->jpc,fs->pc,((1<<8)-1),fs->pc);
fs->jpc=(-1);
}
static void luaK_patchlist(FuncState*fs,int list,int target){
if(target==fs->pc)
luaK_patchtohere(fs,list);
else{
patchlistaux(fs,list,target,((1<<8)-1),target);
}
}
static void luaK_patchtohere(FuncState*fs,int list){
luaK_getlabel(fs);
luaK_concat(fs,&fs->jpc,list);
}
static void luaK_concat(FuncState*fs,int*l1,int l2){
if(l2==(-1))return;
else if(*l1==(-1))
*l1=l2;
else{
int list=*l1;
int next;
while((next=getjump(fs,list))!=(-1))
list=next;
fixjump(fs,list,l2);
}
}
static void luaK_checkstack(FuncState*fs,int n){
int newstack=fs->freereg+n;
if(newstack>fs->f->maxstacksize){
if(newstack>=250)
luaX_syntaxerror(fs->ls,"function or expression too complex");
fs->f->maxstacksize=cast_byte(newstack);
}
}
static void luaK_reserveregs(FuncState*fs,int n){
luaK_checkstack(fs,n);
fs->freereg+=n;
}
static void freereg(FuncState*fs,int reg){
if(!ISK(reg)&&reg>=fs->nactvar){
fs->freereg--;
}
}
static void freeexp(FuncState*fs,expdesc*e){
if(e->k==VNONRELOC)
freereg(fs,e->u.s.info);
}
static int addk(FuncState*fs,TValue*k,TValue*v){
lua_State*L=fs->L;
TValue*idx=luaH_set(L,fs->h,k);
Proto*f=fs->f;
int oldsize=f->sizek;
if(ttisnumber(idx)){
return cast_int(nvalue(idx));
}
else{
setnvalue(idx,cast_num(fs->nk));
luaM_growvector(L,f->k,fs->nk,f->sizek,TValue,
((1<<(9+9))-1),"constant table overflow");
while(oldsize<f->sizek)setnilvalue(&f->k[oldsize++]);
setobj(L,&f->k[fs->nk],v);
luaC_barrier(L,f,v);
return fs->nk++;
}
}
static int luaK_stringK(FuncState*fs,TString*s){
TValue o;
setsvalue(fs->L,&o,s);
return addk(fs,&o,&o);
}
static int luaK_numberK(FuncState*fs,lua_Number r){
TValue o;
setnvalue(&o,r);
return addk(fs,&o,&o);
}
static int boolK(FuncState*fs,int b){
TValue o;
setbvalue(&o,b);
return addk(fs,&o,&o);
}
static int nilK(FuncState*fs){
TValue k,v;
setnilvalue(&v);
sethvalue(fs->L,&k,fs->h);
return addk(fs,&k,&v);
}
static void luaK_setreturns(FuncState*fs,expdesc*e,int nresults){
if(e->k==VCALL){
SETARG_C(getcode(fs,e),nresults+1);
}
else if(e->k==VVARARG){
SETARG_B(getcode(fs,e),nresults+1);
SETARG_A(getcode(fs,e),fs->freereg);
luaK_reserveregs(fs,1);
}
}
static void luaK_setoneret(FuncState*fs,expdesc*e){
if(e->k==VCALL){
e->k=VNONRELOC;
e->u.s.info=GETARG_A(getcode(fs,e));
}
else if(e->k==VVARARG){
SETARG_B(getcode(fs,e),2);
e->k=VRELOCABLE;
}
}
static void luaK_dischargevars(FuncState*fs,expdesc*e){
switch(e->k){
case VLOCAL:{
e->k=VNONRELOC;
break;
}
case VUPVAL:{
e->u.s.info=luaK_codeABC(fs,OP_GETUPVAL,0,e->u.s.info,0);
e->k=VRELOCABLE;
break;
}
case VGLOBAL:{
e->u.s.info=luaK_codeABx(fs,OP_GETGLOBAL,0,e->u.s.info);
e->k=VRELOCABLE;
break;
}
case VINDEXED:{
freereg(fs,e->u.s.aux);
freereg(fs,e->u.s.info);
e->u.s.info=luaK_codeABC(fs,OP_GETTABLE,0,e->u.s.info,e->u.s.aux);
e->k=VRELOCABLE;
break;
}
case VVARARG:
case VCALL:{
luaK_setoneret(fs,e);
break;
}
default:break;
}
}
static int code_label(FuncState*fs,int A,int b,int jump){
luaK_getlabel(fs);
return luaK_codeABC(fs,OP_LOADBOOL,A,b,jump);
}
static void discharge2reg(FuncState*fs,expdesc*e,int reg){
luaK_dischargevars(fs,e);
switch(e->k){
case VNIL:{
luaK_nil(fs,reg,1);
break;
}
case VFALSE:case VTRUE:{
luaK_codeABC(fs,OP_LOADBOOL,reg,e->k==VTRUE,0);
break;
}
case VK:{
luaK_codeABx(fs,OP_LOADK,reg,e->u.s.info);
break;
}
case VKNUM:{
luaK_codeABx(fs,OP_LOADK,reg,luaK_numberK(fs,e->u.nval));
break;
}
case VRELOCABLE:{
Instruction*pc=&getcode(fs,e);
SETARG_A(*pc,reg);
break;
}
case VNONRELOC:{
if(reg!=e->u.s.info)
luaK_codeABC(fs,OP_MOVE,reg,e->u.s.info,0);
break;
}
default:{
return;
}
}
e->u.s.info=reg;
e->k=VNONRELOC;
}
static void discharge2anyreg(FuncState*fs,expdesc*e){
if(e->k!=VNONRELOC){
luaK_reserveregs(fs,1);
discharge2reg(fs,e,fs->freereg-1);
}
}
static void exp2reg(FuncState*fs,expdesc*e,int reg){
discharge2reg(fs,e,reg);
if(e->k==VJMP)
luaK_concat(fs,&e->t,e->u.s.info);
if(hasjumps(e)){
int final;
int p_f=(-1);
int p_t=(-1);
if(need_value(fs,e->t)||need_value(fs,e->f)){
int fj=(e->k==VJMP)?(-1):luaK_jump(fs);
p_f=code_label(fs,reg,0,1);
p_t=code_label(fs,reg,1,0);
luaK_patchtohere(fs,fj);
}
final=luaK_getlabel(fs);
patchlistaux(fs,e->f,final,reg,p_f);
patchlistaux(fs,e->t,final,reg,p_t);
}
e->f=e->t=(-1);
e->u.s.info=reg;
e->k=VNONRELOC;
}
static void luaK_exp2nextreg(FuncState*fs,expdesc*e){
luaK_dischargevars(fs,e);
freeexp(fs,e);
luaK_reserveregs(fs,1);
exp2reg(fs,e,fs->freereg-1);
}
static int luaK_exp2anyreg(FuncState*fs,expdesc*e){
luaK_dischargevars(fs,e);
if(e->k==VNONRELOC){
if(!hasjumps(e))return e->u.s.info;
if(e->u.s.info>=fs->nactvar){
exp2reg(fs,e,e->u.s.info);
return e->u.s.info;
}
}
luaK_exp2nextreg(fs,e);
return e->u.s.info;
}
static void luaK_exp2val(FuncState*fs,expdesc*e){
if(hasjumps(e))
luaK_exp2anyreg(fs,e);
else
luaK_dischargevars(fs,e);
}
static int luaK_exp2RK(FuncState*fs,expdesc*e){
luaK_exp2val(fs,e);
switch(e->k){
case VKNUM:
case VTRUE:
case VFALSE:
case VNIL:{
if(fs->nk<=((1<<(9-1))-1)){
e->u.s.info=(e->k==VNIL)?nilK(fs):
(e->k==VKNUM)?luaK_numberK(fs,e->u.nval):
boolK(fs,(e->k==VTRUE));
e->k=VK;
return RKASK(e->u.s.info);
}
else break;
}
case VK:{
if(e->u.s.info<=((1<<(9-1))-1))
return RKASK(e->u.s.info);
else break;
}
default:break;
}
return luaK_exp2anyreg(fs,e);
}
static void luaK_storevar(FuncState*fs,expdesc*var,expdesc*ex){
switch(var->k){
case VLOCAL:{
freeexp(fs,ex);
exp2reg(fs,ex,var->u.s.info);
return;
}
case VUPVAL:{
int e=luaK_exp2anyreg(fs,ex);
luaK_codeABC(fs,OP_SETUPVAL,e,var->u.s.info,0);
break;
}
case VGLOBAL:{
int e=luaK_exp2anyreg(fs,ex);
luaK_codeABx(fs,OP_SETGLOBAL,e,var->u.s.info);
break;
}
case VINDEXED:{
int e=luaK_exp2RK(fs,ex);
luaK_codeABC(fs,OP_SETTABLE,var->u.s.info,var->u.s.aux,e);
break;
}
default:{
break;
}
}
freeexp(fs,ex);
}
static void luaK_self(FuncState*fs,expdesc*e,expdesc*key){
int func;
luaK_exp2anyreg(fs,e);
freeexp(fs,e);
func=fs->freereg;
luaK_reserveregs(fs,2);
luaK_codeABC(fs,OP_SELF,func,e->u.s.info,luaK_exp2RK(fs,key));
freeexp(fs,key);
e->u.s.info=func;
e->k=VNONRELOC;
}
static void invertjump(FuncState*fs,expdesc*e){
Instruction*pc=getjumpcontrol(fs,e->u.s.info);
SETARG_A(*pc,!(GETARG_A(*pc)));
}
static int jumponcond(FuncState*fs,expdesc*e,int cond){
if(e->k==VRELOCABLE){
Instruction ie=getcode(fs,e);
if(GET_OPCODE(ie)==OP_NOT){
fs->pc--;
return condjump(fs,OP_TEST,GETARG_B(ie),0,!cond);
}
}
discharge2anyreg(fs,e);
freeexp(fs,e);
return condjump(fs,OP_TESTSET,((1<<8)-1),e->u.s.info,cond);
}
static void luaK_goiftrue(FuncState*fs,expdesc*e){
int pc;
luaK_dischargevars(fs,e);
switch(e->k){
case VK:case VKNUM:case VTRUE:{
pc=(-1);
break;
}
case VJMP:{
invertjump(fs,e);
pc=e->u.s.info;
break;
}
default:{
pc=jumponcond(fs,e,0);
break;
}
}
luaK_concat(fs,&e->f,pc);
luaK_patchtohere(fs,e->t);
e->t=(-1);
}
static void luaK_goiffalse(FuncState*fs,expdesc*e){
int pc;
luaK_dischargevars(fs,e);
switch(e->k){
case VNIL:case VFALSE:{
pc=(-1);
break;
}
case VJMP:{
pc=e->u.s.info;
break;
}
default:{
pc=jumponcond(fs,e,1);
break;
}
}
luaK_concat(fs,&e->t,pc);
luaK_patchtohere(fs,e->f);
e->f=(-1);
}
static void codenot(FuncState*fs,expdesc*e){
luaK_dischargevars(fs,e);
switch(e->k){
case VNIL:case VFALSE:{
e->k=VTRUE;
break;
}
case VK:case VKNUM:case VTRUE:{
e->k=VFALSE;
break;
}
case VJMP:{
invertjump(fs,e);
break;
}
case VRELOCABLE:
case VNONRELOC:{
discharge2anyreg(fs,e);
freeexp(fs,e);
e->u.s.info=luaK_codeABC(fs,OP_NOT,0,e->u.s.info,0);
e->k=VRELOCABLE;
break;
}
default:{
break;
}
}
{int temp=e->f;e->f=e->t;e->t=temp;}
removevalues(fs,e->f);
removevalues(fs,e->t);
}
static void luaK_indexed(FuncState*fs,expdesc*t,expdesc*k){
t->u.s.aux=luaK_exp2RK(fs,k);
t->k=VINDEXED;
}
static int constfolding(OpCode op,expdesc*e1,expdesc*e2){
lua_Number v1,v2,r;
if(!isnumeral(e1)||!isnumeral(e2))return 0;
v1=e1->u.nval;
v2=e2->u.nval;
switch(op){
case OP_ADD:r=luai_numadd(v1,v2);break;
case OP_SUB:r=luai_numsub(v1,v2);break;
case OP_MUL:r=luai_nummul(v1,v2);break;
case OP_DIV:
if(v2==0)return 0;
r=luai_numdiv(v1,v2);break;
case OP_MOD:
if(v2==0)return 0;
r=luai_nummod(v1,v2);break;
case OP_POW:r=luai_numpow(v1,v2);break;
case OP_UNM:r=luai_numunm(v1);break;
case OP_LEN:return 0;
default:r=0;break;
}
if(luai_numisnan(r))return 0;
e1->u.nval=r;
return 1;
}
static void codearith(FuncState*fs,OpCode op,expdesc*e1,expdesc*e2){
if(constfolding(op,e1,e2))
return;
else{
int o2=(op!=OP_UNM&&op!=OP_LEN)?luaK_exp2RK(fs,e2):0;
int o1=luaK_exp2RK(fs,e1);
if(o1>o2){
freeexp(fs,e1);
freeexp(fs,e2);
}
else{
freeexp(fs,e2);
freeexp(fs,e1);
}
e1->u.s.info=luaK_codeABC(fs,op,0,o1,o2);
e1->k=VRELOCABLE;
}
}
static void codecomp(FuncState*fs,OpCode op,int cond,expdesc*e1,
expdesc*e2){
int o1=luaK_exp2RK(fs,e1);
int o2=luaK_exp2RK(fs,e2);
freeexp(fs,e2);
freeexp(fs,e1);
if(cond==0&&op!=OP_EQ){
int temp;
temp=o1;o1=o2;o2=temp;
cond=1;
}
e1->u.s.info=condjump(fs,op,cond,o1,o2);
e1->k=VJMP;
}
static void luaK_prefix(FuncState*fs,UnOpr op,expdesc*e){
expdesc e2;
e2.t=e2.f=(-1);e2.k=VKNUM;e2.u.nval=0;
switch(op){
case OPR_MINUS:{
if(!isnumeral(e))
luaK_exp2anyreg(fs,e);
codearith(fs,OP_UNM,e,&e2);
break;
}
case OPR_NOT:codenot(fs,e);break;
case OPR_LEN:{
luaK_exp2anyreg(fs,e);
codearith(fs,OP_LEN,e,&e2);
break;
}
default:;
}
}
static void luaK_infix(FuncState*fs,BinOpr op,expdesc*v){
switch(op){
case OPR_AND:{
luaK_goiftrue(fs,v);
break;
}
case OPR_OR:{
luaK_goiffalse(fs,v);
break;
}
case OPR_CONCAT:{
luaK_exp2nextreg(fs,v);
break;
}
case OPR_ADD:case OPR_SUB:case OPR_MUL:case OPR_DIV:
case OPR_MOD:case OPR_POW:{
if(!isnumeral(v))luaK_exp2RK(fs,v);
break;
}
default:{
luaK_exp2RK(fs,v);
break;
}
}
}
static void luaK_posfix(FuncState*fs,BinOpr op,expdesc*e1,expdesc*e2){
switch(op){
case OPR_AND:{
luaK_dischargevars(fs,e2);
luaK_concat(fs,&e2->f,e1->f);
*e1=*e2;
break;
}
case OPR_OR:{
luaK_dischargevars(fs,e2);
luaK_concat(fs,&e2->t,e1->t);
*e1=*e2;
break;
}
case OPR_CONCAT:{
luaK_exp2val(fs,e2);
if(e2->k==VRELOCABLE&&GET_OPCODE(getcode(fs,e2))==OP_CONCAT){
freeexp(fs,e1);
SETARG_B(getcode(fs,e2),e1->u.s.info);
e1->k=VRELOCABLE;e1->u.s.info=e2->u.s.info;
}
else{
luaK_exp2nextreg(fs,e2);
codearith(fs,OP_CONCAT,e1,e2);
}
break;
}
case OPR_ADD:codearith(fs,OP_ADD,e1,e2);break;
case OPR_SUB:codearith(fs,OP_SUB,e1,e2);break;
case OPR_MUL:codearith(fs,OP_MUL,e1,e2);break;
case OPR_DIV:codearith(fs,OP_DIV,e1,e2);break;
case OPR_MOD:codearith(fs,OP_MOD,e1,e2);break;
case OPR_POW:codearith(fs,OP_POW,e1,e2);break;
case OPR_EQ:codecomp(fs,OP_EQ,1,e1,e2);break;
case OPR_NE:codecomp(fs,OP_EQ,0,e1,e2);break;
case OPR_LT:codecomp(fs,OP_LT,1,e1,e2);break;
case OPR_LE:codecomp(fs,OP_LE,1,e1,e2);break;
case OPR_GT:codecomp(fs,OP_LT,0,e1,e2);break;
case OPR_GE:codecomp(fs,OP_LE,0,e1,e2);break;
default:;
}
}
static void luaK_fixline(FuncState*fs,int line){
fs->f->lineinfo[fs->pc-1]=line;
}
static int luaK_code(FuncState*fs,Instruction i,int line){
Proto*f=fs->f;
dischargejpc(fs);
luaM_growvector(fs->L,f->code,fs->pc,f->sizecode,Instruction,
(INT_MAX-2),"code size overflow");
f->code[fs->pc]=i;
luaM_growvector(fs->L,f->lineinfo,fs->pc,f->sizelineinfo,int,
(INT_MAX-2),"code size overflow");
f->lineinfo[fs->pc]=line;
return fs->pc++;
}
static int luaK_codeABC(FuncState*fs,OpCode o,int a,int b,int c){
return luaK_code(fs,CREATE_ABC(o,a,b,c),fs->ls->lastline);
}
static int luaK_codeABx(FuncState*fs,OpCode o,int a,unsigned int bc){
return luaK_code(fs,CREATE_ABx(o,a,bc),fs->ls->lastline);
}
static void luaK_setlist(FuncState*fs,int base,int nelems,int tostore){
int c=(nelems-1)/50+1;
int b=(tostore==(-1))?0:tostore;
if(c<=((1<<9)-1))
luaK_codeABC(fs,OP_SETLIST,base,b,c);
else{
luaK_codeABC(fs,OP_SETLIST,base,b,0);
luaK_code(fs,cast(Instruction,c),fs->ls->lastline);
}
fs->freereg=base+1;
}
#define hasmultret(k)((k)==VCALL||(k)==VVARARG)
#define getlocvar(fs,i)((fs)->f->locvars[(fs)->actvar[i]])
#define luaY_checklimit(fs,v,l,m)if((v)>(l))errorlimit(fs,l,m)
typedef struct BlockCnt{
struct BlockCnt*previous;
int breaklist;
lu_byte nactvar;
lu_byte upval;
lu_byte isbreakable;
}BlockCnt;
static void chunk(LexState*ls);
static void expr(LexState*ls,expdesc*v);
static void anchor_token(LexState*ls){
if(ls->t.token==TK_NAME||ls->t.token==TK_STRING){
TString*ts=ls->t.seminfo.ts;
luaX_newstring(ls,getstr(ts),ts->tsv.len);
}
}
static void error_expected(LexState*ls,int token){
luaX_syntaxerror(ls,
luaO_pushfstring(ls->L,LUA_QL("%s")" expected",luaX_token2str(ls,token)));
}
static void errorlimit(FuncState*fs,int limit,const char*what){
const char*msg=(fs->f->linedefined==0)?
luaO_pushfstring(fs->L,"main function has more than %d %s",limit,what):
luaO_pushfstring(fs->L,"function at line %d has more than %d %s",
fs->f->linedefined,limit,what);
luaX_lexerror(fs->ls,msg,0);
}
static int testnext(LexState*ls,int c){
if(ls->t.token==c){
luaX_next(ls);
return 1;
}
else return 0;
}
static void check(LexState*ls,int c){
if(ls->t.token!=c)
error_expected(ls,c);
}
static void checknext(LexState*ls,int c){
check(ls,c);
luaX_next(ls);
}
#define check_condition(ls,c,msg){if(!(c))luaX_syntaxerror(ls,msg);}
static void check_match(LexState*ls,int what,int who,int where){
if(!testnext(ls,what)){
if(where==ls->linenumber)
error_expected(ls,what);
else{
luaX_syntaxerror(ls,luaO_pushfstring(ls->L,
LUA_QL("%s")" expected (to close "LUA_QL("%s")" at line %d)",
luaX_token2str(ls,what),luaX_token2str(ls,who),where));
}
}
}
static TString*str_checkname(LexState*ls){
TString*ts;
check(ls,TK_NAME);
ts=ls->t.seminfo.ts;
luaX_next(ls);
return ts;
}
static void init_exp(expdesc*e,expkind k,int i){
e->f=e->t=(-1);
e->k=k;
e->u.s.info=i;
}
static void codestring(LexState*ls,expdesc*e,TString*s){
init_exp(e,VK,luaK_stringK(ls->fs,s));
}
static void checkname(LexState*ls,expdesc*e){
codestring(ls,e,str_checkname(ls));
}
static int registerlocalvar(LexState*ls,TString*varname){
FuncState*fs=ls->fs;
Proto*f=fs->f;
int oldsize=f->sizelocvars;
luaM_growvector(ls->L,f->locvars,fs->nlocvars,f->sizelocvars,
LocVar,SHRT_MAX,"too many local variables");
while(oldsize<f->sizelocvars)f->locvars[oldsize++].varname=NULL;
f->locvars[fs->nlocvars].varname=varname;
luaC_objbarrier(ls->L,f,varname);
return fs->nlocvars++;
}
#define new_localvarliteral(ls,v,n)new_localvar(ls,luaX_newstring(ls,""v,(sizeof(v)/sizeof(char))-1),n)
static void new_localvar(LexState*ls,TString*name,int n){
FuncState*fs=ls->fs;
luaY_checklimit(fs,fs->nactvar+n+1,200,"local variables");
fs->actvar[fs->nactvar+n]=cast(unsigned short,registerlocalvar(ls,name));
}
static void adjustlocalvars(LexState*ls,int nvars){
FuncState*fs=ls->fs;
fs->nactvar=cast_byte(fs->nactvar+nvars);
for(;nvars;nvars--){
getlocvar(fs,fs->nactvar-nvars).startpc=fs->pc;
}
}
static void removevars(LexState*ls,int tolevel){
FuncState*fs=ls->fs;
while(fs->nactvar>tolevel)
getlocvar(fs,--fs->nactvar).endpc=fs->pc;
}
static int indexupvalue(FuncState*fs,TString*name,expdesc*v){
int i;
Proto*f=fs->f;
int oldsize=f->sizeupvalues;
for(i=0;i<f->nups;i++){
if(fs->upvalues[i].k==v->k&&fs->upvalues[i].info==v->u.s.info){
return i;
}
}
luaY_checklimit(fs,f->nups+1,60,"upvalues");
luaM_growvector(fs->L,f->upvalues,f->nups,f->sizeupvalues,
TString*,(INT_MAX-2),"");
while(oldsize<f->sizeupvalues)f->upvalues[oldsize++]=NULL;
f->upvalues[f->nups]=name;
luaC_objbarrier(fs->L,f,name);
fs->upvalues[f->nups].k=cast_byte(v->k);
fs->upvalues[f->nups].info=cast_byte(v->u.s.info);
return f->nups++;
}
static int searchvar(FuncState*fs,TString*n){
int i;
for(i=fs->nactvar-1;i>=0;i--){
if(n==getlocvar(fs,i).varname)
return i;
}
return-1;
}
static void markupval(FuncState*fs,int level){
BlockCnt*bl=fs->bl;
while(bl&&bl->nactvar>level)bl=bl->previous;
if(bl)bl->upval=1;
}
static int singlevaraux(FuncState*fs,TString*n,expdesc*var,int base){
if(fs==NULL){
init_exp(var,VGLOBAL,((1<<8)-1));
return VGLOBAL;
}
else{
int v=searchvar(fs,n);
if(v>=0){
init_exp(var,VLOCAL,v);
if(!base)
markupval(fs,v);
return VLOCAL;
}
else{
if(singlevaraux(fs->prev,n,var,0)==VGLOBAL)
return VGLOBAL;
var->u.s.info=indexupvalue(fs,n,var);
var->k=VUPVAL;
return VUPVAL;
}
}
}
static void singlevar(LexState*ls,expdesc*var){
TString*varname=str_checkname(ls);
FuncState*fs=ls->fs;
if(singlevaraux(fs,varname,var,1)==VGLOBAL)
var->u.s.info=luaK_stringK(fs,varname);
}
static void adjust_assign(LexState*ls,int nvars,int nexps,expdesc*e){
FuncState*fs=ls->fs;
int extra=nvars-nexps;
if(hasmultret(e->k)){
extra++;
if(extra<0)extra=0;
luaK_setreturns(fs,e,extra);
if(extra>1)luaK_reserveregs(fs,extra-1);
}
else{
if(e->k!=VVOID)luaK_exp2nextreg(fs,e);
if(extra>0){
int reg=fs->freereg;
luaK_reserveregs(fs,extra);
luaK_nil(fs,reg,extra);
}
}
}
static void enterlevel(LexState*ls){
if(++ls->L->nCcalls>200)
luaX_lexerror(ls,"chunk has too many syntax levels",0);
}
#define leavelevel(ls)((ls)->L->nCcalls--)
static void enterblock(FuncState*fs,BlockCnt*bl,lu_byte isbreakable){
bl->breaklist=(-1);
bl->isbreakable=isbreakable;
bl->nactvar=fs->nactvar;
bl->upval=0;
bl->previous=fs->bl;
fs->bl=bl;
}
static void leaveblock(FuncState*fs){
BlockCnt*bl=fs->bl;
fs->bl=bl->previous;
removevars(fs->ls,bl->nactvar);
if(bl->upval)
luaK_codeABC(fs,OP_CLOSE,bl->nactvar,0,0);
fs->freereg=fs->nactvar;
luaK_patchtohere(fs,bl->breaklist);
}
static void pushclosure(LexState*ls,FuncState*func,expdesc*v){
FuncState*fs=ls->fs;
Proto*f=fs->f;
int oldsize=f->sizep;
int i;
luaM_growvector(ls->L,f->p,fs->np,f->sizep,Proto*,
((1<<(9+9))-1),"constant table overflow");
while(oldsize<f->sizep)f->p[oldsize++]=NULL;
f->p[fs->np++]=func->f;
luaC_objbarrier(ls->L,f,func->f);
init_exp(v,VRELOCABLE,luaK_codeABx(fs,OP_CLOSURE,0,fs->np-1));
for(i=0;i<func->f->nups;i++){
OpCode o=(func->upvalues[i].k==VLOCAL)?OP_MOVE:OP_GETUPVAL;
luaK_codeABC(fs,o,0,func->upvalues[i].info,0);
}
}
static void open_func(LexState*ls,FuncState*fs){
lua_State*L=ls->L;
Proto*f=luaF_newproto(L);
fs->f=f;
fs->prev=ls->fs;
fs->ls=ls;
fs->L=L;
ls->fs=fs;
fs->pc=0;
fs->lasttarget=-1;
fs->jpc=(-1);
fs->freereg=0;
fs->nk=0;
fs->np=0;
fs->nlocvars=0;
fs->nactvar=0;
fs->bl=NULL;
f->source=ls->source;
f->maxstacksize=2;
fs->h=luaH_new(L,0,0);
sethvalue(L,L->top,fs->h);
incr_top(L);
setptvalue(L,L->top,f);
incr_top(L);
}
static void close_func(LexState*ls){
lua_State*L=ls->L;
FuncState*fs=ls->fs;
Proto*f=fs->f;
removevars(ls,0);
luaK_ret(fs,0,0);
luaM_reallocvector(L,f->code,f->sizecode,fs->pc,Instruction);
f->sizecode=fs->pc;
luaM_reallocvector(L,f->lineinfo,f->sizelineinfo,fs->pc,int);
f->sizelineinfo=fs->pc;
luaM_reallocvector(L,f->k,f->sizek,fs->nk,TValue);
f->sizek=fs->nk;
luaM_reallocvector(L,f->p,f->sizep,fs->np,Proto*);
f->sizep=fs->np;
luaM_reallocvector(L,f->locvars,f->sizelocvars,fs->nlocvars,LocVar);
f->sizelocvars=fs->nlocvars;
luaM_reallocvector(L,f->upvalues,f->sizeupvalues,f->nups,TString*);
f->sizeupvalues=f->nups;
ls->fs=fs->prev;
if(fs)anchor_token(ls);
L->top-=2;
}
static Proto*luaY_parser(lua_State*L,ZIO*z,Mbuffer*buff,const char*name){
struct LexState lexstate;
struct FuncState funcstate;
lexstate.buff=buff;
luaX_setinput(L,&lexstate,z,luaS_new(L,name));
open_func(&lexstate,&funcstate);
funcstate.f->is_vararg=2;
luaX_next(&lexstate);
chunk(&lexstate);
check(&lexstate,TK_EOS);
close_func(&lexstate);
return funcstate.f;
}
static void field(LexState*ls,expdesc*v){
FuncState*fs=ls->fs;
expdesc key;
luaK_exp2anyreg(fs,v);
luaX_next(ls);
checkname(ls,&key);
luaK_indexed(fs,v,&key);
}
static void yindex(LexState*ls,expdesc*v){
luaX_next(ls);
expr(ls,v);
luaK_exp2val(ls->fs,v);
checknext(ls,']');
}
struct ConsControl{
expdesc v;
expdesc*t;
int nh;
int na;
int tostore;
};
static void recfield(LexState*ls,struct ConsControl*cc){
FuncState*fs=ls->fs;
int reg=ls->fs->freereg;
expdesc key,val;
int rkkey;
if(ls->t.token==TK_NAME){
luaY_checklimit(fs,cc->nh,(INT_MAX-2),"items in a constructor");
checkname(ls,&key);
}
else
yindex(ls,&key);
cc->nh++;
checknext(ls,'=');
rkkey=luaK_exp2RK(fs,&key);
expr(ls,&val);
luaK_codeABC(fs,OP_SETTABLE,cc->t->u.s.info,rkkey,luaK_exp2RK(fs,&val));
fs->freereg=reg;
}
static void closelistfield(FuncState*fs,struct ConsControl*cc){
if(cc->v.k==VVOID)return;
luaK_exp2nextreg(fs,&cc->v);
cc->v.k=VVOID;
if(cc->tostore==50){
luaK_setlist(fs,cc->t->u.s.info,cc->na,cc->tostore);
cc->tostore=0;
}
}
static void lastlistfield(FuncState*fs,struct ConsControl*cc){
if(cc->tostore==0)return;
if(hasmultret(cc->v.k)){
luaK_setmultret(fs,&cc->v);
luaK_setlist(fs,cc->t->u.s.info,cc->na,(-1));
cc->na--;
}
else{
if(cc->v.k!=VVOID)
luaK_exp2nextreg(fs,&cc->v);
luaK_setlist(fs,cc->t->u.s.info,cc->na,cc->tostore);
}
}
static void listfield(LexState*ls,struct ConsControl*cc){
expr(ls,&cc->v);
luaY_checklimit(ls->fs,cc->na,(INT_MAX-2),"items in a constructor");
cc->na++;
cc->tostore++;
}
static void constructor(LexState*ls,expdesc*t){
FuncState*fs=ls->fs;
int line=ls->linenumber;
int pc=luaK_codeABC(fs,OP_NEWTABLE,0,0,0);
struct ConsControl cc;
cc.na=cc.nh=cc.tostore=0;
cc.t=t;
init_exp(t,VRELOCABLE,pc);
init_exp(&cc.v,VVOID,0);
luaK_exp2nextreg(ls->fs,t);
checknext(ls,'{');
do{
if(ls->t.token=='}')break;
closelistfield(fs,&cc);
switch(ls->t.token){
case TK_NAME:{
luaX_lookahead(ls);
if(ls->lookahead.token!='=')
listfield(ls,&cc);
else
recfield(ls,&cc);
break;
}
case'[':{
recfield(ls,&cc);
break;
}
default:{
listfield(ls,&cc);
break;
}
}
}while(testnext(ls,',')||testnext(ls,';'));
check_match(ls,'}','{',line);
lastlistfield(fs,&cc);
SETARG_B(fs->f->code[pc],luaO_int2fb(cc.na));
SETARG_C(fs->f->code[pc],luaO_int2fb(cc.nh));
}
static void parlist(LexState*ls){
FuncState*fs=ls->fs;
Proto*f=fs->f;
int nparams=0;
f->is_vararg=0;
if(ls->t.token!=')'){
do{
switch(ls->t.token){
case TK_NAME:{
new_localvar(ls,str_checkname(ls),nparams++);
break;
}
case TK_DOTS:{
luaX_next(ls);
f->is_vararg|=2;
break;
}
default:luaX_syntaxerror(ls,"<name> or "LUA_QL("...")" expected");
}
}while(!f->is_vararg&&testnext(ls,','));
}
adjustlocalvars(ls,nparams);
f->numparams=cast_byte(fs->nactvar-(f->is_vararg&1));
luaK_reserveregs(fs,fs->nactvar);
}
static void body(LexState*ls,expdesc*e,int needself,int line){
FuncState new_fs;
open_func(ls,&new_fs);
new_fs.f->linedefined=line;
checknext(ls,'(');
if(needself){
new_localvarliteral(ls,"self",0);
adjustlocalvars(ls,1);
}
parlist(ls);
checknext(ls,')');
chunk(ls);
new_fs.f->lastlinedefined=ls->linenumber;
check_match(ls,TK_END,TK_FUNCTION,line);
close_func(ls);
pushclosure(ls,&new_fs,e);
}
static int explist1(LexState*ls,expdesc*v){
int n=1;
expr(ls,v);
while(testnext(ls,',')){
luaK_exp2nextreg(ls->fs,v);
expr(ls,v);
n++;
}
return n;
}
static void funcargs(LexState*ls,expdesc*f){
FuncState*fs=ls->fs;
expdesc args;
int base,nparams;
int line=ls->linenumber;
switch(ls->t.token){
case'(':{
if(line!=ls->lastline)
luaX_syntaxerror(ls,"ambiguous syntax (function call x new statement)");
luaX_next(ls);
if(ls->t.token==')')
args.k=VVOID;
else{
explist1(ls,&args);
luaK_setmultret(fs,&args);
}
check_match(ls,')','(',line);
break;
}
case'{':{
constructor(ls,&args);
break;
}
case TK_STRING:{
codestring(ls,&args,ls->t.seminfo.ts);
luaX_next(ls);
break;
}
default:{
luaX_syntaxerror(ls,"function arguments expected");
return;
}
}
base=f->u.s.info;
if(hasmultret(args.k))
nparams=(-1);
else{
if(args.k!=VVOID)
luaK_exp2nextreg(fs,&args);
nparams=fs->freereg-(base+1);
}
init_exp(f,VCALL,luaK_codeABC(fs,OP_CALL,base,nparams+1,2));
luaK_fixline(fs,line);
fs->freereg=base+1;
}
static void prefixexp(LexState*ls,expdesc*v){
switch(ls->t.token){
case'(':{
int line=ls->linenumber;
luaX_next(ls);
expr(ls,v);
check_match(ls,')','(',line);
luaK_dischargevars(ls->fs,v);
return;
}
case TK_NAME:{
singlevar(ls,v);
return;
}
default:{
luaX_syntaxerror(ls,"unexpected symbol");
return;
}
}
}
static void primaryexp(LexState*ls,expdesc*v){
FuncState*fs=ls->fs;
prefixexp(ls,v);
for(;;){
switch(ls->t.token){
case'.':{
field(ls,v);
break;
}
case'[':{
expdesc key;
luaK_exp2anyreg(fs,v);
yindex(ls,&key);
luaK_indexed(fs,v,&key);
break;
}
case':':{
expdesc key;
luaX_next(ls);
checkname(ls,&key);
luaK_self(fs,v,&key);
funcargs(ls,v);
break;
}
case'(':case TK_STRING:case'{':{
luaK_exp2nextreg(fs,v);
funcargs(ls,v);
break;
}
default:return;
}
}
}
static void simpleexp(LexState*ls,expdesc*v){
switch(ls->t.token){
case TK_NUMBER:{
init_exp(v,VKNUM,0);
v->u.nval=ls->t.seminfo.r;
break;
}
case TK_STRING:{
codestring(ls,v,ls->t.seminfo.ts);
break;
}
case TK_NIL:{
init_exp(v,VNIL,0);
break;
}
case TK_TRUE:{
init_exp(v,VTRUE,0);
break;
}
case TK_FALSE:{
init_exp(v,VFALSE,0);
break;
}
case TK_DOTS:{
FuncState*fs=ls->fs;
check_condition(ls,fs->f->is_vararg,
"cannot use "LUA_QL("...")" outside a vararg function");
fs->f->is_vararg&=~4;
init_exp(v,VVARARG,luaK_codeABC(fs,OP_VARARG,0,1,0));
break;
}
case'{':{
constructor(ls,v);
return;
}
case TK_FUNCTION:{
luaX_next(ls);
body(ls,v,0,ls->linenumber);
return;
}
default:{
primaryexp(ls,v);
return;
}
}
luaX_next(ls);
}
static UnOpr getunopr(int op){
switch(op){
case TK_NOT:return OPR_NOT;
case'-':return OPR_MINUS;
case'#':return OPR_LEN;
default:return OPR_NOUNOPR;
}
}
static BinOpr getbinopr(int op){
switch(op){
case'+':return OPR_ADD;
case'-':return OPR_SUB;
case'*':return OPR_MUL;
case'/':return OPR_DIV;
case'%':return OPR_MOD;
case'^':return OPR_POW;
case TK_CONCAT:return OPR_CONCAT;
case TK_NE:return OPR_NE;
case TK_EQ:return OPR_EQ;
case'<':return OPR_LT;
case TK_LE:return OPR_LE;
case'>':return OPR_GT;
case TK_GE:return OPR_GE;
case TK_AND:return OPR_AND;
case TK_OR:return OPR_OR;
default:return OPR_NOBINOPR;
}
}
static const struct{
lu_byte left;
lu_byte right;
}priority[]={
{6,6},{6,6},{7,7},{7,7},{7,7},
{10,9},{5,4},
{3,3},{3,3},
{3,3},{3,3},{3,3},{3,3},
{2,2},{1,1}
};
static BinOpr subexpr(LexState*ls,expdesc*v,unsigned int limit){
BinOpr op;
UnOpr uop;
enterlevel(ls);
uop=getunopr(ls->t.token);
if(uop!=OPR_NOUNOPR){
luaX_next(ls);
subexpr(ls,v,8);
luaK_prefix(ls->fs,uop,v);
}
else simpleexp(ls,v);
op=getbinopr(ls->t.token);
while(op!=OPR_NOBINOPR&&priority[op].left>limit){
expdesc v2;
BinOpr nextop;
luaX_next(ls);
luaK_infix(ls->fs,op,v);
nextop=subexpr(ls,&v2,priority[op].right);
luaK_posfix(ls->fs,op,v,&v2);
op=nextop;
}
leavelevel(ls);
return op;
}
static void expr(LexState*ls,expdesc*v){
subexpr(ls,v,0);
}
static int block_follow(int token){
switch(token){
case TK_ELSE:case TK_ELSEIF:case TK_END:
case TK_UNTIL:case TK_EOS:
return 1;
default:return 0;
}
}
static void block(LexState*ls){
FuncState*fs=ls->fs;
BlockCnt bl;
enterblock(fs,&bl,0);
chunk(ls);
leaveblock(fs);
}
struct LHS_assign{
struct LHS_assign*prev;
expdesc v;
};
static void check_conflict(LexState*ls,struct LHS_assign*lh,expdesc*v){
FuncState*fs=ls->fs;
int extra=fs->freereg;
int conflict=0;
for(;lh;lh=lh->prev){
if(lh->v.k==VINDEXED){
if(lh->v.u.s.info==v->u.s.info){
conflict=1;
lh->v.u.s.info=extra;
}
if(lh->v.u.s.aux==v->u.s.info){
conflict=1;
lh->v.u.s.aux=extra;
}
}
}
if(conflict){
luaK_codeABC(fs,OP_MOVE,fs->freereg,v->u.s.info,0);
luaK_reserveregs(fs,1);
}
}
static void assignment(LexState*ls,struct LHS_assign*lh,int nvars){
expdesc e;
check_condition(ls,VLOCAL<=lh->v.k&&lh->v.k<=VINDEXED,
"syntax error");
if(testnext(ls,',')){
struct LHS_assign nv;
nv.prev=lh;
primaryexp(ls,&nv.v);
if(nv.v.k==VLOCAL)
check_conflict(ls,lh,&nv.v);
luaY_checklimit(ls->fs,nvars,200-ls->L->nCcalls,
"variables in assignment");
assignment(ls,&nv,nvars+1);
}
else{
int nexps;
checknext(ls,'=');
nexps=explist1(ls,&e);
if(nexps!=nvars){
adjust_assign(ls,nvars,nexps,&e);
if(nexps>nvars)
ls->fs->freereg-=nexps-nvars;
}
else{
luaK_setoneret(ls->fs,&e);
luaK_storevar(ls->fs,&lh->v,&e);
return;
}
}
init_exp(&e,VNONRELOC,ls->fs->freereg-1);
luaK_storevar(ls->fs,&lh->v,&e);
}
static int cond(LexState*ls){
expdesc v;
expr(ls,&v);
if(v.k==VNIL)v.k=VFALSE;
luaK_goiftrue(ls->fs,&v);
return v.f;
}
static void breakstat(LexState*ls){
FuncState*fs=ls->fs;
BlockCnt*bl=fs->bl;
int upval=0;
while(bl&&!bl->isbreakable){
upval|=bl->upval;
bl=bl->previous;
}
if(!bl)
luaX_syntaxerror(ls,"no loop to break");
if(upval)
luaK_codeABC(fs,OP_CLOSE,bl->nactvar,0,0);
luaK_concat(fs,&bl->breaklist,luaK_jump(fs));
}
static void whilestat(LexState*ls,int line){
FuncState*fs=ls->fs;
int whileinit;
int condexit;
BlockCnt bl;
luaX_next(ls);
whileinit=luaK_getlabel(fs);
condexit=cond(ls);
enterblock(fs,&bl,1);
checknext(ls,TK_DO);
block(ls);
luaK_patchlist(fs,luaK_jump(fs),whileinit);
check_match(ls,TK_END,TK_WHILE,line);
leaveblock(fs);
luaK_patchtohere(fs,condexit);
}
static void repeatstat(LexState*ls,int line){
int condexit;
FuncState*fs=ls->fs;
int repeat_init=luaK_getlabel(fs);
BlockCnt bl1,bl2;
enterblock(fs,&bl1,1);
enterblock(fs,&bl2,0);
luaX_next(ls);
chunk(ls);
check_match(ls,TK_UNTIL,TK_REPEAT,line);
condexit=cond(ls);
if(!bl2.upval){
leaveblock(fs);
luaK_patchlist(ls->fs,condexit,repeat_init);
}
else{
breakstat(ls);
luaK_patchtohere(ls->fs,condexit);
leaveblock(fs);
luaK_patchlist(ls->fs,luaK_jump(fs),repeat_init);
}
leaveblock(fs);
}
static int exp1(LexState*ls){
expdesc e;
int k;
expr(ls,&e);
k=e.k;
luaK_exp2nextreg(ls->fs,&e);
return k;
}
static void forbody(LexState*ls,int base,int line,int nvars,int isnum){
BlockCnt bl;
FuncState*fs=ls->fs;
int prep,endfor;
adjustlocalvars(ls,3);
checknext(ls,TK_DO);
prep=isnum?luaK_codeAsBx(fs,OP_FORPREP,base,(-1)):luaK_jump(fs);
enterblock(fs,&bl,0);
adjustlocalvars(ls,nvars);
luaK_reserveregs(fs,nvars);
block(ls);
leaveblock(fs);
luaK_patchtohere(fs,prep);
endfor=(isnum)?luaK_codeAsBx(fs,OP_FORLOOP,base,(-1)):
luaK_codeABC(fs,OP_TFORLOOP,base,0,nvars);
luaK_fixline(fs,line);
luaK_patchlist(fs,(isnum?endfor:luaK_jump(fs)),prep+1);
}
static void fornum(LexState*ls,TString*varname,int line){
FuncState*fs=ls->fs;
int base=fs->freereg;
new_localvarliteral(ls,"(for index)",0);
new_localvarliteral(ls,"(for limit)",1);
new_localvarliteral(ls,"(for step)",2);
new_localvar(ls,varname,3);
checknext(ls,'=');
exp1(ls);
checknext(ls,',');
exp1(ls);
if(testnext(ls,','))
exp1(ls);
else{
luaK_codeABx(fs,OP_LOADK,fs->freereg,luaK_numberK(fs,1));
luaK_reserveregs(fs,1);
}
forbody(ls,base,line,1,1);
}
static void forlist(LexState*ls,TString*indexname){
FuncState*fs=ls->fs;
expdesc e;
int nvars=0;
int line;
int base=fs->freereg;
new_localvarliteral(ls,"(for generator)",nvars++);
new_localvarliteral(ls,"(for state)",nvars++);
new_localvarliteral(ls,"(for control)",nvars++);
new_localvar(ls,indexname,nvars++);
while(testnext(ls,','))
new_localvar(ls,str_checkname(ls),nvars++);
checknext(ls,TK_IN);
line=ls->linenumber;
adjust_assign(ls,3,explist1(ls,&e),&e);
luaK_checkstack(fs,3);
forbody(ls,base,line,nvars-3,0);
}
static void forstat(LexState*ls,int line){
FuncState*fs=ls->fs;
TString*varname;
BlockCnt bl;
enterblock(fs,&bl,1);
luaX_next(ls);
varname=str_checkname(ls);
switch(ls->t.token){
case'=':fornum(ls,varname,line);break;
case',':case TK_IN:forlist(ls,varname);break;
default:luaX_syntaxerror(ls,LUA_QL("=")" or "LUA_QL("in")" expected");
}
check_match(ls,TK_END,TK_FOR,line);
leaveblock(fs);
}
static int test_then_block(LexState*ls){
int condexit;
luaX_next(ls);
condexit=cond(ls);
checknext(ls,TK_THEN);
block(ls);
return condexit;
}
static void ifstat(LexState*ls,int line){
FuncState*fs=ls->fs;
int flist;
int escapelist=(-1);
flist=test_then_block(ls);
while(ls->t.token==TK_ELSEIF){
luaK_concat(fs,&escapelist,luaK_jump(fs));
luaK_patchtohere(fs,flist);
flist=test_then_block(ls);
}
if(ls->t.token==TK_ELSE){
luaK_concat(fs,&escapelist,luaK_jump(fs));
luaK_patchtohere(fs,flist);
luaX_next(ls);
block(ls);
}
else
luaK_concat(fs,&escapelist,flist);
luaK_patchtohere(fs,escapelist);
check_match(ls,TK_END,TK_IF,line);
}
static void localfunc(LexState*ls){
expdesc v,b;
FuncState*fs=ls->fs;
new_localvar(ls,str_checkname(ls),0);
init_exp(&v,VLOCAL,fs->freereg);
luaK_reserveregs(fs,1);
adjustlocalvars(ls,1);
body(ls,&b,0,ls->linenumber);
luaK_storevar(fs,&v,&b);
getlocvar(fs,fs->nactvar-1).startpc=fs->pc;
}
static void localstat(LexState*ls){
int nvars=0;
int nexps;
expdesc e;
do{
new_localvar(ls,str_checkname(ls),nvars++);
}while(testnext(ls,','));
if(testnext(ls,'='))
nexps=explist1(ls,&e);
else{
e.k=VVOID;
nexps=0;
}
adjust_assign(ls,nvars,nexps,&e);
adjustlocalvars(ls,nvars);
}
static int funcname(LexState*ls,expdesc*v){
int needself=0;
singlevar(ls,v);
while(ls->t.token=='.')
field(ls,v);
if(ls->t.token==':'){
needself=1;
field(ls,v);
}
return needself;
}
static void funcstat(LexState*ls,int line){
int needself;
expdesc v,b;
luaX_next(ls);
needself=funcname(ls,&v);
body(ls,&b,needself,line);
luaK_storevar(ls->fs,&v,&b);
luaK_fixline(ls->fs,line);
}
static void exprstat(LexState*ls){
FuncState*fs=ls->fs;
struct LHS_assign v;
primaryexp(ls,&v.v);
if(v.v.k==VCALL)
SETARG_C(getcode(fs,&v.v),1);
else{
v.prev=NULL;
assignment(ls,&v,1);
}
}
static void retstat(LexState*ls){
FuncState*fs=ls->fs;
expdesc e;
int first,nret;
luaX_next(ls);
if(block_follow(ls->t.token)||ls->t.token==';')
first=nret=0;
else{
nret=explist1(ls,&e);
if(hasmultret(e.k)){
luaK_setmultret(fs,&e);
if(e.k==VCALL&&nret==1){
SET_OPCODE(getcode(fs,&e),OP_TAILCALL);
}
first=fs->nactvar;
nret=(-1);
}
else{
if(nret==1)
first=luaK_exp2anyreg(fs,&e);
else{
luaK_exp2nextreg(fs,&e);
first=fs->nactvar;
}
}
}
luaK_ret(fs,first,nret);
}
static int statement(LexState*ls){
int line=ls->linenumber;
switch(ls->t.token){
case TK_IF:{
ifstat(ls,line);
return 0;
}
case TK_WHILE:{
whilestat(ls,line);
return 0;
}
case TK_DO:{
luaX_next(ls);
block(ls);
check_match(ls,TK_END,TK_DO,line);
return 0;
}
case TK_FOR:{
forstat(ls,line);
return 0;
}
case TK_REPEAT:{
repeatstat(ls,line);
return 0;
}
case TK_FUNCTION:{
funcstat(ls,line);
return 0;
}
case TK_LOCAL:{
luaX_next(ls);
if(testnext(ls,TK_FUNCTION))
localfunc(ls);
else
localstat(ls);
return 0;
}
case TK_RETURN:{
retstat(ls);
return 1;
}
case TK_BREAK:{
luaX_next(ls);
breakstat(ls);
return 1;
}
default:{
exprstat(ls);
return 0;
}
}
}
static void chunk(LexState*ls){
int islast=0;
enterlevel(ls);
while(!islast&&!block_follow(ls->t.token)){
islast=statement(ls);
testnext(ls,';');
ls->fs->freereg=ls->fs->nactvar;
}
leavelevel(ls);
}
static const TValue*luaV_tonumber(const TValue*obj,TValue*n){
lua_Number num;
if(ttisnumber(obj))return obj;
if(ttisstring(obj)&&luaO_str2d(svalue(obj),&num)){
setnvalue(n,num);
return n;
}
else
return NULL;
}
static int luaV_tostring(lua_State*L,StkId obj){
if(!ttisnumber(obj))
return 0;
else{
char s[32];
lua_Number n=nvalue(obj);
lua_number2str(s,n);
setsvalue(L,obj,luaS_new(L,s));
return 1;
}
}
static void callTMres(lua_State*L,StkId res,const TValue*f,
const TValue*p1,const TValue*p2){
ptrdiff_t result=savestack(L,res);
setobj(L,L->top,f);
setobj(L,L->top+1,p1);
setobj(L,L->top+2,p2);
luaD_checkstack(L,3);
L->top+=3;
luaD_call(L,L->top-3,1);
res=restorestack(L,result);
L->top--;
setobj(L,res,L->top);
}
static void callTM(lua_State*L,const TValue*f,const TValue*p1,
const TValue*p2,const TValue*p3){
setobj(L,L->top,f);
setobj(L,L->top+1,p1);
setobj(L,L->top+2,p2);
setobj(L,L->top+3,p3);
luaD_checkstack(L,4);
L->top+=4;
luaD_call(L,L->top-4,0);
}
static void luaV_gettable(lua_State*L,const TValue*t,TValue*key,StkId val){
int loop;
for(loop=0;loop<100;loop++){
const TValue*tm;
if(ttistable(t)){
Table*h=hvalue(t);
const TValue*res=luaH_get(h,key);
if(!ttisnil(res)||
(tm=fasttm(L,h->metatable,TM_INDEX))==NULL){
setobj(L,val,res);
return;
}
}
else if(ttisnil(tm=luaT_gettmbyobj(L,t,TM_INDEX)))
luaG_typeerror(L,t,"index");
if(ttisfunction(tm)){
callTMres(L,val,tm,t,key);
return;
}
t=tm;
}
luaG_runerror(L,"loop in gettable");
}
static void luaV_settable(lua_State*L,const TValue*t,TValue*key,StkId val){
int loop;
TValue temp;
for(loop=0;loop<100;loop++){
const TValue*tm;
if(ttistable(t)){
Table*h=hvalue(t);
TValue*oldval=luaH_set(L,h,key);
if(!ttisnil(oldval)||
(tm=fasttm(L,h->metatable,TM_NEWINDEX))==NULL){
setobj(L,oldval,val);
h->flags=0;
luaC_barriert(L,h,val);
return;
}
}
else if(ttisnil(tm=luaT_gettmbyobj(L,t,TM_NEWINDEX)))
luaG_typeerror(L,t,"index");
if(ttisfunction(tm)){
callTM(L,tm,t,key,val);
return;
}
setobj(L,&temp,tm);
t=&temp;
}
luaG_runerror(L,"loop in settable");
}
static int call_binTM(lua_State*L,const TValue*p1,const TValue*p2,
StkId res,TMS event){
const TValue*tm=luaT_gettmbyobj(L,p1,event);
if(ttisnil(tm))
tm=luaT_gettmbyobj(L,p2,event);
if(ttisnil(tm))return 0;
callTMres(L,res,tm,p1,p2);
return 1;
}
static const TValue*get_compTM(lua_State*L,Table*mt1,Table*mt2,
TMS event){
const TValue*tm1=fasttm(L,mt1,event);
const TValue*tm2;
if(tm1==NULL)return NULL;
if(mt1==mt2)return tm1;
tm2=fasttm(L,mt2,event);
if(tm2==NULL)return NULL;
if(luaO_rawequalObj(tm1,tm2))
return tm1;
return NULL;
}
static int call_orderTM(lua_State*L,const TValue*p1,const TValue*p2,
TMS event){
const TValue*tm1=luaT_gettmbyobj(L,p1,event);
const TValue*tm2;
if(ttisnil(tm1))return-1;
tm2=luaT_gettmbyobj(L,p2,event);
if(!luaO_rawequalObj(tm1,tm2))
return-1;
callTMres(L,L->top,tm1,p1,p2);
return!l_isfalse(L->top);
}
static int l_strcmp(const TString*ls,const TString*rs){
const char*l=getstr(ls);
size_t ll=ls->tsv.len;
const char*r=getstr(rs);
size_t lr=rs->tsv.len;
for(;;){
int temp=strcoll(l,r);
if(temp!=0)return temp;
else{
size_t len=strlen(l);
if(len==lr)
return(len==ll)?0:1;
else if(len==ll)
return-1;
len++;
l+=len;ll-=len;r+=len;lr-=len;
}
}
}
static int luaV_lessthan(lua_State*L,const TValue*l,const TValue*r){
int res;
if(ttype(l)!=ttype(r))
return luaG_ordererror(L,l,r);
else if(ttisnumber(l))
return luai_numlt(nvalue(l),nvalue(r));
else if(ttisstring(l))
return l_strcmp(rawtsvalue(l),rawtsvalue(r))<0;
else if((res=call_orderTM(L,l,r,TM_LT))!=-1)
return res;
return luaG_ordererror(L,l,r);
}
static int lessequal(lua_State*L,const TValue*l,const TValue*r){
int res;
if(ttype(l)!=ttype(r))
return luaG_ordererror(L,l,r);
else if(ttisnumber(l))
return luai_numle(nvalue(l),nvalue(r));
else if(ttisstring(l))
return l_strcmp(rawtsvalue(l),rawtsvalue(r))<=0;
else if((res=call_orderTM(L,l,r,TM_LE))!=-1)
return res;
else if((res=call_orderTM(L,r,l,TM_LT))!=-1)
return!res;
return luaG_ordererror(L,l,r);
}
static int luaV_equalval(lua_State*L,const TValue*t1,const TValue*t2){
const TValue*tm;
switch(ttype(t1)){
case 0:return 1;
case 3:return luai_numeq(nvalue(t1),nvalue(t2));
case 1:return bvalue(t1)==bvalue(t2);
case 2:return pvalue(t1)==pvalue(t2);
case 7:{
if(uvalue(t1)==uvalue(t2))return 1;
tm=get_compTM(L,uvalue(t1)->metatable,uvalue(t2)->metatable,
TM_EQ);
break;
}
case 5:{
if(hvalue(t1)==hvalue(t2))return 1;
tm=get_compTM(L,hvalue(t1)->metatable,hvalue(t2)->metatable,TM_EQ);
break;
}
default:return gcvalue(t1)==gcvalue(t2);
}
if(tm==NULL)return 0;
callTMres(L,L->top,tm,t1,t2);
return!l_isfalse(L->top);
}
static void luaV_concat(lua_State*L,int total,int last){
do{
StkId top=L->base+last+1;
int n=2;
if(!(ttisstring(top-2)||ttisnumber(top-2))||!tostring(L,top-1)){
if(!call_binTM(L,top-2,top-1,top-2,TM_CONCAT))
luaG_concaterror(L,top-2,top-1);
}else if(tsvalue(top-1)->len==0)
(void)tostring(L,top-2);
else{
size_t tl=tsvalue(top-1)->len;
char*buffer;
int i;
for(n=1;n<total&&tostring(L,top-n-1);n++){
size_t l=tsvalue(top-n-1)->len;
if(l>=((size_t)(~(size_t)0)-2)-tl)luaG_runerror(L,"string length overflow");
tl+=l;
}
buffer=luaZ_openspace(L,&G(L)->buff,tl);
tl=0;
for(i=n;i>0;i--){
size_t l=tsvalue(top-i)->len;
memcpy(buffer+tl,svalue(top-i),l);
tl+=l;
}
setsvalue(L,top-n,luaS_newlstr(L,buffer,tl));
}
total-=n-1;
last-=n-1;
}while(total>1);
}
static void Arith(lua_State*L,StkId ra,const TValue*rb,
const TValue*rc,TMS op){
TValue tempb,tempc;
const TValue*b,*c;
if((b=luaV_tonumber(rb,&tempb))!=NULL&&
(c=luaV_tonumber(rc,&tempc))!=NULL){
lua_Number nb=nvalue(b),nc=nvalue(c);
switch(op){
case TM_ADD:setnvalue(ra,luai_numadd(nb,nc));break;
case TM_SUB:setnvalue(ra,luai_numsub(nb,nc));break;
case TM_MUL:setnvalue(ra,luai_nummul(nb,nc));break;
case TM_DIV:setnvalue(ra,luai_numdiv(nb,nc));break;
case TM_MOD:setnvalue(ra,luai_nummod(nb,nc));break;
case TM_POW:setnvalue(ra,luai_numpow(nb,nc));break;
case TM_UNM:setnvalue(ra,luai_numunm(nb));break;
default:break;
}
}
else if(!call_binTM(L,rb,rc,ra,op))
luaG_aritherror(L,rb,rc);
}
#define runtime_check(L,c){if(!(c))break;}
#define RA(i)(base+GETARG_A(i))
#define RB(i)check_exp(getBMode(GET_OPCODE(i))==OpArgR,base+GETARG_B(i))
#define RKB(i)check_exp(getBMode(GET_OPCODE(i))==OpArgK,ISK(GETARG_B(i))?k+INDEXK(GETARG_B(i)):base+GETARG_B(i))
#define RKC(i)check_exp(getCMode(GET_OPCODE(i))==OpArgK,ISK(GETARG_C(i))?k+INDEXK(GETARG_C(i)):base+GETARG_C(i))
#define KBx(i)check_exp(getBMode(GET_OPCODE(i))==OpArgK,k+GETARG_Bx(i))
#define dojump(L,pc,i){(pc)+=(i);}
#define Protect(x){L->savedpc=pc;{x;};base=L->base;}
#define arith_op(op,tm){TValue*rb=RKB(i);TValue*rc=RKC(i);if(ttisnumber(rb)&&ttisnumber(rc)){lua_Number nb=nvalue(rb),nc=nvalue(rc);setnvalue(ra,op(nb,nc));}else Protect(Arith(L,ra,rb,rc,tm));}
static void luaV_execute(lua_State*L,int nexeccalls){
LClosure*cl;
StkId base;
TValue*k;
const Instruction*pc;
reentry:
pc=L->savedpc;
cl=&clvalue(L->ci->func)->l;
base=L->base;
k=cl->p->k;
for(;;){
const Instruction i=*pc++;
StkId ra;
ra=RA(i);
switch(GET_OPCODE(i)){
case OP_MOVE:{
setobj(L,ra,RB(i));
continue;
}
case OP_LOADK:{
setobj(L,ra,KBx(i));
continue;
}
case OP_LOADBOOL:{
setbvalue(ra,GETARG_B(i));
if(GETARG_C(i))pc++;
continue;
}
case OP_LOADNIL:{
TValue*rb=RB(i);
do{
setnilvalue(rb--);
}while(rb>=ra);
continue;
}
case OP_GETUPVAL:{
int b=GETARG_B(i);
setobj(L,ra,cl->upvals[b]->v);
continue;
}
case OP_GETGLOBAL:{
TValue g;
TValue*rb=KBx(i);
sethvalue(L,&g,cl->env);
Protect(luaV_gettable(L,&g,rb,ra));
continue;
}
case OP_GETTABLE:{
Protect(luaV_gettable(L,RB(i),RKC(i),ra));
continue;
}
case OP_SETGLOBAL:{
TValue g;
sethvalue(L,&g,cl->env);
Protect(luaV_settable(L,&g,KBx(i),ra));
continue;
}
case OP_SETUPVAL:{
UpVal*uv=cl->upvals[GETARG_B(i)];
setobj(L,uv->v,ra);
luaC_barrier(L,uv,ra);
continue;
}
case OP_SETTABLE:{
Protect(luaV_settable(L,ra,RKB(i),RKC(i)));
continue;
}
case OP_NEWTABLE:{
int b=GETARG_B(i);
int c=GETARG_C(i);
sethvalue(L,ra,luaH_new(L,luaO_fb2int(b),luaO_fb2int(c)));
Protect(luaC_checkGC(L));
continue;
}
case OP_SELF:{
StkId rb=RB(i);
setobj(L,ra+1,rb);
Protect(luaV_gettable(L,rb,RKC(i),ra));
continue;
}
case OP_ADD:{
arith_op(luai_numadd,TM_ADD);
continue;
}
case OP_SUB:{
arith_op(luai_numsub,TM_SUB);
continue;
}
case OP_MUL:{
arith_op(luai_nummul,TM_MUL);
continue;
}
case OP_DIV:{
arith_op(luai_numdiv,TM_DIV);
continue;
}
case OP_MOD:{
arith_op(luai_nummod,TM_MOD);
continue;
}
case OP_POW:{
arith_op(luai_numpow,TM_POW);
continue;
}
case OP_UNM:{
TValue*rb=RB(i);
if(ttisnumber(rb)){
lua_Number nb=nvalue(rb);
setnvalue(ra,luai_numunm(nb));
}
else{
Protect(Arith(L,ra,rb,rb,TM_UNM));
}
continue;
}
case OP_NOT:{
int res=l_isfalse(RB(i));
setbvalue(ra,res);
continue;
}
case OP_LEN:{
const TValue*rb=RB(i);
switch(ttype(rb)){
case 5:{
setnvalue(ra,cast_num(luaH_getn(hvalue(rb))));
break;
}
case 4:{
setnvalue(ra,cast_num(tsvalue(rb)->len));
break;
}
default:{
Protect(
if(!call_binTM(L,rb,(&luaO_nilobject_),ra,TM_LEN))
luaG_typeerror(L,rb,"get length of");
)
}
}
continue;
}
case OP_CONCAT:{
int b=GETARG_B(i);
int c=GETARG_C(i);
Protect(luaV_concat(L,c-b+1,c);luaC_checkGC(L));
setobj(L,RA(i),base+b);
continue;
}
case OP_JMP:{
dojump(L,pc,GETARG_sBx(i));
continue;
}
case OP_EQ:{
TValue*rb=RKB(i);
TValue*rc=RKC(i);
Protect(
if(equalobj(L,rb,rc)==GETARG_A(i))
dojump(L,pc,GETARG_sBx(*pc));
)
pc++;
continue;
}
case OP_LT:{
Protect(
if(luaV_lessthan(L,RKB(i),RKC(i))==GETARG_A(i))
dojump(L,pc,GETARG_sBx(*pc));
)
pc++;
continue;
}
case OP_LE:{
Protect(
if(lessequal(L,RKB(i),RKC(i))==GETARG_A(i))
dojump(L,pc,GETARG_sBx(*pc));
)
pc++;
continue;
}