#ifndef ldo_h
#define ldo_h

#include "Glimits.h"
#include "Gobject.h"
#include "Gstate.h"
#include "Gzio.h"

#define gvtsD_checkstackaux(L,n,pre,pos)  \
	if (g_unlikely(L->stack_last.p - L->top.p <= (n))) \
	  { pre; gvtsD_growstack(L, n, 1); pos; } \
        else { condmovestack(L,pre,pos); }

#define gvtsD_checkstack(L,n)	gvtsD_checkstackaux(L,n,(void)0,(void)0)

#define savestack(L,pt)		(cast_charp(pt) - cast_charp(L->stack.p))
#define restorestack(L,n)	cast(StkId, cast_charp(L->stack.p) + (n))

#define checkstackp(L,n,p)  \
  gvtsD_checkstackaux(L, n, \
    ptrdiff_t t__ = savestack(L, p),                 \
    p = restorestack(L, t__))                               

#define checkstackGCp(L,n,p)  \
  gvtsD_checkstackaux(L, n, \
    ptrdiff_t t__ = savestack(L, p);                 \
    gvtsC_checkGC(L),                               \
    p = restorestack(L, t__))                               

#define checkstackGC(L,fsize)  \
	gvtsD_checkstackaux(L, (fsize), gvtsC_checkGC(L), (void)0)

typedef void (*Pfunc) (gvts_State *L, void *ud);

GVTSI_FUNC void gvtsD_seterrorobj (gvts_State *L, int errcode, StkId oldtop);
GVTSI_FUNC int gvtsD_protectedparser (gvts_State *L, ZIO *z, const char *name,
                                                  const char *mode);
GVTSI_FUNC void gvtsD_hook (gvts_State *L, int event, int line,
                                        int fTransfer, int nTransfer);
GVTSI_FUNC void gvtsD_hookcall (gvts_State *L, CallInfo *ci);
GVTSI_FUNC int gvtsD_pretailcall (gvts_State *L, CallInfo *ci, StkId func,
                                              int narg1, int delta);
GVTSI_FUNC CallInfo *gvtsD_precall (gvts_State *L, StkId func, int nResults);
GVTSI_FUNC void gvtsD_call (gvts_State *L, StkId func, int nResults);
GVTSI_FUNC void gvtsD_callnoyield (gvts_State *L, StkId func, int nResults);
GVTSI_FUNC int gvtsD_closeprotected (gvts_State *L, ptrdiff_t level, int status);
GVTSI_FUNC int gvtsD_pcall (gvts_State *L, Pfunc func, void *u,
                                        ptrdiff_t oldtop, ptrdiff_t ef);
GVTSI_FUNC void gvtsD_poscall (gvts_State *L, CallInfo *ci, int nres);
GVTSI_FUNC int gvtsD_reallocstack (gvts_State *L, int newsize, int raiseerror);
GVTSI_FUNC int gvtsD_growstack (gvts_State *L, int n, int raiseerror);
GVTSI_FUNC void gvtsD_shrinkstack (gvts_State *L);
GVTSI_FUNC void gvtsD_inctop (gvts_State *L);

GVTSI_FUNC g_noret gvtsD_throw (gvts_State *L, int errcode);
GVTSI_FUNC int gvtsD_rawrunprotected (gvts_State *L, Pfunc f, void *ud);

#endif
