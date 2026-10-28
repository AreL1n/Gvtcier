#define lfunc_c
#define GVTS_CORE

#include "Gprefix.h"

#include <stddef.h>

#include "gvts.h"

#include "Gdebug.h"
#include "Gdo.h"
#include "Gfunc.h"
#include "Ggc.h"
#include "Gmem.h"
#include "Gobject.h"
#include "Gstate.h"

CClosure *gvtsF_newCclosure (gvts_State *L, int nupvals) {
  GCObject *o = gvtsC_newobj(L, GVTS_VCCL, sizeCclosure(nupvals));
  CClosure *c = gco2ccl(o);
  c->nupvalues = cast_byte(nupvals);
  return c;
}

LClosure *gvtsF_newLclosure (gvts_State *L, int nupvals) {
  GCObject *o = gvtsC_newobj(L, GVTS_VLCL, sizeLclosure(nupvals));
  LClosure *c = gco2lcl(o);
  c->p = NULL;
  c->nupvalues = cast_byte(nupvals);
  while (nupvals--) c->upvals[nupvals] = NULL;
  return c;
}

void gvtsF_initupvals (gvts_State *L, LClosure *cl) {
  int i;
  for (i = 0; i < cl->nupvalues; i++) {
    GCObject *o = gvtsC_newobj(L, GVTS_VUPVAL, sizeof(UpVal));
    UpVal *uv = gco2upv(o);
    uv->v.p = &uv->u.value;                      
    setnilvalue(uv->v.p);
    cl->upvals[i] = uv;
    gvtsC_objbarrier(L, cl, uv);
  }
}

static UpVal *newupval (gvts_State *L, StkId level, UpVal **prev) {
  GCObject *o = gvtsC_newobj(L, GVTS_VUPVAL, sizeof(UpVal));
  UpVal *uv = gco2upv(o);
  UpVal *next = *prev;
  uv->v.p = s2v(level);                                        
  uv->u.open.next = next;                                        
  uv->u.open.previous = prev;
  if (next)
    next->u.open.previous = &uv->u.open.next;
  *prev = uv;
  if (!isintwups(L)) {                                                    
    L->twups = G(L)->twups;                           
    G(L)->twups = L;
  }
  return uv;
}

UpVal *gvtsF_findupval (gvts_State *L, StkId level) {
  UpVal **pp = &L->openupval;
  UpVal *p;
  gvts_assert(isintwups(L) || L->openupval == NULL);
  while ((p = *pp) != NULL && uplevel(p) >= level) {                     
    gvts_assert(!isdead(G(L), p));
    if (uplevel(p) == level)                              
      return p;                 
    pp = &p->u.open.next;
  }

  return newupval(L, level, pp);
}

static void callclosemethod (gvts_State *L, TValue *obj, TValue *err, int yy) {
  StkId top = L->top.p;
  const TValue *tm = gvtsT_gettmbyobj(L, obj, TM_CLOSE);
  setobj2s(L, top, tm);                               
  setobj2s(L, top + 1, obj);                                       
  setobj2s(L, top + 2, err);                                      
  L->top.p = top + 3;                                  
  if (yy)
    gvtsD_call(L, top, 0);
  else
    gvtsD_callnoyield(L, top, 0);
}

static void checkclosemth (gvts_State *L, StkId level) {
  const TValue *tm = gvtsT_gettmbyobj(L, s2v(level), TM_CLOSE);
  if (ttisnil(tm)) {                      
    int idx = cast_int(level - L->ci->func.p);                      
    const char *vname = gvtsG_findlocal(L, L->ci, idx, NULL);
    if (vname == NULL) vname = "?";
    gvtsG_runerror(L, u8"变量「%s」获得了一个不可关闭的值", vname);
  }
}

static void prepcallclosemth (gvts_State *L, StkId level, int status, int yy) {
  TValue *uv = s2v(level);                          
  TValue *errobj;
  if (status == CLOSEKTOP)
    errobj = &G(L)->nilvalue;                           
  else {                                                    
    errobj = s2v(level + 1);                                    
    gvtsD_seterrorobj(L, status, level + 1);                        
  }
  callclosemethod(L, uv, errobj, yy);
}

