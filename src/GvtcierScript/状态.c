#define lstate_c
#define GVTS_CORE

#include "Gprefix.h"

#include <stddef.h>
#include <string.h>

#include "gvts.h"

#include "Gapi.h"
#include "Gdebug.h"
#include "Gdo.h"
#include "Gfunc.h"
#include "Ggc.h"
#include "Glex.h"
#include "Gmem.h"
#include "Gstate.h"
#include "Gstring.h"
#include "Gtable.h"
#include "Gtm.h"

typedef struct LX {
  lu_byte extra_[GVTS_EXTRASPACE];
  gvts_State l;
} LX;

typedef struct LG {
  LX l;
  global_State g;
} LG;

#define fromstate(L)	(cast(LX *, cast(lu_byte *, (L)) - offsetof(LX, l)))

#if !defined(gvtsi_makeseed)

#include <time.h>

#define addbuff(b,p,e) \
  { size_t t = cast_sizet(e); \
    memcpy(b + p, &t, sizeof(t)); p += sizeof(t); }

static unsigned int gvtsi_makeseed (gvts_State *L) {
  char buff[3 * sizeof(size_t)];
  unsigned int h = cast_uint(time(NULL));
  int p = 0;
  addbuff(buff, p, L);                     
  addbuff(buff, p, &h);                      
  addbuff(buff, p, &gvts_newstate);                       
  gvts_assert(p == sizeof(buff));
  return gvtsS_hash(buff, p, h);
}

#endif

void gvtsE_setdebt (global_State *g, g_mem debt) {
  g_mem tb = gettotalbytes(g);
  gvts_assert(tb > 0);
  if (debt < tb - MAX_LMEM)
    debt = tb - MAX_LMEM;                                          
  g->totalbytes = tb - debt;
  g->GCdebt = debt;
}

GVTS_API int gvts_setcstacklimit (gvts_State *L, unsigned int limit) {
  UNUSED(L); UNUSED(limit);
  return GVTSI_MAXCCALLS;                 
}

CallInfo *gvtsE_extendCI (gvts_State *L) {
  CallInfo *ci;
  gvts_assert(L->ci->next == NULL);
  ci = gvtsM_new(L, CallInfo);
  gvts_assert(L->ci->next == NULL);
  L->ci->next = ci;
  ci->previous = L->ci;
  ci->next = NULL;
  ci->u.l.trap = 0;
  L->nci++;
  return ci;
}

static void freeCI (gvts_State *L) {
  CallInfo *ci = L->ci;
  CallInfo *next = ci->next;
  ci->next = NULL;
  while ((ci = next) != NULL) {
    next = ci->next;
    gvtsM_free(L, ci);
    L->nci--;
  }
}

void gvtsE_shrinkCI (gvts_State *L) {
  CallInfo *ci = L->ci->next;                           
  CallInfo *next;
  if (ci == NULL)
    return;                         
  while ((next = ci->next) != NULL) {                           
    CallInfo *next2 = next->next;                   
    ci->next = next2;                                 
    L->nci--;
    gvtsM_free(L, next);                 
    if (next2 == NULL)
      break;                        
    else {
      next2->previous = ci;
      ci = next2;                
    }
  }
}

void gvtsE_checkcstack (gvts_State *L) {
  if (getCcalls(L) == GVTSI_MAXCCALLS)
    gvtsG_runerror(L, u8"C 调用栈溢出");
  else if (getCcalls(L) >= (GVTSI_MAXCCALLS / 10 * 11))
    gvtsD_throw(L, GVTS_ERRERR);                                        
}

GVTSI_FUNC void gvtsE_incCstack (gvts_State *L) {
  L->nCcalls++;
  if (g_unlikely(getCcalls(L) >= GVTSI_MAXCCALLS))
    gvtsE_checkcstack(L);
}

