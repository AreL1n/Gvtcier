#define ldo_c
#define GVTS_CORE

#include "Gprefix.h"

#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include "gvts.h"

#include "Gapi.h"
#include "Gdebug.h"
#include "Gdo.h"
#include "Gfunc.h"
#include "Ggc.h"
#include "Gmem.h"
#include "Gobject.h"
#include "Gopcodes.h"
#include "Gparser.h"
#include "Gstate.h"
#include "Gstring.h"
#include "Gtable.h"
#include "Gtm.h"
#include "Gundump.h"
#include "Gvm.h"
#include "Gzio.h"

#define errorstatus(s)	((s) > GVTS_YIELD)

#if !defined(GVTSI_THROW)				       

#if defined(__cplusplus) && !defined(GVTS_USE_LONGJMP)	       

#define GVTSI_THROW(L,c)		throw(c)
#define GVTSI_TRY(L,c,a) \
	try { a } catch(...) { if ((c)->status == 0) (c)->status = -1; }
#define gvtsi_jmpbuf		int                      

#elif defined(GVTS_USE_POSIX)				        

#define GVTSI_THROW(L,c)		_longjmp((c)->b, 1)
#define GVTSI_TRY(L,c,a)		if (_setjmp((c)->b) == 0) { a }
#define gvtsi_jmpbuf		jmp_buf

#else							        

#define GVTSI_THROW(L,c)		longjmp((c)->b, 1)
#define GVTSI_TRY(L,c,a)		if (setjmp((c)->b) == 0) { a }
#define gvtsi_jmpbuf		jmp_buf

#endif							       

#endif							       

struct gvts_longjmp {
  struct gvts_longjmp *previous;
  gvtsi_jmpbuf b;
  volatile int status;                  
};

void gvtsD_seterrorobj (gvts_State *L, int errcode, StkId oldtop) {
  switch (errcode) {
    case GVTS_ERRMEM: {                     
      setsvalue2s(L, oldtop, G(L)->memerrmsg);                               
      break;
    }
    case GVTS_ERRERR: {
      setsvalue2s(L, oldtop, gvtsS_newliteral(L, "error in error handling"));
      break;
    }
    case GVTS_OK: {                                              
      setnilvalue(s2v(oldtop));                        
      break;
    }
    default: {
      gvts_assert(errorstatus(errcode));                  
      setobjs2s(L, oldtop, L->top.p - 1);                                    
      break;
    }
  }
  L->top.p = oldtop + 1;
}

g_noret gvtsD_throw (gvts_State *L, int errcode) {
  if (L->errorJmp) {                                    
    L->errorJmp->status = errcode;                  
    GVTSI_THROW(L, L->errorJmp);                  
  }
  else {                                   
    global_State *g = G(L);
    errcode = gvtsE_resetthread(L, errcode);                          
    if (g->mainthread->errorJmp) {                                  
      setobjs2s(L, g->mainthread->top.p++, L->top.p - 1);                       
      gvtsD_throw(g->mainthread, errcode);                               
    }
    else {                                
      if (g->panic) {                       
        gvts_unlock(L);
        g->panic(L);                                                     
      }
      abort();
    }
  }
}

int gvtsD_rawrunprotected (gvts_State *L, Pfunc f, void *ud) {
  g_uint32 oldnCcalls = L->nCcalls;
  struct gvts_longjmp lj;
  lj.status = GVTS_OK;
  lj.previous = L->errorJmp;                               
  L->errorJmp = &lj;
  GVTSI_TRY(L, &lj,
    (*f)(L, ud);
  );
  L->errorJmp = lj.previous;                                 
  L->nCcalls = oldnCcalls;
  return lj.status;
}

static void relstack (gvts_State *L) {
  CallInfo *ci;
  UpVal *up;
  L->top.offset = savestack(L, L->top.p);
  L->tbclist.offset = savestack(L, L->tbclist.p);
  for (up = L->openupval; up != NULL; up = up->u.open.next)
    up->v.offset = savestack(L, uplevel(up));
  for (ci = L->ci; ci != NULL; ci = ci->previous) {
    ci->top.offset = savestack(L, ci->top.p);
    ci->func.offset = savestack(L, ci->func.p);
  }
}

