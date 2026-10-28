#define lundump_c
#define GVTS_CORE

#include "Gprefix.h"

#include <limits.h>
#include <string.h>

#include "gvts.h"

#include "Gdebug.h"
#include "Gdo.h"
#include "Gfunc.h"
#include "Gmem.h"
#include "Gobject.h"
#include "Gstring.h"
#include "Gundump.h"
#include "Gzio.h"

#if !defined(gvtsi_verifycode)
#define gvtsi_verifycode(L,f)             
#endif

typedef struct {
  gvts_State *L;
  ZIO *Z;
  const char *name;
} LoadState;

static g_noret error (LoadState *S, const char *why) {
  gvtsO_pushfstring(S->L, "%s: bad binary format (%s)", S->name, why);
  gvtsD_throw(S->L, GVTS_ERRSYNTAX);
}

#define loadVector(S,b,n)	loadBlock(S,b,(n)*sizeof((b)[0]))

static void loadBlock (LoadState *S, void *b, size_t size) {
  if (gvtsZ_read(S->Z, b, size) != 0)
    error(S, "truncated chunk");
}

#define loadVar(S,x)		loadVector(S,&x,1)

static lu_byte loadByte (LoadState *S) {
  int b = zgetc(S->Z);
  if (b == EOZ)
    error(S, "truncated chunk");
  return cast_byte(b);
}

static size_t loadUnsigned (LoadState *S, size_t limit) {
  size_t x = 0;
  int b;
  limit >>= 7;
  do {
    b = loadByte(S);
    if (x >= limit)
      error(S, "integer overflow");
    x = (x << 7) | (b & 0x7f);
  } while ((b & 0x80) == 0);
  return x;
}

static size_t loadSize (LoadState *S) {
  return loadUnsigned(S, MAX_SIZET);
}

static int loadInt (LoadState *S) {
  return cast_int(loadUnsigned(S, INT_MAX));
}

static gvts_Number loadNumber (LoadState *S) {
  gvts_Number x;
  loadVar(S, x);
  return x;
}

static gvts_Integer loadInteger (LoadState *S) {
  gvts_Integer x;
  loadVar(S, x);
  return x;
}

static TString *loadStringN (LoadState *S, Proto *p) {
  gvts_State *L = S->L;
  TString *ts;
  size_t size = loadSize(S);
  if (size == 0)                  
    return NULL;
  else if (--size <= GVTSI_MAXSHORTLEN) {                     
    char buff[GVTSI_MAXSHORTLEN];
    loadVector(S, buff, size);                               
    ts = gvtsS_newlstr(L, buff, size);                     
  }
  else {                   
    ts = gvtsS_createlngstrobj(L, size);                     
    setsvalue2s(L, L->top.p, ts);                                       
    gvtsD_inctop(L);
    loadVector(S, getlngstr(ts), size);                                    
    L->top.p--;                  
  }
  gvtsC_objbarrier(L, p, ts);
  return ts;
}

static TString *loadString (LoadState *S, Proto *p) {
  TString *st = loadStringN(S, p);
  if (st == NULL)
    error(S, "bad format for constant string");
  return st;
}

static void loadCode (LoadState *S, Proto *f) {
  int n = loadInt(S);
  f->code = gvtsM_newvectorchecked(S->L, n, Instruction);
  f->sizecode = n;
  loadVector(S, f->code, n);
}

static void loadFunction(LoadState *S, Proto *f, TString *psource);

static void loadConstants (LoadState *S, Proto *f) {
  int i;
  int n = loadInt(S);
  f->k = gvtsM_newvectorchecked(S->L, n, TValue);
  f->sizek = n;
  for (i = 0; i < n; i++)
    setnilvalue(&f->k[i]);
  for (i = 0; i < n; i++) {
    TValue *o = &f->k[i];
    int t = loadByte(S);
    switch (t) {
      case GVTS_VNIL:
        setnilvalue(o);
        break;
      case GVTS_VFALSE:
        setbfvalue(o);
        break;
      case GVTS_VTRUE:
        setbtvalue(o);
        break;
      case GVTS_VNUMFLT:
        setfltvalue(o, loadNumber(S));
        break;
      case GVTS_VNUMINT:
        setivalue(o, loadInteger(S));
        break;
      case GVTS_VSHRSTR:
      case GVTS_VLNGSTR:
        setsvalue2n(S->L, o, loadString(S, f));
        break;
      default: gvts_assert(0);
    }
  }
}

static void loadProtos (LoadState *S, Proto *f) {
  int i;
  int n = loadInt(S);
  f->p = gvtsM_newvectorchecked(S->L, n, Proto *);
  f->sizep = n;
  for (i = 0; i < n; i++)
    f->p[i] = NULL;
  for (i = 0; i < n; i++) {
    f->p[i] = gvtsF_newproto(S->L);
    gvtsC_objbarrier(S->L, f, f->p[i]);
    loadFunction(S, f->p[i], f->source);
  }
}

