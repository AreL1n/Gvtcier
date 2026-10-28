#ifndef gvtsconf_h
#define gvtsconf_h

#include <limits.h>
#include <stddef.h>

#if !defined(GVTS_USE_C89) && defined(_WIN32) && !defined(_WIN32_WCE)
#define GVTS_USE_WINDOWS                                          
#endif

#if defined(GVTS_USE_WINDOWS)
#define GVTS_DL_DLL	                            
#define GVTS_USE_C89	                             
#endif

#if defined(GVTS_USE_LINUX)
#define GVTS_USE_POSIX
#define GVTS_USE_DLOPEN		                                  
#endif

#if defined(GVTS_USE_MACOSX)
#define GVTS_USE_POSIX
#define GVTS_USE_DLOPEN		                              
#endif

#if defined(GVTS_USE_IOS)
#define GVTS_USE_POSIX
#define GVTS_USE_DLOPEN
#endif

#define GVTSI_IS32INT	((UINT_MAX >> 30) >= 3)

#define GVTS_INT_INT		1
#define GVTS_INT_LONG		2
#define GVTS_INT_LONGLONG	3

#define GVTS_FLOAT_FLOAT		1
#define GVTS_FLOAT_DOUBLE	2
#define GVTS_FLOAT_LONGDOUBLE	3

#define GVTS_INT_DEFAULT		GVTS_INT_LONGLONG
#define GVTS_FLOAT_DEFAULT	GVTS_FLOAT_DOUBLE

#define GVTS_32BITS	0

#if defined(GVTS_USE_C89) && !defined(GVTS_USE_WINDOWS)
#define GVTS_C89_NUMBERS		1
#else
#define GVTS_C89_NUMBERS		0
#endif

#if GVTS_32BITS		       

#if GVTSI_IS32INT                               
#define GVTS_INT_TYPE	GVTS_INT_INT
#else                            
#define GVTS_INT_TYPE	GVTS_INT_LONG
#endif
#define GVTS_FLOAT_TYPE	GVTS_FLOAT_FLOAT

#elif GVTS_C89_NUMBERS	        

#define GVTS_INT_TYPE	GVTS_INT_LONG
#define GVTS_FLOAT_TYPE	GVTS_FLOAT_DOUBLE

#else		        

#define GVTS_INT_TYPE	GVTS_INT_DEFAULT
#define GVTS_FLOAT_TYPE	GVTS_FLOAT_DEFAULT

#endif				       

#define GVTS_PATH_SEP            ";"
#define GVTS_PATH_MARK           "?"
#define GVTS_EXEC_DIR            "!"

#define GVTS_VDIR	GVTS_VERSION_MAJOR "." GVTS_VERSION_MINOR
#if defined(_WIN32)	       

#define GVTS_LDIR	"!\\gvts\\"
#define GVTS_CDIR	"!\\"
#define GVTS_SHRDIR	"!\\..\\share\\gvts\\" GVTS_VDIR "\\"

#if !defined(GVTS_PATH_DEFAULT)
#define GVTS_PATH_DEFAULT  \
		GVTS_LDIR"?.gvts;"  GVTS_LDIR"?\\init.gvts;" \
		GVTS_CDIR"?.gvts;"  GVTS_CDIR"?\\init.gvts;" \
		GVTS_SHRDIR"?.gvts;" GVTS_SHRDIR"?\\init.gvts;" \
		".\\?.gvts;" ".\\?\\init.gvts"
#endif

#if !defined(GVTS_CPATH_DEFAULT)
#define GVTS_CPATH_DEFAULT \
		GVTS_CDIR"?.dll;" \
		GVTS_CDIR"..\\lib\\gvts\\" GVTS_VDIR "\\?.dll;" \
		GVTS_CDIR"loadall.dll;" ".\\?.dll"
#endif

#else			        

#define GVTS_ROOT	"/usr/local/"
#define GVTS_LDIR	GVTS_ROOT "share/gvts/" GVTS_VDIR "/"
#define GVTS_CDIR	GVTS_ROOT "lib/gvts/" GVTS_VDIR "/"

#if !defined(GVTS_PATH_DEFAULT)
#define GVTS_PATH_DEFAULT  \
		GVTS_LDIR"?.gvts;"  GVTS_LDIR"?/init.gvts;" \
		GVTS_CDIR"?.gvts;"  GVTS_CDIR"?/init.gvts;" \
		"./?.gvts;" "./?/init.gvts"