static void correctstack (gvts_State *L) {
  CallInfo *ci;
  UpVal *up;
  L->top.p = restorestack(L, L->top.offset);
  L->tbclist.p = restorestack(L, L->tbclist.offset);
  for (up = L->openupval; up != NULL; up = up->u.open.next)
    up->v.p = s2v(restorestack(L, up->v.offset));
  for (ci = L->ci; ci != NULL; ci = ci->previous) {
    ci->top.p = restorestack(L, ci->top.offset);
    ci->func.p = restorestack(L, ci->func.offset);
    if (isGvts(ci))
      ci->u.l.trap = 1;                                                 
  }
}

#define ERRORSTACKSIZE	(GVTSI_MAXSTACK + 200)

int gvtsD_reallocstack (gvts_State *L, int newsize, int raiseerror) {
  int oldsize = stacksize(L);
  int i;
  StkId newstack;
  int oldgcstop = G(L)->gcstopem;
  gvts_assert(newsize <= GVTSI_MAXSTACK || newsize == ERRORSTACKSIZE);
  relstack(L);                                  
  G(L)->gcstopem = 1;                                 
  newstack = gvtsM_reallocvector(L, L->stack.p, oldsize + EXTRA_STACK,
                                   newsize + EXTRA_STACK, StackValue);
  G(L)->gcstopem = oldgcstop;                                    
  if (g_unlikely(newstack == NULL)) {                            
    correctstack(L);                                       
    if (raiseerror)
      gvtsM_error(L);
    else return 0;                             
  }
  L->stack.p = newstack;
  correctstack(L);                                       
  L->stack_last.p = L->stack.p + newsize;
  for (i = oldsize + EXTRA_STACK; i < newsize + EXTRA_STACK; i++)
    setnilvalue(s2v(newstack + i));                        
  return 1;
}

int gvtsD_growstack (gvts_State *L, int n, int raiseerror) {
  int size = stacksize(L);
  if (g_unlikely(size > GVTSI_MAXSTACK)) {

    gvts_assert(stacksize(L) == ERRORSTACKSIZE);
    if (raiseerror)
      gvtsD_throw(L, GVTS_ERRERR);                                    
    return 0;                                           
  }
  else if (n < GVTSI_MAXSTACK) {                                   
    int newsize = 2 * size;                          
    int needed = cast_int(L->top.p - L->stack.p) + n;
    if (newsize > GVTSI_MAXSTACK)                              
      newsize = GVTSI_MAXSTACK;
    if (newsize < needed)                                           
      newsize = needed;
    if (g_likely(newsize <= GVTSI_MAXSTACK))
      return gvtsD_reallocstack(L, newsize, raiseerror);
  }

  gvtsD_reallocstack(L, ERRORSTACKSIZE, raiseerror);
  if (raiseerror)
    gvtsG_runerror(L, u8"栈溢出");
  return 0;
}

static int stackinuse (gvts_State *L) {
  CallInfo *ci;
  int res;
  StkId lim = L->top.p;
  for (ci = L->ci; ci != NULL; ci = ci->previous) {
    if (lim < ci->top.p) lim = ci->top.p;
  }
  gvts_assert(lim <= L->stack_last.p + EXTRA_STACK);
  res = cast_int(lim - L->stack.p) + 1;                            
  if (res < GVTS_MINSTACK)
    res = GVTS_MINSTACK;                             
  return res;
}

void gvtsD_shrinkstack (gvts_State *L) {
  int inuse = stackinuse(L);
  int max = (inuse > GVTSI_MAXSTACK / 3) ? GVTSI_MAXSTACK : inuse * 3;

  if (inuse <= GVTSI_MAXSTACK && stacksize(L) > max) {
    int nsize = (inuse > GVTSI_MAXSTACK / 2) ? GVTSI_MAXSTACK : inuse * 2;
    gvtsD_reallocstack(L, nsize, 0);                        
  }
  else                          
    condmovestack(L,{},{});                                   
  gvtsE_shrinkCI(L);                      
}

