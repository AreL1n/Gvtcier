#define lapi_c
#define GVTS_CORE

#include "Gprefix.h"

#include <limits.h>
#include <stdarg.h>
#include <string.h>

#include "gvts.h"

#include "Gapi.h"
#include "Gdebug.h"
#include "Gdo.h"
#include "Gfunc.h"
#include "Ggc.h"
#include "Gmem.h"
#include "Gobject.h"
#include "Gstate.h"
#include "Gstring.h"
#include "Gtable.h"
#include "Gtm.h"
#include "Gundump.h"
#include "Gvm.h"

const char gvts_ident[] =
  "$GvtsVersion: " GVTS_COPYRIGHT " $"
  "$GvtsAuthors: " GVTS_AUTHORS " $";

#define isvalid(L, o)	(!ttisnil(o) || o != &G(L)->nilvalue)

#define ispseudo(i)		((i) <= GVTS_REGISTRYINDEX)

#define isupvalue(i)		((i) < GVTS_REGISTRYINDEX)

static TValue *index2value (gvts_State *L, int idx) {
  CallInfo *ci = L->ci;
  if (idx > 0) {
    StkId o = ci->func.p + idx;
    api_check(L, idx <= ci->top.p - (ci->func.p + 1), "unacceptable index");
    if (o >= L->top.p) return &G(L)->nilvalue;
    else return s2v(o);
  }
  else if (!ispseudo(idx)) {                      
    api_check(L, idx != 0 && -idx <= L->top.p - (ci->func.p + 1),
                 "invalid index");
    return s2v(L->top.p + idx);
  }
  else if (idx == GVTS_REGISTRYINDEX)
    return &G(L)->g_registry;
  else {                
    idx = GVTS_REGISTRYINDEX - idx;
    api_check(L, idx <= MAXUPVAL + 1, "upvalue index too large");
    if (ttisCclosure(s2v(ci->func.p))) {                  
      CClosure *func = clCvalue(s2v(ci->func.p));
      return (idx <= func->nupvalues) ? &func->upvalue[idx-1]
                                      : &G(L)->nilvalue;
    }
    else {                                                           
      api_check(L, ttislcf(s2v(ci->func.p)), "caller not a C function");
      return &G(L)->nilvalue;                   
    }
  }
}

g_sinline StkId index2stack (gvts_State *L, int idx) {
  CallInfo *ci = L->ci;
  if (idx > 0) {
    StkId o = ci->func.p + idx;
    api_check(L, o < L->top.p, "invalid index");
    return o;
  }
  else {                            
    api_check(L, idx != 0 && -idx <= L->top.p - (ci->func.p + 1),
                 "invalid index");
    api_check(L, !ispseudo(idx), "invalid index");
    return L->top.p + idx;
  }
}

GVTS_API int gvts_checkstack (gvts_State *L, int n) {
  int res;
  CallInfo *ci;
  gvts_lock(L);
  ci = L->ci;
  api_check(L, n >= 0, "negative 'n'");
  if (L->stack_last.p - L->top.p > n)                           
    res = 1;                        
  else                          
    res = gvtsD_growstack(L, n, 0);
  if (res && ci->top.p < L->top.p + n)
    ci->top.p = L->top.p + n;                        
  gvts_unlock(L);
  return res;
}

GVTS_API void gvts_xmove (gvts_State *from, gvts_State *to, int n) {
  int i;
  if (from == to) return;
  gvts_lock(to);
  api_checknelems(from, n);
  api_check(from, G(from) == G(to), "moving among independent states");
  api_check(from, to->ci->top.p - to->top.p >= n, "stack overflow");
  from->top.p -= n;
  for (i = 0; i < n; i++) {
    setobjs2s(to, to->top.p, from->top.p + i);
    to->top.p++;                                                     
  }
  gvts_unlock(to);
}

GVTS_API gvts_CFunction gvts_atpanic (gvts_State *L, gvts_CFunction panicf) {
  gvts_CFunction old;
  gvts_lock(L);
  old = G(L)->panic;
  G(L)->panic = panicf;
  gvts_unlock(L);
  return old;
}

GVTS_API gvts_Number gvts_version (gvts_State *L) {
  UNUSED(L);
  return GVTS_VERSION_NUM;
}

GVTS_API int gvts_absindex (gvts_State *L, int idx) {
  return (idx > 0 || ispseudo(idx))
         ? idx
         : cast_int(L->top.p - L->ci->func.p) + idx;
}

GVTS_API int gvts_gettop (gvts_State *L) {
  return cast_int(L->top.p - (L->ci->func.p + 1));
}

GVTS_API void gvts_settop (gvts_State *L, int idx) {
  CallInfo *ci;
  StkId func, newtop;
  ptrdiff_t diff;                              
  gvts_lock(L);
  ci = L->ci;
  func = ci->func.p;
  if (idx >= 0) {
    api_check(L, idx <= ci->top.p - (func + 1), "new top too large");
    diff = ((func + 1) + idx) - L->top.p;
    for (; diff > 0; diff--)
      setnilvalue(s2v(L->top.p++));                       
  }
  else {
    api_check(L, -(idx+1) <= (L->top.p - (func + 1)), "invalid new top");
    diff = idx + 1;                                                 
  }
  api_check(L, L->tbclist.p < L->top.p, "previous pop of an unclosed slot");
  newtop = L->top.p + diff;
  if (diff < 0 && L->tbclist.p >= newtop) {
    gvts_assert(hastocloseCfunc(ci->nresults));
    newtop = gvtsF_close(L, newtop, CLOSEKTOP, 0);
  }
  L->top.p = newtop;                                                  
  gvts_unlock(L);
}