#endif

#if !defined(GVTS_CPATH_DEFAULT)
#define GVTS_CPATH_DEFAULT \
		GVTS_CDIR"?.so;" GVTS_CDIR"loadall.so;" "./?.so"
#endif

#endif			       

#if !defined(GVTS_DIRSEP)

#if defined(_WIN32)
#define GVTS_DIRSEP	"\\"
#else
#define GVTS_DIRSEP	"/"
#endif

#endif

#define GVTS_IGMARK		"-"

#if defined(GVTS_BUILD_AS_DLL)	       

#if defined(GVTS_CORE) || defined(GVTS_LIB)	       
#define GVTS_API __declspec(dllexport)
#else						        
#define GVTS_API __declspec(dllimport)
#endif						       

#else				        

#define GVTS_API		extern

#endif				       

#define GVTSLIB_API	GVTS_API
#define GVTSMOD_API	GVTS_API

#if defined(__GNUC__) && ((__GNUC__*100 + __GNUC_MINOR__) >= 302) && \
    defined(__ELF__)		       
#define GVTSI_FUNC	__attribute__((visibility("internal"))) extern
#else				        
#define GVTSI_FUNC	extern
#endif				       

#define GVTSI_DDEC(dec)	GVTSI_FUNC dec
#define GVTSI_DDEF	           

#if defined(GVTS_COMPAT_5_3)	       

#define GVTS_COMPAT_MATHLIB

#define GVTS_COMPAT_APIINTCASTS

#define GVTS_COMPAT_LT_LE

#define gvts_strlen(L,i)		gvts_rawlen(L, (i))

#define gvts_objlen(L,i)		gvts_rawlen(L, (i))

#define gvts_equal(L,idx1,idx2)		gvts_compare(L,(idx1),(idx2),GVTS_OPEQ)
#define gvts_lessthan(L,idx1,idx2)	gvts_compare(L,(idx1),(idx2),GVTS_OPLT)

#endif				       

#define g_floor(x)		(g_mathop(floor)(x))

#define gvts_number2str(s,sz,n)  \
	g_sprintf((s), sz, GVTS_NUMBER_FMT, (GVTSI_UACNUMBER)(n))

#define gvts_numbertointeger(n,p) \
  ((n) >= (GVTS_NUMBER)(GVTS_MININTEGER) && \
   (n) < -(GVTS_NUMBER)(GVTS_MININTEGER) && \
      (*(p) = (GVTS_INTEGER)(n), 1))

#if GVTS_FLOAT_TYPE == GVTS_FLOAT_FLOAT		                    

#define GVTS_NUMBER	float

#define g_floatatt(n)		(FLT_##n)

#define GVTSI_UACNUMBER	double

#define GVTS_NUMBER_FRMLEN	""
#define GVTS_NUMBER_FMT		"%.7g"

#define g_mathop(op)		op##f

#define gvts_str2number(s,p)	strtof((s), (p))

#elif GVTS_FLOAT_TYPE == GVTS_FLOAT_LONGDOUBLE	                    

#define GVTS_NUMBER	long double

#define g_floatatt(n)		(LDBL_##n)

#define GVTSI_UACNUMBER	long double

#define GVTS_NUMBER_FRMLEN	"L"
#define GVTS_NUMBER_FMT		"%.19Lg"

#define g_mathop(op)		op##l

#define gvts_str2number(s,p)	strtold((s), (p))

#elif GVTS_FLOAT_TYPE == GVTS_FLOAT_DOUBLE	               

#define GVTS_NUMBER	double

#define g_floatatt(n)		(DBL_##n)

#define GVTSI_UACNUMBER	double

#define GVTS_NUMBER_FRMLEN	""
#define GVTS_NUMBER_FMT		"%.14g"

#define g_mathop(op)		op

#define gvts_str2number(s,p)	strtod((s), (p))

#else						        

#error "numeric float type not defined"

#endif					       

#define GVTS_INTEGER_FMT		"%" GVTS_INTEGER_FRMLEN "d"

#define GVTSI_UACINT		GVTS_INTEGER

#define gvts_integer2str(s,sz,n)  \
	g_sprintf((s), sz, GVTS_INTEGER_FMT, (GVTSI_UACINT)(n))

#define GVTS_UNSIGNED		unsigned GVTSI_UACINT

#if GVTS_INT_TYPE == GVTS_INT_INT		           

#define GVTS_INTEGER		int
#define GVTS_INTEGER_FRMLEN	""