void gvtsD_inctop (gvts_State *L) {
  gvtsD_checkstack(L, 1);
  L->top.p++;
}

void gvtsD_hook (gvts_State *L, int event, int line,
                              int ftransfer, int ntransfer) {
  gvts_Hook hook = L->hook;
  if (hook && L->allowhook) {                                 
    int mask = CIST_HOOKED;
    CallInfo *ci = L->ci;
    ptrdiff_t top = savestack(L, L->top.p);                               
    ptrdiff_t ci_top = savestack(L, ci->top.p);                          
    gvts_Debug ar;
    ar.event = event;
    ar.currentline = line;
    ar.i_ci = ci;
    if (ntransfer != 0) {
      mask |= CIST_TRAN;                                     
      ci->u2.transferinfo.ftransfer = ftransfer;
      ci->u2.transferinfo.ntransfer = ntransfer;
    }
    if (isGvts(ci) && L->top.p < ci->top.p)
      L->top.p = ci->top.p;                                          
    gvtsD_checkstack(L, GVTS_MINSTACK);                                 
    if (ci->top.p < L->top.p + GVTS_MINSTACK)
      ci->top.p = L->top.p + GVTS_MINSTACK;
    L->allowhook = 0;                                       
    ci->callstatus |= mask;
    gvts_unlock(L);
    (*hook)(L, &ar);
    gvts_lock(L);
    gvts_assert(!L->allowhook);
    L->allowhook = 1;
    ci->top.p = restorestack(L, ci_top);
    L->top.p = restorestack(L, top);
    ci->callstatus &= ~mask;
  }
}

void gvtsD_hookcall (gvts_State *L, CallInfo *ci) {
  L->oldpc = 0;                                    
  if (L->hookmask & GVTS_MASKCALL) {                        
    int event = (ci->callstatus & CIST_TAIL) ? GVTS_HOOKTAILCALL
                                             : GVTS_HOOKCALL;
    Proto *p = ci_func(ci)->p;
    ci->u.l.savedpc++;                                                
    gvtsD_hook(L, event, -1, 1, p->numparams);
    ci->u.l.savedpc--;                    
  }
}

static void rethook (gvts_State *L, CallInfo *ci, int nres) {
  if (L->hookmask & GVTS_MASKRET) {                          
    StkId firstres = L->top.p - nres;                             
    int delta = 0;                                       
    int ftransfer;
    if (isGvts(ci)) {
      Proto *p = ci_func(ci)->p;
      if (p->is_vararg)
        delta = ci->u.l.nextraargs + p->numparams + 1;
    }
    ci->func.p += delta;                                         
    ftransfer = cast(unsigned short, firstres - ci->func.p);
    gvtsD_hook(L, GVTS_HOOKRET, -1, ftransfer, nres);               
    ci->func.p -= delta;
  }
  if (isGvts(ci = ci->previous))
    L->oldpc = pcRel(ci->u.l.savedpc, ci_func(ci)->p);                   
}

static StkId tryfuncTM (gvts_State *L, StkId func) {
  const TValue *tm;
  StkId p;
  checkstackGCp(L, 1, func);                            
  tm = gvtsT_gettmbyobj(L, s2v(func), TM_CALL);                           
  if (g_unlikely(ttisnil(tm)))
    gvtsG_callerror(L, s2v(func));                       
  for (p = L->top.p; p > func; p--)                                 
    setobjs2s(L, p, p-1);
  L->top.p++;                                               
  setobj2s(L, func, tm);                                                   
  return func;
}