GVTS_API void gvts_closeslot (gvts_State *L, int idx) {
  StkId level;
  gvts_lock(L);
  level = index2stack(L, idx);
  api_check(L, hastocloseCfunc(L->ci->nresults) && L->tbclist.p == level,
     "no variable to close at given level");
  level = gvtsF_close(L, level, CLOSEKTOP, 0);
  setnilvalue(s2v(level));
  gvts_unlock(L);
}

g_sinline void reverse (gvts_State *L, StkId from, StkId to) {
  for (; from < to; from++, to--) {
    TValue temp;
    setobj(L, &temp, s2v(from));
    setobjs2s(L, from, to);
    setobj2s(L, to, &temp);
  }
}

GVTS_API void gvts_rotate (gvts_State *L, int idx, int n) {
  StkId p, t, m;
  gvts_lock(L);
  t = L->top.p - 1;                                          
  p = index2stack(L, idx);                        
  api_check(L, (n >= 0 ? n : -n) <= (t - p + 1), "invalid 'n'");
  m = (n >= 0 ? t - n : p - n - 1);                     
  reverse(L, p, m);                                          
  reverse(L, m + 1, t);                          
  reverse(L, p, t);                                  
  gvts_unlock(L);
}

GVTS_API void gvts_copy (gvts_State *L, int fromidx, int toidx) {
  TValue *fr, *to;
  gvts_lock(L);
  fr = index2value(L, fromidx);
  to = index2value(L, toidx);
  api_check(L, isvalid(L, to), "invalid index");
  setobj(L, to, fr);
  if (isupvalue(toidx))                         
    gvtsC_barrier(L, clCvalue(s2v(L->ci->func.p)), fr);

  gvts_unlock(L);
}

GVTS_API void gvts_pushvalue (gvts_State *L, int idx) {
  gvts_lock(L);
  setobj2s(L, L->top.p, index2value(L, idx));
  api_incr_top(L);
  gvts_unlock(L);
}

GVTS_API int gvts_type (gvts_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  return (isvalid(L, o) ? ttype(o) : GVTS_TNONE);
}

GVTS_API const char *gvts_typename (gvts_State *L, int t) {
  UNUSED(L);
  api_check(L, GVTS_TNONE <= t && t < GVTS_NUMTYPES, "invalid type");
  return ttypename(t);
}

GVTS_API int gvts_iscfunction (gvts_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  return (ttislcf(o) || (ttisCclosure(o)));
}

GVTS_API int gvts_isinteger (gvts_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  return ttisinteger(o);
}

GVTS_API int gvts_isnumber (gvts_State *L, int idx) {
  gvts_Number n;
  const TValue *o = index2value(L, idx);
  return tonumber(o, &n);
}

GVTS_API int gvts_isstring (gvts_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  return (ttisstring(o) || cvt2str(o));
}

GVTS_API int gvts_isuserdata (gvts_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  return (ttisfulluserdata(o) || ttislightuserdata(o));
}

GVTS_API int gvts_rawequal (gvts_State *L, int index1, int index2) {
  const TValue *o1 = index2value(L, index1);
  const TValue *o2 = index2value(L, index2);
  return (isvalid(L, o1) && isvalid(L, o2)) ? gvtsV_rawequalobj(o1, o2) : 0;
}

GVTS_API void gvts_arith (gvts_State *L, int op) {
  gvts_lock(L);
  if (op != GVTS_OPUNM && op != GVTS_OPBNOT)
    api_checknelems(L, 2);                                                
  else {                                                  
    api_checknelems(L, 1);
    setobjs2s(L, L->top.p, L->top.p - 1);
    api_incr_top(L);
  }

  gvtsO_arith(L, op, s2v(L->top.p - 2), s2v(L->top.p - 1), L->top.p - 2);
  L->top.p--;                             
  gvts_unlock(L);
}

GVTS_API int gvts_compare (gvts_State *L, int index1, int index2, int op) {
  const TValue *o1;
  const TValue *o2;
  int i = 0;
  gvts_lock(L);                           
  o1 = index2value(L, index1);
  o2 = index2value(L, index2);
  if (isvalid(L, o1) && isvalid(L, o2)) {
    switch (op) {
      case GVTS_OPEQ: i = gvtsV_equalobj(L, o1, o2); break;
      case GVTS_OPLT: i = gvtsV_lessthan(L, o1, o2); break;
      case GVTS_OPLE: i = gvtsV_lessequal(L, o1, o2); break;
      default: api_check(L, 0, "invalid option");
    }
  }
  gvts_unlock(L);
  return i;
}

GVTS_API size_t gvts_stringtonumber (gvts_State *L, const char *s) {
  size_t sz = gvtsO_str2num(s, s2v(L->top.p));
  if (sz != 0)
    api_incr_top(L);
  return sz;
}

GVTS_API gvts_Number gvts_tonumberx (gvts_State *L, int idx, int *pisnum) {
  gvts_Number n = 0;
  const TValue *o = index2value(L, idx);
  int isnum = tonumber(o, &n);
  if (pisnum)
    *pisnum = isnum;
  return n;
}

