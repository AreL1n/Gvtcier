#define lbaselib_c
#define GVTS_LIB

#include "Gprefix.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gvts.h"

#include "Gauxlib.h"
#include "gvtslib.h"

static int gvtsB_print (gvts_State *L) {
  int n = gvts_gettop(L);                           
  int i;
  for (i = 1; i <= n; i++) {                         
    size_t l;
    const char *s = gvtsG_tolstring(L, i, &l);                            
    if (i > 1)                              
      gvts_writestring("\t", 1);                           
    gvts_writestring(s, l);                
    gvts_pop(L, 1);                  
  }
  gvts_writeline();
  return 0;
}

static int gvtsB_warn (gvts_State *L) {
  int n = gvts_gettop(L);                           
  int i;
  gvtsG_checkstring(L, 1);                             
  for (i = 2; i <= n; i++)
    gvtsG_checkstring(L, i);                                           
  for (i = 1; i < n; i++)                       
    gvts_warning(L, gvts_tostring(L, i), 1);
  gvts_warning(L, gvts_tostring(L, n), 0);                     
  return 0;
}

#define SPACECHARS	" \f\n\r\t\v"

static const char *b_str2int (const char *s, int base, gvts_Integer *pn) {
  gvts_Unsigned n = 0;
  int neg = 0;
  s += strspn(s, SPACECHARS);                           
  if (*s == '-') { s++; neg = 1; }                   
  else if (*s == '+') s++;
  if (!isalnum((unsigned char)*s))                 
    return NULL;
  do {
    int digit = (isdigit((unsigned char)*s)) ? *s - '0'
                   : (toupper((unsigned char)*s) - 'A') + 10;
    if (digit >= base) return NULL;                       
    n = n * base + digit;
    s++;
  } while (isalnum((unsigned char)*s));
  s += strspn(s, SPACECHARS);                            
  *pn = (gvts_Integer)((neg) ? (0u - n) : n);
  return s;
}

static int gvtsB_tonumber (gvts_State *L) {
  if (gvts_isnoneornil(L, 2)) {                            
    if (gvts_type(L, 1) == GVTS_TNUMBER) {                         
      gvts_settop(L, 1);                      
      return 1;
    }
    else {
      size_t l;
      const char *s = gvts_tolstring(L, 1, &l);
      if (s != NULL && gvts_stringtonumber(L, s) == l + 1)
        return 1;                                       

      gvtsG_checkany(L, 1);                                          
    }
  }
  else {
    size_t l;
    const char *s;
    gvts_Integer n = 0;                         
    gvts_Integer base = gvtsG_checkinteger(L, 2);
    gvtsG_checktype(L, 1, GVTS_TSTRING);                             
    s = gvts_tolstring(L, 1, &l);
    gvtsG_argcheck(L, 2 <= base && base <= 36, 2, u8"进制超出范围");
    if (b_str2int(s, (int)base, &n) == s + l) {
      gvts_pushinteger(L, n);
      return 1;
    }                         
  }                         
  gvtsG_pushfail(L);                    
  return 1;
}

static int gvtsB_error (gvts_State *L) {
  int level = (int)gvtsG_optinteger(L, 2, 1);
  gvts_settop(L, 1);
  if (gvts_type(L, 1) == GVTS_TSTRING && level > 0) {
    gvtsG_where(L, level);                              
    gvts_pushvalue(L, 1);
    gvts_concat(L, 2);
  }
  return gvts_error(L);
}

static int gvtsB_getmetatable (gvts_State *L) {
  gvtsG_checkany(L, 1);
  if (!gvts_getmetatable(L, 1)) {
    gvts_pushnil(L);
    return 1;                    
  }
  gvtsG_getmetafield(L, 1, "__metatable");
  return 1;                                                                  
}

static int gvtsB_setmetatable (gvts_State *L) {
  int t = gvts_type(L, 2);
  gvtsG_checktype(L, 1, GVTS_TTABLE);
  gvtsG_argexpected(L, t == GVTS_TNIL || t == GVTS_TTABLE, 2, u8"空值或表");
  if (g_unlikely(gvtsG_getmetafield(L, 1, "__metatable") != GVTS_TNIL))
    return gvtsG_error(L, u8"不能修改受保护的元表");
  gvts_settop(L, 2);
  gvts_setmetatable(L, 1);
  return 1;
}