static void stack_init (gvts_State *L1, gvts_State *L) {
  int i; CallInfo *ci;

  L1->stack.p = gvtsM_newvector(L, BASIC_STACK_SIZE + EXTRA_STACK, StackValue);
  L1->tbclist.p = L1->stack.p;
  for (i = 0; i < BASIC_STACK_SIZE + EXTRA_STACK; i++)
    setnilvalue(s2v(L1->stack.p + i));                       
  L1->top.p = L1->stack.p;
  L1->stack_last.p = L1->stack.p + BASIC_STACK_SIZE;

  ci = &L1->base_ci;
  ci->next = ci->previous = NULL;
  ci->callstatus = CIST_C;
  ci->func.p = L1->top.p;
  ci->u.c.k = NULL;
  ci->nresults = 0;
  setnilvalue(s2v(L1->top.p));                                      
  L1->top.p++;
  ci->top.p = L1->top.p + GVTS_MINSTACK;
  L1->ci = ci;
}

static void freestack (gvts_State *L) {
  if (L->stack.p == NULL)
    return;                                      
  L->ci = &L->base_ci;                                 
  freeCI(L);
  gvts_assert(L->nci == 0);
  gvtsM_freearray(L, L->stack.p, stacksize(L) + EXTRA_STACK);                  
}

static void init_registry (gvts_State *L, global_State *g) {

  Table *registry = gvtsH_new(L);
  sethvalue(L, &g->g_registry, registry);
  gvtsH_resize(L, registry, GVTS_RIDX_LAST, 0);

  setthvalue(L, &registry->array[GVTS_RIDX_MAINTHREAD - 1], L);

  sethvalue(L, &registry->array[GVTS_RIDX_GLOBALS - 1], gvtsH_new(L));
}

static void f_gvtsopen (gvts_State *L, void *ud) {
  global_State *g = G(L);
  UNUSED(ud);
  stack_init(L, L);                  
  init_registry(L, g);
  gvtsS_init(L);
  gvtsT_init(L);
  gvtsX_init(L);
  g->gcstp = 0;                
  setnilvalue(&g->nilvalue);                             
  gvtsi_userstateopen(L);
}

static void preinit_thread (gvts_State *L, global_State *g) {
  G(L) = g;
  L->stack.p = NULL;
  L->ci = NULL;
  L->nci = 0;
  L->twups = L;                              
  L->nCcalls = 0;
  L->errorJmp = NULL;
  L->hook = NULL;
  L->hookmask = 0;
  L->basehookcount = 0;
  L->allowhook = 1;
  resethookcount(L);
  L->openupval = NULL;
  L->status = GVTS_OK;
  L->errfunc = 0;
  L->oldpc = 0;
}

static void close_state (gvts_State *L) {
  global_State *g = G(L);
  if (!completestate(g))                                        
    gvtsC_freeallobjects(L);                                
  else {                                   
    L->ci = &L->base_ci;                            
    gvtsD_closeprotected(L, 1, GVTS_OK);                          
    gvtsC_freeallobjects(L);                           
    gvtsi_userstateclose(L);
  }
  gvtsM_freearray(L, G(L)->strt.hash, G(L)->strt.size);
  freestack(L);
  gvts_assert(gettotalbytes(g) == sizeof(LG));
  (*g->frealloc)(g->ud, fromstate(L), sizeof(LG), 0);                       
}

GVTS_API gvts_State *gvts_newthread (gvts_State *L) {
  global_State *g = G(L);
  GCObject *o;
  gvts_State *L1;
  gvts_lock(L);
  gvtsC_checkGC(L);

  o = gvtsC_newobjdt(L, GVTS_TTHREAD, sizeof(LX), offsetof(LX, l));
  L1 = gco2th(o);

  setthvalue2s(L, L->top.p, L1);
  api_incr_top(L);
  preinit_thread(L1, g);
  L1->hookmask = L->hookmask;
  L1->basehookcount = L->basehookcount;
  L1->hook = L->hook;
  resethookcount(L1);

  memcpy(gvts_getextraspace(L1), gvts_getextraspace(g->mainthread),
         GVTS_EXTRASPACE);
  gvtsi_userstatethread(L, L1);
  stack_init(L1, L);                  
  gvts_unlock(L);
  return L1;
}

void gvtsE_freethread (gvts_State *L, gvts_State *L1) {
  LX *l = fromstate(L1);
  gvtsF_closeupval(L1, L1->stack.p);                          
  gvts_assert(L1->openupval == NULL);
  gvtsi_userstatefree(L, L1);
  freestack(L1);
  gvtsM_free(L, l);
}

