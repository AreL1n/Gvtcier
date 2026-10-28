#define lgc_c
#define GVTS_CORE

#include "Gprefix.h"

#include <stdio.h>
#include <string.h>

#include "gvts.h"

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

#define GCSWEEPMAX	100

#define GCFINMAX	10

#define GCFINALIZECOST	50

#define WORK2MEM	sizeof(TValue)

#define PAUSEADJ		100

#define maskcolors	(bitmask(BLACKBIT) | WHITEBITS)

#define maskgcbits      (maskcolors | AGEBITS)

#define makewhite(g,x)	\
  (x->marked = cast_byte((x->marked & ~maskcolors) | gvtsC_white(g)))

#define set2gray(x)	resetbits(x->marked, maskcolors)

#define set2black(x)  \
  (x->marked = cast_byte((x->marked & ~WHITEBITS) | bitmask(BLACKBIT)))

#define valiswhite(x)   (iscollectable(x) && iswhite(gcvalue(x)))

#define keyiswhite(n)   (keyiscollectable(n) && iswhite(gckey(n)))

#define gcvalueN(o)     (iscollectable(o) ? gcvalue(o) : NULL)

#define markvalue(g,o) { checkliveness(g->mainthread,o); \
  if (valiswhite(o)) reallymarkobject(g,gcvalue(o)); }

#define markkey(g, n)	{ if keyiswhite(n) reallymarkobject(g,gckey(n)); }

#define markobject(g,t)	{ if (iswhite(t)) reallymarkobject(g, obj2gco(t)); }

#define markobjectN(g,t)	{ if (t) markobject(g,t); }

static void reallymarkobject (global_State *g, GCObject *o);
static lu_mem atomic (gvts_State *L);
static void entersweep (gvts_State *L);

#define gnodelast(h)	gnode(h, cast_sizet(sizenode(h)))

static GCObject **getgclist (GCObject *o) {
  switch (o->tt) {
    case GVTS_VTABLE: return &gco2t(o)->gclist;
    case GVTS_VLCL: return &gco2lcl(o)->gclist;
    case GVTS_VCCL: return &gco2ccl(o)->gclist;
    case GVTS_VTHREAD: return &gco2th(o)->gclist;
    case GVTS_VPROTO: return &gco2p(o)->gclist;
    case GVTS_VUSERDATA: {
      Udata *u = gco2u(o);
      gvts_assert(u->nuvalue > 0);
      return &u->gclist;
    }
    default: gvts_assert(0); return 0;
  }
}

#define linkgclist(o,p)	linkgclist_(obj2gco(o), &(o)->gclist, &(p))

static void linkgclist_ (GCObject *o, GCObject **pnext, GCObject **list) {
  gvts_assert(!isgray(o));                                
  *pnext = *list;
  *list = o;
  set2gray(o);                 
}

#define linkobjgclist(o,p) linkgclist_(obj2gco(o), getgclist(o), &(p))

static void clearkey (Node *n) {
  gvts_assert(isempty(gval(n)));
  if (keyiscollectable(n))
    setdeadkey(n);                             
}

static int iscleared (global_State *g, const GCObject *o) {
  if (o == NULL) return 0;                             
  else if (novariant(o->tt) == GVTS_TSTRING) {
    markobject(g, o);                                               
    return 0;
  }
  else return iswhite(o);
}

void gvtsC_barrier_ (gvts_State *L, GCObject *o, GCObject *v) {
  global_State *g = G(L);
  gvts_assert(isblack(o) && iswhite(v) && !isdead(g, v) && !isdead(g, o));
  if (keepinvariant(g)) {                            
    reallymarkobject(g, v);                         
    if (isold(o)) {
      gvts_assert(!isold(v));                                     
      setage(v, G_OLD0);                                      
    }
  }
  else {                   
    gvts_assert(issweepphase(g));
    if (g->gckind == KGC_INC)                         
      makewhite(g, o);                                                 
  }
}

void gvtsC_barrierback_ (gvts_State *L, GCObject *o) {
  global_State *g = G(L);
  gvts_assert(isblack(o) && !isdead(g, o));
  gvts_assert((g->gckind == KGC_GEN) == (isold(o) && getage(o) != G_TOUCHED1));
  if (getage(o) == G_TOUCHED2)                             
    set2gray(o);                                       
  else                                                
    linkobjgclist(o, g->grayagain);
  if (isold(o))                          
    setage(o, G_TOUCHED1);                                
}

void gvtsC_fix (gvts_State *L, GCObject *o) {
  global_State *g = G(L);
  gvts_assert(g->allgc == o);                                           
  set2gray(o);                                 
  setage(o, G_OLD);                       
  g->allgc = o->next;                                       
  o->next = g->fixedgc;                                 
  g->fixedgc = o;
}

GCObject *gvtsC_newobjdt (gvts_State *L, int tt, size_t sz, size_t offset) {
  global_State *g = G(L);
  char *p = cast_charp(gvtsM_newobject(L, novariant(tt), sz));
  GCObject *o = cast(GCObject *, p + offset);
  o->marked = gvtsC_white(g);
  o->tt = tt;
  o->next = g->allgc;
  g->allgc = o;
  return o;
}