g_sinline void moveresults (gvts_State *L, StkId res, int nres, int wanted) {
  StkId firstresult;
  int i;
  switch (wanted) {                                       
    case 0:                        
      L->top.p = res;
      return;
    case 1:                        
      if (nres == 0)                    
        setnilvalue(s2v(res));                       
      else                           
        setobjs2s(L, res, L->top.p - nres);                               
      L->top.p = res + 1;
      return;
    case GVTS_MULTRET:
      wanted = nres;                           
      break;
    default:                                                      
      if (hastocloseCfunc(wanted)) {                               
        L->ci->callstatus |= CIST_CLSRET;                         
        L->ci->u2.nres = nres;
        res = gvtsF_close(L, res, CLOSEKTOP, 1);
        L->ci->callstatus &= ~CIST_CLSRET;
        if (L->hookmask) {                                             
          ptrdiff_t savedres = savestack(L, res);
          rethook(L, L->ci, nres);
          res = restorestack(L, savedres);                           
        }
        wanted = decodeNresults(wanted);
        if (wanted == GVTS_MULTRET)
          wanted = nres;                           
      }
      break;
  }

  firstresult = L->top.p - nres;                             
  if (nres > wanted)                      
    nres = wanted;                       
  for (i = 0; i < nres; i++)                                         
    setobjs2s(L, res + i, firstresult + i);
  for (; i < wanted; i++)                                         
    setnilvalue(s2v(res + i));
  L->top.p = res + wanted;                                        
}

void gvtsD_poscall (gvts_State *L, CallInfo *ci, int nres) {
  int wanted = ci->nresults;
  if (g_unlikely(L->hookmask && !hastocloseCfunc(wanted)))
    rethook(L, ci, nres);

  moveresults(L, ci->func.p, nres, wanted);

  gvts_assert(!(ci->callstatus &
        (CIST_HOOKED | CIST_YPCALL | CIST_FIN | CIST_TRAN | CIST_CLSRET)));
  L->ci = ci->previous;                                                
}

#define next_ci(L)  (L->ci->next ? L->ci->next : gvtsE_extendCI(L))

g_sinline CallInfo *prepCallInfo (gvts_State *L, StkId func, int nret,
                                                int mask, StkId top) {
  CallInfo *ci = L->ci = next_ci(L);                 
  ci->func.p = func;
  ci->nresults = nret;
  ci->callstatus = mask;
  ci->top.p = top;
  return ci;
}

g_sinline int precallC (gvts_State *L, StkId func, int nresults,
                                            gvts_CFunction f) {
  int n;                         
  CallInfo *ci;
  checkstackGCp(L, GVTS_MINSTACK, func);                                 
  L->ci = ci = prepCallInfo(L, func, nresults, CIST_C,
                               L->top.p + GVTS_MINSTACK);
  gvts_assert(ci->top.p <= L->stack_last.p);
  if (g_unlikely(L->hookmask & GVTS_MASKCALL)) {
    int narg = cast_int(L->top.p - func) - 1;
    gvtsD_hook(L, GVTS_HOOKCALL, -1, 1, narg);
  }
  gvts_unlock(L);
  n = (*f)(L);                          
  gvts_lock(L);
  api_checknelems(L, n);
  gvtsD_poscall(L, ci, n);
  return n;
}

int gvtsD_pretailcall (gvts_State *L, CallInfo *ci, StkId func,
                                    int narg1, int delta) {
 retry:
  switch (ttypetag(s2v(func))) {
    case GVTS_VCCL:                 
      return precallC(L, func, GVTS_MULTRET, clCvalue(s2v(func))->f);
    case GVTS_VLCF:                        
      return precallC(L, func, GVTS_MULTRET, fvalue(s2v(func)));
    case GVTS_VLCL: {                    
      Proto *p = clLvalue(s2v(func))->p;
      int fsize = p->maxstacksize;                  
      int nfixparams = p->numparams;
      int i;
      checkstackGCp(L, fsize - delta, func);
      ci->func.p -= delta;                                  
      for (i = 0; i < narg1; i++)                                        
        setobjs2s(L, ci->func.p + i, func + i);
      func = ci->func.p;                           
      for (; narg1 <= nfixparams; narg1++)
        setnilvalue(s2v(func + narg1));                                  
      ci->top.p = func + 1 + fsize;                            
      gvts_assert(ci->top.p <= L->stack_last.p);
      ci->u.l.savedpc = p->code;                      
      ci->callstatus |= CIST_TAIL;
      L->top.p = func + narg1;               
      return -1;
    }
    default: {                      
      func = tryfuncTM(L, func);                                      

      narg1++;
      goto retry;                 
    }
  }
}

