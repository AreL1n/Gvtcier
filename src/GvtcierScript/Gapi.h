#ifndef lapi_h
#define lapi_h

#include "Glimits.h"
#include "Gstate.h"

#define api_incr_top(L)	{L->top.p++; \
			 api_check(L, L->top.p <= L->ci->top.p, \
					"stack overflow");}

#define adjustresults(L,nres) \
    { if ((nres) <= GVTS_MULTRET && L->ci->top.p < L->top.p) \
	L->ci->top.p = L->top.p; }

#define api_checknelems(L,n) \
	api_check(L, (n) < (L->top.p - L->ci->func.p), \
			  "not enough elements in the stack")

#define hastocloseCfunc(n)	((n) < GVTS_MULTRET)

#define codeNresults(n)		(-(n) - 3)
#define decodeNresults(n)	(-(n) - 3)

#endif