static int gvtsB_rawequal (gvts_State *L) {
  gvtsG_checkany(L, 1);
  gvtsG_checkany(L, 2);
  gvts_pushboolean(L, gvts_rawequal(L, 1, 2));
  return 1;
}

static int gvtsB_rawlen (gvts_State *L) {
  int t = gvts_type(L, 1);
  gvtsG_argexpected(L, t == GVTS_TTABLE || t == GVTS_TSTRING, 1,
                      u8"表或字符串");
  gvts_pushinteger(L, gvts_rawlen(L, 1));
  return 1;
}

static int gvtsB_rawget (gvts_State *L) {
  gvtsG_checktype(L, 1, GVTS_TTABLE);
  gvtsG_checkany(L, 2);
  gvts_settop(L, 2);
  gvts_rawget(L, 1);
  return 1;
}

static int gvtsB_rawset (gvts_State *L) {
  gvtsG_checktype(L, 1, GVTS_TTABLE);
  gvtsG_checkany(L, 2);
  gvtsG_checkany(L, 3);
  gvts_settop(L, 3);
  gvts_rawset(L, 1);
  return 1;
}

static int pushmode (gvts_State *L, int oldmode) {
  if (oldmode == -1)
    gvtsG_pushfail(L);                                
  else
    gvts_pushstring(L, (oldmode == GVTS_GCINC) ? "incremental"
                                             : "generational");
  return 1;
}

#define checkvalres(res) { if (res == -1) break; }

static int gvtsB_collectgarbage (gvts_State *L) {
  static const char *const opts[] = {"stop", "restart", "collect",
    "count", "step", "setpause", "setstepmul",
    "isrunning", "generational", "incremental", NULL};
  static const int optsnum[] = {GVTS_GCSTOP, GVTS_GCRESTART, GVTS_GCCOLLECT,
    GVTS_GCCOUNT, GVTS_GCSTEP, GVTS_GCSETPAUSE, GVTS_GCSETSTEPMUL,
    GVTS_GCISRUNNING, GVTS_GCGEN, GVTS_GCINC};
  int o = optsnum[gvtsG_checkoption(L, 1, "collect", opts)];
  switch (o) {
    case GVTS_GCCOUNT: {
      int k = gvts_gc(L, o);
      int b = gvts_gc(L, GVTS_GCCOUNTB);
      checkvalres(k);
      gvts_pushnumber(L, (gvts_Number)k + ((gvts_Number)b/1024));
      return 1;
    }
    case GVTS_GCSTEP: {
      int step = (int)gvtsG_optinteger(L, 2, 0);
      int res = gvts_gc(L, o, step);
      checkvalres(res);
      gvts_pushboolean(L, res);
      return 1;
    }
    case GVTS_GCSETPAUSE:
    case GVTS_GCSETSTEPMUL: {
      int p = (int)gvtsG_optinteger(L, 2, 0);
      int previous = gvts_gc(L, o, p);
      checkvalres(previous);
      gvts_pushinteger(L, previous);
      return 1;
    }
    case GVTS_GCISRUNNING: {
      int res = gvts_gc(L, o);
      checkvalres(res);
      gvts_pushboolean(L, res);
      return 1;
    }
    case GVTS_GCGEN: {
      int minormul = (int)gvtsG_optinteger(L, 2, 0);
      int majormul = (int)gvtsG_optinteger(L, 3, 0);
      return pushmode(L, gvts_gc(L, o, minormul, majormul));
    }
    case GVTS_GCINC: {
      int pause = (int)gvtsG_optinteger(L, 2, 0);
      int stepmul = (int)gvtsG_optinteger(L, 3, 0);
      int stepsize = (int)gvtsG_optinteger(L, 4, 0);
      return pushmode(L, gvts_gc(L, o, pause, stepmul, stepsize));
    }
    default: {
      int res = gvts_gc(L, o);
      checkvalres(res);
      gvts_pushinteger(L, res);
      return 1;
    }
  }
  gvtsG_pushfail(L);                                         
  return 1;
}

static int gvtsB_type (gvts_State *L) {
  int t = gvts_type(L, 1);
  gvtsG_argcheck(L, t != GVTS_TNONE, 1, u8"需要一个值");
  gvts_pushstring(L, gvts_typename(L, t));
  return 1;
}

