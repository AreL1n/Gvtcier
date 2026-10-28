#define loslib_c
#define GVTS_LIB

#include "Gprefix.h"

#include <errno.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gvts.h"

#include "Gauxlib.h"
#include "gvtslib.h"

#if !defined(GVTS_STRFTIMEOPTIONS)	       

#if defined(GVTS_USE_WINDOWS)
#define GVTS_STRFTIMEOPTIONS  "aAbBcdHIjmMpSUwWxXyYzZ%" \
    "||" "#c#x#d#H#I#j#m#M#S#U#w#W#y#Y"                        
#elif defined(GVTS_USE_C89)                                       
#define GVTS_STRFTIMEOPTIONS  "aAbBcdHIjmMpSUwWxXyYZ%"
#else                         
#define GVTS_STRFTIMEOPTIONS  "aAbBcCdDeFgGhHIjmMnprRStTuUVwWxXyYzZ%" \
    "||" "EcECExEXEyEY" "OdOeOHOIOmOMOSOuOUOVOwOWOy"                        
#endif

#endif					       

#if !defined(GVTS_NUMTIME)	       

#define g_timet			gvts_Integer
#define g_pushtime(L,t)		gvts_pushinteger(L,(gvts_Integer)(t))
#define g_gettime(L,arg)	gvtsG_checkinteger(L, arg)

#else				        

#define g_timet			gvts_Number
#define g_pushtime(L,t)		gvts_pushnumber(L,(gvts_Number)(t))
#define g_gettime(L,arg)	gvtsG_checknumber(L, arg)

#endif				       

#if !defined(g_gmtime)		       

#if defined(GVTS_USE_POSIX)	       

#define g_gmtime(t,r)		gmtime_r(t,r)
#define g_localtime(t,r)	localtime_r(t,r)

#else				        

#define g_gmtime(t,r)		((void)(r)->tm_sec, gmtime(t))
#define g_localtime(t,r)	((void)(r)->tm_sec, localtime(t))

#endif				       

#endif				       

#if !defined(gvts_tmpnam)	       

#if defined(GVTS_USE_POSIX)	       

#include <unistd.h>

#define GVTS_TMPNAMBUFSIZE	32

#if !defined(GVTS_TMPNAMTEMPLATE)
#define GVTS_TMPNAMTEMPLATE	"/tmp/gvts_XXXXXX"
#endif

#define gvts_tmpnam(b,e) { \
        strcpy(b, GVTS_TMPNAMTEMPLATE); \
        e = mkstemp(b); \
        if (e != -1) close(e); \
        e = (e == -1); }

#else				        

#define GVTS_TMPNAMBUFSIZE	L_tmpnam
#define gvts_tmpnam(b,e)		{ e = (tmpnam(b) == NULL); }

#endif				       

#endif				       

#if !defined(g_system)
#if defined(GVTS_USE_IOS)

#define g_system(cmd) ((cmd) == NULL ? 0 : -1)
#else
#define g_system(cmd)	system(cmd)                          
#endif
#endif

static int os_execute (gvts_State *L) {
  const char *cmd = gvtsG_optstring(L, 1, NULL);
  int stat;
  errno = 0;
  stat = g_system(cmd);
  if (cmd != NULL)
    return gvtsG_execresult(L, stat);
  else {
    gvts_pushboolean(L, stat);                                
    return 1;
  }
}

static int os_remove (gvts_State *L) {
  const char *filename = gvtsG_checkstring(L, 1);
  errno = 0;
  return gvtsG_fileresult(L, remove(filename) == 0, filename);
}

static int os_rename (gvts_State *L) {
  const char *fromname = gvtsG_checkstring(L, 1);
  const char *toname = gvtsG_checkstring(L, 2);
  errno = 0;
  return gvtsG_fileresult(L, rename(fromname, toname) == 0, NULL);
}