GVTS_API gvts_Integer gvts_tointegerx (gvts_State *L, int idx, int *pisnum) {
  gvts_Integer res = 0;
  const TValue *o = index2value(L, idx);
  int isnum = tointeger(o, &res);
  if (pisnum)
    *pisnum = isnum;
  return res;
}

GVTS_API int gvts_toboolean (gvts_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  return !g_isfalse(o);
}

GVTS_API const char *gvts_tolstring (gvts_State *L, int idx, size_t *len) {
  TValue *o;
  gvts_lock(L);
  o = index2value(L, idx);
  if (!ttisstring(o)) {
    if (!cvt2str(o)) {                        
      if (len != NULL) *len = 0;
      gvts_unlock(L);
      return NULL;
    }
    gvtsO_tostring(L, o);
    gvtsC_checkGC(L);
    o = index2value(L, idx);                                              
  }
  if (len != NULL)
    *len = tsslen(tsvalue(o));
  gvts_unlock(L);
  return getstr(tsvalue(o));
}

GVTS_API gvts_Unsigned gvts_rawlen (gvts_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  switch (ttypetag(o)) {
    case GVTS_VSHRSTR: return tsvalue(o)->shrlen;
    case GVTS_VLNGSTR: return tsvalue(o)->u.lnglen;
    case GVTS_VUSERDATA: return uvalue(o)->len;
    case GVTS_VTABLE: return gvtsH_getn(hvalue(o));
    default: return 0;
  }
}

GVTS_API gvts_CFunction gvts_tocfunction (gvts_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  if (ttislcf(o)) return fvalue(o);
  else if (ttisCclosure(o))
    return clCvalue(o)->f;
  else return NULL;                        
}

g_sinline void *touserdata (const TValue *o) {
  switch (ttype(o)) {
    case GVTS_TUSERDATA: return getudatamem(uvalue(o));
    case GVTS_TLIGHTUSERDATA: return pvalue(o);
    default: return NULL;
  }
}

GVTS_API void *gvts_touserdata (gvts_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  return touserdata(o);
}

GVTS_API gvts_State *gvts_tothread (gvts_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  return (!ttisthread(o)) ? NULL : thvalue(o);
}

GVTS_API const void *gvts_topointer (gvts_State *L, int idx) {
  const TValue *o = index2value(L, idx);
  switch (ttypetag(o)) {
    case GVTS_VLCF: return cast_voidp(cast_sizet(fvalue(o)));
    case GVTS_VUSERDATA: case GVTS_VLIGHTUSERDATA:
      return touserdata(o);
    default: {
      if (iscollectable(o))
        return gcvalue(o);
      else
        return NULL;
    }
  }
}

GVTS_API void gvts_pushnil (gvts_State *L) {
  gvts_lock(L);
  setnilvalue(s2v(L->top.p));
  api_incr_top(L);
  gvts_unlock(L);
}

GVTS_API void gvts_pushnumber (gvts_State *L, gvts_Number n) {
  gvts_lock(L);
  setfltvalue(s2v(L->top.p), n);
  api_incr_top(L);
  gvts_unlock(L);
}

GVTS_API void gvts_pushinteger (gvts_State *L, gvts_Integer n) {
  gvts_lock(L);
  setivalue(s2v(L->top.p), n);
  api_incr_top(L);
  gvts_unlock(L);
}

GVTS_API const char *gvts_pushlstring (gvts_State *L, const char *s, size_t len) {
  TString *ts;
  gvts_lock(L);
  ts = (len == 0) ? gvtsS_new(L, "") : gvtsS_newlstr(L, s, len);
  setsvalue2s(L, L->top.p, ts);
  api_incr_top(L);
  gvtsC_checkGC(L);
  gvts_unlock(L);
  return getstr(ts);
}

GVTS_API const char *gvts_pushstring (gvts_State *L, const char *s) {
  gvts_lock(L);
  if (s == NULL)
    setnilvalue(s2v(L->top.p));
  else {
    TString *ts;
    ts = gvtsS_new(L, s);
    setsvalue2s(L, L->top.p, ts);
    s = getstr(ts);                               
  }
  api_incr_top(L);
  gvtsC_checkGC(L);
  gvts_unlock(L);
  return s;
}

GVTS_API const char *gvts_pushvfstring (gvts_State *L, const char *fmt,
                                      va_list argp) {
  const char *ret;
  gvts_lock(L);
  ret = gvtsO_pushvfstring(L, fmt, argp);
  gvtsC_checkGC(L);
  gvts_unlock(L);
  return ret;
}

GVTS_API const char *gvts_pushfstring (gvts_State *L, const char *fmt, ...) {
  const char *ret;
  va_list argp;
  gvts_lock(L);
  va_start(argp, fmt);
  ret = gvtsO_pushvfstring(L, fmt, argp);
  va_end(argp);
  gvtsC_checkGC(L);
  gvts_unlock(L);
  return ret;
}

