#ifndef lauxlib_h
#define lauxlib_h

#include <stddef.h>
#include <stdio.h>

#include "gvtsconf.h"
#include "gvts.h"

#define GVTS_GNAME	"_G"

typedef struct gvtsG_Buffer gvtsG_Buffer;

#define GVTS_ERRFILE     (GVTS_ERRERR+1)

#define GVTS_LOADED_TABLE	"_LOADED"

#define GVTS_PRELOAD_TABLE	"_PRELOAD"

typedef struct gvtsG_Reg {
  const char *name;
  gvts_CFunction func;
} gvtsG_Reg;

#define GVTSG_NUMSIZES	(sizeof(gvts_Integer)*16 + sizeof(gvts_Number))

GVTSLIB_API void (gvtsG_checkversion_) (gvts_State *L, gvts_Number ver, size_t sz);
#define gvtsG_checkversion(L)  \
	  gvtsG_checkversion_(L, GVTS_VERSION_NUM, GVTSG_NUMSIZES)

GVTSLIB_API int (gvtsG_getmetafield) (gvts_State *L, int obj, const char *e);
GVTSLIB_API int (gvtsG_callmeta) (gvts_State *L, int obj, const char *e);
GVTSLIB_API const char *(gvtsG_tolstring) (gvts_State *L, int idx, size_t *len);
GVTSLIB_API int (gvtsG_argerror) (gvts_State *L, int arg, const char *extramsg);
GVTSLIB_API int (gvtsG_typeerror) (gvts_State *L, int arg, const char *tname);
GVTSLIB_API const char *(gvtsG_checklstring) (gvts_State *L, int arg,
                                                          size_t *l);
GVTSLIB_API const char *(gvtsG_optlstring) (gvts_State *L, int arg,
                                          const char *def, size_t *l);
GVTSLIB_API gvts_Number (gvtsG_checknumber) (gvts_State *L, int arg);
GVTSLIB_API gvts_Number (gvtsG_optnumber) (gvts_State *L, int arg, gvts_Number def);

GVTSLIB_API gvts_Integer (gvtsG_checkinteger) (gvts_State *L, int arg);
GVTSLIB_API gvts_Integer (gvtsG_optinteger) (gvts_State *L, int arg,
                                          gvts_Integer def);

GVTSLIB_API void (gvtsG_checkstack) (gvts_State *L, int sz, const char *msg);
GVTSLIB_API void (gvtsG_checktype) (gvts_State *L, int arg, int t);
GVTSLIB_API void (gvtsG_checkany) (gvts_State *L, int arg);

GVTSLIB_API int   (gvtsG_newmetatable) (gvts_State *L, const char *tname);
GVTSLIB_API void  (gvtsG_setmetatable) (gvts_State *L, const char *tname);
GVTSLIB_API void *(gvtsG_testudata) (gvts_State *L, int ud, const char *tname);
GVTSLIB_API void *(gvtsG_checkudata) (gvts_State *L, int ud, const char *tname);

GVTSLIB_API void (gvtsG_where) (gvts_State *L, int lvl);
GVTSLIB_API int (gvtsG_error) (gvts_State *L, const char *fmt, ...);

GVTSLIB_API int (gvtsG_checkoption) (gvts_State *L, int arg, const char *def,
                                   const char *const lst[]);

GVTSLIB_API int (gvtsG_fileresult) (gvts_State *L, int stat, const char *fname);
GVTSLIB_API int (gvtsG_execresult) (gvts_State *L, int stat);

#define GVTS_NOREF       (-2)
#define GVTS_REFNIL      (-1)

GVTSLIB_API int (gvtsG_ref) (gvts_State *L, int t);
GVTSLIB_API void (gvtsG_unref) (gvts_State *L, int t, int ref);

GVTSLIB_API int (gvtsG_loadfilex) (gvts_State *L, const char *filename,
                                               const char *mode);

#define gvtsG_loadfile(L,f)	gvtsG_loadfilex(L,f,NULL)

GVTSLIB_API int (gvtsG_loadbufferx) (gvts_State *L, const char *buff, size_t sz,
                                   const char *name, const char *mode);
GVTSLIB_API int (gvtsG_loadstring) (gvts_State *L, const char *s);

GVTSLIB_API gvts_State *(gvtsG_newstate) (void);

GVTSLIB_API gvts_Integer (gvtsG_len) (gvts_State *L, int idx);

GVTSLIB_API void (gvtsG_addgsub) (gvtsG_Buffer *b, const char *s,
                                     const char *p, const char *r);
GVTSLIB_API const char *(gvtsG_gsub) (gvts_State *L, const char *s,
                                    const char *p, const char *r);

GVTSLIB_API void (gvtsG_setfuncs) (gvts_State *L, const gvtsG_Reg *l, int nup);

GVTSLIB_API int (gvtsG_getsubtable) (gvts_State *L, int idx, const char *fname);

GVTSLIB_API void (gvtsG_traceback) (gvts_State *L, gvts_State *L1,
                                  const char *msg, int level);

GVTSLIB_API void (gvtsG_requiref) (gvts_State *L, const char *modname,
                                 gvts_CFunction openf, int glb);

#define gvtsG_newlibtable(L,l)	\
  gvts_createtable(L, 0, sizeof(l)/sizeof((l)[0]) - 1)

