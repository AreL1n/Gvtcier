#define lvm_c
#define GVTS_CORE

#include "Gprefix.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gvts.h"

#include "Gdebug.h"
#include "Gdo.h"
#include "Gfunc.h"
#include "Ggc.h"
#include "Gobject.h"
#include "Gopcodes.h"
#include "Gstate.h"
#include "Gstring.h"
#include "Gtable.h"
#include "Gtm.h"
#include "Gvm.h"

#if !defined(GVTS_USE_JUMPTABLE)
#if defined(__GNUC__)
#define GVTS_USE_JUMPTABLE	1
#else
#define GVTS_USE_JUMPTABLE	0
#endif
#endif

#define MAXTAGLOOP	2000

#define NBM		(g_floatatt(MANT_DIG))

#if ((((GVTS_MAXINTEGER >> (NBM / 4)) >> (NBM / 4)) >> (NBM / 4)) \
	>> (NBM - (3 * (NBM / 4))))  >  0

#define MAXINTFITSF	((gvts_Unsigned)1 << NBM)

#define g_intfitsf(i)	((MAXINTFITSF + g_castS2U(i)) <= (2 * MAXINTFITSF))

#else                                             

#define g_intfitsf(i)	1

#endif

static int g_strton (const TValue *obj, TValue *result) {
  gvts_assert(obj != result);
  if (!cvt2num(obj))                               
    return 0;
  else {
    TString *st = tsvalue(obj);
    return (gvtsO_str2num(getstr(st), result) == tsslen(st) + 1);
  }
}

int gvtsV_tonumber_ (const TValue *obj, gvts_Number *n) {
  TValue v;
  if (ttisinteger(obj)) {
    *n = cast_num(ivalue(obj));
    return 1;
  }
  else if (g_strton(obj, &v)) {                                   
    *n = nvalue(&v);                                                   
    return 1;
  }
  else
    return 0;                         
}

int gvtsV_flttointeger (gvts_Number n, gvts_Integer *p, F2Imod mode) {
  gvts_Number f = g_floor(n);
  if (n != f) {                              
    if (mode == F2Ieq) return 0;                                            
    else if (mode == F2Iceil)                   
      f += 1;                                                
  }
  return gvts_numbertointeger(f, p);
}

int gvtsV_tointegerns (const TValue *obj, gvts_Integer *p, F2Imod mode) {
  if (ttisfloat(obj))
    return gvtsV_flttointeger(fltvalue(obj), p, mode);
  else if (ttisinteger(obj)) {
    *p = ivalue(obj);
    return 1;
  }
  else
    return 0;
}

int gvtsV_tointeger (const TValue *obj, gvts_Integer *p, F2Imod mode) {
  TValue v;
  if (g_strton(obj, &v))                                               
    obj = &v;                                                      
  return gvtsV_tointegerns(obj, p, mode);
}

static int forlimit (gvts_State *L, gvts_Integer init, const TValue *lim,
                                   gvts_Integer *p, gvts_Integer step) {
  if (!gvtsV_tointeger(lim, p, (step < 0 ? F2Iceil : F2Ifloor))) {

    gvts_Number flim;                               
    if (!tonumber(lim, &flim))                               
      gvtsG_forerror(L, lim, "limit");

    if (gvtsi_numlt(0, flim)) {                                          
      if (step < 0) return 1;                                          
      *p = GVTS_MAXINTEGER;                
    }
    else {                                   
      if (step > 0) return 1;                                             
      *p = GVTS_MININTEGER;                
    }
  }
  return (step > 0 ? init > *p : init < *p);                   
}

static int forprep (gvts_State *L, StkId ra) {
  TValue *pinit = s2v(ra);
  TValue *plimit = s2v(ra + 1);
  TValue *pstep = s2v(ra + 2);
  if (ttisinteger(pinit) && ttisinteger(pstep)) {                    
    gvts_Integer init = ivalue(pinit);
    gvts_Integer step = ivalue(pstep);
    gvts_Integer limit;
    if (step == 0)
      gvtsG_runerror(L, u8"for 循环步长为零");
    setivalue(s2v(ra + 3), init);                        
    if (forlimit(L, init, plimit, &limit, step))
      return 1;                     
    else {                            
      gvts_Unsigned count;
      if (step > 0) {                       
        count = g_castS2U(limit) - g_castS2U(init);
        if (step != 1)                                             
          count /= g_castS2U(step);
      }
      else {                                 
        count = g_castS2U(init) - g_castS2U(limit);

        count /= g_castS2U(-(step + 1)) + 1u;
      }

      setivalue(plimit, g_castU2S(count));
    }
  }
  else {                                    
    gvts_Number init; gvts_Number limit; gvts_Number step;
    if (g_unlikely(!tonumber(plimit, &limit)))
      gvtsG_forerror(L, plimit, u8"结束值");
    if (g_unlikely(!tonumber(pstep, &step)))
      gvtsG_forerror(L, pstep, u8"步长");
    if (g_unlikely(!tonumber(pinit, &init)))
      gvtsG_forerror(L, pinit, u8"初值");
    if (step == 0)
      gvtsG_runerror(L, u8"for 循环步长为零");
    if (gvtsi_numlt(0, step) ? gvtsi_numlt(limit, init)
                            : gvtsi_numlt(init, limit))
      return 1;                     
    else {

      setfltvalue(plimit, limit);
      setfltvalue(pstep, step);
      setfltvalue(s2v(ra), init);                      
      setfltvalue(s2v(ra + 3), init);                        
    }
  }
  return 0;
}

static int floatforloop (StkId ra) {
  gvts_Number step = fltvalue(s2v(ra + 2));
  gvts_Number limit = fltvalue(s2v(ra + 1));
  gvts_Number idx = fltvalue(s2v(ra));                      
  idx = gvtsi_numadd(L, idx, step);                       
  if (gvtsi_numlt(0, step) ? gvtsi_numle(idx, limit)
                          : gvtsi_numle(limit, idx)) {
    chgfltvalue(s2v(ra), idx);                             
    setfltvalue(s2v(ra + 3), idx);                            
    return 1;                 
  }
  else
    return 0;                       
}

void gvtsV_finishget (gvts_State *L, const TValue *t, TValue *key, StkId val,
                      const TValue *slot) {
  int loop;                                       
  const TValue *tm;                  
  for (loop = 0; loop < MAXTAGLOOP; loop++) {
    if (slot == NULL) {                           
      gvts_assert(!ttistable(t));
      tm = gvtsT_gettmbyobj(L, t, TM_INDEX);
      if (g_unlikely(notm(tm)))
        gvtsG_typeerror(L, t, u8"索引");                     

    }
    else {                      
      gvts_assert(isempty(slot));
      tm = fasttm(L, hvalue(t)->metatable, TM_INDEX);                          
      if (tm == NULL) {                      
        setnilvalue(s2v(val));                     
        return;
      }

    }
    if (ttisfunction(tm)) {                                 
      gvtsT_callTMres(L, tm, t, key, val);               
      return;
    }
    t = tm;                                    
    if (gvtsV_fastget(L, t, key, slot, gvtsH_get)) {                   
      setobj2s(L, val, slot);            
      return;
    }

  }
  gvtsG_runerror(L, u8"__index 元方法链过长，可能存在循环");
}