GCObject *gvtsC_newobj (gvts_State *L, int tt, size_t sz) {
  return gvtsC_newobjdt(L, tt, sz, 0);
}

static void reallymarkobject (global_State *g, GCObject *o) {
  switch (o->tt) {
    case GVTS_VSHRSTR:
    case GVTS_VLNGSTR: {
      set2black(o);                        
      break;
    }
    case GVTS_VUPVAL: {
      UpVal *uv = gco2upv(o);
      if (upisopen(uv))
        set2gray(uv);                                   
      else
        set2black(uv);                                        
      markvalue(g, uv->v.p);                        
      break;
    }
    case GVTS_VUSERDATA: {
      Udata *u = gco2u(o);
      if (u->nuvalue == 0) {                       
        markobjectN(g, u->metatable);                          
        set2black(u);                            
        break;
      }

    }                   
    case GVTS_VLCL: case GVTS_VCCL: case GVTS_VTABLE:
    case GVTS_VTHREAD: case GVTS_VPROTO: {
      linkobjgclist(o, g->gray);                           
      break;
    }
    default: gvts_assert(0); break;
  }
}

static void markmt (global_State *g) {
  int i;
  for (i=0; i < GVTS_NUMTAGS; i++)
    markobjectN(g, g->mt[i]);
}

static lu_mem markbeingfnz (global_State *g) {
  GCObject *o;
  lu_mem count = 0;
  for (o = g->tobefnz; o != NULL; o = o->next) {
    count++;
    markobject(g, o);
  }
  return count;
}

static int remarkupvals (global_State *g) {
  gvts_State *thread;
  gvts_State **p = &g->twups;
  int work = 0;                                               
  while ((thread = *p) != NULL) {
    work++;
    if (!iswhite(thread) && thread->openupval != NULL)
      p = &thread->twups;                                                    
    else {                                                
      UpVal *uv;
      gvts_assert(!isold(thread) || thread->openupval == NULL);
      *p = thread->twups;                                   
      thread->twups = thread;                                   
      for (uv = thread->openupval; uv != NULL; uv = uv->u.open.next) {
        gvts_assert(getage(uv) <= getage(thread));
        work++;
        if (!iswhite(uv)) {                                
          gvts_assert(upisopen(uv) && isgray(uv));
          markvalue(g, uv->v.p);                      
        }
      }
    }
  }
  return work;
}

static void cleargraylists (global_State *g) {
  g->gray = g->grayagain = NULL;
  g->weak = g->allweak = g->ephemeron = NULL;
}

static void restartcollection (global_State *g) {
  cleargraylists(g);
  markobject(g, g->mainthread);
  markvalue(g, &g->g_registry);
  markmt(g);
  markbeingfnz(g);                                                           
}

static void genlink (global_State *g, GCObject *o) {
  gvts_assert(isblack(o));
  if (getage(o) == G_TOUCHED1) {                              
    linkobjgclist(o, g->grayagain);                                   
  }                                                     
  else if (getage(o) == G_TOUCHED2)
    changeage(o, G_TOUCHED2, G_OLD);                   
}

static void traverseweakvalue (global_State *g, Table *h) {
  Node *n, *limit = gnodelast(h);

  int hasclears = (h->alimit > 0);
  for (n = gnode(h, 0); n < limit; n++) {                          
    if (isempty(gval(n)))                       
      clearkey(n);                     
    else {
      gvts_assert(!keyisnil(n));
      markkey(g, n);
      if (!hasclears && iscleared(g, gcvalueN(gval(n))))                      
        hasclears = 1;                                     
    }
  }
  if (g->gcstate == GCSatomic && hasclears)
    linkgclist(h, g->weak);                               
  else
    linkgclist(h, g->grayagain);                                          
}

static int traverseephemeron (global_State *g, Table *h, int inv) {
  int marked = 0;                                                     
  int hasclears = 0;                                    
  int hasww = 0;                                                          
  unsigned int i;
  unsigned int asize = gvtsH_realasize(h);
  unsigned int nsize = sizenode(h);

  for (i = 0; i < asize; i++) {
    if (valiswhite(&h->array[i])) {
      marked = 1;
      reallymarkobject(g, gcvalue(&h->array[i]));
    }
  }

  for (i = 0; i < nsize; i++) {
    Node *n = inv ? gnode(h, nsize - 1 - i) : gnode(h, i);
    if (isempty(gval(n)))                       
      clearkey(n);                     
    else if (iscleared(g, gckeyN(n))) {                                
      hasclears = 1;                             
      if (valiswhite(gval(n)))                             
        hasww = 1;                         
    }
    else if (valiswhite(gval(n))) {                             
      marked = 1;
      reallymarkobject(g, gcvalue(gval(n)));                   
    }
  }

  if (g->gcstate == GCSpropagate)
    linkgclist(h, g->grayagain);                                          
  else if (hasww)                                       
    linkgclist(h, g->ephemeron);                               
  else if (hasclears)                             
    linkgclist(h, g->allweak);                                    
  else
    genlink(g, obj2gco(h));                                                     
  return marked;
}

