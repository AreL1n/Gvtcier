#define ltm_c
#define GVTS_CORE

#include "Gprefix.h"

#include <string.h>

#include "gvts.h"

#include "Gdebug.h"
#include "Gdo.h"
#include "Ggc.h"
#include "Gobject.h"
#include "Gstate.h"
#include "Gstring.h"
#include "Gtable.h"
#include "Gtm.h"
#include "Gvm.h"

static const char udatatypename[] = u8"用户数据";

GVTSI_DDEF const char *const gvtsT_typenames_[GVTS_TOTALTYPES] = {
  u8"无值",
  u8"空值", u8"布尔值", udatatypename, u8"数字",
  u8"字符串", u8"表", u8"函数", udatatypename, u8"线程",
  u8"上值", u8"原型"                                               
};

void gvtsT_init (gvts_State *L) {
  static const char *const gvtsT_eventname[] = {                
    "__index", "__newindex",
    "__gc", "__mode", "__len", "__eq",
    "__add", "__sub", "__mul", "__mod", "__pow",
    "__div", "__idiv",
    "__band", "__bor", "__bxor", "__shl", "__shr",
    "__unm", "__bnot", "__lt", "__le",
    "__concat", "__call", "__close"
  };
  int i;
  for (i=0; i<TM_N; i++) {
    G(L)->tmname[i] = gvtsS_new(L, gvtsT_eventname[i]);
    gvtsC_fix(L, obj2gco(G(L)->tmname[i]));                                 
  }
}

const TValue *gvtsT_gettm (Table *events, TMS event, TString *ename) {
  const TValue *tm = gvtsH_getshortstr(events, ename);
  gvts_assert(event <= TM_EQ);
  if (notm(tm)) {                      
    events->flags |= cast_byte(1u<<event);                       
    return NULL;
  }
  else return tm;
}

const TValue *gvtsT_gettmbyobj (gvts_State *L, const TValue *o, TMS event) {
  Table *mt;
  switch (ttype(o)) {
    case GVTS_TTABLE:
      mt = hvalue(o)->metatable;
      break;
    case GVTS_TUSERDATA:
      mt = uvalue(o)->metatable;
      break;
    default:
      mt = G(L)->mt[ttype(o)];
  }
  return (mt ? gvtsH_getshortstr(mt, G(L)->tmname[event]) : &G(L)->nilvalue);
}

const char *gvtsT_objtypename (gvts_State *L, const TValue *o) {
  Table *mt;
  if ((ttistable(o) && (mt = hvalue(o)->metatable) != NULL) ||
      (ttisfulluserdata(o) && (mt = uvalue(o)->metatable) != NULL)) {
    const TValue *name = gvtsH_getshortstr(mt, gvtsS_new(L, "__name"));
    if (ttisstring(name))                             
      return getstr(tsvalue(name));                           
  }
  return ttypename(ttype(o));                                   
}

void gvtsT_callTM (gvts_State *L, const TValue *f, const TValue *p1,
                  const TValue *p2, const TValue *p3) {
  StkId func = L->top.p;
  setobj2s(L, func, f);                                          
  setobj2s(L, func + 1, p1);                    
  setobj2s(L, func + 2, p2);                    
  setobj2s(L, func + 3, p3);                    
  L->top.p = func + 4;

  if (isGvtscode(L->ci))
    gvtsD_call(L, func, 0);
  else
    gvtsD_callnoyield(L, func, 0);
}

void gvtsT_callTMres (gvts_State *L, const TValue *f, const TValue *p1,
                     const TValue *p2, StkId res) {
  ptrdiff_t result = savestack(L, res);
  StkId func = L->top.p;
  setobj2s(L, func, f);                                          
  setobj2s(L, func + 1, p1);                    
  setobj2s(L, func + 2, p2);                    
  L->top.p += 3;

  if (isGvtscode(L->ci))
    gvtsD_call(L, func, 1);
  else
    gvtsD_callnoyield(L, func, 1);
  res = restorestack(L, result);
  setobjs2s(L, res, --L->top.p);                                
}

static int callbinTM (gvts_State *L, const TValue *p1, const TValue *p2,
                      StkId res, TMS event) {
  const TValue *tm = gvtsT_gettmbyobj(L, p1, event);                         
  if (notm(tm))
    tm = gvtsT_gettmbyobj(L, p2, event);                          
  if (notm(tm)) return 0;
  gvtsT_callTMres(L, tm, p1, p2, res);
  return 1;
}

