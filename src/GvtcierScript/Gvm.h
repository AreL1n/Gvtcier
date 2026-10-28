#ifndef lvm_h
#define lvm_h

#include "Gdo.h"
#include "Gobject.h"
#include "Gtm.h"

#if !defined(GVTS_NOCVTN2S)
#define cvt2str(o)	ttisnumber(o)
#else
#define cvt2str(o)	0	                                           
#endif

#if !defined(GVTS_NOCVTS2N)
#define cvt2num(o)	ttisstring(o)
#else
#define cvt2num(o)	0	                                           
#endif

#if !defined(GVTS_FLOORN2I)
#define GVTS_FLOORN2I		F2Ieq
#endif

typedef enum {
  F2Ieq,                                                    
  F2Ifloor,                                     
  F2Iceil                                      
} F2Imod;

#define tonumber(o,n) \
	(ttisfloat(o) ? (*(n) = fltvalue(o), 1) : gvtsV_tonumber_(o,n))

#define tonumberns(o,n) \
	(ttisfloat(o) ? ((n) = fltvalue(o), 1) : \
	(ttisinteger(o) ? ((n) = cast_num(ivalue(o)), 1) : 0))

#define tointeger(o,i) \
  (g_likely(ttisinteger(o)) ? (*(i) = ivalue(o), 1) \
                          : gvtsV_tointeger(o,i,GVTS_FLOORN2I))

#define tointegerns(o,i) \
  (g_likely(ttisinteger(o)) ? (*(i) = ivalue(o), 1) \
                          : gvtsV_tointegerns(o,i,GVTS_FLOORN2I))

#define intop(op,v1,v2) g_castU2S(g_castS2U(v1) op g_castS2U(v2))

#define gvtsV_rawequalobj(t1,t2)		gvtsV_equalobj(NULL,t1,t2)

#define gvtsV_fastget(L,t,k,slot,f) \
  (!ttistable(t)  \
   ? (slot = NULL, 0)                                                     \
   : (slot = f(hvalue(t), k),                             \
      !isempty(slot)))                         

#define gvtsV_fastgeti(L,t,k,slot) \
  (!ttistable(t)  \
   ? (slot = NULL, 0)                                                     \
   : (slot = (g_castS2U(k) - 1u < hvalue(t)->alimit) \
              ? &hvalue(t)->array[k - 1] : gvtsH_getint(hvalue(t), k), \
      !isempty(slot)))                         

#define gvtsV_finishfastset(L,t,slot,v) \
    { setobj2t(L, cast(TValue *,slot), v); \
      gvtsC_barrierback(L, gcvalue(t), v); }

#define gvtsV_shiftr(x,y)	gvtsV_shiftl(x,intop(-, 0, y))

GVTSI_FUNC int gvtsV_equalobj (gvts_State *L, const TValue *t1, const TValue *t2);
GVTSI_FUNC int gvtsV_lessthan (gvts_State *L, const TValue *l, const TValue *r);
GVTSI_FUNC int gvtsV_lessequal (gvts_State *L, const TValue *l, const TValue *r);
GVTSI_FUNC int gvtsV_tonumber_ (const TValue *obj, gvts_Number *n);
GVTSI_FUNC int gvtsV_tointeger (const TValue *obj, gvts_Integer *p, F2Imod mode);
GVTSI_FUNC int gvtsV_tointegerns (const TValue *obj, gvts_Integer *p,
                                F2Imod mode);
GVTSI_FUNC int gvtsV_flttointeger (gvts_Number n, gvts_Integer *p, F2Imod mode);
GVTSI_FUNC void gvtsV_finishget (gvts_State *L, const TValue *t, TValue *key,
                               StkId val, const TValue *slot);
GVTSI_FUNC void gvtsV_finishset (gvts_State *L, const TValue *t, TValue *key,
                               TValue *val, const TValue *slot);
GVTSI_FUNC void gvtsV_finishOp (gvts_State *L);
GVTSI_FUNC void gvtsV_execute (gvts_State *L, CallInfo *ci);
GVTSI_FUNC void gvtsV_concat (gvts_State *L, int total);
GVTSI_FUNC gvts_Integer gvtsV_idiv (gvts_State *L, gvts_Integer x, gvts_Integer y);
GVTSI_FUNC gvts_Integer gvtsV_mod (gvts_State *L, gvts_Integer x, gvts_Integer y);
GVTSI_FUNC gvts_Number gvtsV_modf (gvts_State *L, gvts_Number x, gvts_Number y);
GVTSI_FUNC gvts_Integer gvtsV_shiftl (gvts_Integer x, gvts_Integer y);
GVTSI_FUNC void gvtsV_objlen (gvts_State *L, StkId ra, const TValue *rb);

#endif