static void traversestrongtable (global_State *g, Table *h) {
  Node *n, *limit = gnodelast(h);
  unsigned int i;
  unsigned int asize = gvtsH_realasize(h);
  for (i = 0; i < asize; i++)                           
    markvalue(g, &h->array[i]);
  for (n = gnode(h, 0); n < limit; n++) {                          
    if (isempty(gval(n)))                       
      clearkey(n);                     
    else {
      gvts_assert(!keyisnil(n));
      markkey(g, n);
      markvalue(g, gval(n));
    }
  }
  genlink(g, obj2gco(h));
}

static lu_mem traversetable (global_State *g, Table *h) {
  const char *weakkey, *weakvalue;
  const TValue *mode = gfasttm(g, h->metatable, TM_MODE);
  TString *smode;
  markobjectN(g, h->metatable);
  if (mode && ttisshrstring(mode) &&                             
      (cast_void(smode = tsvalue(mode)),
       cast_void(weakkey = strchr(getshrstr(smode), 'k')),
       cast_void(weakvalue = strchr(getshrstr(smode), 'v')),
       (weakkey || weakvalue))) {                       
    if (!weakkey)                    
      traverseweakvalue(g, h);
    else if (!weakvalue)                      
      traverseephemeron(g, h, 0);
    else                
      linkgclist(h, g->allweak);                               
  }
  else                
    traversestrongtable(g, h);
  return 1 + h->alimit + 2 * allocsizenode(h);
}

static int traverseudata (global_State *g, Udata *u) {
  int i;
  markobjectN(g, u->metatable);                          
  for (i = 0; i < u->nuvalue; i++)
    markvalue(g, &u->uv[i].uv);
  genlink(g, obj2gco(u));
  return 1 + u->nuvalue;
}

static int traverseproto (global_State *g, Proto *f) {
  int i;
  markobjectN(g, f->source);
  for (i = 0; i < f->sizek; i++)                     
    markvalue(g, &f->k[i]);
  for (i = 0; i < f->sizeupvalues; i++)                          
    markobjectN(g, f->upvalues[i].name);
  for (i = 0; i < f->sizep; i++)                          
    markobjectN(g, f->p[i]);
  for (i = 0; i < f->sizelocvars; i++)                                 
    markobjectN(g, f->locvars[i].varname);
  return 1 + f->sizek + f->sizeupvalues + f->sizep + f->sizelocvars;
}

static int traverseCclosure (global_State *g, CClosure *cl) {
  int i;
  for (i = 0; i < cl->nupvalues; i++)                         
    markvalue(g, &cl->upvalue[i]);
  return 1 + cl->nupvalues;
}

static int traverseLclosure (global_State *g, LClosure *cl) {
  int i;
  markobjectN(g, cl->p);                          
  for (i = 0; i < cl->nupvalues; i++) {                          
    UpVal *uv = cl->upvals[i];
    markobjectN(g, uv);                    
  }
  return 1 + cl->nupvalues;
}

static int traversethread (global_State *g, gvts_State *th) {
  UpVal *uv;
  StkId o = th->stack.p;
  if (isold(th) || g->gcstate == GCSpropagate)
    linkgclist(th, g->grayagain);                                    
  if (o == NULL)
    return 1;                                      
  gvts_assert(g->gcstate == GCSatomic ||
             th->openupval == NULL || isintwups(th));
  for (; o < th->top.p; o++)                                       
    markvalue(g, s2v(o));
  for (uv = th->openupval; uv != NULL; uv = uv->u.open.next)
    markobject(g, uv);                                         
  if (g->gcstate == GCSatomic) {                        
    if (!g->gcemergency)
      gvtsD_shrinkstack(th);                                             
    for (o = th->top.p; o < th->stack_last.p + EXTRA_STACK; o++)
      setnilvalue(s2v(o));                              

    if (!isintwups(th) && th->openupval != NULL) {
      th->twups = g->twups;                                
      g->twups = th;
    }
  }
  return 1 + stacksize(th);
}

static lu_mem propagatemark (global_State *g) {
  GCObject *o = g->gray;
  nw2black(o);
  g->gray = *getgclist(o);                               
  switch (o->tt) {
    case GVTS_VTABLE: return traversetable(g, gco2t(o));
    case GVTS_VUSERDATA: return traverseudata(g, gco2u(o));
    case GVTS_VLCL: return traverseLclosure(g, gco2lcl(o));
    case GVTS_VCCL: return traverseCclosure(g, gco2ccl(o));
    case GVTS_VPROTO: return traverseproto(g, gco2p(o));
    case GVTS_VTHREAD: return traversethread(g, gco2th(o));
    default: gvts_assert(0); return 0;
  }
}