#define gvtsG_newlib(L,l)  \
  (gvtsG_checkversion(L), gvtsG_newlibtable(L,l), gvtsG_setfuncs(L,l,0))

#define gvtsG_argcheck(L, cond,arg,extramsg)	\
	((void)(gvtsi_likely(cond) || gvtsG_argerror(L, (arg), (extramsg))))

#define gvtsG_argexpected(L,cond,arg,tname)	\
	((void)(gvtsi_likely(cond) || gvtsG_typeerror(L, (arg), (tname))))

#define gvtsG_checkstring(L,n)	(gvtsG_checklstring(L, (n), NULL))
#define gvtsG_optstring(L,n,d)	(gvtsG_optlstring(L, (n), (d), NULL))

#define gvtsG_typename(L,i)	gvts_typename(L, gvts_type(L,(i)))

#define gvtsG_dofile(L, fn) \
	(gvtsG_loadfile(L, fn) || gvts_pcall(L, 0, GVTS_MULTRET, 0))

#define gvtsG_dostring(L, s) \
	(gvtsG_loadstring(L, s) || gvts_pcall(L, 0, GVTS_MULTRET, 0))

#define gvtsG_getmetatable(L,n)	(gvts_getfield(L, GVTS_REGISTRYINDEX, (n)))

#define gvtsG_opt(L,f,n,d)	(gvts_isnoneornil(L,(n)) ? (d) : f(L,(n)))

#define gvtsG_loadbuffer(L,s,sz,n)	gvtsG_loadbufferx(L,s,sz,n,NULL)

#define gvtsG_intop(op,v1,v2)  \
	((gvts_Integer)((gvts_Unsigned)(v1) op (gvts_Unsigned)(v2)))

#define gvtsG_pushfail(L)	gvts_pushnil(L)

#if !defined(gvts_assert)

#if defined GVTSI_ASSERT
  #include <assert.h>
  #define gvts_assert(c)		assert(c)
#else
  #define gvts_assert(c)		((void)0)
#endif

#endif

struct gvtsG_Buffer {
  char *b;                      
  size_t size;                   
  size_t n;                                      
  gvts_State *L;
  union {
    GVTSI_MAXALIGN;                                           
    char b[GVTSG_BUFFERSIZE];                      
  } init;
};

#define gvtsG_bufflen(bf)	((bf)->n)
#define gvtsG_buffaddr(bf)	((bf)->b)

#define gvtsG_addchar(B,c) \
  ((void)((B)->n < (B)->size || gvtsG_prepbuffsize((B), 1)), \
   ((B)->b[(B)->n++] = (c)))

#define gvtsG_addsize(B,s)	((B)->n += (s))

#define gvtsG_buffsub(B,s)	((B)->n -= (s))

GVTSLIB_API void (gvtsG_buffinit) (gvts_State *L, gvtsG_Buffer *B);
GVTSLIB_API char *(gvtsG_prepbuffsize) (gvtsG_Buffer *B, size_t sz);
GVTSLIB_API void (gvtsG_addlstring) (gvtsG_Buffer *B, const char *s, size_t l);
GVTSLIB_API void (gvtsG_addstring) (gvtsG_Buffer *B, const char *s);
GVTSLIB_API void (gvtsG_addvalue) (gvtsG_Buffer *B);
GVTSLIB_API void (gvtsG_pushresult) (gvtsG_Buffer *B);
GVTSLIB_API void (gvtsG_pushresultsize) (gvtsG_Buffer *B, size_t sz);
GVTSLIB_API char *(gvtsG_buffinitsize) (gvts_State *L, gvtsG_Buffer *B, size_t sz);

#define gvtsG_prepbuffer(B)	gvtsG_prepbuffsize(B, GVTSG_BUFFERSIZE)

#define GVTS_FILEHANDLE          "FILE*"

typedef struct gvtsG_Stream {
  FILE *f;                                                      
  gvts_CFunction closef;                                                 
} gvtsG_Stream;

#if !defined(gvts_writestring)
#define gvts_writestring(s,l)   fwrite((s), sizeof(char), (l), stdout)
#endif

#if !defined(gvts_writeline)
#define gvts_writeline()        (gvts_writestring("\n", 1), fflush(stdout))
#endif

#if !defined(gvts_writestringerror)
#define gvts_writestringerror(s,p) \
        (fprintf(stderr, (s), (p)), fflush(stderr))
#endif

#if defined(GVTS_COMPAT_APIINTCASTS)

#define gvtsG_checkunsigned(L,a)	((gvts_Unsigned)gvtsG_checkinteger(L,a))
#define gvtsG_optunsigned(L,a,d)	\
	((gvts_Unsigned)gvtsG_optinteger(L,a,(gvts_Integer)(d)))

#define gvtsG_checkint(L,n)	((int)gvtsG_checkinteger(L, (n)))
#define gvtsG_optint(L,n,d)	((int)gvtsG_optinteger(L, (n), (d)))

#define gvtsG_checklong(L,n)	((long)gvtsG_checkinteger(L, (n)))
#define gvtsG_optlong(L,n,d)	((long)gvtsG_optinteger(L, (n), (d)))

#endif

#endif