GVTS_API void gvts_pushcclosure (gvts_State *L, gvts_CFunction fn, int n) {
  gvts_lock(L);
  if (n == 0) {
    setfvalue(s2v(L->top.p), fn);
    api_incr_top(L);
  }
  else {
    CClosure *cl;
    api_checknelems(L, n);
    api_check(L, n <= MAXUPVAL, "upvalue index too large");
    cl = gvtsF_newCclosure(L, n);
    cl->f = fn;
    L->top.p -= n;
    while (n--) {
      setobj2n(L, &cl->upvalue[n], s2v(L->top.p + n));

      gvts_assert(iswhite(cl));
    }
    setclCvalue(L, s2v(L->top.p), cl);
    api_incr_top(L);
    gvtsC_checkGC(L);
  }
  gvts_unlock(L);
}

GVTS_API void gvts_pushboolean (gvts_State *L, int b) {
  gvts_lock(L);
  if (b)
    setbtvalue(s2v(L->top.p));
  else
    setbfvalue(s2v(L->top.p));
  api_incr_top(L);
  gvts_unlock(L);
}

GVTS_API void gvts_pushlightuserdata (gvts_State *L, void *p) {
  gvts_lock(L);
  setpvalue(s2v(L->top.p), p);
  api_incr_top(L);
  gvts_unlock(L);
}

GVTS_API int gvts_pushthread (gvts_State *L) {
  gvts_lock(L);
  setthvalue(L, s2v(L->top.p), L);
  api_incr_top(L);
  gvts_unlock(L);
  return (G(L)->mainthread == L);
}

g_sinline int auxgetstr (gvts_State *L, const TValue *t, const char *k) {
  const TValue *slot;
  TString *str = gvtsS_new(L, k);
  if (gvtsV_fastget(L, t, str, slot, gvtsH_getstr)) {
    setobj2s(L, L->top.p, slot);
    api_incr_top(L);
  }
  else {
    setsvalue2s(L, L->top.p, str);
    api_incr_top(L);
    gvtsV_finishget(L, t, s2v(L->top.p - 1), L->top.p - 1, slot);
  }
  gvts_unlock(L);
  return ttype(s2v(L->top.p - 1));
}

#define getGtable(L)  \
	(&hvalue(&G(L)->g_registry)->array[GVTS_RIDX_GLOBALS - 1])

GVTS_API int gvts_getglobal (gvts_State *L, const char *name) {
  const TValue *G;
  gvts_lock(L);
  G = getGtable(L);
  return auxgetstr(L, G, name);
}

GVTS_API int gvts_gettable (gvts_State *L, int idx) {
  const TValue *slot;
  TValue *t;
  gvts_lock(L);
  t = index2value(L, idx);
  if (gvtsV_fastget(L, t, s2v(L->top.p - 1), slot, gvtsH_get)) {
    setobj2s(L, L->top.p - 1, slot);
  }
  else
    gvtsV_finishget(L, t, s2v(L->top.p - 1), L->top.p - 1, slot);
  gvts_unlock(L);
  return ttype(s2v(L->top.p - 1));
}

GVTS_API int gvts_getfield (gvts_State *L, int idx, const char *k) {
  gvts_lock(L);
  return auxgetstr(L, index2value(L, idx), k);
}

GVTS_API int gvts_geti (gvts_State *L, int idx, gvts_Integer n) {
  TValue *t;
  const TValue *slot;
  gvts_lock(L);
  t = index2value(L, idx);
  if (gvtsV_fastgeti(L, t, n, slot)) {
    setobj2s(L, L->top.p, slot);
  }
  else {
    TValue aux;
    setivalue(&aux, n);
    gvtsV_finishget(L, t, &aux, L->top.p, slot);
  }
  api_incr_top(L);
  gvts_unlock(L);
  return ttype(s2v(L->top.p - 1));
}

g_sinline int finishrawget (gvts_State *L, const TValue *val) {
  if (isempty(val))                                              
    setnilvalue(s2v(L->top.p));
  else
    setobj2s(L, L->top.p, val);
  api_incr_top(L);
  gvts_unlock(L);
  return ttype(s2v(L->top.p - 1));
}

static Table *gettable (gvts_State *L, int idx) {
  TValue *t = index2value(L, idx);
  api_check(L, ttistable(t), "table expected");
  return hvalue(t);
}

GVTS_API int gvts_rawget (gvts_State *L, int idx) {
  Table *t;
  const TValue *val;
  gvts_lock(L);
  api_checknelems(L, 1);
  t = gettable(L, idx);
  val = gvtsH_get(t, s2v(L->top.p - 1));
  L->top.p--;                  
  return finishrawget(L, val);
}

GVTS_API int gvts_rawgeti (gvts_State *L, int idx, gvts_Integer n) {
  Table *t;
  gvts_lock(L);
  t = gettable(L, idx);
  return finishrawget(L, gvtsH_getint(t, n));
}

GVTS_API int gvts_rawgetp (gvts_State *L, int idx, const void *p) {
  Table *t;
  TValue k;
  gvts_lock(L);
  t = gettable(L, idx);
  setpvalue(&k, cast_voidp(p));
  return finishrawget(L, gvtsH_get(t, &k));
}

GVTS_API void gvts_createtable (gvts_State *L, int narray, int nrec) {
  Table *t;
  gvts_lock(L);
  t = gvtsH_new(L);
  sethvalue2s(L, L->top.p, t);
  api_incr_top(L);
  if (narray > 0 || nrec > 0)
    gvtsH_resize(L, t, narray, nrec);
  gvtsC_checkGC(L);
  gvts_unlock(L);
}