static lu_mem propagateall (global_State *g) {
  lu_mem tot = 0;
  while (g->gray)
    tot += propagatemark(g);
  return tot;
}

static void convergeephemerons (global_State *g) {
  int changed;
  int dir = 0;
  do {
    GCObject *w;
    GCObject *next = g->ephemeron;                          
    g->ephemeron = NULL;                                                     
    changed = 0;
    while ((w = next) != NULL) {                                
      Table *h = gco2t(w);
      next = h->gclist;                                   
      nw2black(h);                                 
      if (traverseephemeron(g, h, dir)) {                          
        propagateall(g);                         
        changed = 1;                                                 
      }
    }
    dir = !dir;                                  
  } while (changed);                                    
}

static void clearbykeys (global_State *g, GCObject *l) {
  for (; l; l = gco2t(l)->gclist) {
    Table *h = gco2t(l);
    Node *limit = gnodelast(h);
    Node *n;
    for (n = gnode(h, 0); n < limit; n++) {
      if (iscleared(g, gckeyN(n)))                     
        setempty(gval(n));                    
      if (isempty(gval(n)))                       
        clearkey(n);                     
    }
  }
}

static void clearbyvalues (global_State *g, GCObject *l, GCObject *f) {
  for (; l != f; l = gco2t(l)->gclist) {
    Table *h = gco2t(l);
    Node *n, *limit = gnodelast(h);
    unsigned int i;
    unsigned int asize = gvtsH_realasize(h);
    for (i = 0; i < asize; i++) {
      TValue *o = &h->array[i];
      if (iscleared(g, gcvalueN(o)))                            
        setempty(o);                    
    }
    for (n = gnode(h, 0); n < limit; n++) {
      if (iscleared(g, gcvalueN(gval(n))))                       
        setempty(gval(n));                    
      if (isempty(gval(n)))                       
        clearkey(n);                     
    }
  }
}

static void freeupval (gvts_State *L, UpVal *uv) {
  if (upisopen(uv))
    gvtsF_unlinkupval(uv);
  gvtsM_free(L, uv);
}

static void freeobj (gvts_State *L, GCObject *o) {
  switch (o->tt) {
    case GVTS_VPROTO:
      gvtsF_freeproto(L, gco2p(o));
      break;
    case GVTS_VUPVAL:
      freeupval(L, gco2upv(o));
      break;
    case GVTS_VLCL: {
      LClosure *cl = gco2lcl(o);
      gvtsM_freemem(L, cl, sizeLclosure(cl->nupvalues));
      break;
    }
    case GVTS_VCCL: {
      CClosure *cl = gco2ccl(o);
      gvtsM_freemem(L, cl, sizeCclosure(cl->nupvalues));
      break;
    }
    case GVTS_VTABLE:
      gvtsH_free(L, gco2t(o));
      break;
    case GVTS_VTHREAD:
      gvtsE_freethread(L, gco2th(o));
      break;
    case GVTS_VUSERDATA: {
      Udata *u = gco2u(o);
      gvtsM_freemem(L, o, sizeudata(u->nuvalue, u->len));
      break;
    }
    case GVTS_VSHRSTR: {
      TString *ts = gco2ts(o);
      gvtsS_remove(L, ts);                                 
      gvtsM_freemem(L, ts, sizelstring(ts->shrlen));
      break;
    }
    case GVTS_VLNGSTR: {
      TString *ts = gco2ts(o);
      gvtsM_freemem(L, ts, sizelstring(ts->u.lnglen));
      break;
    }
    default: gvts_assert(0);
  }
}

static GCObject **sweeplist (gvts_State *L, GCObject **p, int countin,
                             int *countout) {
  global_State *g = G(L);
  int ow = otherwhite(g);
  int i;
  int white = gvtsC_white(g);                     
  for (i = 0; *p != NULL && i < countin; i++) {
    GCObject *curr = *p;
    int marked = curr->marked;
    if (isdeadm(ow, marked)) {                       
      *p = curr->next;                               
      freeobj(L, curr);                    
    }
    else {                              
      curr->marked = cast_byte((marked & ~maskgcbits) | white);
      p = &curr->next;                          
    }
  }
  if (countout)
    *countout = i;                                    
  return (*p == NULL) ? NULL : p;
}

static GCObject **sweeptolive (gvts_State *L, GCObject **p) {
  GCObject **old = p;
  do {
    p = sweeplist(L, p, 1, NULL);
  } while (p == old);
  return p;
}

static void checkSizes (gvts_State *L, global_State *g) {
  if (!g->gcemergency) {
    if (g->strt.nuse < g->strt.size / 4) {                             
      g_mem olddebt = g->GCdebt;
      gvtsS_resize(L, g->strt.size / 2);
      g->GCestimate += g->GCdebt - olddebt;                        
    }
  }
}