CallInfo *gvtsD_precall (gvts_State *L, StkId func, int nresults) {
 retry:
  switch (ttypetag(s2v(func))) {
    case GVTS_VCCL:                 
      precallC(L, func, nresults, clCvalue(s2v(func))->f);
      return NULL;
    case GVTS_VLCF:                        
      precallC(L, func, nresults, fvalue(s2v(func)));
      return NULL;
    case GVTS_VLCL: {                    
      CallInfo *ci;
      Proto *p = clLvalue(s2v(func))->p;
      int narg = cast_int(L->top.p - func) - 1;                                
      int nfixparams = p->numparams;
      int fsize = p->maxstacksize;                  
      checkstackGCp(L, fsize, func);
      L->ci = ci = prepCallInfo(L, func, nresults, 0, func + 1 + fsize);
      ci->u.l.savedpc = p->code;                      
      for (; narg < nfixparams; narg++)
        setnilvalue(s2v(L->top.p++));                                  
      gvts_assert(ci->top.p <= L->stack_last.p);
      return ci;
    }
    default: {                      
      func = tryfuncTM(L, func);                                      

      goto retry;                                 
    }
  }
}

g_sinline void ccall (gvts_State *L, StkId func, int nResults, g_uint32 inc) {
  CallInfo *ci;
  L->nCcalls += inc;
  if (g_unlikely(getCcalls(L) >= GVTSI_MAXCCALLS)) {
    checkstackp(L, 0, func);                                   
    gvtsE_checkcstack(L);
  }
  if ((ci = gvtsD_precall(L, func, nResults)) != NULL) {                     
    ci->callstatus = CIST_FRESH;                                         
    gvtsV_execute(L, ci);               
  }
  L->nCcalls -= inc;
}

void gvtsD_call (gvts_State *L, StkId func, int nResults) {
  ccall(L, func, nResults, 1);
}

void gvtsD_callnoyield (gvts_State *L, StkId func, int nResults) {
  ccall(L, func, nResults, nyci);
}

static int finishpcallk (gvts_State *L,  CallInfo *ci) {
  int status = getcistrecst(ci);                           
  if (g_likely(status == GVTS_OK))                 
    status = GVTS_YIELD;                                   
  else {             
    StkId func = restorestack(L, ci->u2.funcidx);
    L->allowhook = getoah(ci->callstatus);                           
    func = gvtsF_close(L, func, status, 1);                                   
    gvtsD_seterrorobj(L, status, func);
    gvtsD_shrinkstack(L);                                               
    setcistrecst(ci, GVTS_OK);                             
  }
  ci->callstatus &= ~CIST_YPCALL;
  L->errfunc = ci->u.c.old_errfunc;

  return status;
}

static void finishCcall (gvts_State *L, CallInfo *ci) {
  int n;                                                
  if (ci->callstatus & CIST_CLSRET) {                      
    gvts_assert(hastocloseCfunc(ci->nresults));
    n = ci->u2.nres;                                

  }
  else {
    int status = GVTS_YIELD;                                       

    gvts_assert(ci->u.c.k != NULL && yieldable(L));
    if (ci->callstatus & CIST_YPCALL)                                   
      status = finishpcallk(L, ci);                 
    adjustresults(L, GVTS_MULTRET);                          
    gvts_unlock(L);
    n = (*ci->u.c.k)(L, status, ci->u.c.ctx);                         
    gvts_lock(L);
    api_checknelems(L, n);
  }
  gvtsD_poscall(L, ci, n);                          
}

static void unroll (gvts_State *L, void *ud) {
  CallInfo *ci;
  UNUSED(ud);
  while ((ci = L->ci) != &L->base_ci) {                              
    if (!isGvts(ci))                   
      finishCcall(L, ci);                              
    else {                    
      gvtsV_finishOp(L);                                      
      gvtsV_execute(L, ci);                                           
    }
  }
}

static CallInfo *findpcall (gvts_State *L) {
  CallInfo *ci;
  for (ci = L->ci; ci != NULL; ci = ci->previous) {                          
    if (ci->callstatus & CIST_YPCALL)
      return ci;
  }
  return NULL;                        
}