void gvtsT_trybinTM (gvts_State *L, const TValue *p1, const TValue *p2,
                    StkId res, TMS event) {
  if (g_unlikely(!callbinTM(L, p1, p2, res, event))) {
    switch (event) {
      case TM_BAND: case TM_BOR: case TM_BXOR:
      case TM_SHL: case TM_SHR: case TM_BNOT: {
        if (ttisnumber(p1) && ttisnumber(p2))
          gvtsG_tointerror(L, p1, p2);
        else
          gvtsG_opinterror(L, p1, p2, "perform bitwise operation on");
      }

      default:
        gvtsG_opinterror(L, p1, p2, "perform arithmetic on");
    }
  }
}

void gvtsT_tryconcatTM (gvts_State *L) {
  StkId top = L->top.p;
  if (g_unlikely(!callbinTM(L, s2v(top - 2), s2v(top - 1), top - 2,
                               TM_CONCAT)))
    gvtsG_concaterror(L, s2v(top - 2), s2v(top - 1));
}

void gvtsT_trybinassocTM (gvts_State *L, const TValue *p1, const TValue *p2,
                                       int flip, StkId res, TMS event) {
  if (flip)
    gvtsT_trybinTM(L, p2, p1, res, event);
  else
    gvtsT_trybinTM(L, p1, p2, res, event);
}

void gvtsT_trybiniTM (gvts_State *L, const TValue *p1, gvts_Integer i2,
                                   int flip, StkId res, TMS event) {
  TValue aux;
  setivalue(&aux, i2);
  gvtsT_trybinassocTM(L, p1, &aux, flip, res, event);
}

int gvtsT_callorderTM (gvts_State *L, const TValue *p1, const TValue *p2,
                      TMS event) {
  if (callbinTM(L, p1, p2, L->top.p, event))                          
    return !g_isfalse(s2v(L->top.p));
#if defined(GVTS_COMPAT_LT_LE)
  else if (event == TM_LE) {

      L->ci->callstatus |= CIST_LEQ;                                      
      if (callbinTM(L, p2, p1, L->top.p, TM_LT)) {
        L->ci->callstatus ^= CIST_LEQ;                  
        return g_isfalse(s2v(L->top.p));
      }

  }
#endif
  gvtsG_ordererror(L, p1, p2);                           
  return 0;                         
}

int gvtsT_callorderiTM (gvts_State *L, const TValue *p1, int v2,
                       int flip, int isfloat, TMS event) {
  TValue aux; const TValue *p2;
  if (isfloat) {
    setfltvalue(&aux, cast_num(v2));
  }
  else
    setivalue(&aux, v2);
  if (flip) {                                 
    p2 = p1; p1 = &aux;                    
  }
  else
    p2 = &aux;
  return gvtsT_callorderTM(L, p1, p2, event);
}

void gvtsT_adjustvarargs (gvts_State *L, int nfixparams, CallInfo *ci,
                         const Proto *p) {
  int i;
  int actual = cast_int(L->top.p - ci->func.p) - 1;                           
  int nextra = actual - nfixparams;                                 
  ci->u.l.nextraargs = nextra;
  gvtsD_checkstack(L, p->maxstacksize + 1);

  setobjs2s(L, L->top.p++, ci->func.p);

  for (i = 1; i <= nfixparams; i++) {
    setobjs2s(L, L->top.p++, ci->func.p + i);
    setnilvalue(s2v(ci->func.p + i));                                         
  }
  ci->func.p += actual + 1;
  ci->top.p += actual + 1;
  gvts_assert(L->top.p <= ci->top.p && ci->top.p <= L->stack_last.p);
}

void gvtsT_getvarargs (gvts_State *L, CallInfo *ci, StkId where, int wanted) {
  int i;
  int nextra = ci->u.l.nextraargs;
  if (wanted < 0) {
    wanted = nextra;                                         
    checkstackGCp(L, nextra, where);                          
    L->top.p = where + nextra;                                      
  }
  for (i = 0; i < wanted && i < nextra; i++)
    setobjs2s(L, where + i, ci->func.p - nextra + i);
  for (; i < wanted; i++)                                           
    setnilvalue(s2v(where + i));
}
