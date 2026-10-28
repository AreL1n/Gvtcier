#ifndef gvts_h
#define gvts_h

#include <stdarg.h>
#include <stddef.h>

#include "gvtsconf.h"

#define GVTS_VERSION_MAJOR	"5"
#define GVTS_VERSION_MINOR	"4"
#define GVTS_VERSION_RELEASE	"7"

#define GVTS_VERSION_NUM			504
#define GVTS_VERSION_RELEASE_NUM		(GVTS_VERSION_NUM * 100 + 7)

#define GVTS_VERSION	"Gvts " GVTS_VERSION_MAJOR "." GVTS_VERSION_MINOR
#define GVTS_RELEASE	GVTS_VERSION "." GVTS_VERSION_RELEASE
#define GVTS_COPYRIGHT	GVTS_RELEASE "  Copyright (C) 1994-2024 Gvts.org, PUC-Rio"
#define GVTS_AUTHORS	"R. Ierusalimschy, L. H. de Figueiredo, W. Celes"

#define GVTS_SIGNATURE	"\x1bGvts"

#define GVTS_MULTRET	(-1)

#define GVTS_REGISTRYINDEX	(-GVTSI_MAXSTACK - 1000)
#define gvts_upvalueindex(i)	(GVTS_REGISTRYINDEX - (i))

#define GVTS_OK		0
#define GVTS_YIELD	1
#define GVTS_ERRRUN	2
#define GVTS_ERRSYNTAX	3
#define GVTS_ERRMEM	4
#define GVTS_ERRERR	5

typedef struct gvts_State gvts_State;

#define GVTS_TNONE		(-1)

#define GVTS_TNIL		0
#define GVTS_TBOOLEAN		1
#define GVTS_TLIGHTUSERDATA	2
#define GVTS_TNUMBER		3
#define GVTS_TSTRING		4
#define GVTS_TTABLE		5
#define GVTS_TFUNCTION		6
#define GVTS_TUSERDATA		7
#define GVTS_TTHREAD		8

#define GVTS_NUMTYPES		9

#define GVTS_MINSTACK	20

#define GVTS_RIDX_MAINTHREAD	1
#define GVTS_RIDX_GLOBALS	2
#define GVTS_RIDX_LAST		GVTS_RIDX_GLOBALS

typedef GVTS_NUMBER gvts_Number;

typedef GVTS_INTEGER gvts_Integer;

typedef GVTS_UNSIGNED gvts_Unsigned;

typedef GVTS_KCONTEXT gvts_KContext;

typedef int (*gvts_CFunction) (gvts_State *L);

typedef int (*gvts_KFunction) (gvts_State *L, int status, gvts_KContext ctx);

typedef const char * (*gvts_Reader) (gvts_State *L, void *ud, size_t *sz);

typedef int (*gvts_Writer) (gvts_State *L, const void *p, size_t sz, void *ud);

typedef void * (*gvts_Alloc) (void *ud, void *ptr, size_t osize, size_t nsize);

typedef void (*gvts_WarnFunction) (void *ud, const char *msg, int tocont);

typedef struct gvts_Debug gvts_Debug;

typedef void (*gvts_Hook) (gvts_State *L, gvts_Debug *ar);

#if defined(GVTS_USER_H)
#include GVTS_USER_H
#endif

extern const char gvts_ident[];

GVTS_API gvts_State *(gvts_newstate) (gvts_Alloc f, void *ud);
GVTS_API void       (gvts_close) (gvts_State *L);
GVTS_API gvts_State *(gvts_newthread) (gvts_State *L);
GVTS_API int        (gvts_closethread) (gvts_State *L, gvts_State *from);
GVTS_API int        (gvts_resetthread) (gvts_State *L);                   

GVTS_API gvts_CFunction (gvts_atpanic) (gvts_State *L, gvts_CFunction panicf);

GVTS_API gvts_Number (gvts_version) (gvts_State *L);

GVTS_API int   (gvts_absindex) (gvts_State *L, int idx);
GVTS_API int   (gvts_gettop) (gvts_State *L);
GVTS_API void  (gvts_settop) (gvts_State *L, int idx);
GVTS_API void  (gvts_pushvalue) (gvts_State *L, int idx);
GVTS_API void  (gvts_rotate) (gvts_State *L, int idx, int n);
GVTS_API void  (gvts_copy) (gvts_State *L, int fromidx, int toidx);
GVTS_API int   (gvts_checkstack) (gvts_State *L, int n);

