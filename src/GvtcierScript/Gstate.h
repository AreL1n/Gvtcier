#ifndef lstate_h
#define lstate_h

#include "gvts.h"

typedef struct CallInfo CallInfo;

#include "Gobject.h"
#include "Gtm.h"
#include "Gzio.h"

#define yieldable(L)		(((L)->nCcalls & 0xffff0000) == 0)

#define getCcalls(L)	((L)->nCcalls & 0xffff)

#define incnny(L)	((L)->nCcalls += 0x10000)

#define decnny(L)	((L)->nCcalls -= 0x10000)

#define nyci	(0x10000 | 1)

struct gvts_longjmp;                        

#if !defined(g_signalT)
#include <signal.h>
#define g_signalT	sig_atomic_t
#endif

#define EXTRA_STACK   5

#define BASIC_STACK_SIZE        (2*GVTS_MINSTACK)

#define stacksize(th)	cast_int((th)->stack_last.p - (th)->stack.p)

#define KGC_INC		0	                    
#define KGC_GEN		1	                     

typedef struct stringtable {
  TString **hash;
  int nuse;                          
  int size;
} stringtable;

struct CallInfo {
  StkIdRel func;                                   
  StkIdRel	top;                             
  struct CallInfo *previous, *next;                         
  union {
    struct {                              
      const Instruction *savedpc;
      volatile g_signalT trap;                                        
      int nextraargs;                                                
    } l;
    struct {                            
      gvts_KFunction k;                                      
      ptrdiff_t old_errfunc;
      gvts_KContext ctx;                                       
    } c;
  } u;
  union {
    int funcidx;                             
    int nyield;                                
    int nres;                                 
    struct {                                                             
      unsigned short ftransfer;                                         
      unsigned short ntransfer;                                    
    } transferinfo;
  } u2;
  short nresults;                                                     
  unsigned short callstatus;
};

#define CIST_OAH	(1<<0)	                                   
#define CIST_C		(1<<1)	                                  
#define CIST_FRESH	(1<<2)	                                             
#define CIST_HOOKED	(1<<3)	                                  
#define CIST_YPCALL	(1<<4)	                                      
#define CIST_TAIL	(1<<5)	                          
#define CIST_HOOKYIELD	(1<<6)	                              
#define CIST_FIN	(1<<7)	                                   
#define CIST_TRAN	(1<<8)	                                   
#define CIST_CLSRET	(1<<9)                                         

#define CIST_RECST	10
#if defined(GVTS_COMPAT_LT_LE)
#define CIST_LEQ	(1<<13)                           
#endif

#define getcistrecst(ci)     (((ci)->callstatus >> CIST_RECST) & 7)
#define setcistrecst(ci,st)  \
  check_exp(((st) & 7) == (st),                                        \
            ((ci)->callstatus = ((ci)->callstatus & ~(7 << CIST_RECST))  \
                                                  | ((st) << CIST_RECST)))

#define isGvts(ci)	(!((ci)->callstatus & CIST_C))

#define isGvtscode(ci)	(!((ci)->callstatus & (CIST_C | CIST_HOOKED)))

#define setoah(st,v)	((st) = ((st) & ~CIST_OAH) | (v))
#define getoah(st)	((st) & CIST_OAH)

typedef struct global_State {
  gvts_Alloc frealloc;                                     
  void *ud;                                           
  g_mem totalbytes;                                                    
  g_mem GCdebt;                                                            
  lu_mem GCestimate;                                                    
  lu_mem lastatomic;                                              
  stringtable strt;                              
  TValue g_registry;
  TValue nilvalue;                   
  unsigned int seed;                                  
  lu_byte currentwhite;
  lu_byte gcstate;                                  
  lu_byte gckind;                          
  lu_byte gcstopem;                                   
  lu_byte genminormul;                                                  
  lu_byte genmajormul;                                                  
  lu_byte gcstp;                                     
  lu_byte gcemergency;                                               
  lu_byte gcpause;                                            
  lu_byte gcstepmul;                  
  lu_byte gcstepsize;                                
  GCObject *allgc;                                       
  GCObject **sweepgc;                                         
  GCObject *finobj;                                                   
  GCObject *gray;                            
  GCObject *grayagain;                                                  
  GCObject *weak;                                       
  GCObject *ephemeron;                                            
  GCObject *allweak;                               
  GCObject *tobefnz;                                 
  GCObject *fixedgc;                                           

  GCObject *survival;                                                   
  GCObject *old1;                             
  GCObject *reallyold;                                                      
  GCObject *firstold1;                                              
  GCObject *finobjsur;                                                
  GCObject *finobjold1;                                            
  GCObject *finobjrold;                                                  
  struct gvts_State *twups;                                          
  gvts_CFunction panic;                                          
  struct gvts_State *mainthread;
  TString *memerrmsg;                                            
  TString *tmname[TM_N];                                   
  struct Table *mt[GVTS_NUMTYPES];                                  
  TString *strcache[STRCACHE_N][STRCACHE_M];                                
  gvts_WarnFunction warnf;                        
  void *ud_warn;                                        
} global_State;

