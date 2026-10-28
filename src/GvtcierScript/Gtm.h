#ifndef ltm_h
#define ltm_h

#include "Gobject.h"

typedef enum {
  TM_INDEX,
  TM_NEWINDEX,
  TM_GC,
  TM_MODE,
  TM_LEN,
  TM_EQ,                                        
  TM_ADD,
  TM_SUB,
  TM_MUL,
  TM_MOD,
  TM_POW,
  TM_DIV,
  TM_IDIV,
  TM_BAND,
  TM_BOR,
  TM_BXOR,
  TM_SHL,
  TM_SHR,
  TM_UNM,
  TM_BNOT,
  TM_LT,
  TM_LE,
  TM_CONCAT,
  TM_CALL,
  TM_CLOSE,
  TM_N		                                    
} TMS;

#define maskflags	(~(~0u << (TM_EQ + 1)))

#define notm(tm)	ttisnil(tm)

#define gfasttm(g,et,e) ((et) == NULL ? NULL : \
  ((et)->flags & (1u<<(e))) ? NULL : gvtsT_gettm(et, e, (g)->tmname[e]))

#define fasttm(l,et,e)	gfasttm(G(l), et, e)

#define ttypename(x)	gvtsT_typenames_[(x) + 1]

GVTSI_DDEC(const char *const gvtsT_typenames_[GVTS_TOTALTYPES];)

GVTSI_FUNC const char *gvtsT_objtypename (gvts_State *L, const TValue *o);

GVTSI_FUNC const TValue *gvtsT_gettm (Table *events, TMS event, TString *ename);
GVTSI_FUNC const TValue *gvtsT_gettmbyobj (gvts_State *L, const TValue *o,
                                                       TMS event);
GVTSI_FUNC void gvtsT_init (gvts_State *L);

GVTSI_FUNC void gvtsT_callTM (gvts_State *L, const TValue *f, const TValue *p1,
                            const TValue *p2, const TValue *p3);
GVTSI_FUNC void gvtsT_callTMres (gvts_State *L, const TValue *f,
                            const TValue *p1, const TValue *p2, StkId p3);
GVTSI_FUNC void gvtsT_trybinTM (gvts_State *L, const TValue *p1, const TValue *p2,
                              StkId res, TMS event);
GVTSI_FUNC void gvtsT_tryconcatTM (gvts_State *L);
GVTSI_FUNC void gvtsT_trybinassocTM (gvts_State *L, const TValue *p1,
       const TValue *p2, int inv, StkId res, TMS event);
GVTSI_FUNC void gvtsT_trybiniTM (gvts_State *L, const TValue *p1, gvts_Integer i2,
                               int inv, StkId res, TMS event);
GVTSI_FUNC int gvtsT_callorderTM (gvts_State *L, const TValue *p1,
                                const TValue *p2, TMS event);
GVTSI_FUNC int gvtsT_callorderiTM (gvts_State *L, const TValue *p1, int v2,
                                 int inv, int isfloat, TMS event);

GVTSI_FUNC void gvtsT_adjustvarargs (gvts_State *L, int nfixparams,
                                   struct CallInfo *ci, const Proto *p);
GVTSI_FUNC void gvtsT_getvarargs (gvts_State *L, struct CallInfo *ci,
                                              StkId where, int wanted);

#endif