static void loadUpvalues (LoadState *S, Proto *f) {
  int i, n;
  n = loadInt(S);
  f->upvalues = gvtsM_newvectorchecked(S->L, n, Upvaldesc);
  f->sizeupvalues = n;
  for (i = 0; i < n; i++)                               
    f->upvalues[i].name = NULL;
  for (i = 0; i < n; i++) {                                        
    f->upvalues[i].instack = loadByte(S);
    f->upvalues[i].idx = loadByte(S);
    f->upvalues[i].kind = loadByte(S);
  }
}

static void loadDebug (LoadState *S, Proto *f) {
  int i, n;
  n = loadInt(S);
  f->lineinfo = gvtsM_newvectorchecked(S->L, n, ls_byte);
  f->sizelineinfo = n;
  loadVector(S, f->lineinfo, n);
  n = loadInt(S);
  f->abslineinfo = gvtsM_newvectorchecked(S->L, n, AbsLineInfo);
  f->sizeabslineinfo = n;
  for (i = 0; i < n; i++) {
    f->abslineinfo[i].pc = loadInt(S);
    f->abslineinfo[i].line = loadInt(S);
  }
  n = loadInt(S);
  f->locvars = gvtsM_newvectorchecked(S->L, n, LocVar);
  f->sizelocvars = n;
  for (i = 0; i < n; i++)
    f->locvars[i].varname = NULL;
  for (i = 0; i < n; i++) {
    f->locvars[i].varname = loadStringN(S, f);
    f->locvars[i].startpc = loadInt(S);
    f->locvars[i].endpc = loadInt(S);
  }
  n = loadInt(S);
  if (n != 0)                                       
    n = f->sizeupvalues;                         
  for (i = 0; i < n; i++)
    f->upvalues[i].name = loadStringN(S, f);
}

static void loadFunction (LoadState *S, Proto *f, TString *psource) {
  f->source = loadStringN(S, f);
  if (f->source == NULL)                          
    f->source = psource;                             
  f->linedefined = loadInt(S);
  f->lastlinedefined = loadInt(S);
  f->numparams = loadByte(S);
  f->is_vararg = loadByte(S);
  f->maxstacksize = loadByte(S);
  loadCode(S, f);
  loadConstants(S, f);
  loadUpvalues(S, f);
  loadProtos(S, f);
  loadDebug(S, f);
}

static void checkliteral (LoadState *S, const char *s, const char *msg) {
  char buff[sizeof(GVTS_SIGNATURE) + sizeof(GVTSC_DATA)];                       
  size_t len = strlen(s);
  loadVector(S, buff, len);
  if (memcmp(s, buff, len) != 0)
    error(S, msg);
}

static void fchecksize (LoadState *S, size_t size, const char *tname) {
  if (loadByte(S) != size)
    error(S, gvtsO_pushfstring(S->L, "%s size mismatch", tname));
}

#define checksize(S,t)	fchecksize(S,sizeof(t),#t)

static void checkHeader (LoadState *S) {

  checkliteral(S, &GVTS_SIGNATURE[1], "not a binary chunk");
  if (loadByte(S) != GVTSC_VERSION)
    error(S, "version mismatch");
  if (loadByte(S) != GVTSC_FORMAT)
    error(S, "format mismatch");
  checkliteral(S, GVTSC_DATA, "corrupted chunk");
  checksize(S, Instruction);
  checksize(S, gvts_Integer);
  checksize(S, gvts_Number);
  if (loadInteger(S) != GVTSC_INT)
    error(S, "integer format mismatch");
  if (loadNumber(S) != GVTSC_NUM)
    error(S, "float format mismatch");
}

LClosure *gvtsU_undump(gvts_State *L, ZIO *Z, const char *name) {
  LoadState S;
  LClosure *cl;
  if (*name == '@' || *name == '=')
    S.name = name + 1;
  else if (*name == GVTS_SIGNATURE[0])
    S.name = "binary string";
  else
    S.name = name;
  S.L = L;
  S.Z = Z;
  checkHeader(&S);
  cl = gvtsF_newLclosure(L, loadByte(&S));
  setclLvalue2s(L, L->top.p, cl);
  gvtsD_inctop(L);
  cl->p = gvtsF_newproto(L);
  gvtsC_objbarrier(L, cl, cl->p);
  loadFunction(&S, cl->p, NULL);
  gvts_assert(cl->nupvalues == cl->p->sizeupvalues);
  gvtsi_verifycode(L, cl->p);
  return cl;
}