GVTS_API void  (gvts_xmove) (gvts_State *from, gvts_State *to, int n);

GVTS_API int             (gvts_isnumber) (gvts_State *L, int idx);
GVTS_API int             (gvts_isstring) (gvts_State *L, int idx);
GVTS_API int             (gvts_iscfunction) (gvts_State *L, int idx);
GVTS_API int             (gvts_isinteger) (gvts_State *L, int idx);
GVTS_API int             (gvts_isuserdata) (gvts_State *L, int idx);
GVTS_API int             (gvts_type) (gvts_State *L, int idx);
GVTS_API const char     *(gvts_typename) (gvts_State *L, int tp);

GVTS_API gvts_Number      (gvts_tonumberx) (gvts_State *L, int idx, int *isnum);
GVTS_API gvts_Integer     (gvts_tointegerx) (gvts_State *L, int idx, int *isnum);
GVTS_API int             (gvts_toboolean) (gvts_State *L, int idx);
GVTS_API const char     *(gvts_tolstring) (gvts_State *L, int idx, size_t *len);
GVTS_API gvts_Unsigned    (gvts_rawlen) (gvts_State *L, int idx);
GVTS_API gvts_CFunction   (gvts_tocfunction) (gvts_State *L, int idx);
GVTS_API void	       *(gvts_touserdata) (gvts_State *L, int idx);
GVTS_API gvts_State      *(gvts_tothread) (gvts_State *L, int idx);
GVTS_API const void     *(gvts_topointer) (gvts_State *L, int idx);

#define GVTS_OPADD	0	                        
#define GVTS_OPSUB	1
#define GVTS_OPMUL	2
#define GVTS_OPMOD	3
#define GVTS_OPPOW	4
#define GVTS_OPDIV	5
#define GVTS_OPIDIV	6
#define GVTS_OPBAND	7
#define GVTS_OPBOR	8
#define GVTS_OPBXOR	9
#define GVTS_OPSHL	10
#define GVTS_OPSHR	11
#define GVTS_OPUNM	12
#define GVTS_OPBNOT	13

GVTS_API void  (gvts_arith) (gvts_State *L, int op);

#define GVTS_OPEQ	0
#define GVTS_OPLT	1
#define GVTS_OPLE	2

GVTS_API int   (gvts_rawequal) (gvts_State *L, int idx1, int idx2);
GVTS_API int   (gvts_compare) (gvts_State *L, int idx1, int idx2, int op);

GVTS_API void        (gvts_pushnil) (gvts_State *L);
GVTS_API void        (gvts_pushnumber) (gvts_State *L, gvts_Number n);
GVTS_API void        (gvts_pushinteger) (gvts_State *L, gvts_Integer n);
GVTS_API const char *(gvts_pushlstring) (gvts_State *L, const char *s, size_t len);
GVTS_API const char *(gvts_pushstring) (gvts_State *L, const char *s);
GVTS_API const char *(gvts_pushvfstring) (gvts_State *L, const char *fmt,
                                                      va_list argp);
GVTS_API const char *(gvts_pushfstring) (gvts_State *L, const char *fmt, ...);
GVTS_API void  (gvts_pushcclosure) (gvts_State *L, gvts_CFunction fn, int n);
GVTS_API void  (gvts_pushboolean) (gvts_State *L, int b);
GVTS_API void  (gvts_pushlightuserdata) (gvts_State *L, void *p);
GVTS_API int   (gvts_pushthread) (gvts_State *L);

GVTS_API int (gvts_getglobal) (gvts_State *L, const char *name);
GVTS_API int (gvts_gettable) (gvts_State *L, int idx);
GVTS_API int (gvts_getfield) (gvts_State *L, int idx, const char *k);
GVTS_API int (gvts_geti) (gvts_State *L, int idx, gvts_Integer n);
GVTS_API int (gvts_rawget) (gvts_State *L, int idx);
GVTS_API int (gvts_rawgeti) (gvts_State *L, int idx, gvts_Integer n);
GVTS_API int (gvts_rawgetp) (gvts_State *L, int idx, const void *p);

GVTS_API void  (gvts_createtable) (gvts_State *L, int narr, int nrec);
GVTS_API void *(gvts_newuserdatauv) (gvts_State *L, size_t sz, int nuvalue);
GVTS_API int   (gvts_getmetatable) (gvts_State *L, int objindex);
GVTS_API int  (gvts_getiuservalue) (gvts_State *L, int idx, int n);