static GCObject *udata2finalize (global_State *g) {
  GCObject *o = g->tobefnz;                         
  gvts_assert(tofinalize(o));
  g->tobefnz = o->next;                                     
  o->next = g->allgc;                                 
  g->allgc = o;
  resetbit(o->marked, FINALIZEDBIT);                                
  if (issweepphase(g))
    makewhite(g, o);                      
  else if (getage(o) == G_OLD1)
    g->firstold1 = o;                                               
  return o;
}

static void dothecall (gvts_State *L, void *ud) {
  UNUSED(ud);
  gvtsD_callnoyield(L, L->top.p - 2, 0);
}

static void GCTM (gvts_State *L) {
  global_State *g = G(L);
  const TValue *tm;
  TValue v;
  gvts_assert(!g->gcemergency);
  setgcovalue(L, &v, udata2finalize(g));
  tm = gvtsT_gettmbyobj(L, &v, TM_GC);
  if (!notm(tm)) {                             
    int status;
    lu_byte oldah = L->allowhook;
    int oldgcstp  = g->gcstp;
    g->gcstp |= GCSTPGC;                      
    L->allowhook = 0;                                             
    setobj2s(L, L->top.p++, tm);                         
    setobj2s(L, L->top.p++, &v);                            
    L->ci->callstatus |= CIST_FIN;                            
    status = gvtsD_pcall(L, dothecall, NULL, savestack(L, L->top.p - 2), 0);
    L->ci->callstatus &= ~CIST_FIN;                                       
    L->allowhook = oldah;                     
    g->gcstp = oldgcstp;                     
    if (g_unlikely(status != GVTS_OK)) {                                 
      gvtsE_warnerror(L, "__gc");
      L->top.p--;                         
    }
  }
}

static int runafewfinalizers (gvts_State *L, int n) {
  global_State *g = G(L);
  int i;
  for (i = 0; i < n && g->tobefnz; i++)
    GCTM(L);                          
  return i;
}

static void callallpendingfinalizers (gvts_State *L) {
  global_State *g = G(L);
  while (g->tobefnz)
    GCTM(L);
}

static GCObject **findlast (GCObject **p) {
  while (*p != NULL)
    p = &(*p)->next;
  return p;
}

static void separatetobefnz (global_State *g, int all) {
  GCObject *curr;
  GCObject **p = &g->finobj;
  GCObject **lastnext = findlast(&g->tobefnz);
  while ((curr = *p) != g->finobjold1) {                                        
    gvts_assert(tofinalize(curr));
    if (!(iswhite(curr) || all))                            
      p = &curr->next;                            
    else {
      if (curr == g->finobjsur)                             
        g->finobjsur = curr->next;                  
      *p = curr->next;                                        
      curr->next = *lastnext;                                         
      *lastnext = curr;
      lastnext = &curr->next;
    }
  }
}

static void checkpointer (GCObject **p, GCObject *o) {
  if (o == *p)
    *p = o->next;
}

static void correctpointers (global_State *g, GCObject *o) {
  checkpointer(&g->survival, o);
  checkpointer(&g->old1, o);
  checkpointer(&g->reallyold, o);
  checkpointer(&g->firstold1, o);
}

void gvtsC_checkfinalizer (gvts_State *L, GCObject *o, Table *mt) {
  global_State *g = G(L);
  if (tofinalize(o) ||                                                
      gfasttm(g, mt, TM_GC) == NULL ||                                
      (g->gcstp & GCSTPCLS))                                          
    return;                          
  else {                                 
    GCObject **p;
    if (issweepphase(g)) {
      makewhite(g, o);                          
      if (g->sweepgc == &o->next)                                          
        g->sweepgc = sweeptolive(L, g->sweepgc);                        
    }
    else
      correctpointers(g, o);

    for (p = &g->allgc; *p != o; p = &(*p)->next) {             }
    *p = o->next;                                    
    o->next = g->finobj;                                
    g->finobj = o;
    g_setbit(o->marked, FINALIZEDBIT);                       
  }
}

static void setpause (global_State *g) {
  g_mem threshold, debt;
  int pause = getgcparam(g->gcpause);
  g_mem estimate = g->GCestimate / PAUSEADJ;                         
  gvts_assert(estimate > 0);
  threshold = (pause < MAX_LMEM / estimate)                 
            ? estimate * pause                   
            : MAX_LMEM;                                     
  debt = gettotalbytes(g) - threshold;
  if (debt > 0) debt = 0;
  gvtsE_setdebt(g, debt);
}

static void sweep2old (gvts_State *L, GCObject **p) {
  GCObject *curr;
  global_State *g = G(L);
  while ((curr = *p) != NULL) {
    if (iswhite(curr)) {                       
      gvts_assert(isdead(g, curr));
      *p = curr->next;                               
      freeobj(L, curr);                    
    }
    else {                                        
      setage(curr, G_OLD);
      if (curr->tt == GVTS_VTHREAD) {                               
        gvts_State *th = gco2th(curr);
        linkgclist(th, g->grayagain);                                    
      }
      else if (curr->tt == GVTS_VUPVAL && upisopen(gco2upv(curr)))
        set2gray(curr);                                     
      else                                
        nw2black(curr);
      p = &curr->next;                          
    }
  }
}

