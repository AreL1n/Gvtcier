#ifndef gvtslib_h
#define gvtslib_h

#include "gvts.h"

#define GVTS_VERSUFFIX          "_" GVTS_VERSION_MAJOR "_" GVTS_VERSION_MINOR

GVTSMOD_API int (gvtsopen_base) (gvts_State *L);

#define GVTS_COLIBNAME	"coroutine"
GVTSMOD_API int (gvtsopen_coroutine) (gvts_State *L);

#define GVTS_TABLIBNAME	"table"
GVTSMOD_API int (gvtsopen_table) (gvts_State *L);

#define GVTS_IOLIBNAME	"io"
GVTSMOD_API int (gvtsopen_io) (gvts_State *L);

#define GVTS_OSLIBNAME	"os"
GVTSMOD_API int (gvtsopen_os) (gvts_State *L);

#define GVTS_STRLIBNAME	"string"
GVTSMOD_API int (gvtsopen_string) (gvts_State *L);

#define GVTS_UTF8LIBNAME	"utf8"
GVTSMOD_API int (gvtsopen_utf8) (gvts_State *L);

#define GVTS_MATHLIBNAME	"math"
GVTSMOD_API int (gvtsopen_math) (gvts_State *L);

#define GVTS_DBLIBNAME	"debug"
GVTSMOD_API int (gvtsopen_debug) (gvts_State *L);

#define GVTS_LOADLIBNAME	"package"
GVTSMOD_API int (gvtsopen_package) (gvts_State *L);

GVTSLIB_API void (gvtsG_openlibs) (gvts_State *L);

#endif