void gvtsV_finishset (gvts_State *L, const TValue *t, TValue *key,
                     TValue *val, const TValue *slot) {
  int loop;                                       
  for (loop = 0; loop < MAXTAGLOOP; loop++) {
    const TValue *tm;                               
    if (slot != NULL) {                       
      Table *h = hvalue(t);                      
      gvts_assert(isempty(slot));                          
      tm = fasttm(L, h->metatable, TM_NEWINDEX);                      
      if (tm == NULL) {                      
        gvtsH_finishset(L, h, key, slot, val);                     
        invalidateTMcache(h);
        gvtsC_barrierback(L, obj2gco(h), val);
        return;
      }

    }
    else {                                     
      tm = gvtsT_gettmbyobj(L, t, TM_NEWINDEX);
      if (g_unlikely(notm(tm)))
        gvtsG_typeerror(L, t, u8"索引");
    }

    if (ttisfunction(tm)) {
      gvtsT_callTM(L, tm, t, key, val);
      return;
    }
    t = tm;                                        
    if (gvtsV_fastget(L, t, key, slot, gvtsH_get)) {
      gvtsV_finishfastset(L, t, slot, val);
      return;            
    }

  }
  gvtsG_runerror(L, u8"__newindex 元方法链过长，可能存在循环");
}

static int g_strcmp (const TString *ts1, const TString *ts2) {
  const char *s1 = getstr(ts1);
  size_t rl1 = tsslen(ts1);                   
  const char *s2 = getstr(ts2);
  size_t rl2 = tsslen(ts2);
  for (;;) {                        
    int temp = strcoll(s1, s2);
    if (temp != 0)                  
      return temp;            
    else {                                      
      size_t zl1 = strlen(s1);                                   
      size_t zl2 = strlen(s2);                                   
      if (zl2 == rl2)                         
        return (zl1 == rl1) ? 0 : 1;                  
      else if (zl1 == rl1)                         
        return -1;                                                     

      zl1++; zl2++;
      s1 += zl1; rl1 -= zl1; s2 += zl2; rl2 -= zl2;
    }
  }
}

g_sinline int LTintfloat (gvts_Integer i, gvts_Number f) {
  if (g_intfitsf(i))
    return gvtsi_numlt(cast_num(i), f);                              
  else {                             
    gvts_Integer fi;
    if (gvtsV_flttointeger(f, &fi, F2Iceil))                    
      return i < fi;                                 
    else                                                       
      return f > 0;                
  }
}

g_sinline int LEintfloat (gvts_Integer i, gvts_Number f) {
  if (g_intfitsf(i))
    return gvtsi_numle(cast_num(i), f);                              
  else {                                
    gvts_Integer fi;
    if (gvtsV_flttointeger(f, &fi, F2Ifloor))                     
      return i <= fi;                                 
    else                                                       
      return f > 0;                
  }
}

g_sinline int LTfloatint (gvts_Number f, gvts_Integer i) {
  if (g_intfitsf(i))
    return gvtsi_numlt(f, cast_num(i));                              
  else {                              
    gvts_Integer fi;
    if (gvtsV_flttointeger(f, &fi, F2Ifloor))                     
      return fi < i;                                 
    else                                                       
      return f < 0;             
  }
}

g_sinline int LEfloatint (gvts_Number f, gvts_Integer i) {
  if (g_intfitsf(i))
    return gvtsi_numle(f, cast_num(i));                              
  else {                               
    gvts_Integer fi;
    if (gvtsV_flttointeger(f, &fi, F2Iceil))                    
      return fi <= i;                                 
    else                                                       
      return f < 0;             
  }
}

g_sinline int LTnum (const TValue *l, const TValue *r) {
  gvts_assert(ttisnumber(l) && ttisnumber(r));
  if (ttisinteger(l)) {
    gvts_Integer li = ivalue(l);
    if (ttisinteger(r))
      return li < ivalue(r);                         
    else                                   
      return LTintfloat(li, fltvalue(r));               
  }
  else {
    gvts_Number lf = fltvalue(l);                         
    if (ttisfloat(r))
      return gvtsi_numlt(lf, fltvalue(r));                      
    else                                   
      return LTfloatint(lf, ivalue(r));
  }
}

g_sinline int LEnum (const TValue *l, const TValue *r) {
  gvts_assert(ttisnumber(l) && ttisnumber(r));
  if (ttisinteger(l)) {
    gvts_Integer li = ivalue(l);
    if (ttisinteger(r))
      return li <= ivalue(r);                         
    else                                   
      return LEintfloat(li, fltvalue(r));                
  }
  else {
    gvts_Number lf = fltvalue(l);                         
    if (ttisfloat(r))
      return gvtsi_numle(lf, fltvalue(r));                      
    else                                   
      return LEfloatint(lf, ivalue(r));
  }
}

static int lessthanothers (gvts_State *L, const TValue *l, const TValue *r) {
  gvts_assert(!ttisnumber(l) || !ttisnumber(r));
  if (ttisstring(l) && ttisstring(r))                         
    return g_strcmp(tsvalue(l), tsvalue(r)) < 0;
  else
    return gvtsT_callorderTM(L, l, r, TM_LT);
}

int gvtsV_lessthan (gvts_State *L, const TValue *l, const TValue *r) {
  if (ttisnumber(l) && ttisnumber(r))                                  
    return LTnum(l, r);
  else return lessthanothers(L, l, r);
}

static int lessequalothers (gvts_State *L, const TValue *l, const TValue *r) {
  gvts_assert(!ttisnumber(l) || !ttisnumber(r));
  if (ttisstring(l) && ttisstring(r))                         
    return g_strcmp(tsvalue(l), tsvalue(r)) <= 0;
  else
    return gvtsT_callorderTM(L, l, r, TM_LE);
}

int gvtsV_lessequal (gvts_State *L, const TValue *l, const TValue *r) {
  if (ttisnumber(l) && ttisnumber(r))                                  
    return LEnum(l, r);
  else return lessequalothers(L, l, r);
}