int gvtsE_resetthread (gvts_State *L, int status) {
  CallInfo *ci = L->ci = &L->base_ci;                            
  setnilvalue(s2v(L->stack.p));                                       
  ci->func.p = L->stack.p;
  ci->callstatus = CIST_C;
  if (status == GVTS_YIELD)
    status = GVTS_OK;
  L->status = GVTS_OK;                                         
  status = gvtsD_closeprotected(L, 1, status);
  if (status != GVTS_OK)               
    gvtsD_seterrorobj(L, status, L->stack.p + 1);
  else
    L->top.p = L->stack.p + 1;
  ci->top.p = L->top.p + GVTS_MINSTACK;
  gvtsD_reallocstack(L, cast_int(ci->top.p - L->stack.p), 0);
  return status;
}

GVTS_API int gvts_closethread (gvts_State *L, gvts_State *from) {
  int status;
  gvts_lock(L);
  L->nCcalls = (from) ? getCcalls(from) : 0;
  status = gvtsE_resetthread(L, L->status);
  gvts_unlock(L);
  return status;
}

GVTS_API int gvts_resetthread (gvts_State *L) {
  return gvts_closethread(L, NULL);
}

GVTS_API gvts_State *gvts_newstate (gvts_Alloc f, void *ud) {
  int i;
  gvts_State *L;
  global_State *g;
  LG *l = cast(LG *, (*f)(ud, NULL, GVTS_TTHREAD, sizeof(LG)));
  if (l == NULL) return NULL;
  L = &l->l.l;
  g = &l->g;
  L->tt = GVTS_VTHREAD;
  g->currentwhite = bitmask(WHITE0BIT);
  L->marked = gvtsC_white(g);
  preinit_thread(L, g);
  g->allgc = obj2gco(L);                                              
  L->next = NULL;
  incnny(L);                                           
  g->frealloc = f;
  g->ud = ud;
  g->warnf = NULL;
  g->ud_warn = NULL;
  g->mainthread = L;
  g->seed = gvtsi_makeseed(L);
  g->gcstp = GCSTPGC;                                  
  g->strt.size = g->strt.nuse = 0;
  g->strt.hash = NULL;
  setnilvalue(&g->g_registry);
  g->panic = NULL;
  g->gcstate = GCSpause;
  g->gckind = KGC_INC;
  g->gcstopem = 0;
  g->gcemergency = 0;
  g->finobj = g->tobefnz = g->fixedgc = NULL;
  g->firstold1 = g->survival = g->old1 = g->reallyold = NULL;
  g->finobjsur = g->finobjold1 = g->finobjrold = NULL;
  g->sweepgc = NULL;
  g->gray = g->grayagain = NULL;
  g->weak = g->ephemeron = g->allweak = NULL;
  g->twups = NULL;
  g->totalbytes = sizeof(LG);
  g->GCdebt = 0;
  g->lastatomic = 0;
  setivalue(&g->nilvalue, 0);                                             
  setgcparam(g->gcpause, GVTSI_GCPAUSE);
  setgcparam(g->gcstepmul, GVTSI_GCMUL);
  g->gcstepsize = GVTSI_GCSTEPSIZE;
  setgcparam(g->genmajormul, GVTSI_GENMAJORMUL);
  g->genminormul = GVTSI_GENMINORMUL;
  for (i=0; i < GVTS_NUMTAGS; i++) g->mt[i] = NULL;
  if (gvtsD_rawrunprotected(L, f_gvtsopen, NULL) != GVTS_OK) {

    close_state(L);
    L = NULL;
  }
  return L;
}

GVTS_API void gvts_close (gvts_State *L) {
  gvts_lock(L);
  L = G(L)->mainthread;                                          
  close_state(L);
}

void gvtsE_warning (gvts_State *L, const char *msg, int tocont) {
  gvts_WarnFunction wf = G(L)->warnf;
  if (wf != NULL)
    wf(G(L)->ud_warn, msg, tocont);
}

void gvtsE_warnerror (gvts_State *L, const char *where) {
  TValue *errobj = s2v(L->top.p - 1);                    
  const char *msg = (ttisstring(errobj))
                  ? getstr(tsvalue(errobj))
                  : "error object is not a string";

  gvtsE_warning(L, "error in ", 1);
  gvtsE_warning(L, where, 1);
  gvtsE_warning(L, " (", 1);
  gvtsE_warning(L, msg, 1);
  gvtsE_warning(L, ")", 0);
}