static int gvtsB_next (gvts_State *L) {
  gvtsG_checktype(L, 1, GVTS_TTABLE);
  gvts_settop(L, 2);                                                
  if (gvts_next(L, 1))
    return 2;
  else {
    gvts_pushnil(L);
    return 1;
  }
}

static int pairscont (gvts_State *L, int status, gvts_KContext k) {
  (void)L; (void)status; (void)k;              
  return 3;
}

static int gvtsB_pairs (gvts_State *L) {
  gvtsG_checkany(L, 1);
  if (gvtsG_getmetafield(L, 1, "__pairs") == GVTS_TNIL) {                      
    gvts_pushcfunction(L, gvtsB_next);                              
    gvts_pushvalue(L, 1);              
    gvts_pushnil(L);                         
  }
  else {
    gvts_pushvalue(L, 1);                                     
    gvts_callk(L, 1, 3, 0, pairscont);                                    
  }
  return 3;
}

static int ipairsaux (gvts_State *L) {
  gvts_Integer i = gvtsG_checkinteger(L, 2);
  i = gvtsG_intop(+, i, 1);
  gvts_pushinteger(L, i);
  return (gvts_geti(L, 1, i) == GVTS_TNIL) ? 1 : 2;
}

static int gvtsB_ipairs (gvts_State *L) {
  gvtsG_checkany(L, 1);
  gvts_pushcfunction(L, ipairsaux);                          
  gvts_pushvalue(L, 1);             
  gvts_pushinteger(L, 0);                     
  return 3;
}

static int load_aux (gvts_State *L, int status, int envidx) {
  if (g_likely(status == GVTS_OK)) {
    if (envidx != 0) {                        
      gvts_pushvalue(L, envidx);                                       
      if (!gvts_setupvalue(L, -2, 1))                             
        gvts_pop(L, 1);                                                 
    }
    return 1;
  }
  else {                                              
    gvtsG_pushfail(L);
    gvts_insert(L, -2);                                
    return 2;                                      
  }
}

static int gvtsB_loadfile (gvts_State *L) {
  const char *fname = gvtsG_optstring(L, 1, NULL);
  const char *mode = gvtsG_optstring(L, 2, NULL);
  int env = (!gvts_isnone(L, 3) ? 3 : 0);                                    
  int status = gvtsG_loadfilex(L, fname, mode);
  return load_aux(L, status, env);
}

#define RESERVEDSLOT	5

static const char *generic_reader (gvts_State *L, void *ud, size_t *size) {
  (void)(ud);                
  gvtsG_checkstack(L, 2, u8"嵌套函数太多");
  gvts_pushvalue(L, 1);                    
  gvts_call(L, 0, 1);               
  if (gvts_isnil(L, -1)) {
    gvts_pop(L, 1);                  
    *size = 0;
    return NULL;
  }
  else if (g_unlikely(!gvts_isstring(L, -1)))
    gvtsG_error(L, u8"读取函数必须返回字符串");
  gvts_replace(L, RESERVEDSLOT);                                    
  return gvts_tolstring(L, RESERVEDSLOT, size);
}

static int gvtsB_load (gvts_State *L) {
  int status;
  size_t l;
  const char *s = gvts_tolstring(L, 1, &l);
  const char *mode = gvtsG_optstring(L, 3, "bt");
  int env = (!gvts_isnone(L, 4) ? 4 : 0);                                    
  if (s != NULL) {                         
    const char *chunkname = gvtsG_optstring(L, 2, s);
    status = gvtsG_loadbufferx(L, s, l, chunkname, mode);
  }
  else {                                      
    const char *chunkname = gvtsG_optstring(L, 2, "=(load)");
    gvtsG_checktype(L, 1, GVTS_TFUNCTION);
    gvts_settop(L, RESERVEDSLOT);                            
    status = gvts_load(L, generic_reader, NULL, chunkname, mode);
  }
  return load_aux(L, status, env);
}

static int dofilecont (gvts_State *L, int d1, gvts_KContext d2) {
  (void)d1;  (void)d2;                                               
  return gvts_gettop(L) - 1;
}