static int os_tmpname (gvts_State *L) {
  char buff[GVTS_TMPNAMBUFSIZE];
  int err;
  gvts_tmpnam(buff, err);
  if (g_unlikely(err))
    return gvtsG_error(L, "unable to generate a unique filename");
  gvts_pushstring(L, buff);
  return 1;
}

static int os_getenv (gvts_State *L) {
  gvts_pushstring(L, getenv(gvtsG_checkstring(L, 1)));                        
  return 1;
}

static int os_clock (gvts_State *L) {
  gvts_pushnumber(L, ((gvts_Number)clock())/(gvts_Number)CLOCKS_PER_SEC);
  return 1;
}

static void setfield (gvts_State *L, const char *key, int value, int delta) {
  #if (defined(GVTS_NUMTIME) && GVTS_MAXINTEGER <= INT_MAX)
    if (g_unlikely(value > GVTS_MAXINTEGER - delta))
      gvtsG_error(L, "field '%s' is out-of-bound", key);
  #endif
  gvts_pushinteger(L, (gvts_Integer)value + delta);
  gvts_setfield(L, -2, key);
}

static void setboolfield (gvts_State *L, const char *key, int value) {
  if (value < 0)                  
    return;                          
  gvts_pushboolean(L, value);
  gvts_setfield(L, -2, key);
}

static void setallfields (gvts_State *L, struct tm *stm) {
  setfield(L, "year", stm->tm_year, 1900);
  setfield(L, "month", stm->tm_mon, 1);
  setfield(L, "day", stm->tm_mday, 0);
  setfield(L, "hour", stm->tm_hour, 0);
  setfield(L, "min", stm->tm_min, 0);
  setfield(L, "sec", stm->tm_sec, 0);
  setfield(L, "yday", stm->tm_yday, 1);
  setfield(L, "wday", stm->tm_wday, 1);
  setboolfield(L, "isdst", stm->tm_isdst);
}

static int getboolfield (gvts_State *L, const char *key) {
  int res;
  res = (gvts_getfield(L, -1, key) == GVTS_TNIL) ? -1 : gvts_toboolean(L, -1);
  gvts_pop(L, 1);
  return res;
}

static int getfield (gvts_State *L, const char *key, int d, int delta) {
  int isnum;
  int t = gvts_getfield(L, -1, key);                              
  gvts_Integer res = gvts_tointegerx(L, -1, &isnum);
  if (!isnum) {                                
    if (g_unlikely(t != GVTS_TNIL))                         
      return gvtsG_error(L, "field '%s' is not an integer", key);
    else if (g_unlikely(d < 0))                                 
      return gvtsG_error(L, "field '%s' missing in date table", key);
    res = d;
  }
  else {
    if (!(res >= 0 ? res - delta <= INT_MAX : INT_MIN + delta <= res))
      return gvtsG_error(L, "field '%s' is out-of-bound", key);
    res -= delta;
  }
  gvts_pop(L, 1);
  return (int)res;
}

static const char *checkoption (gvts_State *L, const char *conv,
                                ptrdiff_t convlen, char *buff) {
  const char *option = GVTS_STRFTIMEOPTIONS;
  int oplen = 1;                                       
  for (; *option != '\0' && oplen <= convlen; option += oplen) {
    if (*option == '|')                   
      oplen++;                                                
    else if (memcmp(conv, option, oplen) == 0) {              
      memcpy(buff, conv, oplen);                                   
      buff[oplen] = '\0';
      return conv + oplen;                        
    }
  }
  gvtsG_argerror(L, 1,
    gvts_pushfstring(L, "invalid conversion specifier '%%%s'", conv));
  return conv;                         
}

static time_t g_checktime (gvts_State *L, int arg) {
  g_timet t = g_gettime(L, arg);
  gvtsG_argcheck(L, (time_t)t == t, arg, "time out-of-bounds");
  return (time_t)t;
}

#define SIZETIMEFMT	250

