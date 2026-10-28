#define linit_c
#define GVTS_LIB

#include "Gprefix.h"

#include <stddef.h>

#include "gvts.h"

#include "gvtslib.h"
#include "Gauxlib.h"

static const gvtsG_Reg loadedlibs[] = {
  {GVTS_GNAME, gvtsopen_base},
  {GVTS_LOADLIBNAME, gvtsopen_package},
  {GVTS_COLIBNAME, gvtsopen_coroutine},
  {GVTS_TABLIBNAME, gvtsopen_table},
  {GVTS_IOLIBNAME, gvtsopen_io},
  {GVTS_OSLIBNAME, gvtsopen_os},
  {GVTS_STRLIBNAME, gvtsopen_string},
  {GVTS_MATHLIBNAME, gvtsopen_math},
  {GVTS_UTF8LIBNAME, gvtsopen_utf8},
  {GVTS_DBLIBNAME, gvtsopen_debug},
  {NULL, NULL}
};

GVTSLIB_API void gvtsG_openlibs (gvts_State *L) {
  const gvtsG_Reg *lib;

  for (lib = loadedlibs; lib->func; lib++) {
    gvtsG_requiref(L, lib->name, lib->func, 1);
    gvts_pop(L, 1);                  
  }
}
