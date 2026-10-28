#ifndef llimits_h
#define llimits_h

#include <limits.h>
#include <stddef.h>

#include "gvts.h"

#if defined(GVTSI_MEM)		                             
typedef GVTSI_UMEM lu_mem;
typedef GVTSI_MEM g_mem;
#elif GVTSI_IS32INT	        
typedef size_t lu_mem;
typedef ptrdiff_t g_mem;
#else                   	        
typedef unsigned long lu_mem;
typedef long g_mem;
#endif				       

typedef unsigned char lu_byte;
typedef signed char ls_byte;

#define MAX_SIZET	((size_t)(~(size_t)0))

#define MAX_SIZE	(sizeof(size_t) < sizeof(gvts_Integer) ? MAX_SIZET \
                          : (size_t)(GVTS_MAXINTEGER))

#define MAX_LUMEM	((lu_mem)(~(lu_mem)0))

#define MAX_LMEM	((g_mem)(MAX_LUMEM >> 1))

#define MAX_INT		INT_MAX                               

#define log2maxs(t)	(sizeof(t) * 8 - 2)

#define ispow2(x)	(((x) & ((x) - 1)) == 0)

#define LL(x)   (sizeof(x)/sizeof(char) - 1)

#if !defined(GVTS_USE_C89) && defined(__STDC_VERSION__) && \
    __STDC_VERSION__ >= 199901L
#include <stdint.h>
#if defined(UINTPTR_MAX)                                         
#define L_P2I	uintptr_t
#else                    
#define L_P2I	uintmax_t                                         
#endif
#else                  
#define L_P2I	size_t
#endif

#define point2uint(p)	((unsigned int)((L_P2I)(p) & UINT_MAX))

typedef GVTSI_UACNUMBER g_uacNumber;
typedef GVTSI_UACINT g_uacInt;

#if defined GVTSI_ASSERT
#undef NDEBUG
#include <assert.h>
#define gvts_assert(c)           assert(c)
#endif

#if defined(gvts_assert)
#define check_exp(c,e)		(gvts_assert(c), (e))

#define gvts_longassert(c)	((c) ? (void)0 : gvts_assert(0))
#else
#define gvts_assert(c)		((void)0)
#define check_exp(c,e)		(e)
#define gvts_longassert(c)	((void)0)
#endif

#if !defined(gvtsi_apicheck)
#define gvtsi_apicheck(l,e)	((void)l, gvts_assert(e))
#endif

#define api_check(l,e,msg)	gvtsi_apicheck(l,(e) && msg)

#if !defined(UNUSED)
#define UNUSED(x)	((void)(x))
#endif

#define cast(t, exp)	((t)(exp))

#define cast_void(i)	cast(void, (i))
#define cast_voidp(i)	cast(void *, (i))
#define cast_num(i)	cast(gvts_Number, (i))
#define cast_int(i)	cast(int, (i))
#define cast_uint(i)	cast(unsigned int, (i))
#define cast_byte(i)	cast(lu_byte, (i))
#define cast_uchar(i)	cast(unsigned char, (i))
#define cast_char(i)	cast(char, (i))
#define cast_charp(i)	cast(char *, (i))
#define cast_sizet(i)	cast(size_t, (i))

#if !defined(g_castS2U)
#define g_castS2U(i)	((gvts_Unsigned)(i))
#endif

#if !defined(g_castU2S)
#define g_castU2S(i)	((gvts_Integer)(i))
#endif

#if !defined(g_noret)

#if defined(__GNUC__)
#define g_noret		void __attribute__((noreturn))
#elif defined(_MSC_VER) && _MSC_VER >= 1200
#define g_noret		void __declspec(noreturn)
#else
#define g_noret		void
#endif

#endif

#if !defined(GVTS_USE_C89)
#define g_inline	inline
#elif defined(__GNUC__)
#define g_inline	__inline__
#else
#define g_inline	           
#endif