static int gvtsB_dofile (gvts_State *L) {
  const char *fname = gvtsG_optstring(L, 1, NULL);
  gvts_settop(L, 1);
  if (g_unlikely(gvtsG_loadfile(L, fname) != GVTS_OK))
    return gvts_error(L);
  gvts_callk(L, 0, GVTS_MULTRET, 0, dofilecont);
  return dofilecont(L, 0, 0);
}

static int gvtsB_assert (gvts_State *L) {
  if (g_likely(gvts_toboolean(L, 1)))                          
    return gvts_gettop(L);                            
  else {             
    gvtsG_checkany(L, 1);                                 
    gvts_remove(L, 1);                 
    gvts_pushliteral(L, u8"断言失败！");                       
    gvts_settop(L, 1);                                                    
    return gvtsB_error(L);                    
  }
}

static int gvtsB_select (gvts_State *L) {
  int n = gvts_gettop(L);
  if (gvts_type(L, 1) == GVTS_TSTRING && *gvts_tostring(L, 1) == '#') {
    gvts_pushinteger(L, n-1);
    return 1;
  }
  else {
    gvts_Integer i = gvtsG_checkinteger(L, 1);
    if (i < 0) i = n + i;
    else if (i > n) i = n;
    gvtsG_argcheck(L, 1 <= i, 1, u8"索引超出范围");
    return n - (int)i;
  }
}

static int finishpcall (gvts_State *L, int status, gvts_KContext extra) {
  if (g_unlikely(status != GVTS_OK && status != GVTS_YIELD)) {              
    gvts_pushboolean(L, 0);                            
    gvts_pushvalue(L, -2);                     
    return 2;                         
  }
  else
    return gvts_gettop(L) - (int)extra;                          
}

static int gvtsB_pcall (gvts_State *L) {
  int status;
  gvtsG_checkany(L, 1);
  gvts_pushboolean(L, 1);                                 
  gvts_insert(L, 1);                       
  status = gvts_pcallk(L, gvts_gettop(L) - 2, GVTS_MULTRET, 0, 0, finishpcall);
  return finishpcall(L, status, 0);
}

static int gvtsB_xpcall (gvts_State *L) {
  int status;
  int n = gvts_gettop(L);
  gvtsG_checktype(L, 2, GVTS_TFUNCTION);                            
  gvts_pushboolean(L, 1);                    
  gvts_pushvalue(L, 1);                
  gvts_rotate(L, 3, 2);                                            
  status = gvts_pcallk(L, n - 2, GVTS_MULTRET, 2, 2, finishpcall);
  return finishpcall(L, status, 2);
}

static int gvtsB_tostring (gvts_State *L) {
  gvtsG_checkany(L, 1);
  gvtsG_tolstring(L, 1, NULL);
  return 1;
}

static const gvtsG_Reg base_funcs[] = {
  {"assert", gvtsB_assert},
  {"collectgarbage", gvtsB_collectgarbage},
  {"dofile", gvtsB_dofile},
  {"error", gvtsB_error},
  {"getmetatable", gvtsB_getmetatable},
  {"ipairs", gvtsB_ipairs},
  {"loadfile", gvtsB_loadfile},
  {"load", gvtsB_load},
  {"next", gvtsB_next},
  {"pairs", gvtsB_pairs},
  {"pcall", gvtsB_pcall},
  {"print", gvtsB_print},
  {"warn", gvtsB_warn},
  {"rawequal", gvtsB_rawequal},
  {"rawlen", gvtsB_rawlen},
  {"rawget", gvtsB_rawget},
  {"rawset", gvtsB_rawset},
  {"select", gvtsB_select},
  {"setmetatable", gvtsB_setmetatable},
  {"tonumber", gvtsB_tonumber},
  {"tostring", gvtsB_tostring},
  {"type", gvtsB_type},
  {"xpcall", gvtsB_xpcall},

  {GVTS_GNAME, NULL},
  {"_VERSION", NULL},
  {NULL, NULL}
};

GVTSMOD_API int gvtsopen_base (gvts_State *L) {

  gvts_pushglobaltable(L);
  gvtsG_setfuncs(L, base_funcs, 0);

  gvts_pushvalue(L, -1);
  gvts_setfield(L, -2, GVTS_GNAME);

  gvts_pushliteral(L, GVTS_VERSION);
  gvts_setfield(L, -2, "_VERSION");
  return 1;
}
