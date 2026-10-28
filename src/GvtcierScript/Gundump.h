#ifndef lundump_h
#define lundump_h

#include "Glimits.h"
#include "Gobject.h"
#include "Gzio.h"

#define GVTSC_DATA	"\x19\x93\r\n\x1a\n"

#define GVTSC_INT	0x5678
#define GVTSC_NUM	cast_num(370.5)

#define GVTSC_VERSION  (((GVTS_VERSION_NUM / 100) * 16) + GVTS_VERSION_NUM % 100)

#define GVTSC_FORMAT	0	                                 

GVTSI_FUNC LClosure* gvtsU_undump (gvts_State* L, ZIO* Z, const char* name);

GVTSI_FUNC int gvtsU_dump (gvts_State* L, const Proto* f, gvts_Writer w,
                         void* data, int strip);

#endif