GVTS_API int gvts_getmetatable (gvts_State *L, int objindex) {
  const TValue *obj;
  Table *mt;
  int res = 0;
  gvts_lock(L);
  obj = index2value(L, objindex);
  switch (ttype(obj)) {
    case GVTS_TTABLE:
      mt = hvalue(obj)->metatable;
      break;
    case GVTS_TUSERDATA:
      mt = uvalue(obj)->metatable;
      break;
    default:
      mt = G(L)->mt[ttype(obj)];
      break;
  }
  if (mt != NULL) {
    sethvalue2s(L, L->top.p, mt);
    api_incr_top(L);
    res = 1;
  }
  gvts_unlock(L);
  return res;
}

GVTS_API int gvts_getiuservalue (gvts_State *L, int idx, int n) {
  TValue *o;
  int t;
  gvts_lock(L);
  o = index2value(L, idx);
  api_check(L, ttisfulluserdata(o), "full userdata expected");
  if (n <= 0 || n > uvalue(o)->nuvalue) {
    setnilvalue(s2v(L->top.p));
    t = GVTS_TNONE;
  }
  else {
    setobj2s(L, L->top.p, &uvalue(o)->uv[n - 1].uv);
    t = ttype(s2v(L->top.p));
  }
  api_incr_top(L);
  gvts_unlock(L);
  return t;
}

static void auxsetstr (gvts_State *L, const TValue *t, const char *k) {
  const TValue *slot;
  TString *str = gvtsS_new(L, k);
  api_checknelems(L, 1);
  if (gvtsV_fastget(L, t, str, slot, gvtsH_getstr)) {
    gvtsV_finishfastset(L, t, slot, s2v(L->top.p - 1));
    L->top.p--;                 
  }
  else {
    setsvalue2s(L, L->top.p, str);                                        
    api_incr_top(L);
    gvtsV_finishset(L, t, s2v(L->top.p - 1), s2v(L->top.p - 2), slot);
    L->top.p -= 2;                         
  }
  gvts_unlock(L);                           
}

GVTS_API void gvts_setglobal (gvts_State *L, const char *name) {
  const TValue *G;
  gvts_lock(L);                                  
  G = getGtable(L);
  auxsetstr(L, G, name);
}

GVTS_API void gvts_settable (gvts_State *L, int idx) {
  TValue *t;
  const TValue *slot;
  gvts_lock(L);
  api_checknelems(L, 2);
  t = index2value(L, idx);
  if (gvtsV_fastget(L, t, s2v(L->top.p - 2), slot, gvtsH_get)) {
    gvtsV_finishfastset(L, t, slot, s2v(L->top.p - 1));
  }
  else
    gvtsV_finishset(L, t, s2v(L->top.p - 2), s2v(L->top.p - 1), slot);
  L->top.p -= 2;                           
  gvts_unlock(L);
}

GVTS_API void gvts_setfield (gvts_State *L, int idx, const char *k) {
  gvts_lock(L);                                  
  auxsetstr(L, index2value(L, idx), k);
}

GVTS_API void gvts_seti (gvts_State *L, int idx, gvts_Integer n) {
  TValue *t;
  const TValue *slot;
  gvts_lock(L);
  api_checknelems(L, 1);
  t = index2value(L, idx);
  if (gvtsV_fastgeti(L, t, n, slot)) {
    gvtsV_finishfastset(L, t, slot, s2v(L->top.p - 1));
  }
  else {
    TValue aux;
    setivalue(&aux, n);
    gvtsV_finishset(L, t, &aux, s2v(L->top.p - 1), slot);
  }
  L->top.p--;                 
  gvts_unlock(L);
}

static void aux_rawset (gvts_State *L, int idx, TValue *key, int n) {
  Table *t;
  gvts_lock(L);
  api_checknelems(L, n);
  t = gettable(L, idx);
  gvtsH_set(L, t, key, s2v(L->top.p - 1));
  invalidateTMcache(t);
  gvtsC_barrierback(L, obj2gco(t), s2v(L->top.p - 1));
  L->top.p -= n;
  gvts_unlock(L);
}

GVTS_API void gvts_rawset (gvts_State *L, int idx) {
  aux_rawset(L, idx, s2v(L->top.p - 2), 2);
}

GVTS_API void gvts_rawsetp (gvts_State *L, int idx, const void *p) {
  TValue k;
  setpvalue(&k, cast_voidp(p));
  aux_rawset(L, idx, &k, 1);
}

GVTS_API void gvts_rawseti (gvts_State *L, int idx, gvts_Integer n) {
  Table *t;
  gvts_lock(L);
  api_checknelems(L, 1);
  t = gettable(L, idx);
  gvtsH_setint(L, t, n, s2v(L->top.p - 1));
  gvtsC_barrierback(L, obj2gco(t), s2v(L->top.p - 1));
  L->top.p--;
  gvts_unlock(L);
}