int gvtsV_equalobj (gvts_State *L, const TValue *t1, const TValue *t2) {
  const TValue *tm;
  if (ttypetag(t1) != ttypetag(t2)) {                             
    if (ttype(t1) != ttype(t2) || ttype(t1) != GVTS_TNUMBER)
      return 0;                                                         
    else {                                           

      gvts_Integer i1, i2;
      return (gvtsV_tointegerns(t1, &i1, F2Ieq) &&
              gvtsV_tointegerns(t2, &i2, F2Ieq) &&
              i1 == i2);
    }
  }

  switch (ttypetag(t1)) {
    case GVTS_VNIL: case GVTS_VFALSE: case GVTS_VTRUE: return 1;
    case GVTS_VNUMINT: return (ivalue(t1) == ivalue(t2));
    case GVTS_VNUMFLT: return gvtsi_numeq(fltvalue(t1), fltvalue(t2));
    case GVTS_VLIGHTUSERDATA: return pvalue(t1) == pvalue(t2);
    case GVTS_VLCF: return fvalue(t1) == fvalue(t2);
    case GVTS_VSHRSTR: return eqshrstr(tsvalue(t1), tsvalue(t2));
    case GVTS_VLNGSTR: return gvtsS_eqlngstr(tsvalue(t1), tsvalue(t2));
    case GVTS_VUSERDATA: {
      if (uvalue(t1) == uvalue(t2)) return 1;
      else if (L == NULL) return 0;
      tm = fasttm(L, uvalue(t1)->metatable, TM_EQ);
      if (tm == NULL)
        tm = fasttm(L, uvalue(t2)->metatable, TM_EQ);
      break;                   
    }
    case GVTS_VTABLE: {
      if (hvalue(t1) == hvalue(t2)) return 1;
      else if (L == NULL) return 0;
      tm = fasttm(L, hvalue(t1)->metatable, TM_EQ);
      if (tm == NULL)
        tm = fasttm(L, hvalue(t2)->metatable, TM_EQ);
      break;                   
    }
    default:
      return gcvalue(t1) == gcvalue(t2);
  }
  if (tm == NULL)              
    return 0;                             
  else {
    gvtsT_callTMres(L, tm, t1, t2, L->top.p);               
    return !g_isfalse(s2v(L->top.p));
  }
}

#define tostring(L,o)  \
	(ttisstring(o) || (cvt2str(o) && (gvtsO_tostring(L, o), 1)))

#define isemptystr(o)	(ttisshrstring(o) && tsvalue(o)->shrlen == 0)

static void copy2buff (StkId top, int n, char *buff) {
  size_t tl = 0;                           
  do {
    TString *st = tsvalue(s2v(top - n));
    size_t l = tsslen(st);                                     
    memcpy(buff + tl, getstr(st), l * sizeof(char));
    tl += l;
  } while (--n > 0);
}

void gvtsV_concat (gvts_State *L, int total) {
  if (total == 1)
    return;                                         
  do {
    StkId top = L->top.p;
    int n = 2;                                                            
    if (!(ttisstring(s2v(top - 2)) || cvt2str(s2v(top - 2))) ||
        !tostring(L, s2v(top - 1)))
      gvtsT_tryconcatTM(L);                            
    else if (isemptystr(s2v(top - 1)))                                
      cast_void(tostring(L, s2v(top - 2)));                               
    else if (isemptystr(s2v(top - 2))) {                                      
      setobjs2s(L, top - 2, top - 1);                            
    }
    else {

      size_t tl = tsslen(tsvalue(s2v(top - 1)));
      TString *ts;

      for (n = 1; n < total && tostring(L, s2v(top - n - 1)); n++) {
        size_t l = tsslen(tsvalue(s2v(top - n - 1)));
        if (g_unlikely(l >= MAX_SIZE - sizeof(TString) - tl)) {
          L->top.p = top - total;                                          
          gvtsG_runerror(L, u8"字符串长度溢出");
        }
        tl += l;
      }
      if (tl <= GVTSI_MAXSHORTLEN) {                                 
        char buff[GVTSI_MAXSHORTLEN];
        copy2buff(top, n, buff);                              
        ts = gvtsS_newlstr(L, buff, tl);
      }
      else {                                                          
        ts = gvtsS_createlngstrobj(L, tl);
        copy2buff(top, n, getlngstr(ts));
      }
      setsvalue2s(L, top - n, ts);                     
    }
    total -= n - 1;                                         
    L->top.p -= n - 1;                                         
  } while (total > 1);                                       
}

void gvtsV_objlen (gvts_State *L, StkId ra, const TValue *rb) {
  const TValue *tm;
  switch (ttypetag(rb)) {
    case GVTS_VTABLE: {
      Table *h = hvalue(rb);
      tm = fasttm(L, h->metatable, TM_LEN);
      if (tm) break;                                           
      setivalue(s2v(ra), gvtsH_getn(h));                          
      return;
    }
    case GVTS_VSHRSTR: {
      setivalue(s2v(ra), tsvalue(rb)->shrlen);
      return;
    }
    case GVTS_VLNGSTR: {
      setivalue(s2v(ra), tsvalue(rb)->u.lnglen);
      return;
    }
    default: {                      
      tm = gvtsT_gettmbyobj(L, rb, TM_LEN);
      if (g_unlikely(notm(tm)))                      
        gvtsG_typeerror(L, rb, u8"取长度");
      break;
    }
  }
  gvtsT_callTMres(L, tm, rb, rb, ra);
}

gvts_Integer gvtsV_idiv (gvts_State *L, gvts_Integer m, gvts_Integer n) {
  if (g_unlikely(g_castS2U(n) + 1u <= 1u)) {                              
    if (n == 0)
      gvtsG_runerror(L, u8"除数为零");
    return intop(-, 0, m);                                                  
  }
  else {
    gvts_Integer q = m / n;                          
    if ((m ^ n) < 0 && m % n != 0)                                            
      q -= 1;                                             
    return q;
  }
}

gvts_Integer gvtsV_mod (gvts_State *L, gvts_Integer m, gvts_Integer n) {
  if (g_unlikely(g_castS2U(n) + 1u <= 1u)) {                              
    if (n == 0)
      gvtsG_runerror(L, u8"对零取模");
    return 0;                                                       
  }
  else {
    gvts_Integer r = m % n;
    if (r != 0 && (r ^ n) < 0)                                            
      r += n;                                             
    return r;
  }
}

gvts_Number gvtsV_modf (gvts_State *L, gvts_Number m, gvts_Number n) {
  gvts_Number r;
  gvtsi_nummod(L, m, n, r);
  return r;
}

#define NBITS	cast_int(sizeof(gvts_Integer) * CHAR_BIT)

gvts_Integer gvtsV_shiftl (gvts_Integer x, gvts_Integer y) {
  if (y < 0) {                    
    if (y <= -NBITS) return 0;
    else return intop(>>, x, -y);
  }
  else {                  
    if (y >= NBITS) return 0;
    else return intop(<<, x, y);
  }
}

static void pushclosure (gvts_State *L, Proto *p, UpVal **encup, StkId base,
                         StkId ra) {
  int nup = p->sizeupvalues;
  Upvaldesc *uv = p->upvalues;
  int i;
  LClosure *ncl = gvtsF_newLclosure(L, nup);
  ncl->p = p;
  setclLvalue2s(L, ra, ncl);                                   
  for (i = 0; i < nup; i++) {                            
    if (uv[i].instack)                                         
      ncl->upvals[i] = gvtsF_findupval(L, base + uv[i].idx);
    else                                           
      ncl->upvals[i] = encup[uv[i].idx];
    gvtsC_objbarrier(L, ncl, ncl->upvals[i]);
  }
}