static int resume_error (gvts_State *L, const char *msg, int narg) {
  L->top.p -= narg;                                  
  setsvalue2s(L, L->top.p, gvtsS_new(L, msg));                          
  api_incr_top(L);
  gvts_unlock(L);
  return GVTS_ERRRUN;
}

static void resume (gvts_State *L, void *ud) {
  int n = *(cast(int*, ud));                           
  StkId firstArg = L->top.p - n;                      
  CallInfo *ci = L->ci;
  if (L->status == GVTS_OK)                             
    ccall(L, firstArg - 1, GVTS_MULTRET, 0);                          
  else {                                    
    gvts_assert(L->status == GVTS_YIELD);
    L->status = GVTS_OK;                                       
    if (isGvts(ci)) {                              

      gvts_assert(ci->callstatus & CIST_HOOKYIELD);
      ci->u.l.savedpc--;
      L->top.p = firstArg;                         
      gvtsV_execute(L, ci);                                      
    }
    else {                      
      if (ci->u.c.k != NULL) {                                             
        gvts_unlock(L);
        n = (*ci->u.c.k)(L, GVTS_YIELD, ci->u.c.ctx);                        
        gvts_lock(L);
        api_checknelems(L, n);
      }
      gvtsD_poscall(L, ci, n);                          
    }
    unroll(L, NULL);                        
  }
}

static int precover (gvts_State *L, int status) {
  CallInfo *ci;
  while (errorstatus(status) && (ci = findpcall(L)) != NULL) {
    L->ci = ci;                                     
    setcistrecst(ci, status);                                
    status = gvtsD_rawrunprotected(L, unroll, NULL);
  }
  return status;
}

GVTS_API int gvts_resume (gvts_State *L, gvts_State *from, int nargs,
                                      int *nresults) {
  int status;
  gvts_lock(L);
  if (L->status == GVTS_OK) {                                   
    if (L->ci != &L->base_ci)                          
      return resume_error(L, "cannot resume non-suspended coroutine", nargs);
    else if (L->top.p - (L->ci->func.p + 1) == nargs)                    
      return resume_error(L, "cannot resume dead coroutine", nargs);
  }
  else if (L->status != GVTS_YIELD)                          
    return resume_error(L, "cannot resume dead coroutine", nargs);
  L->nCcalls = (from) ? getCcalls(from) : 0;
  if (getCcalls(L) >= GVTSI_MAXCCALLS)
    return resume_error(L, "C stack overflow", nargs);
  L->nCcalls++;
  gvtsi_userstateresume(L, nargs);
  api_checknelems(L, (L->status == GVTS_OK) ? nargs + 1 : nargs);
  status = gvtsD_rawrunprotected(L, resume, &nargs);

  status = precover(L, status);
  if (g_likely(!errorstatus(status)))
    gvts_assert(status == L->status);                           
  else {                           
    L->status = cast_byte(status);                             
    gvtsD_seterrorobj(L, status, L->top.p);                          
    L->ci->top.p = L->top.p;
  }
  *nresults = (status == GVTS_YIELD) ? L->ci->u2.nyield
                                    : cast_int(L->top.p - (L->ci->func.p + 1));
  gvts_unlock(L);
  return status;
}

GVTS_API int gvts_isyieldable (gvts_State *L) {
  return yieldable(L);
}

GVTS_API int gvts_yieldk (gvts_State *L, int nresults, gvts_KContext ctx,
                        gvts_KFunction k) {
  CallInfo *ci;
  gvtsi_userstateyield(L, nresults);
  gvts_lock(L);
  ci = L->ci;
  api_checknelems(L, nresults);
  if (g_unlikely(!yieldable(L))) {
    if (L != G(L)->mainthread)
      gvtsG_runerror(L, u8"不能在 C 调用边界之间让出");
    else
      gvtsG_runerror(L, u8"不能在协程之外让出");
  }
  L->status = GVTS_YIELD;
  ci->u2.nyield = nresults;                              
  if (isGvts(ci)) {                      
    gvts_assert(!isGvtscode(ci));
    api_check(L, nresults == 0, "hooks cannot yield values");
    api_check(L, k == NULL, "hooks cannot continue after yielding");
  }
  else {
    if ((ci->u.c.k = k) != NULL)                                
      ci->u.c.ctx = ctx;                    
    gvtsD_throw(L, GVTS_YIELD);
  }
  gvts_assert(ci->callstatus & CIST_HOOKED);                             
  gvts_unlock(L);
  return 0;                             
}

