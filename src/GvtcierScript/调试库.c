#define ldblib_c
#define GVTS_LIB

#include "Gprefix.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gvts.h"

#include "Gauxlib.h"
#include "gvtslib.h"

static const char *const HOOKKEY = "_HOOKKEY";

static void checkstack (gvts_State *L, gvts_State *L1, int n) {
  if (g_unlikely(L != L1 && !gvts_checkstack(L1, n)))
    gvtsG_error(L, "stack overflow");
}

static int db_getregistry (gvts_State *L) {
  gvts_pushvalue(L, GVTS_REGISTRYINDEX);
  return 1;
}

static int db_getmetatable (gvts_State *L) {
  gvtsG_checkany(L, 1);
  if (!gvts_getmetatable(L, 1)) {
    gvts_pushnil(L);                    
  }
  return 1;
}

static int db_setmetatable (gvts_State *L) {
  int t = gvts_type(L, 2);
  gvtsG_argexpected(L, t == GVTS_TNIL || t == GVTS_TTABLE, 2, "nil or table");
  gvts_settop(L, 2);
  gvts_setmetatable(L, 1);
  return 1;                           
}

static int db_getuservalue (gvts_State *L) {
  int n = (int)gvtsG_optinteger(L, 2, 1);
  if (gvts_type(L, 1) != GVTS_TUSERDATA)
    gvtsG_pushfail(L);
  else if (gvts_getiuservalue(L, 1, n) != GVTS_TNONE) {
    gvts_pushboolean(L, 1);
    return 2;
  }
  return 1;
}

static int db_setuservalue (gvts_State *L) {
  int n = (int)gvtsG_optinteger(L, 3, 1);
  gvtsG_checktype(L, 1, GVTS_TUSERDATA);
  gvtsG_checkany(L, 2);
  gvts_settop(L, 2);
  if (!gvts_setiuservalue(L, 1, n))
    gvtsG_pushfail(L);
  return 1;
}

static gvts_State *getthread (gvts_State *L, int *arg) {
  if (gvts_isthread(L, 1)) {
    *arg = 1;
    return gvts_tothread(L, 1);
  }
  else {
    *arg = 0;
    return L;                                                 
  }
}

static void settabss (gvts_State *L, const char *k, const char *v) {
  gvts_pushstring(L, v);
  gvts_setfield(L, -2, k);
}

static void settabsi (gvts_State *L, const char *k, int v) {
  gvts_pushinteger(L, v);
  gvts_setfield(L, -2, k);
}

static void settabsb (gvts_State *L, const char *k, int v) {
  gvts_pushboolean(L, v);
  gvts_setfield(L, -2, k);
}

static void treatstackoption (gvts_State *L, gvts_State *L1, const char *fname) {
  if (L == L1)
    gvts_rotate(L, -2, 1);                                 
  else
    gvts_xmove(L1, L, 1);                                       
  gvts_setfield(L, -2, fname);                             
}

static int db_getinfo (gvts_State *L) {
  gvts_Debug ar;
  int arg;
  gvts_State *L1 = getthread(L, &arg);
  const char *options = gvtsG_optstring(L, arg+2, "flnSrtu");
  checkstack(L, L1, 3);
  gvtsG_argcheck(L, options[0] != '>', arg + 2, "invalid option '>'");
  if (gvts_isfunction(L, arg + 1)) {                              
    options = gvts_pushfstring(L, ">%s", options);                            
    gvts_pushvalue(L, arg + 1);                                   
    gvts_xmove(L, L1, 1);
  }
  else {                   
    if (!gvts_getstack(L1, (int)gvtsG_checkinteger(L, arg + 1), &ar)) {
      gvtsG_pushfail(L);                          
      return 1;
    }
  }
  if (!gvts_getinfo(L1, options, &ar))
    return gvtsG_argerror(L, arg+2, "invalid option");
  gvts_newtable(L);                                
  if (strchr(options, 'S')) {
    gvts_pushlstring(L, ar.source, ar.srclen);
    gvts_setfield(L, -2, "source");
    settabss(L, "short_src", ar.short_src);
    settabsi(L, "linedefined", ar.linedefined);
    settabsi(L, "lastlinedefined", ar.lastlinedefined);
    settabss(L, "what", ar.what);
  }
  if (strchr(options, 'l'))
    settabsi(L, "currentline", ar.currentline);
  if (strchr(options, 'u')) {
    settabsi(L, "nups", ar.nups);
    settabsi(L, "nparams", ar.nparams);
    settabsb(L, "isvararg", ar.isvararg);
  }
  if (strchr(options, 'n')) {
    settabss(L, "name", ar.name);
    settabss(L, "namewhat", ar.namewhat);
  }
  if (strchr(options, 'r')) {
    settabsi(L, "ftransfer", ar.ftransfer);
    settabsi(L, "ntransfer", ar.ntransfer);
  }
  if (strchr(options, 't'))
    settabsb(L, "istailcall", ar.istailcall);
  if (strchr(options, 'L'))
    treatstackoption(L, L1, "activelines");
  if (strchr(options, 'f'))
    treatstackoption(L, L1, "func");
  return 1;                    
}