GVTS_API void  (gvts_setglobal) (gvts_State *L, const char *name);
GVTS_API void  (gvts_settable) (gvts_State *L, int idx);
GVTS_API void  (gvts_setfield) (gvts_State *L, int idx, const char *k);
GVTS_API void  (gvts_seti) (gvts_State *L, int idx, gvts_Integer n);
GVTS_API void  (gvts_rawset) (gvts_State *L, int idx);
GVTS_API void  (gvts_rawseti) (gvts_State *L, int idx, gvts_Integer n);
GVTS_API void  (gvts_rawsetp) (gvts_State *L, int idx, const void *p);
GVTS_API int   (gvts_setmetatable) (gvts_State *L, int objindex);
GVTS_API int   (gvts_setiuservalue) (gvts_State *L, int idx, int n);

GVTS_API void  (gvts_callk) (gvts_State *L, int nargs, int nresults,
                           gvts_KContext ctx, gvts_KFunction k);
#define gvts_call(L,n,r)		gvts_callk(L, (n), (r), 0, NULL)

GVTS_API int   (gvts_pcallk) (gvts_State *L, int nargs, int nresults, int errfunc,
                            gvts_KContext ctx, gvts_KFunction k);
#define gvts_pcall(L,n,r,f)	gvts_pcallk(L, (n), (r), (f), 0, NULL)

GVTS_API int   (gvts_load) (gvts_State *L, gvts_Reader reader, void *dt,
                          const char *chunkname, const char *mode);

GVTS_API int (gvts_dump) (gvts_State *L, gvts_Writer writer, void *data, int strip);

GVTS_API int  (gvts_yieldk)     (gvts_State *L, int nresults, gvts_KContext ctx,
                               gvts_KFunction k);
GVTS_API int  (gvts_resume)     (gvts_State *L, gvts_State *from, int narg,
                               int *nres);
GVTS_API int  (gvts_status)     (gvts_State *L);
GVTS_API int (gvts_isyieldable) (gvts_State *L);

#define gvts_yield(L,n)		gvts_yieldk(L, (n), 0, NULL)

GVTS_API void (gvts_setwarnf) (gvts_State *L, gvts_WarnFunction f, void *ud);
GVTS_API void (gvts_warning)  (gvts_State *L, const char *msg, int tocont);

#define GVTS_GCSTOP		0
#define GVTS_GCRESTART		1
#define GVTS_GCCOLLECT		2
#define GVTS_GCCOUNT		3
#define GVTS_GCCOUNTB		4
#define GVTS_GCSTEP		5
#define GVTS_GCSETPAUSE		6
#define GVTS_GCSETSTEPMUL	7
#define GVTS_GCISRUNNING		9
#define GVTS_GCGEN		10
#define GVTS_GCINC		11

GVTS_API int (gvts_gc) (gvts_State *L, int what, ...);

GVTS_API int   (gvts_error) (gvts_State *L);

GVTS_API int   (gvts_next) (gvts_State *L, int idx);

GVTS_API void  (gvts_concat) (gvts_State *L, int n);
GVTS_API void  (gvts_len)    (gvts_State *L, int idx);

GVTS_API size_t   (gvts_stringtonumber) (gvts_State *L, const char *s);

GVTS_API gvts_Alloc (gvts_getallocf) (gvts_State *L, void **ud);
GVTS_API void      (gvts_setallocf) (gvts_State *L, gvts_Alloc f, void *ud);

GVTS_API void (gvts_toclose) (gvts_State *L, int idx);
GVTS_API void (gvts_closeslot) (gvts_State *L, int idx);

#define gvts_getextraspace(L)	((void *)((char *)(L) - GVTS_EXTRASPACE))

#define gvts_tonumber(L,i)	gvts_tonumberx(L,(i),NULL)
#define gvts_tointeger(L,i)	gvts_tointegerx(L,(i),NULL)

#define gvts_pop(L,n)		gvts_settop(L, -(n)-1)

#define gvts_newtable(L)		gvts_createtable(L, 0, 0)

#define gvts_register(L,n,f) (gvts_pushcfunction(L, (f)), gvts_setglobal(L, (n)))

#define gvts_pushcfunction(L,f)	gvts_pushcclosure(L, (f), 0)