void gvtsV_finishOp (gvts_State *L) {
  CallInfo *ci = L->ci;
  StkId base = ci->func.p + 1;
  Instruction inst = *(ci->u.l.savedpc - 1);                               
  OpCode op = GET_OPCODE(inst);
  switch (op) {                            
    case OP_MMBIN: case OP_MMBINI: case OP_MMBINK: {
      setobjs2s(L, base + GETARG_A(*(ci->u.l.savedpc - 2)), --L->top.p);
      break;
    }
    case OP_UNM: case OP_BNOT: case OP_LEN:
    case OP_GETTABUP: case OP_GETTABLE: case OP_GETI:
    case OP_GETFIELD: case OP_SELF: {
      setobjs2s(L, base + GETARG_A(inst), --L->top.p);
      break;
    }
    case OP_LT: case OP_LE:
    case OP_LTI: case OP_LEI:
    case OP_GTI: case OP_GEI:
    case OP_EQ: {                                                
      int res = !g_isfalse(s2v(L->top.p - 1));
      L->top.p--;
#if defined(GVTS_COMPAT_LT_LE)
      if (ci->callstatus & CIST_LEQ) {                               
        ci->callstatus ^= CIST_LEQ;                  
        res = !res;                     
      }
#endif
      gvts_assert(GET_OPCODE(*ci->u.l.savedpc) == OP_JMP);
      if (res != GETARG_k(inst))                         
        ci->u.l.savedpc++;                             
      break;
    }
    case OP_CONCAT: {
      StkId top = L->top.p - 1;                                              
      int a = GETARG_A(inst);                                        
      int total = cast_int(top - 1 - (base + a));                          
      setobjs2s(L, top - 2, top);                                        
      L->top.p = top - 1;                                                
      gvtsV_concat(L, total);                                     
      break;
    }
    case OP_CLOSE: {                                 
      ci->u.l.savedpc--;                                               
      break;
    }
    case OP_RETURN: {                                 
      StkId ra = base + GETARG_A(inst);

      L->top.p = ra + ci->u2.nres;

      ci->u.l.savedpc--;
      break;
    }
    default: {

      gvts_assert(op == OP_TFORCALL || op == OP_CALL ||
           op == OP_TAILCALL || op == OP_SETTABUP || op == OP_SETTABLE ||
           op == OP_SETI || op == OP_SETFIELD);
      break;
    }
  }
}

#define g_addi(L,a,b)	intop(+, a, b)
#define g_subi(L,a,b)	intop(-, a, b)
#define g_muli(L,a,b)	intop(*, a, b)
#define g_band(a,b)	intop(&, a, b)
#define g_bor(a,b)	intop(|, a, b)
#define g_bxor(a,b)	intop(^, a, b)

#define g_lti(a,b)	(a < b)
#define g_lei(a,b)	(a <= b)
#define g_gti(a,b)	(a > b)
#define g_gei(a,b)	(a >= b)

#define op_arithI(L,iop,fop) {  \
  StkId ra = RA(i); \
  TValue *v1 = vRB(i);  \
  int imm = GETARG_sC(i);  \
  if (ttisinteger(v1)) {  \
    gvts_Integer iv1 = ivalue(v1);  \
    pc++; setivalue(s2v(ra), iop(L, iv1, imm));  \
  }  \
  else if (ttisfloat(v1)) {  \
    gvts_Number nb = fltvalue(v1);  \
    gvts_Number fimm = cast_num(imm);  \
    pc++; setfltvalue(s2v(ra), fop(L, nb, fimm)); \
  }}

#define op_arithf_aux(L,v1,v2,fop) {  \
  gvts_Number n1; gvts_Number n2;  \
  if (tonumberns(v1, n1) && tonumberns(v2, n2)) {  \
    pc++; setfltvalue(s2v(ra), fop(L, n1, n2));  \
  }}

#define op_arithf(L,fop) {  \
  StkId ra = RA(i); \
  TValue *v1 = vRB(i);  \
  TValue *v2 = vRC(i);  \
  op_arithf_aux(L, v1, v2, fop); }

#define op_arithfK(L,fop) {  \
  StkId ra = RA(i); \
  TValue *v1 = vRB(i);  \
  TValue *v2 = KC(i); gvts_assert(ttisnumber(v2));  \
  op_arithf_aux(L, v1, v2, fop); }

#define op_arith_aux(L,v1,v2,iop,fop) {  \
  StkId ra = RA(i); \
  if (ttisinteger(v1) && ttisinteger(v2)) {  \
    gvts_Integer i1 = ivalue(v1); gvts_Integer i2 = ivalue(v2);  \
    pc++; setivalue(s2v(ra), iop(L, i1, i2));  \
  }  \
  else op_arithf_aux(L, v1, v2, fop); }

#define op_arith(L,iop,fop) {  \
  TValue *v1 = vRB(i);  \
  TValue *v2 = vRC(i);  \
  op_arith_aux(L, v1, v2, iop, fop); }

#define op_arithK(L,iop,fop) {  \
  TValue *v1 = vRB(i);  \
  TValue *v2 = KC(i); gvts_assert(ttisnumber(v2));  \
  op_arith_aux(L, v1, v2, iop, fop); }

#define op_bitwiseK(L,op) {  \
  StkId ra = RA(i); \
  TValue *v1 = vRB(i);  \
  TValue *v2 = KC(i);  \
  gvts_Integer i1;  \
  gvts_Integer i2 = ivalue(v2);  \
  if (tointegerns(v1, &i1)) {  \
    pc++; setivalue(s2v(ra), op(i1, i2));  \
  }}

#define op_bitwise(L,op) {  \
  StkId ra = RA(i); \
  TValue *v1 = vRB(i);  \
  TValue *v2 = vRC(i);  \
  gvts_Integer i1; gvts_Integer i2;  \
  if (tointegerns(v1, &i1) && tointegerns(v2, &i2)) {  \
    pc++; setivalue(s2v(ra), op(i1, i2));  \
  }}

#define op_order(L,opi,opn,other) {  \
  StkId ra = RA(i); \
  int cond;  \
  TValue *rb = vRB(i);  \
  if (ttisinteger(s2v(ra)) && ttisinteger(rb)) {  \
    gvts_Integer ia = ivalue(s2v(ra));  \
    gvts_Integer ib = ivalue(rb);  \
    cond = opi(ia, ib);  \
  }  \
  else if (ttisnumber(s2v(ra)) && ttisnumber(rb))  \
    cond = opn(s2v(ra), rb);  \
  else  \
    Protect(cond = other(L, s2v(ra), rb));  \
  docondjump(); }