static int db_getlocal (gvts_State *L) {
  int arg;
  gvts_State *L1 = getthread(L, &arg);
  int nvar = (int)gvtsG_checkinteger(L, arg + 2);                            
  if (gvts_isfunction(L, arg + 1)) {                          
    gvts_pushvalue(L, arg + 1);                     
    gvts_pushstring(L, gvts_getlocal(L, NULL, nvar));                       
    return 1;                                            
  }
  else {                            
    gvts_Debug ar;
    const char *name;
    int level = (int)gvtsG_checkinteger(L, arg + 1);
    if (g_unlikely(!gvts_getstack(L1, level, &ar)))                     
      return gvtsG_argerror(L, arg+1, "level out of range");
    checkstack(L, L1, 1);
    name = gvts_getlocal(L1, &ar, nvar);
    if (name) {
      gvts_xmove(L1, L, 1);                        
      gvts_pushstring(L, name);                 
      gvts_rotate(L, -2, 1);                
      return 2;
    }
    else {
      gvtsG_pushfail(L);                           
      return 1;
    }
  }
}

static int db_setlocal (gvts_State *L) {
  int arg;
  const char *name;
  gvts_State *L1 = getthread(L, &arg);
  gvts_Debug ar;
  int level = (int)gvtsG_checkinteger(L, arg + 1);
  int nvar = (int)gvtsG_checkinteger(L, arg + 2);
  if (g_unlikely(!gvts_getstack(L1, level, &ar)))                     
    return gvtsG_argerror(L, arg+1, "level out of range");
  gvtsG_checkany(L, arg+3);
  gvts_settop(L, arg+3);
  checkstack(L, L1, 1);
  gvts_xmove(L, L1, 1);
  name = gvts_setlocal(L1, &ar, nvar);
  if (name == NULL)
    gvts_pop(L1, 1);                                                   
  gvts_pushstring(L, name);
  return 1;
}

static int auxupvalue (gvts_State *L, int get) {
  const char *name;
  int n = (int)gvtsG_checkinteger(L, 2);                     
  gvtsG_checktype(L, 1, GVTS_TFUNCTION);               
  name = get ? gvts_getupvalue(L, 1, n) : gvts_setupvalue(L, 1, n);
  if (name == NULL) return 0;
  gvts_pushstring(L, name);
  gvts_insert(L, -(get+1));                             
  return get + 1;
}

static int db_getupvalue (gvts_State *L) {
  return auxupvalue(L, 1);
}

static int db_setupvalue (gvts_State *L) {
  gvtsG_checkany(L, 3);
  return auxupvalue(L, 0);
}

static void *checkupval (gvts_State *L, int argf, int argnup, int *pnup) {
  void *id;
  int nup = (int)gvtsG_checkinteger(L, argnup);                     
  gvtsG_checktype(L, argf, GVTS_TFUNCTION);               
  id = gvts_upvalueid(L, argf, nup);
  if (pnup) {
    gvtsG_argcheck(L, id != NULL, argnup, "invalid upvalue index");
    *pnup = nup;
  }
  return id;
}

static int db_upvalueid (gvts_State *L) {
  void *id = checkupval(L, 1, 2, NULL);
  if (id != NULL)
    gvts_pushlightuserdata(L, id);
  else
    gvtsG_pushfail(L);
  return 1;
}

static int db_upvaluejoin (gvts_State *L) {
  int n1, n2;
  checkupval(L, 1, 2, &n1);
  checkupval(L, 3, 4, &n2);
  gvtsG_argcheck(L, !gvts_iscfunction(L, 1), 1, "Gvts function expected");
  gvtsG_argcheck(L, !gvts_iscfunction(L, 3), 3, "Gvts function expected");
  gvts_upvaluejoin(L, 1, n1, 3, n2);
  return 0;
}

static void hookf (gvts_State *L, gvts_Debug *ar) {
  static const char *const hooknames[] =
    {"call", "return", "line", "count", "tail call"};
  gvts_getfield(L, GVTS_REGISTRYINDEX, HOOKKEY);
  gvts_pushthread(L);
  if (gvts_rawget(L, -2) == GVTS_TFUNCTION) {                                 
    gvts_pushstring(L, hooknames[(int)ar->event]);                       
    if (ar->currentline >= 0)
      gvts_pushinteger(L, ar->currentline);                         
    else gvts_pushnil(L);
    gvts_assert(gvts_getinfo(L, "lS", ar));
    gvts_call(L, 2, 0);                          
  }
}

static int makemask (const char *smask, int count) {
  int mask = 0;
  if (strchr(smask, 'c')) mask |= GVTS_MASKCALL;
  if (strchr(smask, 'r')) mask |= GVTS_MASKRET;
  if (strchr(smask, 'l')) mask |= GVTS_MASKLINE;
  if (count > 0) mask |= GVTS_MASKCOUNT;
  return mask;
}