static GCObject **sweepgen (gvts_State *L, global_State *g, GCObject **p,
                            GCObject *limit, GCObject **pfirstold1) {
  static const lu_byte nextage[] = {
    G_SURVIVAL,                  
    G_OLD1,                           
    G_OLD1,                       
    G_OLD,                        
    G_OLD,                                       
    G_TOUCHED1,                                       
    G_TOUCHED2                                        
  };
  int white = gvtsC_white(g);
  GCObject *curr;
  while ((curr = *p) != limit) {
    if (iswhite(curr)) {                       
      gvts_assert(!isold(curr) && isdead(g, curr));
      *p = curr->next;                               
      freeobj(L, curr);                    
    }
    else {                            
      if (getage(curr) == G_NEW) {                                    
        int marked = curr->marked & ~maskgcbits;                     
        curr->marked = cast_byte(marked | G_SURVIVAL | white);
      }
      else {                                                              
        setage(curr, nextage[getage(curr)]);
        if (getage(curr) == G_OLD1 && *pfirstold1 == NULL)
          *pfirstold1 = curr;                                     
      }
      p = &curr->next;                          
    }
  }
  return p;
}

static void whitelist (global_State *g, GCObject *p) {
  int white = gvtsC_white(g);
  for (; p != NULL; p = p->next)
    p->marked = cast_byte((p->marked & ~maskgcbits) | white);
}

static GCObject **correctgraylist (GCObject **p) {
  GCObject *curr;
  while ((curr = *p) != NULL) {
    GCObject **next = getgclist(curr);
    if (iswhite(curr))
      goto remove;                                
    else if (getage(curr) == G_TOUCHED1) {                              
      gvts_assert(isgray(curr));
      nw2black(curr);                                       
      changeage(curr, G_TOUCHED1, G_TOUCHED2);
      goto remain;                                                  
    }
    else if (curr->tt == GVTS_VTHREAD) {
      gvts_assert(isgray(curr));
      goto remain;                                          
    }
    else {                                  
      gvts_assert(isold(curr));                                          
      if (getage(curr) == G_TOUCHED2)                                
        changeage(curr, G_TOUCHED2, G_OLD);                  
      nw2black(curr);                                         
      goto remove;
    }
    remove: *p = *next; continue;
    remain: p = next; continue;
  }
  return p;
}

static void correctgraylists (global_State *g) {
  GCObject **list = correctgraylist(&g->grayagain);
  *list = g->weak; g->weak = NULL;
  list = correctgraylist(list);
  *list = g->allweak; g->allweak = NULL;
  list = correctgraylist(list);
  *list = g->ephemeron; g->ephemeron = NULL;
  correctgraylist(list);
}

static void markold (global_State *g, GCObject *from, GCObject *to) {
  GCObject *p;
  for (p = from; p != to; p = p->next) {
    if (getage(p) == G_OLD1) {
      gvts_assert(!iswhite(p));
      changeage(p, G_OLD1, G_OLD);                        
      if (isblack(p))
        reallymarkobject(g, p);
    }
  }
}

static void finishgencycle (gvts_State *L, global_State *g) {
  correctgraylists(g);
  checkSizes(L, g);
  g->gcstate = GCSpropagate;                    
  if (!g->gcemergency)
    callallpendingfinalizers(L);
}

static void youngcollection (gvts_State *L, global_State *g) {
  GCObject **psurvival;                                                  
  GCObject *dummy;                                         
  gvts_assert(g->gcstate == GCSpropagate);
  if (g->firstold1) {                                       
    markold(g, g->firstold1, g->reallyold);                 
    g->firstold1 = NULL;                                      
  }
  markold(g, g->finobj, g->finobjrold);
  markold(g, g->tobefnz, NULL);
  atomic(L);

  g->gcstate = GCSswpallgc;
  psurvival = sweepgen(L, g, &g->allgc, g->survival, &g->firstold1);

  sweepgen(L, g, psurvival, g->old1, &g->firstold1);
  g->reallyold = g->old1;
  g->old1 = *psurvival;                                        
  g->survival = g->allgc;                              

  dummy = NULL;                                                      
  psurvival = sweepgen(L, g, &g->finobj, g->finobjsur, &dummy);

  sweepgen(L, g, psurvival, g->finobjold1, &dummy);
  g->finobjrold = g->finobjold1;
  g->finobjold1 = *psurvival;                                        
  g->finobjsur = g->finobj;                              

  sweepgen(L, g, &g->tobefnz, NULL, &dummy);
  finishgencycle(L, g);
}

static void atomic2gen (gvts_State *L, global_State *g) {
  cleargraylists(g);

  g->gcstate = GCSswpallgc;
  sweep2old(L, &g->allgc);

  g->reallyold = g->old1 = g->survival = g->allgc;
  g->firstold1 = NULL;                                          

  sweep2old(L, &g->finobj);
  g->finobjrold = g->finobjold1 = g->finobjsur = g->finobj;

  sweep2old(L, &g->tobefnz);

  g->gckind = KGC_GEN;
  g->lastatomic = 0;
  g->GCestimate = gettotalbytes(g);                               
  finishgencycle(L, g);
}