struct gvts_State {
  CommonHeader;
  lu_byte status;
  lu_byte allowhook;
  unsigned short nci;                                    
  StkIdRel top;                                    
  global_State *g_G;
  CallInfo *ci;                                      
  StkIdRel stack_last;                                       
  StkIdRel stack;                  
  UpVal *openupval;                                           
  StkIdRel tbclist;                                      
  GCObject *gclist;
  struct gvts_State *twups;                                          
  struct gvts_longjmp *errorJmp;                                   
  CallInfo base_ci;                                                
  volatile gvts_Hook hook;
  ptrdiff_t errfunc;                                                     
  g_uint32 nCcalls;                                                   
  int oldpc;                      
  int basehookcount;
  int hookcount;
  volatile g_signalT hookmask;
};

#define G(L)	(L->g_G)

#define completestate(g)	ttisnil(&g->nilvalue)

union GCUnion {
  GCObject gc;                     
  struct TString ts;
  struct Udata u;
  union Closure cl;
  struct Table h;
  struct Proto p;
  struct gvts_State th;              
  struct UpVal upv;
};

#define cast_u(o)	cast(union GCUnion *, (o))

#define gco2ts(o)  \
	check_exp(novariant((o)->tt) == GVTS_TSTRING, &((cast_u(o))->ts))
#define gco2u(o)  check_exp((o)->tt == GVTS_VUSERDATA, &((cast_u(o))->u))
#define gco2lcl(o)  check_exp((o)->tt == GVTS_VLCL, &((cast_u(o))->cl.l))
#define gco2ccl(o)  check_exp((o)->tt == GVTS_VCCL, &((cast_u(o))->cl.c))
#define gco2cl(o)  \
	check_exp(novariant((o)->tt) == GVTS_TFUNCTION, &((cast_u(o))->cl))
#define gco2t(o)  check_exp((o)->tt == GVTS_VTABLE, &((cast_u(o))->h))
#define gco2p(o)  check_exp((o)->tt == GVTS_VPROTO, &((cast_u(o))->p))
#define gco2th(o)  check_exp((o)->tt == GVTS_VTHREAD, &((cast_u(o))->th))
#define gco2upv(o)	check_exp((o)->tt == GVTS_VUPVAL, &((cast_u(o))->upv))

#define obj2gco(v)	check_exp((v)->tt >= GVTS_TSTRING, &(cast_u(v)->gc))

#define gettotalbytes(g)	cast(lu_mem, (g)->totalbytes + (g)->GCdebt)

GVTSI_FUNC void gvtsE_setdebt (global_State *g, g_mem debt);
GVTSI_FUNC void gvtsE_freethread (gvts_State *L, gvts_State *L1);
GVTSI_FUNC CallInfo *gvtsE_extendCI (gvts_State *L);
GVTSI_FUNC void gvtsE_shrinkCI (gvts_State *L);
GVTSI_FUNC void gvtsE_checkcstack (gvts_State *L);
GVTSI_FUNC void gvtsE_incCstack (gvts_State *L);
GVTSI_FUNC void gvtsE_warning (gvts_State *L, const char *msg, int tocont);
GVTSI_FUNC void gvtsE_warnerror (gvts_State *L, const char *where);
GVTSI_FUNC int gvtsE_resetthread (gvts_State *L, int status);

#endif
