#ifndef ldebug_h
#define ldebug_h

#include "Gstate.h"

#define pcRel(pc, p)	(cast_int((pc) - (p)->code) - 1)

#define ci_func(ci)		(clLvalue(s2v((ci)->func.p)))

#define resethookcount(L)	(L->hookcount = L->basehookcount)

#define ABSLINEINFO	(-0x80)

#if !defined(MAXIWTHABS)
#define MAXIWTHABS	128
#endif

GVTSI_FUNC int gvtsG_getfuncline (const Proto *f, int pc);
GVTSI_FUNC const char *gvtsG_findlocal (gvts_State *L, CallInfo *ci, int n,
                                                    StkId *pos);
GVTSI_FUNC g_noret gvtsG_typeerror (gvts_State *L, const TValue *o,
                                                const char *opname);
GVTSI_FUNC g_noret gvtsG_callerror (gvts_State *L, const TValue *o);
GVTSI_FUNC g_noret gvtsG_forerror (gvts_State *L, const TValue *o,
                                               const char *what);
GVTSI_FUNC g_noret gvtsG_concaterror (gvts_State *L, const TValue *p1,
                                                  const TValue *p2);
GVTSI_FUNC g_noret gvtsG_opinterror (gvts_State *L, const TValue *p1,
                                                 const TValue *p2,
                                                 const char *msg);
GVTSI_FUNC g_noret gvtsG_tointerror (gvts_State *L, const TValue *p1,
                                                 const TValue *p2);
GVTSI_FUNC g_noret gvtsG_ordererror (gvts_State *L, const TValue *p1,
                                                 const TValue *p2);
GVTSI_FUNC g_noret gvtsG_runerror (gvts_State *L, const char *fmt, ...);
GVTSI_FUNC const char *gvtsG_addinfo (gvts_State *L, const char *msg,
                                                  TString *src, int line);
GVTSI_FUNC g_noret gvtsG_errormsg (gvts_State *L);
GVTSI_FUNC int gvtsG_traceexec (gvts_State *L, const Instruction *pc);
GVTSI_FUNC int gvtsG_tracecall (gvts_State *L);

#endif