#define g_sinline	static g_inline

#if GVTSI_IS32INT
typedef unsigned int g_uint32;
#else
typedef unsigned long g_uint32;
#endif

typedef g_uint32 Instruction;

#if !defined(GVTSI_MAXSHORTLEN)
#define GVTSI_MAXSHORTLEN	40
#endif

#if !defined(MINSTRTABSIZE)
#define MINSTRTABSIZE	128
#endif

#if !defined(STRCACHE_N)
#define STRCACHE_N		53
#define STRCACHE_M		2
#endif

#if !defined(GVTS_MINBUFFER)
#define GVTS_MINBUFFER	32
#endif

#if !defined(GVTSI_MAXCCALLS)
#define GVTSI_MAXCCALLS		200
#endif

#if !defined(gvts_lock)
#define gvts_lock(L)	((void) 0)
#define gvts_unlock(L)	((void) 0)
#endif

#if !defined(gvtsi_threadyield)
#define gvtsi_threadyield(L)	{gvts_unlock(L); gvts_lock(L);}
#endif

#if !defined(gvtsi_userstateopen)
#define gvtsi_userstateopen(L)		((void)L)
#endif

#if !defined(gvtsi_userstateclose)
#define gvtsi_userstateclose(L)		((void)L)
#endif

#if !defined(gvtsi_userstatethread)
#define gvtsi_userstatethread(L,L1)	((void)L)
#endif

#if !defined(gvtsi_userstatefree)
#define gvtsi_userstatefree(L,L1)	((void)L)
#endif

#if !defined(gvtsi_userstateresume)
#define gvtsi_userstateresume(L,n)	((void)L)
#endif

#if !defined(gvtsi_userstateyield)
#define gvtsi_userstateyield(L,n)	((void)L)
#endif

#if !defined(gvtsi_numidiv)
#define gvtsi_numidiv(L,a,b)     ((void)L, g_floor(gvtsi_numdiv(L,a,b)))
#endif

#if !defined(gvtsi_numdiv)
#define gvtsi_numdiv(L,a,b)      ((a)/(b))
#endif

#if !defined(gvtsi_nummod)
#define gvtsi_nummod(L,a,b,m)  \
  { (void)L; (m) = g_mathop(fmod)(a,b); \
    if (((m) > 0) ? (b) < 0 : ((m) < 0 && (b) > 0)) (m) += (b); }
#endif

#if !defined(gvtsi_numpow)
#define gvtsi_numpow(L,a,b)  \
  ((void)L, (b == 2) ? (a)*(a) : g_mathop(pow)(a,b))
#endif

#if !defined(gvtsi_numadd)
#define gvtsi_numadd(L,a,b)      ((a)+(b))
#define gvtsi_numsub(L,a,b)      ((a)-(b))
#define gvtsi_nummul(L,a,b)      ((a)*(b))
#define gvtsi_numunm(L,a)        (-(a))
#define gvtsi_numeq(a,b)         ((a)==(b))
#define gvtsi_numlt(a,b)         ((a)<(b))
#define gvtsi_numle(a,b)         ((a)<=(b))
#define gvtsi_numgt(a,b)         ((a)>(b))
#define gvtsi_numge(a,b)         ((a)>=(b))
#define gvtsi_numisnan(a)        (!gvtsi_numeq((a), (a)))
#endif

#if !defined(HARDSTACKTESTS)
#define condmovestack(L,pre,pos)	((void)0)
#else

#define condmovestack(L,pre,pos)  \
  { int sz_ = stacksize(L); pre; gvtsD_reallocstack((L), sz_, 0); pos; }
#endif

#if !defined(HARDMEMTESTS)
#define condchangemem(L,pre,pos)	((void)0)
#else
#define condchangemem(L,pre,pos)  \
	{ if (gcrunning(G(L))) { pre; gvtsC_fullgc(L, 0); pos; } }
#endif

#endif