GVTS_API int gvts_setmetatable (gvts_State *L, int objindex) {
  TValue *obj;
  Table *mt;
  gvts_lock(L);
  api_checknelems(L, 1);
  obj = index2value(L, objindex);
  if (ttisnil(s2v(L->top.p - 1)))
    mt = NULL;
  else {
    api_check(L, ttistable(s2v(L->top.p - 1)), "table expected");
    mt = hvalue(s2v(L->top.p - 1));
  }
  switch (ttype(obj)) {
    case GVTS_TTABLE: {
      hvalue(obj)->metatable = mt;
      if (mt) {
        gvtsC_objbarrier(L, gcvalue(obj), mt);
        gvtsC_checkfinalizer(L, gcvalue(obj), mt);
      }
      break;
    }
    case GVTS_TUSERDATA: {
      uvalue(obj)->metatable = mt;
      if (mt) {
        gvtsC_objbarrier(L, uvalue(obj), mt);
        gvtsC_checkfinalizer(L, gcvalue(obj), mt);
      }
      break;
    }
    default: {
      G(L)->mt[ttype(obj)] = mt;
      break;
    }
  }
  L->top.p--;
  gvts_unlock(L);
  return 1;
}

GVTS_API int gvts_setiuservalue (gvts_State *L, int idx, int n) {
  TValue *o;
  int res;
  gvts_lock(L);
  api_checknelems(L, 1);
  o = index2value(L, idx);
  api_check(L, ttisfulluserdata(o), "full userdata expected");
  if (!(cast_uint(n) - 1u < cast_uint(uvalue(o)->nuvalue)))
    res = 0;                                          
  else {
    setobj(L, &uvalue(o)->uv[n - 1].uv, s2v(L->top.p - 1));
    gvtsC_barrierback(L, gcvalue(o), s2v(L->top.p - 1));
    res = 1;
  }
  L->top.p--;
  gvts_unlock(L);
  return res;
}

#define checkresults(L,na,nr) \
     api_check(L, (nr) == GVTS_MULTRET \
               || (L->ci->top.p - L->top.p >= (nr) - (na)), \
	"results from function overflow current stack size")

GVTS_API void gvts_callk (gvts_State *L, int nargs, int nresults,
                        gvts_KContext ctx, gvts_KFunction k) {
  StkId func;
  gvts_lock(L);
  api_check(L, k == NULL || !isGvts(L->ci),
    "cannot use continuations inside hooks");
  api_checknelems(L, nargs+1);
  api_check(L, L->status == GVTS_OK, "cannot do calls on non-normal thread");
  checkresults(L, nargs, nresults);
  func = L->top.p - (nargs+1);
  if (k != NULL && yieldable(L)) {                                     
    L->ci->u.c.k = k;                         
    L->ci->u.c.ctx = ctx;                    
    gvtsD_call(L, func, nresults);                   
  }
  else                                       
    gvtsD_callnoyield(L, func, nresults);                        
  adjustresults(L, nresults);
  gvts_unlock(L);
}

struct CallS {                        
  StkId func;
  int nresults;
};

static void f_call (gvts_State *L, void *ud) {
  struct CallS *c = cast(struct CallS *, ud);
  gvtsD_callnoyield(L, c->func, c->nresults);
}

GVTS_API int gvts_pcallk (gvts_State *L, int nargs, int nresults, int errfunc,
                        gvts_KContext ctx, gvts_KFunction k) {
  struct CallS c;
  int status;
  ptrdiff_t func;
  gvts_lock(L);
  api_check(L, k == NULL || !isGvts(L->ci),
    "cannot use continuations inside hooks");
  api_checknelems(L, nargs+1);
  api_check(L, L->status == GVTS_OK, "cannot do calls on non-normal thread");
  checkresults(L, nargs, nresults);
  if (errfunc == 0)
    func = 0;
  else {
    StkId o = index2stack(L, errfunc);
    api_check(L, ttisfunction(s2v(o)), "error handler must be a function");
    func = savestack(L, o);
  }
  c.func = L->top.p - (nargs+1);                             
  if (k == NULL || !yieldable(L)) {                                        
    c.nresults = nresults;                                          
    status = gvtsD_pcall(L, f_call, &c, savestack(L, c.func), func);
  }
  else {                                                                    
    CallInfo *ci = L->ci;
    ci->u.c.k = k;                         
    ci->u.c.ctx = ctx;                    

    ci->u2.funcidx = cast_int(savestack(L, c.func));
    ci->u.c.old_errfunc = L->errfunc;
    L->errfunc = func;
    setoah(ci->callstatus, L->allowhook);                                 
    ci->callstatus |= CIST_YPCALL;                                      
    gvtsD_call(L, c.func, nresults);                   
    ci->callstatus &= ~CIST_YPCALL;
    L->errfunc = ci->u.c.old_errfunc;
    status = GVTS_OK;                                           
  }
  adjustresults(L, nresults);
  gvts_unlock(L);
  return status;
}

GVTS_API int gvts_load (gvts_State *L, gvts_Reader reader, void *data,
                      const char *chunkname, const char *mode) {
  ZIO z;
  int status;
  gvts_lock(L);
  if (!chunkname) chunkname = "?";
  gvtsZ_init(L, &z, reader, data);
  status = gvtsD_protectedparser(L, &z, chunkname, mode);
  if (status == GVTS_OK) {                  
    LClosure *f = clLvalue(s2v(L->top.p - 1));                        
    if (f->nupvalues >= 1) {                                

      const TValue *gt = getGtable(L);

      setobj(L, f->upvals[0]->v.p, gt);
      gvtsC_barrier(L, f->upvals[0], gt);
    }
  }
  gvts_unlock(L);
  return status;
}