static int os_date (gvts_State *L) {
  size_t slen;
  const char *s = gvtsG_optlstring(L, 1, "%c", &slen);
  time_t t = gvtsG_opt(L, g_checktime, 2, time(NULL));
  const char *se = s + slen;               
  struct tm tmr, *stm;
  if (*s == '!') {            
    stm = g_gmtime(&t, &tmr);
    s++;                
  }
  else
    stm = g_localtime(&t, &tmr);
  if (stm == NULL)                     
    return gvtsG_error(L,
                 "date result cannot be represented in this installation");
  if (strcmp(s, "*t") == 0) {
    gvts_createtable(L, 0, 9);                            
    setallfields(L, stm);
  }
  else {
    char cc[4];                                                   
    gvtsG_Buffer b;
    cc[0] = '%';
    gvtsG_buffinit(L, &b);
    while (s < se) {
      if (*s != '%')                                   
        gvtsG_addchar(&b, *s++);
      else {
        size_t reslen;
        char *buff = gvtsG_prepbuffsize(&b, SIZETIMEFMT);
        s++;                
        s = checkoption(L, s, se - s, cc + 1);                              
        reslen = strftime(buff, SIZETIMEFMT, cc, stm);
        gvtsG_addsize(&b, reslen);
      }
    }
    gvtsG_pushresult(&b);
  }
  return 1;
}

static int os_time (gvts_State *L) {
  time_t t;
  if (gvts_isnoneornil(L, 1))                            
    t = time(NULL);                        
  else {
    struct tm ts;
    gvtsG_checktype(L, 1, GVTS_TTABLE);
    gvts_settop(L, 1);                                     
    ts.tm_year = getfield(L, "year", -1, 1900);
    ts.tm_mon = getfield(L, "month", -1, 1);
    ts.tm_mday = getfield(L, "day", -1, 0);
    ts.tm_hour = getfield(L, "hour", 12, 0);
    ts.tm_min = getfield(L, "min", 0, 0);
    ts.tm_sec = getfield(L, "sec", 0, 0);
    ts.tm_isdst = getboolfield(L, "isdst");
    t = mktime(&ts);
    setallfields(L, &ts);                                            
  }
  if (t != (time_t)(g_timet)t || t == (time_t)(-1))
    return gvtsG_error(L,
                  "time result cannot be represented in this installation");
  g_pushtime(L, t);
  return 1;
}

static int os_difftime (gvts_State *L) {
  time_t t1 = g_checktime(L, 1);
  time_t t2 = g_checktime(L, 2);
  gvts_pushnumber(L, (gvts_Number)difftime(t1, t2));
  return 1;
}

static int os_setlocale (gvts_State *L) {
  static const int cat[] = {LC_ALL, LC_COLLATE, LC_CTYPE, LC_MONETARY,
                      LC_NUMERIC, LC_TIME};
  static const char *const catnames[] = {"all", "collate", "ctype", "monetary",
     "numeric", "time", NULL};
  const char *l = gvtsG_optstring(L, 1, NULL);
  int op = gvtsG_checkoption(L, 2, "all", catnames);
  gvts_pushstring(L, setlocale(cat[op], l));
  return 1;
}

static int os_exit (gvts_State *L) {
  int status;
  if (gvts_isboolean(L, 1))
    status = (gvts_toboolean(L, 1) ? EXIT_SUCCESS : EXIT_FAILURE);
  else
    status = (int)gvtsG_optinteger(L, 1, EXIT_SUCCESS);
  if (gvts_toboolean(L, 2))
    gvts_close(L);
  if (L) exit(status);                                                       
  return 0;
}

static const gvtsG_Reg syslib[] = {
  {"clock",     os_clock},
  {"date",      os_date},
  {"difftime",  os_difftime},
  {"execute",   os_execute},
  {"exit",      os_exit},
  {"getenv",    os_getenv},
  {"remove",    os_remove},
  {"rename",    os_rename},
  {"setlocale", os_setlocale},
  {"time",      os_time},
  {"tmpname",   os_tmpname},
  {NULL, NULL}
};

GVTSMOD_API int gvtsopen_os (gvts_State *L) {
  gvtsG_newlib(L, syslib);
  return 1;
}