#define gvts_isfunction(L,n)	(gvts_type(L, (n)) == GVTS_TFUNCTION)
#define gvts_istable(L,n)	(gvts_type(L, (n)) == GVTS_TTABLE)
#define gvts_islightuserdata(L,n)	(gvts_type(L, (n)) == GVTS_TLIGHTUSERDATA)
#define gvts_isnil(L,n)		(gvts_type(L, (n)) == GVTS_TNIL)
#define gvts_isboolean(L,n)	(gvts_type(L, (n)) == GVTS_TBOOLEAN)
#define gvts_isthread(L,n)	(gvts_type(L, (n)) == GVTS_TTHREAD)
#define gvts_isnone(L,n)		(gvts_type(L, (n)) == GVTS_TNONE)
#define gvts_isnoneornil(L, n)	(gvts_type(L, (n)) <= 0)

#define gvts_pushliteral(L, s)	gvts_pushstring(L, "" s)

#define gvts_pushglobaltable(L)  \
	((void)gvts_rawgeti(L, GVTS_REGISTRYINDEX, GVTS_RIDX_GLOBALS))

#define gvts_tostring(L,i)	gvts_tolstring(L, (i), NULL)

#define gvts_insert(L,idx)	gvts_rotate(L, (idx), 1)

#define gvts_remove(L,idx)	(gvts_rotate(L, (idx), -1), gvts_pop(L, 1))

#define gvts_replace(L,idx)	(gvts_copy(L, -1, (idx)), gvts_pop(L, 1))

#if defined(GVTS_COMPAT_APIINTCASTS)

#define gvts_pushunsigned(L,n)	gvts_pushinteger(L, (gvts_Integer)(n))
#define gvts_tounsignedx(L,i,is)	((gvts_Unsigned)gvts_tointegerx(L,i,is))
#define gvts_tounsigned(L,i)	gvts_tounsignedx(L,(i),NULL)

#endif

#define gvts_newuserdata(L,s)	gvts_newuserdatauv(L,s,1)
#define gvts_getuservalue(L,idx)	gvts_getiuservalue(L,idx,1)
#define gvts_setuservalue(L,idx)	gvts_setiuservalue(L,idx,1)

#define GVTS_NUMTAGS		GVTS_NUMTYPES

#define GVTS_HOOKCALL	0
#define GVTS_HOOKRET	1
#define GVTS_HOOKLINE	2
#define GVTS_HOOKCOUNT	3
#define GVTS_HOOKTAILCALL 4

#define GVTS_MASKCALL	(1 << GVTS_HOOKCALL)
#define GVTS_MASKRET	(1 << GVTS_HOOKRET)
#define GVTS_MASKLINE	(1 << GVTS_HOOKLINE)
#define GVTS_MASKCOUNT	(1 << GVTS_HOOKCOUNT)

GVTS_API int (gvts_getstack) (gvts_State *L, int level, gvts_Debug *ar);
GVTS_API int (gvts_getinfo) (gvts_State *L, const char *what, gvts_Debug *ar);
GVTS_API const char *(gvts_getlocal) (gvts_State *L, const gvts_Debug *ar, int n);
GVTS_API const char *(gvts_setlocal) (gvts_State *L, const gvts_Debug *ar, int n);
GVTS_API const char *(gvts_getupvalue) (gvts_State *L, int funcindex, int n);
GVTS_API const char *(gvts_setupvalue) (gvts_State *L, int funcindex, int n);

GVTS_API void *(gvts_upvalueid) (gvts_State *L, int fidx, int n);
GVTS_API void  (gvts_upvaluejoin) (gvts_State *L, int fidx1, int n1,
                                               int fidx2, int n2);

GVTS_API void (gvts_sethook) (gvts_State *L, gvts_Hook func, int mask, int count);
GVTS_API gvts_Hook (gvts_gethook) (gvts_State *L);
GVTS_API int (gvts_gethookmask) (gvts_State *L);
GVTS_API int (gvts_gethookcount) (gvts_State *L);

GVTS_API int (gvts_setcstacklimit) (gvts_State *L, unsigned int limit);

struct gvts_Debug {
  int event;
  const char *name;	         
  const char *namewhat;	                                              
  const char *what;	                                    
  const char *source;	         
  size_t srclen;	         
  int currentline;	         
  int linedefined;	         
  int lastlinedefined;	         
  unsigned char nups;	                            
  unsigned char nparams;                              
  char isvararg;                 
  char istailcall;	         
  unsigned short ftransfer;                                             
  unsigned short ntransfer;                                         
  char short_src[GVTS_IDSIZE];          

  struct CallInfo *i_ci;                       
};

#endif
