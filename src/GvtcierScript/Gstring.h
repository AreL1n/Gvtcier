#ifndef lstring_h
#define lstring_h

#include "Ggc.h"
#include "Gobject.h"
#include "Gstate.h"

#define MEMERRMSG       "not enough memory"

#define sizelstring(l)  (offsetof(TString, contents) + ((l) + 1) * sizeof(char))

#define gvtsS_newliteral(L, s)	(gvtsS_newlstr(L, "" s, \
                                 (sizeof(s)/sizeof(char))-1))

#define isreserved(s)	((s)->tt == GVTS_VSHRSTR && (s)->extra > 0)

#define eqshrstr(a,b)	check_exp((a)->tt == GVTS_VSHRSTR, (a) == (b))

GVTSI_FUNC unsigned int gvtsS_hash (const char *str, size_t l, unsigned int seed);
GVTSI_FUNC unsigned int gvtsS_hashlongstr (TString *ts);
GVTSI_FUNC int gvtsS_eqlngstr (TString *a, TString *b);
GVTSI_FUNC void gvtsS_resize (gvts_State *L, int newsize);
GVTSI_FUNC void gvtsS_clearcache (global_State *g);
GVTSI_FUNC void gvtsS_init (gvts_State *L);
GVTSI_FUNC void gvtsS_remove (gvts_State *L, TString *ts);
GVTSI_FUNC Udata *gvtsS_newudata (gvts_State *L, size_t s, int nuvalue);
GVTSI_FUNC TString *gvtsS_newlstr (gvts_State *L, const char *str, size_t l);
GVTSI_FUNC TString *gvtsS_new (gvts_State *L, const char *str);
GVTSI_FUNC TString *gvtsS_createlngstrobj (gvts_State *L, size_t l);

#endif