static void setminordebt (global_State *g) {
  gvtsE_setdebt(g, -(cast(g_mem, (gettotalbytes(g) / 100)) * g->genminormul));
}

static lu_mem entergen (gvts_State *L, global_State *g) {
  lu_mem numobjs;
  gvtsC_runtilstate(L, bitmask(GCSpause));                                    
  gvtsC_runtilstate(L, bitmask(GCSpropagate));                       
  numobjs = atomic(L);                                                   
  atomic2gen(L, g);
  setminordebt(g);                                                  
  return numobjs;
}

static void enterinc (global_State *g) {
  whitelist(g, g->allgc);
  g->reallyold = g->old1 = g->survival = NULL;
  whitelist(g, g->finobj);
  whitelist(g, g->tobefnz);
  g->finobjrold = g->finobjold1 = g->finobjsur = NULL;
  g->gcstate = GCSpause;
  g->gckind = KGC_INC;
  g->lastatomic = 0;
}

void gvtsC_changemode (gvts_State *L, int newmode) {
  global_State *g = G(L);
  if (newmode != g->gckind) {
    if (newmode == KGC_GEN)                                   
      entergen(L, g);
    else
      enterinc(g);                                 
  }
  g->lastatomic = 0;
}

static lu_mem fullgen (gvts_State *L, global_State *g) {
  enterinc(g);
  return entergen(L, g);
}

static void stepgenfull (gvts_State *L, global_State *g) {
  lu_mem newatomic;                                  
  lu_mem lastatomic = g->lastatomic;                                  
  if (g->gckind == KGC_GEN)                                   
    enterinc(g);                              
  gvtsC_runtilstate(L, bitmask(GCSpropagate));                       
  newatomic = atomic(L);                      
  if (newatomic < lastatomic + (lastatomic >> 3)) {                        
    atomic2gen(L, g);                                   
    setminordebt(g);
  }
  else {                                                        
    g->GCestimate = gettotalbytes(g);                      
    entersweep(L);
    gvtsC_runtilstate(L, bitmask(GCSpause));                         
    setpause(g);
    g->lastatomic = newatomic;
  }
}

static void genstep (gvts_State *L, global_State *g) {
  if (g->lastatomic != 0)                                      
    stepgenfull(L, g);                      
  else {
    lu_mem majorbase = g->GCestimate;                                          
    lu_mem majorinc = (majorbase / 100) * getgcparam(g->genmajormul);
    if (g->GCdebt > 0 && gettotalbytes(g) > majorbase + majorinc) {
      lu_mem numobjs = fullgen(L, g);                             
      if (gettotalbytes(g) < majorbase + (majorinc / 2)) {

        gvts_assert(g->lastatomic == 0);
      }
      else {                      
        g->lastatomic = numobjs;                                           
        setpause(g);                                                  
      }
    }
    else {                                           
      youngcollection(L, g);
      setminordebt(g);
      g->GCestimate = majorbase;                           
    }
  }
  gvts_assert(isdecGCmodegen(g));
}

static void entersweep (gvts_State *L) {
  global_State *g = G(L);
  g->gcstate = GCSswpallgc;
  gvts_assert(g->sweepgc == NULL);
  g->sweepgc = sweeptolive(L, &g->allgc);
}

static void deletelist (gvts_State *L, GCObject *p, GCObject *limit) {
  while (p != limit) {
    GCObject *next = p->next;
    freeobj(L, p);
    p = next;
  }
}

void gvtsC_freeallobjects (gvts_State *L) {
  global_State *g = G(L);
  g->gcstp = GCSTPCLS;                                      
  gvtsC_changemode(L, KGC_INC);
  separatetobefnz(g, 1);                                            
  gvts_assert(g->finobj == NULL);
  callallpendingfinalizers(L);
  deletelist(L, g->allgc, obj2gco(g->mainthread));
  gvts_assert(g->finobj == NULL);                         
  deletelist(L, g->fixedgc, NULL);                             
  gvts_assert(g->strt.nuse == 0);
}

static lu_mem atomic (gvts_State *L) {
  global_State *g = G(L);
  lu_mem work = 0;
  GCObject *origweak, *origall;
  GCObject *grayagain = g->grayagain;                          
  g->grayagain = NULL;
  gvts_assert(g->ephemeron == NULL && g->weak == NULL);
  gvts_assert(!iswhite(g->mainthread));
  g->gcstate = GCSatomic;
  markobject(g, L);                           

  markvalue(g, &g->g_registry);
  markmt(g);                              
  work += propagateall(g);                           

  work += remarkupvals(g);
  work += propagateall(g);                         
  g->gray = grayagain;
  work += propagateall(g);                                 
  convergeephemerons(g);

  clearbyvalues(g, g->weak, NULL);
  clearbyvalues(g, g->allweak, NULL);
  origweak = g->weak; origall = g->allweak;
  separatetobefnz(g, 0);                                        
  work += markbeingfnz(g);                                           
  work += propagateall(g);                                           
  convergeephemerons(g);

  clearbykeys(g, g->ephemeron);                                            
  clearbykeys(g, g->allweak);                                            

  clearbyvalues(g, g->weak, origweak);
  clearbyvalues(g, g->allweak, origall);
  gvtsS_clearcache(g);
  g->currentwhite = cast_byte(otherwhite(g));                          
  gvts_assert(g->gray == NULL);
  return work;                                            
}