static char *unmakemask (int mask, char *smask) {
  int i = 0;
  if (mask & GVTS_MASKCALL) smask[i++] = 'c';
  if (mask & GVTS_MASKRET) smask[i++] = 'r';
  if (mask & GVTS_MASKLINE) smask[i++] = 'l';
  smask[i] = '\0';
  return smask;
}

static int db_sethook (gvts_State *L) {
  int arg, mask, count;
  gvts_Hook func;
  gvts_State *L1 = getthread(L, &arg);
  if (gvts_isnoneornil(L, arg+1)) {                
    gvts_settop(L, arg+1);
    func = NULL; mask = 0; count = 0;                      
  }
  else {
    const char *smask = gvtsG_checkstring(L, arg+2);
    gvtsG_checktype(L, arg+1, GVTS_TFUNCTION);
    count = (int)gvtsG_optinteger(L, arg + 3, 0);
    func = hookf; mask = makemask(smask, count);
  }
  if (!gvtsG_getsubtable(L, GVTS_REGISTRYINDEX, HOOKKEY)) {

    gvts_pushliteral(L, "k");
    gvts_setfield(L, -2, "__mode");                               
    gvts_pushvalue(L, -1);
    gvts_setmetatable(L, -2);                                        
  }
  checkstack(L, L1, 1);
  gvts_pushthread(L1); gvts_xmove(L1, L, 1);                    
  gvts_pushvalue(L, arg + 1);                             
  gvts_rawset(L, -3);                                    
  gvts_sethook(L1, func, mask, count);
  return 0;
}

static int db_gethook (gvts_State *L) {
  int arg;
  gvts_State *L1 = getthread(L, &arg);
  char buff[5];
  int mask = gvts_gethookmask(L1);
  gvts_Hook hook = gvts_gethook(L1);
  if (hook == NULL) {                
    gvtsG_pushfail(L);
    return 1;
  }
  else if (hook != hookf)                      
    gvts_pushliteral(L, "external hook");
  else {                             
    gvts_getfield(L, GVTS_REGISTRYINDEX, HOOKKEY);
    checkstack(L, L1, 1);
    gvts_pushthread(L1); gvts_xmove(L1, L, 1);
    gvts_rawget(L, -2);                                   
    gvts_remove(L, -2);                         
  }
  gvts_pushstring(L, unmakemask(mask, buff));                         
  gvts_pushinteger(L, gvts_gethookcount(L1));                          
  return 3;
}

static int db_debug (gvts_State *L) {
  for (;;) {
    char buffer[250];
    gvts_writestringerror("%s", "gvts_debug> ");
    if (fgets(buffer, sizeof(buffer), stdin) == NULL ||
        strcmp(buffer, "cont\n") == 0)
      return 0;
    if (gvtsG_loadbuffer(L, buffer, strlen(buffer), "=(debug command)") ||
        gvts_pcall(L, 0, 0, 0))
      gvts_writestringerror("%s\n", gvtsG_tolstring(L, -1, NULL));
    gvts_settop(L, 0);                               
  }
}

static int db_traceback (gvts_State *L) {
  int arg;
  gvts_State *L1 = getthread(L, &arg);
  const char *msg = gvts_tostring(L, arg + 1);
  if (msg == NULL && !gvts_isnoneornil(L, arg + 1))                         
    gvts_pushvalue(L, arg + 1);                           
  else {
    int level = (int)gvtsG_optinteger(L, arg + 2, (L == L1) ? 1 : 0);
    gvtsG_traceback(L, L1, msg, level);
  }
  return 1;
}

static int db_setcstacklimit (gvts_State *L) {
  int limit = (int)gvtsG_checkinteger(L, 1);
  int res = gvts_setcstacklimit(L, limit);
  gvts_pushinteger(L, res);
  return 1;
}

static const gvtsG_Reg dblib[] = {
  {"debug", db_debug},
  {"getuservalue", db_getuservalue},
  {"gethook", db_gethook},
  {"getinfo", db_getinfo},
  {"getlocal", db_getlocal},
  {"getregistry", db_getregistry},
  {"getmetatable", db_getmetatable},
  {"getupvalue", db_getupvalue},
  {"upvaluejoin", db_upvaluejoin},
  {"upvalueid", db_upvalueid},
  {"setuservalue", db_setuservalue},
  {"sethook", db_sethook},
  {"setlocal", db_setlocal},
  {"setmetatable", db_setmetatable},
  {"setupvalue", db_setupvalue},
  {"traceback", db_traceback},
  {"setcstacklimit", db_setcstacklimit},
  {NULL, NULL}
};

GVTSMOD_API int gvtsopen_debug (gvts_State *L) {
  gvtsG_newlib(L, dblib);
  return 1;
}