#define op_orderI(L,opi,opf,inv,tm) {  \
  StkId ra = RA(i); \
  int cond;  \
  int im = GETARG_sB(i);  \
  if (ttisinteger(s2v(ra)))  \
    cond = opi(ivalue(s2v(ra)), im);  \
  else if (ttisfloat(s2v(ra))) {  \
    gvts_Number fa = fltvalue(s2v(ra));  \
    gvts_Number fim = cast_num(im);  \
    cond = opf(fa, fim);  \
  }  \
  else {  \
    int isf = GETARG_C(i);  \
    Protect(cond = gvtsT_callorderiTM(L, s2v(ra), im, inv, isf, tm));  \
  }  \
  docondjump(); }

#define RA(i)	(base+GETARG_A(i))
#define RB(i)	(base+GETARG_B(i))
#define vRB(i)	s2v(RB(i))
#define KB(i)	(k+GETARG_B(i))
#define RC(i)	(base+GETARG_C(i))
#define vRC(i)	s2v(RC(i))
#define KC(i)	(k+GETARG_C(i))
#define RKC(i)	((TESTARG_k(i)) ? k + GETARG_C(i) : s2v(base + GETARG_C(i)))

#define updatetrap(ci)  (trap = ci->u.l.trap)

#define updatebase(ci)	(base = ci->func.p + 1)

#define updatestack(ci)  \
	{ if (g_unlikely(trap)) { updatebase(ci); ra = RA(i); } }

#define dojump(ci,i,e)	{ pc += GETARG_sJ(i) + e; updatetrap(ci); }

#define donextjump(ci)	{ Instruction ni = *pc; dojump(ci, ni, 1); }

#define docondjump()	if (cond != GETARG_k(i)) pc++; else donextjump(ci);

#define savepc(L)	(ci->u.l.savedpc = pc)

#define savestate(L,ci)		(savepc(L), L->top.p = ci->top.p)

#define Protect(exp)  (savestate(L,ci), (exp), updatetrap(ci))

#define ProtectNT(exp)  (savepc(L), (exp), updatetrap(ci))

#define halfProtect(exp)  (savestate(L,ci), (exp))

#define checkGC(L,c)  \
	{ gvtsC_condGC(L, (savepc(L), L->top.p = (c)), \
                         updatetrap(ci)); \
           gvtsi_threadyield(L); }

#define vmfetch()	{ \
  if (g_unlikely(trap)) {                                     \
    trap = gvtsG_traceexec(L, pc);                     \
    updatebase(ci);                      \
  } \
  i = *(pc++); \
}

#define vmdispatch(o)	switch(o)
#define vmcase(l)	case l:
#define vmbreak		break