static int sweepstep (gvts_State *L, global_State *g,
                      int nextstate, GCObject **nextlist) {
  if (g->sweepgc) {
    g_mem olddebt = g->GCdebt;
    int count;
    g->sweepgc = sweeplist(L, g->sweepgc, GCSWEEPMAX, &count);
    g->GCestimate += g->GCdebt - olddebt;                       
    return count;
  }
  else {                        
    g->gcstate = nextstate;
    g->sweepgc = nextlist;
    return 0;                    
  }
}

static lu_mem singlestep (gvts_State *L) {
  global_State *g = G(L);
  lu_mem work;
  gvts_assert(!g->gcstopem);                                  
  g->gcstopem = 1;                                                 
  switch (g->gcstate) {
    case GCSpause: {
      restartcollection(g);
      g->gcstate = GCSpropagate;
      work = 1;
      break;
    }
    case GCSpropagate: {
      if (g->gray == NULL) {                             
        g->gcstate = GCSenteratomic;                              
        work = 0;
      }
      else
        work = propagatemark(g);                                
      break;
    }
    case GCSenteratomic: {
      work = atomic(L);                                              
      entersweep(L);
      g->GCestimate = gettotalbytes(g);                      
      break;
    }
    case GCSswpallgc: {                               
      work = sweepstep(L, g, GCSswpfinobj, &g->finobj);
      break;
    }
    case GCSswpfinobj: {                                     
      work = sweepstep(L, g, GCSswptobefnz, &g->tobefnz);
      break;
    }
    case GCSswptobefnz: {                                     
      work = sweepstep(L, g, GCSswpend, NULL);
      break;
    }
    case GCSswpend: {                     
      checkSizes(L, g);
      g->gcstate = GCScallfin;
      work = 0;
      break;
    }
    case GCScallfin: {                                 
      if (g->tobefnz && !g->gcemergency) {
        g->gcstopem = 0;                                        
        work = runafewfinalizers(L, GCFINMAX) * GCFINALIZECOST;
      }
      else {                                            
        g->gcstate = GCSpause;                         
        work = 0;
      }
      break;
    }
    default: gvts_assert(0); return 0;
  }
  g->gcstopem = 0;
  return work;
}

void gvtsC_runtilstate (gvts_State *L, int statesmask) {
  global_State *g = G(L);
  while (!testbit(statesmask, g->gcstate))
    singlestep(L);
}

static void incstep (gvts_State *L, global_State *g) {
  int stepmul = (getgcparam(g->gcstepmul) | 1);                           
  g_mem debt = (g->GCdebt / WORK2MEM) * stepmul;
  g_mem stepsize = (g->gcstepsize <= log2maxs(g_mem))
                 ? ((cast(g_mem, 1) << g->gcstepsize) / WORK2MEM) * stepmul
                 : MAX_LMEM;                                    
  do {                                                             
    lu_mem work = singlestep(L);                               
    debt -= work;
  } while (debt > -stepsize && g->gcstate != GCSpause);
  if (g->gcstate == GCSpause)
    setpause(g);                              
  else {
    debt = (debt / stepmul) * WORK2MEM;                                     
    gvtsE_setdebt(g, debt);
  }
}

void gvtsC_step (gvts_State *L) {
  global_State *g = G(L);
  if (!gcrunning(g))                    
    gvtsE_setdebt(g, -2000);
  else {
    if(isdecGCmodegen(g))
      genstep(L, g);
    else
      incstep(L, g);
  }
}

static void fullinc (gvts_State *L, global_State *g) {
  if (keepinvariant(g))                      
    entersweep(L);                                                  

  gvtsC_runtilstate(L, bitmask(GCSpause));
  gvtsC_runtilstate(L, bitmask(GCSpropagate));                       
  g->gcstate = GCSenteratomic;                                   
  gvtsC_runtilstate(L, bitmask(GCScallfin));                            

  gvts_assert(g->GCestimate == gettotalbytes(g));
  gvtsC_runtilstate(L, bitmask(GCSpause));                         
  setpause(g);
}

void gvtsC_fullgc (gvts_State *L, int isemergency) {
  global_State *g = G(L);
  gvts_assert(!g->gcemergency);
  g->gcemergency = isemergency;                
  if (g->gckind == KGC_INC)
    fullinc(L, g);
  else
    fullgen(L, g);
  g->gcemergency = 0;
}