#define GVTS_MAXINTEGER		INT_MAX
#define GVTS_MININTEGER		INT_MIN

#define GVTS_MAXUNSIGNED		UINT_MAX

#elif GVTS_INT_TYPE == GVTS_INT_LONG	             

#define GVTS_INTEGER		long
#define GVTS_INTEGER_FRMLEN	"l"

#define GVTS_MAXINTEGER		LONG_MAX
#define GVTS_MININTEGER		LONG_MIN

#define GVTS_MAXUNSIGNED		ULONG_MAX

#elif GVTS_INT_TYPE == GVTS_INT_LONGLONG	                  

#if defined(LLONG_MAX)		       

#define GVTS_INTEGER		long long
#define GVTS_INTEGER_FRMLEN	"ll"

#define GVTS_MAXINTEGER		LLONG_MAX
#define GVTS_MININTEGER		LLONG_MIN

#define GVTS_MAXUNSIGNED		ULLONG_MAX

#elif defined(GVTS_USE_WINDOWS)         

#define GVTS_INTEGER		__int64
#define GVTS_INTEGER_FRMLEN	"I64"

#define GVTS_MAXINTEGER		_I64_MAX
#define GVTS_MININTEGER		_I64_MIN

#define GVTS_MAXUNSIGNED		_UI64_MAX

#else				        

#error "Compiler does not support 'long long'. Use option '-DGVTS_32BITS' \
  or '-DGVTS_C89_NUMBERS' (see file 'gvtsconf.h' for details)"

#endif				       

#else				        

#error "numeric integer type not defined"

#endif				       

#if !defined(GVTS_USE_C89)
#define g_sprintf(s,sz,f,i)	snprintf(s,sz,f,i)
#else
#define g_sprintf(s,sz,f,i)	((void)(sz), sprintf(s,f,i))
#endif

#if !defined(GVTS_USE_C89)
#define gvts_strx2number(s,p)		gvts_str2number(s,p)
#endif

#define gvts_pointer2str(buff,sz,p)	g_sprintf(buff,sz,"%p",p)

#if !defined(GVTS_USE_C89)
#define gvts_number2strx(L,b,sz,f,n)  \
	((void)L, g_sprintf(b,sz,f,(GVTSI_UACNUMBER)(n)))
#endif

#if defined(GVTS_USE_C89) || (defined(HUGE_VAL) && !defined(HUGE_VALF))
#undef g_mathop                              
#undef gvts_str2number
#define g_mathop(op)		(gvts_Number)op                  
#define gvts_str2number(s,p)	((gvts_Number)strtod((s), (p)))
#endif

#define GVTS_KCONTEXT	ptrdiff_t

#if !defined(GVTS_USE_C89) && defined(__STDC_VERSION__) && \
    __STDC_VERSION__ >= 199901L
#include <stdint.h>
#if defined(INTPTR_MAX)                                         
#undef GVTS_KCONTEXT
#define GVTS_KCONTEXT	intptr_t
#endif
#endif

#if !defined(gvts_getlocaledecpoint)
#define gvts_getlocaledecpoint()		(localeconv()->decimal_point[0])
#endif

#if !defined(gvtsi_likely)

#if defined(__GNUC__) && !defined(GVTS_NOBUILTIN)
#define gvtsi_likely(x)		(__builtin_expect(((x) != 0), 1))
#define gvtsi_unlikely(x)	(__builtin_expect(((x) != 0), 0))
#else
#define gvtsi_likely(x)		(x)
#define gvtsi_unlikely(x)	(x)
#endif

#endif

#if defined(GVTS_CORE) || defined(GVTS_LIB)

#define g_likely(x)	gvtsi_likely(x)
#define g_unlikely(x)	gvtsi_unlikely(x)
#endif

#if defined(GVTS_USE_APICHECK)
#include <assert.h>
#define gvtsi_apicheck(l,e)	assert(e)
#endif

#if GVTSI_IS32INT
#define GVTSI_MAXSTACK		1000000
#else
#define GVTSI_MAXSTACK		15000
#endif

#define GVTS_EXTRASPACE		(sizeof(void *))

#define GVTS_IDSIZE	60

#define GVTSG_BUFFERSIZE   ((int)(16 * sizeof(void*) * sizeof(gvts_Number)))

#define GVTSI_MAXALIGN  gvts_Number n; double u; void *s; gvts_Integer i; long l

#endif