GVTS_API int gvts_dump (gvts_State *L, gvts_Writer writer, void *data, int strip) {
  int status;
  TValue *o;
  gvts_lock(L);
  api_checknelems(L, 1);
  o = s2v(L->top.p - 1);
  if (isLfunction(o))
    status = gvtsU_dump(L, getproto(o), writer, data, strip);
  else
    status = 1;
  gvts_unlock(L);
  return status;
}

GVTS_API int gvts_status (gvts_State *L) {
  return L->status;
}

GVTS_API int gvts_gc (gvts_State *L, int what, ...) {
  va_list argp;
  int res = 0;
  global_State *g = G(L);
  if (g->gcstp & GCSTPGC)                      
    return -1;                                            
  gvts_lock(L);
  va_start(argp, what);
  switch (what) {
    case GVTS_GCSTOP: {
      g->gcstp = GCSTPUSR;                           
      break;
    }
    case GVTS_GCRESTART: {
      gvtsE_setdebt(g, 0);
      g->gcstp = 0;                                           
      break;
    }
    case GVTS_GCCOLLECT: {
      gvtsC_fullgc(L, 0);
      break;
    }
    case GVTS_GCCOUNT: {

      res = cast_int(gettotalbytes(g) >> 10);
      break;
    }
    case GVTS_GCCOUNTB: {
      res = cast_int(gettotalbytes(g) & 0x3ff);
      break;
    }
    case GVTS_GCSTEP: {
      int data = va_arg(argp, int);
      g_mem debt = 1;                                               
      lu_byte oldstp = g->gcstp;
      g->gcstp = 0;                                                   
      if (data == 0) {
        gvtsE_setdebt(g, 0);                       
        gvtsC_step(L);
      }
      else {                                
        debt = cast(g_mem, data) * 1024 + g->GCdebt;
        gvtsE_setdebt(g, debt);
        gvtsC_checkGC(L);
      }
      g->gcstp = oldstp;                              
      if (debt > 0 && g->gcstate == GCSpause)                     
        res = 1;                 
      break;
    }
    case GVTS_GCSETPAUSE: {
      int data = va_arg(argp, int);
      res = getgcparam(g->gcpause);
      setgcparam(g->gcpause, data);
      break;
    }
    case GVTS_GCSETSTEPMUL: {
      int data = va_arg(argp, int);
      res = getgcparam(g->gcstepmul);
      setgcparam(g->gcstepmul, data);
      break;
    }
    case GVTS_GCISRUNNING: {
      res = gcrunning(g);
      break;
    }
    case GVTS_GCGEN: {
      int minormul = va_arg(argp, int);
      int majormul = va_arg(argp, int);
      res = isdecGCmodegen(g) ? GVTS_GCGEN : GVTS_GCINC;
      if (minormul != 0)
        g->genminormul = minormul;
      if (majormul != 0)
        setgcparam(g->genmajormul, majormul);
      gvtsC_changemode(L, KGC_GEN);
      break;
    }
    case GVTS_GCINC: {
      int pause = va_arg(argp, int);
      int stepmul = va_arg(argp, int);
      int stepsize = va_arg(argp, int);
      res = isdecGCmodegen(g) ? GVTS_GCGEN : GVTS_GCINC;
      if (pause != 0)
        setgcparam(g->gcpause, pause);
      if (stepmul != 0)
        setgcparam(g->gcstepmul, stepmul);
      if (stepsize != 0)
        g->gcstepsize = stepsize;
      gvtsC_changemode(L, KGC_INC);
      break;
    }
    default: res = -1;                      
  }
  va_end(argp);
  gvts_unlock(L);
  return res;
}

GVTS_API int gvts_error (gvts_State *L) {
  TValue *errobj;
  gvts_lock(L);
  errobj = s2v(L->top.p - 1);
  api_checknelems(L, 1);

  if (ttisshrstring(errobj) && eqshrstr(tsvalue(errobj), G(L)->memerrmsg))
    gvtsM_error(L);                            
  else
    gvtsG_errormsg(L);                             

  return 0;                         
}

GVTS_API int gvts_next (gvts_State *L, int idx) {
  Table *t;
  int more;
  gvts_lock(L);
  api_checknelems(L, 1);
  t = gettable(L, idx);
  more = gvtsH_next(L, t, L->top.p - 1);
  if (more) {
    api_incr_top(L);
  }
  else                        
    L->top.p -= 1;                  
  gvts_unlock(L);
  return more;
}

GVTS_API void gvts_toclose (gvts_State *L, int idx) {
  int nresults;
  StkId o;
  gvts_lock(L);
  o = index2stack(L, idx);
  nresults = L->ci->nresults;
  api_check(L, L->tbclist.p < o, "given index below or equal a marked one");
  gvtsF_newtbcupval(L, o);                                       
  if (!hastocloseCfunc(nresults))                                
    L->ci->nresults = codeNresults(nresults);               
  gvts_assert(hastocloseCfunc(L->ci->nresults));
  gvts_unlock(L);
}

GVTS_API void gvts_concat (gvts_State *L, int n) {
  gvts_lock(L);
  api_checknelems(L, n);
  if (n > 0)
    gvtsV_concat(L, n);
  else {                              
    setsvalue2s(L, L->top.p, gvtsS_newlstr(L, "", 0));                         
    api_incr_top(L);
  }
  gvtsC_checkGC(L);
  gvts_unlock(L);
}

