#ifndef lfunc_h
#define lfunc_h

#include "Gobject.h"

#define sizeCclosure(n)	(cast_int(offsetof(CClosure, upvalue)) + \
                         cast_int(sizeof(TValue)) * (n))

#define sizeLclosure(n)	(cast_int(offsetof(LClosure, upvals)) + \
                         cast_int(sizeof(TValue *)) * (n))

#define isintwups(L)	(L->twups != L)

#define MAXUPVAL	255

#define upisopen(up)	((up)->v.p != &(up)->u.value)

#define uplevel(up)	check_exp(upisopen(up), cast(StkId, (up)->v.p))

#define MAXMISS		10

#define CLOSEKTOP	(-1)

GVTSI_FUNC Proto *gvtsF_newproto (gvts_State *L);
GVTSI_FUNC CClosure *gvtsF_newCclosure (gvts_State *L, int nupvals);
GVTSI_FUNC LClosure *gvtsF_newLclosure (gvts_State *L, int nupvals);
GVTSI_FUNC void gvtsF_initupvals (gvts_State *L, LClosure *cl);
GVTSI_FUNC UpVal *gvtsF_findupval (gvts_State *L, StkId level);
GVTSI_FUNC void gvtsF_newtbcupval (gvts_State *L, StkId level);
GVTSI_FUNC void gvtsF_closeupval (gvts_State *L, StkId level);
GVTSI_FUNC StkId gvtsF_close (gvts_State *L, StkId level, int status, int yy);
GVTSI_FUNC void gvtsF_unlinkupval (UpVal *uv);
GVTSI_FUNC void gvtsF_freeproto (gvts_State *L, Proto *f);
GVTSI_FUNC const char *gvtsF_getlocalname (const Proto *func, int local_number,
                                         int pc);

#endif