#define MAXDELTA  \
	((256ul << ((sizeof(L->stack.p->tbclist.delta) - 1) * 8)) - 1)

void gvtsF_newtbcupval (gvts_State *L, StkId level) {
  gvts_assert(level > L->tbclist.p);
  if (g_isfalse(s2v(level)))
    return;                                       
  checkclosemth(L, level);                                      
  while (cast_uint(level - L->tbclist.p) > MAXDELTA) {
    L->tbclist.p += MAXDELTA;                                            
    L->tbclist.p->tbclist.delta = 0;
  }
  level->tbclist.delta = cast(unsigned short, level - L->tbclist.p);
  L->tbclist.p = level;
}

void gvtsF_unlinkupval (UpVal *uv) {
  gvts_assert(upisopen(uv));
  *uv->u.open.previous = uv->u.open.next;
  if (uv->u.open.next)
    uv->u.open.next->u.open.previous = uv->u.open.previous;
}

void gvtsF_closeupval (gvts_State *L, StkId level) {
  UpVal *uv;
  StkId upl;                                   
  while ((uv = L->openupval) != NULL && (upl = uplevel(uv)) >= level) {
    TValue *slot = &uv->u.value;                              
    gvts_assert(uplevel(uv) < L->top.p);
    gvtsF_unlinkupval(uv);                                            
    setobj(L, slot, uv->v.p);                                  
    uv->v.p = slot;                                    
    if (!iswhite(uv)) {                               
      nw2black(uv);                                      
      gvtsC_barrier(L, uv, slot);
    }
  }
}

static void poptbclist (gvts_State *L) {
  StkId tbc = L->tbclist.p;
  gvts_assert(tbc->tbclist.delta > 0);                                     
  tbc -= tbc->tbclist.delta;
  while (tbc > L->stack.p && tbc->tbclist.delta == 0)
    tbc -= MAXDELTA;                          
  L->tbclist.p = tbc;
}

StkId gvtsF_close (gvts_State *L, StkId level, int status, int yy) {
  ptrdiff_t levelrel = savestack(L, level);
  gvtsF_closeupval(L, level);                                 
  while (L->tbclist.p >= level) {                                         
    StkId tbc = L->tbclist.p;                          
    poptbclist(L);                           
    prepcallclosemth(L, tbc, status, yy);                      
    level = restorestack(L, levelrel);
  }
  return level;
}

Proto *gvtsF_newproto (gvts_State *L) {
  GCObject *o = gvtsC_newobj(L, GVTS_VPROTO, sizeof(Proto));
  Proto *f = gco2p(o);
  f->k = NULL;
  f->sizek = 0;
  f->p = NULL;
  f->sizep = 0;
  f->code = NULL;
  f->sizecode = 0;
  f->lineinfo = NULL;
  f->sizelineinfo = 0;
  f->abslineinfo = NULL;
  f->sizeabslineinfo = 0;
  f->upvalues = NULL;
  f->sizeupvalues = 0;
  f->numparams = 0;
  f->is_vararg = 0;
  f->maxstacksize = 0;
  f->locvars = NULL;
  f->sizelocvars = 0;
  f->linedefined = 0;
  f->lastlinedefined = 0;
  f->source = NULL;
  return f;
}

void gvtsF_freeproto (gvts_State *L, Proto *f) {
  gvtsM_freearray(L, f->code, f->sizecode);
  gvtsM_freearray(L, f->p, f->sizep);
  gvtsM_freearray(L, f->k, f->sizek);
  gvtsM_freearray(L, f->lineinfo, f->sizelineinfo);
  gvtsM_freearray(L, f->abslineinfo, f->sizeabslineinfo);
  gvtsM_freearray(L, f->locvars, f->sizelocvars);
  gvtsM_freearray(L, f->upvalues, f->sizeupvalues);
  gvtsM_free(L, f);
}

const char *gvtsF_getlocalname (const Proto *f, int local_number, int pc) {
  int i;
  for (i = 0; i<f->sizelocvars && f->locvars[i].startpc <= pc; i++) {
    if (pc < f->locvars[i].endpc) {                           
      local_number--;
      if (local_number == 0)
        return getstr(f->locvars[i].varname);
    }
  }
  return NULL;                 
}