void gvtsV_execute (gvts_State *L, CallInfo *ci) {
  LClosure *cl;
  TValue *k;
  StkId base;
  const Instruction *pc;
  int trap;
#if GVTS_USE_JUMPTABLE
#include "Gjumptab.h"
#endif
 startfunc:
  trap = L->hookmask;
 returning:                        
  cl = ci_func(ci);
  k = cl->p->k;
  pc = ci->u.l.savedpc;
  if (g_unlikely(trap))
    trap = gvtsG_tracecall(L);
  base = ci->func.p + 1;

  for (;;) {
    Instruction i;                                  
    vmfetch();
    #if 0

      printf("line: %d\n", gvtsG_getfuncline(cl->p, pcRel(pc, cl->p)));
    #endif
    gvts_assert(base == ci->func.p + 1);
    gvts_assert(base <= L->top.p && L->top.p <= L->stack_last.p);

    gvts_assert(isIT(i) || (cast_void(L->top.p = base), 1));
    vmdispatch (GET_OPCODE(i)) {
      vmcase(OP_MOVE) {
        StkId ra = RA(i);
        setobjs2s(L, ra, RB(i));
        vmbreak;
      }
      vmcase(OP_LOADI) {
        StkId ra = RA(i);
        gvts_Integer b = GETARG_sBx(i);
        setivalue(s2v(ra), b);
        vmbreak;
      }
      vmcase(OP_LOADF) {
        StkId ra = RA(i);
        int b = GETARG_sBx(i);
        setfltvalue(s2v(ra), cast_num(b));
        vmbreak;
      }
      vmcase(OP_LOADK) {
        StkId ra = RA(i);
        TValue *rb = k + GETARG_Bx(i);
        setobj2s(L, ra, rb);
        vmbreak;
      }
      vmcase(OP_LOADKX) {
        StkId ra = RA(i);
        TValue *rb;
        rb = k + GETARG_Ax(*pc); pc++;
        setobj2s(L, ra, rb);
        vmbreak;
      }
      vmcase(OP_LOADFALSE) {
        StkId ra = RA(i);
        setbfvalue(s2v(ra));
        vmbreak;
      }
      vmcase(OP_LFALSESKIP) {
        StkId ra = RA(i);
        setbfvalue(s2v(ra));
        pc++;                             
        vmbreak;
      }
      vmcase(OP_LOADTRUE) {
        StkId ra = RA(i);
        setbtvalue(s2v(ra));
        vmbreak;
      }
      vmcase(OP_LOADNIL) {
        StkId ra = RA(i);
        int b = GETARG_B(i);
        do {
          setnilvalue(s2v(ra++));
        } while (b--);
        vmbreak;
      }
      vmcase(OP_GETUPVAL) {
        StkId ra = RA(i);
        int b = GETARG_B(i);
        setobj2s(L, ra, cl->upvals[b]->v.p);
        vmbreak;
      }
      vmcase(OP_SETUPVAL) {
        StkId ra = RA(i);
        UpVal *uv = cl->upvals[GETARG_B(i)];
        setobj(L, uv->v.p, s2v(ra));
        gvtsC_barrier(L, uv, s2v(ra));
        vmbreak;
      }
      vmcase(OP_GETTABUP) {
        StkId ra = RA(i);
        const TValue *slot;
        TValue *upval = cl->upvals[GETARG_B(i)]->v.p;
        TValue *rc = KC(i);
        TString *key = tsvalue(rc);                                  
        if (gvtsV_fastget(L, upval, key, slot, gvtsH_getshortstr)) {
          setobj2s(L, ra, slot);
        }
        else
          Protect(gvtsV_finishget(L, upval, rc, ra, slot));
        vmbreak;
      }
      vmcase(OP_GETTABLE) {
        StkId ra = RA(i);
        const TValue *slot;
        TValue *rb = vRB(i);
        TValue *rc = vRC(i);
        gvts_Unsigned n;
        if (ttisinteger(rc)                                
            ? (cast_void(n = ivalue(rc)), gvtsV_fastgeti(L, rb, n, slot))
            : gvtsV_fastget(L, rb, rc, slot, gvtsH_get)) {
          setobj2s(L, ra, slot);
        }
        else
          Protect(gvtsV_finishget(L, rb, rc, ra, slot));
        vmbreak;
      }
      vmcase(OP_GETI) {
        StkId ra = RA(i);
        const TValue *slot;
        TValue *rb = vRB(i);
        int c = GETARG_C(i);
        if (gvtsV_fastgeti(L, rb, c, slot)) {
          setobj2s(L, ra, slot);
        }
        else {
          TValue key;
          setivalue(&key, c);
          Protect(gvtsV_finishget(L, rb, &key, ra, slot));
        }
        vmbreak;
      }
      vmcase(OP_GETFIELD) {
        StkId ra = RA(i);
        const TValue *slot;
        TValue *rb = vRB(i);
        TValue *rc = KC(i);
        TString *key = tsvalue(rc);                                  
        if (gvtsV_fastget(L, rb, key, slot, gvtsH_getshortstr)) {
          setobj2s(L, ra, slot);
        }
        else
          Protect(gvtsV_finishget(L, rb, rc, ra, slot));
        vmbreak;
      }
      vmcase(OP_SETTABUP) {
        const TValue *slot;
        TValue *upval = cl->upvals[GETARG_A(i)]->v.p;
        TValue *rb = KB(i);
        TValue *rc = RKC(i);
        TString *key = tsvalue(rb);                                  
        if (gvtsV_fastget(L, upval, key, slot, gvtsH_getshortstr)) {
          gvtsV_finishfastset(L, upval, slot, rc);
        }
        else
          Protect(gvtsV_finishset(L, upval, rb, rc, slot));
        vmbreak;
      }
      vmcase(OP_SETTABLE) {
        StkId ra = RA(i);
        const TValue *slot;
        TValue *rb = vRB(i);                              
        TValue *rc = RKC(i);             
        gvts_Unsigned n;
        if (ttisinteger(rb)                                
            ? (cast_void(n = ivalue(rb)), gvtsV_fastgeti(L, s2v(ra), n, slot))
            : gvtsV_fastget(L, s2v(ra), rb, slot, gvtsH_get)) {
          gvtsV_finishfastset(L, s2v(ra), slot, rc);
        }
        else
          Protect(gvtsV_finishset(L, s2v(ra), rb, rc, slot));
        vmbreak;
      }
      vmcase(OP_SETI) {
        StkId ra = RA(i);
        const TValue *slot;
        int c = GETARG_B(i);
        TValue *rc = RKC(i);
        if (gvtsV_fastgeti(L, s2v(ra), c, slot)) {
          gvtsV_finishfastset(L, s2v(ra), slot, rc);
        }
        else {
          TValue key;
          setivalue(&key, c);
          Protect(gvtsV_finishset(L, s2v(ra), &key, rc, slot));
        }
        vmbreak;
      }
      vmcase(OP_SETFIELD) {
        StkId ra = RA(i);
        const TValue *slot;
        TValue *rb = KB(i);
        TValue *rc = RKC(i);
        TString *key = tsvalue(rb);                                  
        if (gvtsV_fastget(L, s2v(ra), key, slot, gvtsH_getshortstr)) {
          gvtsV_finishfastset(L, s2v(ra), slot, rc);
        }
        else
          Protect(gvtsV_finishset(L, s2v(ra), rb, rc, slot));
        vmbreak;
      }
      vmcase(OP_NEWTABLE) {
        StkId ra = RA(i);
        int b = GETARG_B(i);                           
        int c = GETARG_C(i);                  
        Table *t;
        if (b > 0)
          b = 1 << (b - 1);                         
        gvts_assert((!TESTARG_k(i)) == (GETARG_Ax(*pc) == 0));
        if (TESTARG_k(i))                                
          c += GETARG_Ax(*pc) * (MAXARG_C + 1);                      
        pc++;                           
        L->top.p = ra + 1;                                           
        t = gvtsH_new(L);                         
        sethvalue2s(L, ra, t);
        if (b != 0 || c != 0)
          gvtsH_resize(L, t, c, b);            
        checkGC(L, ra + 1);
        vmbreak;
      }
      vmcase(OP_SELF) {
        StkId ra = RA(i);
        const TValue *slot;
        TValue *rb = vRB(i);
        TValue *rc = RKC(i);
        TString *key = tsvalue(rc);                            
        setobj2s(L, ra + 1, rb);
        if (gvtsV_fastget(L, rb, key, slot, gvtsH_getstr)) {
          setobj2s(L, ra, slot);
        }
        else
          Protect(gvtsV_finishget(L, rb, rc, ra, slot));
        vmbreak;
      }
      vmcase(OP_ADDI) {
        op_arithI(L, g_addi, gvtsi_numadd);
        vmbreak;
      }
      vmcase(OP_ADDK) {
        op_arithK(L, g_addi, gvtsi_numadd);
        vmbreak;
      }
      vmcase(OP_SUBK) {
        op_arithK(L, g_subi, gvtsi_numsub);
        vmbreak;
      }
      vmcase(OP_MULK) {
        op_arithK(L, g_muli, gvtsi_nummul);
        vmbreak;
      }
      vmcase(OP_MODK) {
        savestate(L, ci);                                
        op_arithK(L, gvtsV_mod, gvtsV_modf);
        vmbreak;
      }
      vmcase(OP_POWK) {
        op_arithfK(L, gvtsi_numpow);
        vmbreak;
      }
      vmcase(OP_DIVK) {
        op_arithfK(L, gvtsi_numdiv);
        vmbreak;
      }
      vmcase(OP_IDIVK) {
        savestate(L, ci);                                
        op_arithK(L, gvtsV_idiv, gvtsi_numidiv);
        vmbreak;
      }
      vmcase(OP_BANDK) {
        op_bitwiseK(L, g_band);
        vmbreak;
      }
      vmcase(OP_BORK) {
        op_bitwiseK(L, g_bor);
        vmbreak;
      }
      vmcase(OP_BXORK) {
        op_bitwiseK(L, g_bxor);
        vmbreak;
      }
      vmcase(OP_SHRI) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        int ic = GETARG_sC(i);
        gvts_Integer ib;
        if (tointegerns(rb, &ib)) {
          pc++; setivalue(s2v(ra), gvtsV_shiftl(ib, -ic));
        }
        vmbreak;
      }
      vmcase(OP_SHLI) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        int ic = GETARG_sC(i);
        gvts_Integer ib;
        if (tointegerns(rb, &ib)) {
          pc++; setivalue(s2v(ra), gvtsV_shiftl(ic, ib));
        }
        vmbreak;
      }
      vmcase(OP_ADD) {
        op_arith(L, g_addi, gvtsi_numadd);
        vmbreak;
      }
      vmcase(OP_SUB) {
        op_arith(L, g_subi, gvtsi_numsub);
        vmbreak;
      }
      vmcase(OP_MUL) {
        op_arith(L, g_muli, gvtsi_nummul);
        vmbreak;
      }
      vmcase(OP_MOD) {
        savestate(L, ci);                                
        op_arith(L, gvtsV_mod, gvtsV_modf);
        vmbreak;
      }
      vmcase(OP_POW) {
        op_arithf(L, gvtsi_numpow);
        vmbreak;
      }
      vmcase(OP_DIV) {                                           
        op_arithf(L, gvtsi_numdiv);
        vmbreak;
      }
      vmcase(OP_IDIV) {                      
        savestate(L, ci);                                
        op_arith(L, gvtsV_idiv, gvtsi_numidiv);
        vmbreak;
      }
      vmcase(OP_BAND) {
        op_bitwise(L, g_band);
        vmbreak;
      }
      vmcase(OP_BOR) {
        op_bitwise(L, g_bor);
        vmbreak;
      }
      vmcase(OP_BXOR) {
        op_bitwise(L, g_bxor);
        vmbreak;
      }
      vmcase(OP_SHR) {
        op_bitwise(L, gvtsV_shiftr);
        vmbreak;
      }
      vmcase(OP_SHL) {
        op_bitwise(L, gvtsV_shiftl);
        vmbreak;
      }
      vmcase(OP_MMBIN) {
        StkId ra = RA(i);
        Instruction pi = *(pc - 2);                                  
        TValue *rb = vRB(i);
        TMS tm = (TMS)GETARG_C(i);
        StkId result = RA(pi);
        gvts_assert(OP_ADD <= GET_OPCODE(pi) && GET_OPCODE(pi) <= OP_SHR);
        Protect(gvtsT_trybinTM(L, s2v(ra), rb, result, tm));
        vmbreak;
      }
      vmcase(OP_MMBINI) {
        StkId ra = RA(i);
        Instruction pi = *(pc - 2);                                  
        int imm = GETARG_sB(i);
        TMS tm = (TMS)GETARG_C(i);
        int flip = GETARG_k(i);
        StkId result = RA(pi);
        Protect(gvtsT_trybiniTM(L, s2v(ra), imm, flip, result, tm));
        vmbreak;
      }
      vmcase(OP_MMBINK) {
        StkId ra = RA(i);
        Instruction pi = *(pc - 2);                                  
        TValue *imm = KB(i);
        TMS tm = (TMS)GETARG_C(i);
        int flip = GETARG_k(i);
        StkId result = RA(pi);
        Protect(gvtsT_trybinassocTM(L, s2v(ra), imm, flip, result, tm));
        vmbreak;
      }
      vmcase(OP_UNM) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        gvts_Number nb;
        if (ttisinteger(rb)) {
          gvts_Integer ib = ivalue(rb);
          setivalue(s2v(ra), intop(-, 0, ib));
        }
        else if (tonumberns(rb, nb)) {
          setfltvalue(s2v(ra), gvtsi_numunm(L, nb));
        }
        else
          Protect(gvtsT_trybinTM(L, rb, rb, ra, TM_UNM));
        vmbreak;
      }
      vmcase(OP_BNOT) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        gvts_Integer ib;
        if (tointegerns(rb, &ib)) {
          setivalue(s2v(ra), intop(^, ~g_castS2U(0), ib));
        }
        else
          Protect(gvtsT_trybinTM(L, rb, rb, ra, TM_BNOT));
        vmbreak;
      }
      vmcase(OP_NOT) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        if (g_isfalse(rb))
          setbtvalue(s2v(ra));
        else
          setbfvalue(s2v(ra));
        vmbreak;
      }
      vmcase(OP_LEN) {
        StkId ra = RA(i);
        Protect(gvtsV_objlen(L, ra, vRB(i)));
        vmbreak;
      }
      vmcase(OP_CONCAT) {
        StkId ra = RA(i);
        int n = GETARG_B(i);                                         
        L->top.p = ra + n;                                       
        ProtectNT(gvtsV_concat(L, n));
        checkGC(L, L->top.p);                                        
        vmbreak;
      }
      vmcase(OP_CLOSE) {
        StkId ra = RA(i);
        Protect(gvtsF_close(L, ra, GVTS_OK, 1));
        vmbreak;
      }
      vmcase(OP_TBC) {
        StkId ra = RA(i);

        halfProtect(gvtsF_newtbcupval(L, ra));
        vmbreak;
      }
      vmcase(OP_JMP) {
        dojump(ci, i, 0);
        vmbreak;
      }
      vmcase(OP_EQ) {
        StkId ra = RA(i);
        int cond;
        TValue *rb = vRB(i);
        Protect(cond = gvtsV_equalobj(L, s2v(ra), rb));
        docondjump();
        vmbreak;
      }
      vmcase(OP_LT) {
        op_order(L, g_lti, LTnum, lessthanothers);
        vmbreak;
      }
      vmcase(OP_LE) {
        op_order(L, g_lei, LEnum, lessequalothers);
        vmbreak;
      }
      vmcase(OP_EQK) {
        StkId ra = RA(i);
        TValue *rb = KB(i);

        int cond = gvtsV_rawequalobj(s2v(ra), rb);
        docondjump();
        vmbreak;
      }
      vmcase(OP_EQI) {
        StkId ra = RA(i);
        int cond;
        int im = GETARG_sB(i);
        if (ttisinteger(s2v(ra)))
          cond = (ivalue(s2v(ra)) == im);
        else if (ttisfloat(s2v(ra)))
          cond = gvtsi_numeq(fltvalue(s2v(ra)), cast_num(im));
        else
          cond = 0;                                               
        docondjump();
        vmbreak;
      }
      vmcase(OP_LTI) {
        op_orderI(L, g_lti, gvtsi_numlt, 0, TM_LT);
        vmbreak;
      }
      vmcase(OP_LEI) {
        op_orderI(L, g_lei, gvtsi_numle, 0, TM_LE);
        vmbreak;
      }
      vmcase(OP_GTI) {
        op_orderI(L, g_gti, gvtsi_numgt, 1, TM_LT);
        vmbreak;
      }
      vmcase(OP_GEI) {
        op_orderI(L, g_gei, gvtsi_numge, 1, TM_LE);
        vmbreak;
      }
      vmcase(OP_TEST) {
        StkId ra = RA(i);
        int cond = !g_isfalse(s2v(ra));
        docondjump();
        vmbreak;
      }
      vmcase(OP_TESTSET) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        if (g_isfalse(rb) == GETARG_k(i))
          pc++;
        else {
          setobj2s(L, ra, rb);
          donextjump(ci);
        }
        vmbreak;
      }
      vmcase(OP_CALL) {
        StkId ra = RA(i);
        CallInfo *newci;
        int b = GETARG_B(i);
        int nresults = GETARG_C(i) - 1;
        if (b != 0)                                  
          L->top.p = ra + b;                                       

        savepc(L);                         
        if ((newci = gvtsD_precall(L, ra, nresults)) == NULL)
          updatetrap(ci);                                       
        else {                                                   
          ci = newci;
          goto startfunc;
        }
        vmbreak;
      }
      vmcase(OP_TAILCALL) {
        StkId ra = RA(i);
        int b = GETARG_B(i);                                          
        int n;                                                   
        int nparams1 = GETARG_C(i);

        int delta = (nparams1) ? ci->u.l.nextraargs + nparams1 : 0;
        if (b != 0)
          L->top.p = ra + b;
        else                                    
          b = cast_int(L->top.p - ra);
        savepc(ci);                                           
        if (TESTARG_k(i)) {
          gvtsF_closeupval(L, base);                                        
          gvts_assert(L->tbclist.p < base);                                
          gvts_assert(base == ci->func.p + 1);
        }
        if ((n = gvtsD_pretailcall(L, ci, ra, b, delta)) < 0)                     
          goto startfunc;                          
        else {                   
          ci->func.p -= delta;                                  
          gvtsD_poscall(L, ci, n);                     
          updatetrap(ci);                                       
          goto ret;                                          
        }
      }
      vmcase(OP_RETURN) {
        StkId ra = RA(i);
        int n = GETARG_B(i) - 1;                         
        int nparams1 = GETARG_C(i);
        if (n < 0)                  
          n = cast_int(L->top.p - ra);                             
        savepc(ci);
        if (TESTARG_k(i)) {                                   
          ci->u2.nres = n;                              
          if (L->top.p < ci->top.p)
            L->top.p = ci->top.p;
          gvtsF_close(L, base, CLOSEKTOP, 1);
          updatetrap(ci);
          updatestack(ci);
        }
        if (nparams1)                        
          ci->func.p -= ci->u.l.nextraargs + nparams1;
        L->top.p = ra + n;                                   
        gvtsD_poscall(L, ci, n);
        updatetrap(ci);                                       
        goto ret;
      }
      vmcase(OP_RETURN0) {
        if (g_unlikely(L->hookmask)) {
          StkId ra = RA(i);
          L->top.p = ra;
          savepc(ci);
          gvtsD_poscall(L, ci, 0);                   
          trap = 1;
        }
        else {                             
          int nres;
          L->ci = ci->previous;                      
          L->top.p = base - 1;
          for (nres = ci->nresults; g_unlikely(nres > 0); nres--)
            setnilvalue(s2v(L->top.p++));                           
        }
        goto ret;
      }
      vmcase(OP_RETURN1) {
        if (g_unlikely(L->hookmask)) {
          StkId ra = RA(i);
          L->top.p = ra + 1;
          savepc(ci);
          gvtsD_poscall(L, ci, 1);                   
          trap = 1;
        }
        else {                             
          int nres = ci->nresults;
          L->ci = ci->previous;                      
          if (nres == 0)
            L->top.p = base - 1;                            
          else {
            StkId ra = RA(i);
            setobjs2s(L, base - 1, ra);                            
            L->top.p = base;
            for (; g_unlikely(nres > 1); nres--)
              setnilvalue(s2v(L->top.p++));                                
          }
        }
       ret:                                  
        if (ci->callstatus & CIST_FRESH)
          return;                      
        else {
          ci = ci->previous;
          goto returning;                                             
        }
      }
      vmcase(OP_FORLOOP) {
        StkId ra = RA(i);
        if (ttisinteger(s2v(ra + 2))) {                     
          gvts_Unsigned count = g_castS2U(ivalue(s2v(ra + 1)));
          if (count > 0) {                              
            gvts_Integer step = ivalue(s2v(ra + 2));
            gvts_Integer idx = ivalue(s2v(ra));                      
            chgivalue(s2v(ra + 1), count - 1);                      
            idx = intop(+, idx, step);                         
            chgivalue(s2v(ra), idx);                             
            setivalue(s2v(ra + 3), idx);                            
            pc -= GETARG_Bx(i);                 
          }
        }
        else if (floatforloop(ra))                  
          pc -= GETARG_Bx(i);                 
        updatetrap(ci);                                         
        vmbreak;
      }
      vmcase(OP_FORPREP) {
        StkId ra = RA(i);
        savestate(L, ci);                         
        if (forprep(L, ra))
          pc += GETARG_Bx(i) + 1;                     
        vmbreak;
      }
      vmcase(OP_TFORPREP) {
       StkId ra = RA(i);

        halfProtect(gvtsF_newtbcupval(L, ra + 3));
        pc += GETARG_Bx(i);
        i = *(pc++);                              
        gvts_assert(GET_OPCODE(i) == OP_TFORCALL && ra == RA(i));
        goto g_tforcall;
      }
      vmcase(OP_TFORCALL) {
       g_tforcall: {
        StkId ra = RA(i);

        memcpy(ra + 4, ra, 3 * sizeof(*ra));
        L->top.p = ra + 4 + 3;
        ProtectNT(gvtsD_call(L, ra + 4, GETARG_C(i)));                   
        updatestack(ci);                              
        i = *(pc++);                              
        gvts_assert(GET_OPCODE(i) == OP_TFORLOOP && ra == RA(i));
        goto g_tforloop;
      }}
      vmcase(OP_TFORLOOP) {
       g_tforloop: {
        StkId ra = RA(i);
        if (!ttisnil(s2v(ra + 4))) {                      
          setobjs2s(L, ra + 2, ra + 4);                             
          pc -= GETARG_Bx(i);                 
        }
        vmbreak;
      }}
      vmcase(OP_SETLIST) {
        StkId ra = RA(i);
        int n = GETARG_B(i);
        unsigned int last = GETARG_C(i);
        Table *h = hvalue(s2v(ra));
        if (n == 0)
          n = cast_int(L->top.p - ra) - 1;                         
        else
          L->top.p = ci->top.p;                                           
        last += n;
        if (TESTARG_k(i)) {
          last += GETARG_Ax(*pc) * (MAXARG_C + 1);
          pc++;
        }
        if (last > gvtsH_realasize(h))                         
          gvtsH_resizearray(L, h, last);                              
        for (; n > 0; n--) {
          TValue *val = s2v(ra + n);
          setobj2t(L, &h->array[last - 1], val);
          last--;
          gvtsC_barrierback(L, obj2gco(h), val);
        }
        vmbreak;
      }
      vmcase(OP_CLOSURE) {
        StkId ra = RA(i);
        Proto *p = cl->p->p[GETARG_Bx(i)];
        halfProtect(pushclosure(L, p, cl->upvals, base, ra));
        checkGC(L, ra + 1);
        vmbreak;
      }
      vmcase(OP_VARARG) {
        StkId ra = RA(i);
        int n = GETARG_C(i) - 1;                        
        Protect(gvtsT_getvarargs(L, ci, ra, n));
        vmbreak;
      }
      vmcase(OP_VARARGPREP) {
        ProtectNT(gvtsT_adjustvarargs(L, GETARG_A(i), ci, cl->p));
        if (g_unlikely(trap)) {                                       
          gvtsD_hookcall(L, ci);
          L->oldpc = 1;                                                
        }
        updatebase(ci);                                              
        vmbreak;
      }
      vmcase(OP_EXTRAARG) {
        gvts_assert(0);
        vmbreak;
      }
    }
  }
}