GVTS_API void gvts_len (gvts_State *L, int idx) {
  TValue *t;
  gvts_lock(L);
  t = index2value(L, idx);
  gvtsV_objlen(L, L->top.p, t);
  api_incr_top(L);
  gvts_unlock(L);
}

GVTS_API gvts_Alloc gvts_getallocf (gvts_State *L, void **ud) {
  gvts_Alloc f;
  gvts_lock(L);
  if (ud) *ud = G(L)->ud;
  f = G(L)->frealloc;
  gvts_unlock(L);
  return f;
}

GVTS_API void gvts_setallocf (gvts_State *L, gvts_Alloc f, void *ud) {
  gvts_lock(L);
  G(L)->ud = ud;
  G(L)->frealloc = f;
  gvts_unlock(L);
}

void gvts_setwarnf (gvts_State *L, gvts_WarnFunction f, void *ud) {
  gvts_lock(L);
  G(L)->ud_warn = ud;
  G(L)->warnf = f;
  gvts_unlock(L);
}

void gvts_warning (gvts_State *L, const char *msg, int tocont) {
  gvts_lock(L);
  gvtsE_warning(L, msg, tocont);
  gvts_unlock(L);
}

GVTS_API void *gvts_newuserdatauv (gvts_State *L, size_t size, int nuvalue) {
  Udata *u;
  gvts_lock(L);
  api_check(L, 0 <= nuvalue && nuvalue < USHRT_MAX, "invalid value");
  u = gvtsS_newudata(L, size, nuvalue);
  setuvalue(L, s2v(L->top.p), u);
  api_incr_top(L);
  gvtsC_checkGC(L);
  gvts_unlock(L);
  return getudatamem(u);
}

static const char *aux_upvalue (TValue *fi, int n, TValue **val,
                                GCObject **owner) {
  switch (ttypetag(fi)) {
    case GVTS_VCCL: {                 
      CClosure *f = clCvalue(fi);
      if (!(cast_uint(n) - 1u < cast_uint(f->nupvalues)))
        return NULL;                                    
      *val = &f->upvalue[n-1];
      if (owner) *owner = obj2gco(f);
      return "";
    }
    case GVTS_VLCL: {                   
      LClosure *f = clLvalue(fi);
      TString *name;
      Proto *p = f->p;
      if (!(cast_uint(n) - 1u  < cast_uint(p->sizeupvalues)))
        return NULL;                                       
      *val = f->upvals[n-1]->v.p;
      if (owner) *owner = obj2gco(f->upvals[n - 1]);
      name = p->upvalues[n-1].name;
      return (name == NULL) ? "(no name)" : getstr(name);
    }
    default: return NULL;                     
  }
}

GVTS_API const char *gvts_getupvalue (gvts_State *L, int funcindex, int n) {
  const char *name;
  TValue *val = NULL;                         
  gvts_lock(L);
  name = aux_upvalue(index2value(L, funcindex), n, &val, NULL);
  if (name) {
    setobj2s(L, L->top.p, val);
    api_incr_top(L);
  }
  gvts_unlock(L);
  return name;
}

GVTS_API const char *gvts_setupvalue (gvts_State *L, int funcindex, int n) {
  const char *name;
  TValue *val = NULL;                         
  GCObject *owner = NULL;                         
  TValue *fi;
  gvts_lock(L);
  fi = index2value(L, funcindex);
  api_checknelems(L, 1);
  name = aux_upvalue(fi, n, &val, &owner);
  if (name) {
    L->top.p--;
    setobj(L, val, s2v(L->top.p));
    gvtsC_barrier(L, owner, val);
  }
  gvts_unlock(L);
  return name;
}

static UpVal **getupvalref (gvts_State *L, int fidx, int n, LClosure **pf) {
  static const UpVal *const nullup = NULL;
  LClosure *f;
  TValue *fi = index2value(L, fidx);
  api_check(L, ttisLclosure(fi), "Gvts function expected");
  f = clLvalue(fi);
  if (pf) *pf = f;
  if (1 <= n && n <= f->p->sizeupvalues)
    return &f->upvals[n - 1];                               
  else
    return (UpVal**)&nullup;
}

GVTS_API void *gvts_upvalueid (gvts_State *L, int fidx, int n) {
  TValue *fi = index2value(L, fidx);
  switch (ttypetag(fi)) {
    case GVTS_VLCL: {                   
      return *getupvalref(L, fidx, n, NULL);
    }
    case GVTS_VCCL: {                 
      CClosure *f = clCvalue(fi);
      if (1 <= n && n <= f->nupvalues)
        return &f->upvalue[n - 1];

    }                   
    case GVTS_VLCF:
      return NULL;                                          
    default: {
      api_check(L, 0, "function expected");
      return NULL;
    }
  }
}

GVTS_API void gvts_upvaluejoin (gvts_State *L, int fidx1, int n1,
                                            int fidx2, int n2) {
  LClosure *f1;
  UpVal **up1 = getupvalref(L, fidx1, n1, &f1);
  UpVal **up2 = getupvalref(L, fidx2, n2, NULL);
  api_check(L, *up1 != NULL && *up2 != NULL, "invalid upvalue index");
  *up1 = *up2;
  gvtsC_objbarrier(L, f1, *up1);
}
