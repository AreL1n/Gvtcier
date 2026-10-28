#ifndef ltable_h
#define ltable_h

#include "Gobject.h"

#define gnode(t,i)	(&(t)->node[i])
#define gval(n)		(&(n)->i_val)
#define gnext(n)	((n)->u.next)

#define invalidateTMcache(t)	((t)->flags &= ~maskflags)

#define isdummy(t)		((t)->lastfree == NULL)

#define allocsizenode(t)	(isdummy(t) ? 0 : sizenode(t))

#define nodefromval(v)	cast(Node *, (v))

GVTSI_FUNC const TValue *gvtsH_getint (Table *t, gvts_Integer key);
GVTSI_FUNC void gvtsH_setint (gvts_State *L, Table *t, gvts_Integer key,
                                                    TValue *value);
GVTSI_FUNC const TValue *gvtsH_getshortstr (Table *t, TString *key);
GVTSI_FUNC const TValue *gvtsH_getstr (Table *t, TString *key);
GVTSI_FUNC const TValue *gvtsH_get (Table *t, const TValue *key);
GVTSI_FUNC void gvtsH_set (gvts_State *L, Table *t, const TValue *key,
                                                 TValue *value);
GVTSI_FUNC void gvtsH_finishset (gvts_State *L, Table *t, const TValue *key,
                                       const TValue *slot, TValue *value);
GVTSI_FUNC Table *gvtsH_new (gvts_State *L);
GVTSI_FUNC void gvtsH_resize (gvts_State *L, Table *t, unsigned int nasize,
                                                    unsigned int nhsize);
GVTSI_FUNC void gvtsH_resizearray (gvts_State *L, Table *t, unsigned int nasize);
GVTSI_FUNC void gvtsH_free (gvts_State *L, Table *t);
GVTSI_FUNC int gvtsH_next (gvts_State *L, Table *t, StkId key);
GVTSI_FUNC gvts_Unsigned gvtsH_getn (Table *t);
GVTSI_FUNC unsigned int gvtsH_realasize (const Table *t);

#if defined(GVTS_DEBUG)
GVTSI_FUNC Node *gvtsH_mainposition (const Table *t, const TValue *key);
#endif

#endif