struct CloseP {
  StkId level;
  int status;
};

static void closepaux (gvts_State *L, void *ud) {
  struct CloseP *pcl = cast(struct CloseP *, ud);
  gvtsF_close(L, pcl->level, pcl->status, 0);
}

int gvtsD_closeprotected (gvts_State *L, ptrdiff_t level, int status) {
  CallInfo *old_ci = L->ci;
  lu_byte old_allowhooks = L->allowhook;
  for (;;) {                                                  
    struct CloseP pcl;
    pcl.level = restorestack(L, level); pcl.status = status;
    status = gvtsD_rawrunprotected(L, &closepaux, &pcl);
    if (g_likely(status == GVTS_OK))                       
      return pcl.status;
    else {                                                         
      L->ci = old_ci;
      L->allowhook = old_allowhooks;
    }
  }
}

int gvtsD_pcall (gvts_State *L, Pfunc func, void *u,
                ptrdiff_t old_top, ptrdiff_t ef) {
  int status;
  CallInfo *old_ci = L->ci;
  lu_byte old_allowhooks = L->allowhook;
  ptrdiff_t old_errfunc = L->errfunc;
  L->errfunc = ef;
  status = gvtsD_rawrunprotected(L, func, u);
  if (g_unlikely(status != GVTS_OK)) {                          
    L->ci = old_ci;
    L->allowhook = old_allowhooks;
    status = gvtsD_closeprotected(L, old_top, status);
    gvtsD_seterrorobj(L, status, restorestack(L, old_top));
    gvtsD_shrinkstack(L);                                               
  }
  L->errfunc = old_errfunc;
  return status;
}

struct SParser {                          
  ZIO *z;
  Mbuffer buff;                                             
  Dyndata dyd;                                             
  const char *mode;
  const char *name;
};

static void checkmode (gvts_State *L, const char *mode, const char *x) {
  if (mode && strchr(mode, x[0]) == NULL) {
    gvtsO_pushfstring(L,
       "attempt to load a %s chunk (mode is '%s')", x, mode);
    gvtsD_throw(L, GVTS_ERRSYNTAX);
  }
}

static void f_parser (gvts_State *L, void *ud) {
  LClosure *cl;
  struct SParser *p = cast(struct SParser *, ud);
  int c = zgetc(p->z);                            
  if (c == GVTS_SIGNATURE[0]) {
    checkmode(L, p->mode, "binary");
    cl = gvtsU_undump(L, p->z, p->name);
  }
  else {
    checkmode(L, p->mode, "text");
    cl = gvtsY_parser(L, p->z, &p->buff, &p->dyd, p->name, c);
  }
  gvts_assert(cl->nupvalues == cl->p->sizeupvalues);
  gvtsF_initupvals(L, cl);
}

int gvtsD_protectedparser (gvts_State *L, ZIO *z, const char *name,
                                        const char *mode) {
  struct SParser p;
  int status;
  incnny(L);                                   
  p.z = z; p.name = name; p.mode = mode;
  p.dyd.actvar.arr = NULL; p.dyd.actvar.size = 0;
  p.dyd.gt.arr = NULL; p.dyd.gt.size = 0;
  p.dyd.label.arr = NULL; p.dyd.label.size = 0;
  gvtsZ_initbuffer(L, &p.buff);
  status = gvtsD_pcall(L, f_parser, &p, savestack(L, L->top.p), L->errfunc);
  gvtsZ_freebuffer(L, &p.buff);
  gvtsM_freearray(L, p.dyd.actvar.arr, p.dyd.actvar.size);
  gvtsM_freearray(L, p.dyd.gt.arr, p.dyd.gt.size);
  gvtsM_freearray(L, p.dyd.label.arr, p.dyd.label.size);
  decnny(L);
  return status;
}
