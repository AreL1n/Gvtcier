#define loadlib_c
#define GVTS_LIB

#include "Gprefix.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gvts.h"

#include "Gauxlib.h"
#include "gvtslib.h"

#if !defined(GVTS_CSUBSEP)
#define GVTS_CSUBSEP		GVTS_DIRSEP
#endif

#if !defined(GVTS_LSUBSEP)
#define GVTS_LSUBSEP		GVTS_DIRSEP
#endif

#define GVTS_POF		"gvtsopen_"

#define GVTS_OFSEP	"_"

static const char *const CLIBS = "_CLIBS";

#define LIB_FAIL	"open"

#define setprogdir(L)           ((void)0)

typedef void (*voidf)(void);

static void lsys_unloadlib (void *lib);

static void *lsys_load (gvts_State *L, const char *path, int seeglb);

static gvts_CFunction lsys_sym (gvts_State *L, void *lib, const char *sym);

#if defined(GVTS_USE_DLOPEN)	       

#include <dlfcn.h>

#if defined(__GNUC__)
#define cast_func(p) (__extension__ (gvts_CFunction)(p))
#else
#define cast_func(p) ((gvts_CFunction)(p))
#endif

static void lsys_unloadlib (void *lib) {
  dlclose(lib);
}

static void *lsys_load (gvts_State *L, const char *path, int seeglb) {
  void *lib = dlopen(path, RTLD_NOW | (seeglb ? RTLD_GLOBAL : RTLD_LOCAL));
  if (g_unlikely(lib == NULL))
    gvts_pushstring(L, dlerror());
  return lib;
}

static gvts_CFunction lsys_sym (gvts_State *L, void *lib, const char *sym) {
  gvts_CFunction f = cast_func(dlsym(lib, sym));
  if (g_unlikely(f == NULL))
    gvts_pushstring(L, dlerror());
  return f;
}

#elif defined(GVTS_DL_DLL)	        

#include <windows.h>

#if !defined(GVTS_LLE_FLAGS)
#define GVTS_LLE_FLAGS	0
#endif

#undef setprogdir

static void setprogdir (gvts_State *L) {
  char buff[MAX_PATH + 1];
  char *lb;
  DWORD nsize = sizeof(buff)/sizeof(char);
  DWORD n = GetModuleFileNameA(NULL, buff, nsize);                      
  if (n == 0 || n == nsize || (lb = strrchr(buff, '\\')) == NULL)
    gvtsG_error(L, "unable to get ModuleFileName");
  else {
    *lb = '\0';                                                 
    gvtsG_gsub(L, gvts_tostring(L, -1), GVTS_EXEC_DIR, buff);
    gvts_remove(L, -2);                              
  }
}

static void pusherror (gvts_State *L) {
  int error = GetLastError();
  char buffer[128];
  if (FormatMessageA(FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM,
      NULL, error, 0, buffer, sizeof(buffer)/sizeof(char), NULL))
    gvts_pushstring(L, buffer);
  else
    gvts_pushfstring(L, "system error %d\n", error);
}

static void lsys_unloadlib (void *lib) {
  FreeLibrary((HMODULE)lib);
}

static void *lsys_load (gvts_State *L, const char *path, int seeglb) {
  HMODULE lib = LoadLibraryExA(path, NULL, GVTS_LLE_FLAGS);
  (void)(seeglb);                                                 
  if (lib == NULL) pusherror(L);
  return lib;
}

static gvts_CFunction lsys_sym (gvts_State *L, void *lib, const char *sym) {
  gvts_CFunction f = (gvts_CFunction)(voidf)GetProcAddress((HMODULE)lib, sym);
  if (f == NULL) pusherror(L);
  return f;
}

#else				        

#undef LIB_FAIL
#define LIB_FAIL	"absent"

#define DLMSG	"dynamic libraries not enabled; check your Gvts installation"

static void lsys_unloadlib (void *lib) {
  (void)(lib);                
}

static void *lsys_load (gvts_State *L, const char *path, int seeglb) {
  (void)(path); (void)(seeglb);                
  gvts_pushliteral(L, DLMSG);
  return NULL;
}

static gvts_CFunction lsys_sym (gvts_State *L, void *lib, const char *sym) {
  (void)(lib); (void)(sym);                
  gvts_pushliteral(L, DLMSG);
  return NULL;
}

#endif				       

#if !defined(GVTS_PATH_VAR)
#define GVTS_PATH_VAR    "GVTS_PATH"
#endif

#if !defined(GVTS_CPATH_VAR)
#define GVTS_CPATH_VAR   "GVTS_CPATH"
#endif

static int noenv (gvts_State *L) {
  int b;
  gvts_getfield(L, GVTS_REGISTRYINDEX, "GVTS_NOENV");
  b = gvts_toboolean(L, -1);
  gvts_pop(L, 1);                    
  return b;
}

static void setpath (gvts_State *L, const char *fieldname,
                                   const char *envname,
                                   const char *dft) {
  const char *dftmark;
  const char *nver = gvts_pushfstring(L, "%s%s", envname, GVTS_VERSUFFIX);
  const char *path = getenv(nver);                          
  if (path == NULL)                                          
    path = getenv(envname);                            
  if (path == NULL || noenv(L))                                
    gvts_pushstring(L, dft);                   
  else if ((dftmark = strstr(path, GVTS_PATH_SEP GVTS_PATH_SEP)) == NULL)
    gvts_pushstring(L, path);                         
  else {                                                              
    size_t len = strlen(path);
    gvtsG_Buffer b;
    gvtsG_buffinit(L, &b);
    if (path < dftmark) {                                      
      gvtsG_addlstring(&b, path, dftmark - path);              
      gvtsG_addchar(&b, *GVTS_PATH_SEP);
    }
    gvtsG_addstring(&b, dft);                   
    if (dftmark < path + len - 2) {                                     
      gvtsG_addchar(&b, *GVTS_PATH_SEP);
      gvtsG_addlstring(&b, dftmark + 2, (path + len - 2) - dftmark);
    }
    gvtsG_pushresult(&b);
  }
  setprogdir(L);
  gvts_setfield(L, -3, fieldname);                                       
  gvts_pop(L, 1);                                            
}

static void *checkclib (gvts_State *L, const char *path) {
  void *plib;
  gvts_getfield(L, GVTS_REGISTRYINDEX, CLIBS);
  gvts_getfield(L, -1, path);
  plib = gvts_touserdata(L, -1);                          
  gvts_pop(L, 2);                                  
  return plib;
}

static void addtoclib (gvts_State *L, const char *path, void *plib) {
  gvts_getfield(L, GVTS_REGISTRYINDEX, CLIBS);
  gvts_pushlightuserdata(L, plib);
  gvts_pushvalue(L, -1);
  gvts_setfield(L, -3, path);                          
  gvts_rawseti(L, -2, gvtsG_len(L, -2) + 1);                                
  gvts_pop(L, 1);                       
}

static int gctm (gvts_State *L) {
  gvts_Integer n = gvtsG_len(L, 1);
  for (; n >= 1; n--) {                                         
    gvts_rawgeti(L, 1, n);                           
    lsys_unloadlib(gvts_touserdata(L, -1));
    gvts_pop(L, 1);                  
  }
  return 0;
}

#define ERRLIB		1
#define ERRFUNC		2

static int lookforfunc (gvts_State *L, const char *path, const char *sym) {
  void *reg = checkclib(L, path);                                
  if (reg == NULL) {                          
    reg = lsys_load(L, path, *sym == '*');                                    
    if (reg == NULL) return ERRLIB;                              
    addtoclib(L, path, reg);
  }
  if (*sym == '*') {                                           
    gvts_pushboolean(L, 1);                     
    return 0;                 
  }
  else {
    gvts_CFunction f = lsys_sym(L, reg, sym);
    if (f == NULL)
      return ERRFUNC;                               
    gvts_pushcfunction(L, f);                                
    return 0;                 
  }
}

static int ll_loadlib (gvts_State *L) {
  const char *path = gvtsG_checkstring(L, 1);
  const char *init = gvtsG_checkstring(L, 2);
  int stat = lookforfunc(L, path, init);
  if (g_likely(stat == 0))                  
    return 1;                                  
  else {                                            
    gvtsG_pushfail(L);
    gvts_insert(L, -2);
    gvts_pushstring(L, (stat == ERRLIB) ?  LIB_FAIL : "init");
    return 3;                                             
  }
}

static int readable (const char *filename) {
  FILE *f = fopen(filename, "r");                        
  if (f == NULL) return 0;                   
  fclose(f);
  return 1;
}

static const char *getnextfilename (char **path, char *end) {
  char *sep;
  char *name = *path;
  if (name == end)
    return NULL;                     
  else if (*name == '\0') {                                
    *name = *GVTS_PATH_SEP;                         
    name++;               
  }
  sep = strchr(name, *GVTS_PATH_SEP);                           
  if (sep == NULL)                            
    sep = end;                               
  *sep = '\0';                        
  *path = sep;                                        
  return name;
}

static void pusherrornotfound (gvts_State *L, const char *path) {
  gvtsG_Buffer b;
  gvtsG_buffinit(L, &b);
  gvtsG_addstring(&b, "no file '");
  gvtsG_addgsub(&b, path, GVTS_PATH_SEP, "'\n\tno file '");
  gvtsG_addstring(&b, "'");
  gvtsG_pushresult(&b);
}

static const char *searchpath (gvts_State *L, const char *name,
                                             const char *path,
                                             const char *sep,
                                             const char *dirsep) {
  gvtsG_Buffer buff;
  char *pathname;                               
  char *endpathname;               
  const char *filename;

  if (*sep != '\0' && strchr(name, *sep) != NULL)
    name = gvtsG_gsub(L, name, sep, dirsep);                              
  gvtsG_buffinit(L, &buff);

  gvtsG_addgsub(&buff, path, GVTS_PATH_MARK, name);
  gvtsG_addchar(&buff, '\0');
  pathname = gvtsG_buffaddr(&buff);                                   
  endpathname = pathname + gvtsG_bufflen(&buff) - 1;
  while ((filename = getnextfilename(&pathname, endpathname)) != NULL) {
    if (readable(filename))                                        
      return gvts_pushstring(L, filename);                            
  }
  gvtsG_pushresult(&buff);                                         
  pusherrornotfound(L, gvts_tostring(L, -1));                            
  return NULL;                 
}

static int ll_searchpath (gvts_State *L) {
  const char *f = searchpath(L, gvtsG_checkstring(L, 1),
                                gvtsG_checkstring(L, 2),
                                gvtsG_optstring(L, 3, "."),
                                gvtsG_optstring(L, 4, GVTS_DIRSEP));
  if (f != NULL) return 1;
  else {                                            
    gvtsG_pushfail(L);
    gvts_insert(L, -2);
    return 2;                                   
  }
}

static const char *findfile (gvts_State *L, const char *name,
                                           const char *pname,
                                           const char *dirsep) {
  const char *path;
  gvts_getfield(L, gvts_upvalueindex(1), pname);
  path = gvts_tostring(L, -1);
  if (g_unlikely(path == NULL))
    gvtsG_error(L, "'package.%s' must be a string", pname);
  return searchpath(L, name, path, ".", dirsep);
}

static int checkload (gvts_State *L, int stat, const char *filename) {
  if (g_likely(stat)) {                                   
    gvts_pushstring(L, filename);                                      
    return 2;                                          
  }
  else
    return gvtsG_error(L, "error loading module '%s' from file '%s':\n\t%s",
                          gvts_tostring(L, 1), filename, gvts_tostring(L, -1));
}

static int searcher_Gvts (gvts_State *L) {
  const char *filename;
  const char *name = gvtsG_checkstring(L, 1);
  filename = findfile(L, name, "path", GVTS_LSUBSEP);
  if (filename == NULL) return 1;                                     
  return checkload(L, (gvtsG_loadfile(L, filename) == GVTS_OK), filename);
}

static int loadfunc (gvts_State *L, const char *filename, const char *modname) {
  const char *openfunc;
  const char *mark;
  modname = gvtsG_gsub(L, modname, ".", GVTS_OFSEP);
  mark = strchr(modname, *GVTS_IGMARK);
  if (mark) {
    int stat;
    openfunc = gvts_pushlstring(L, modname, mark - modname);
    openfunc = gvts_pushfstring(L, GVTS_POF"%s", openfunc);
    stat = lookforfunc(L, filename, openfunc);
    if (stat != ERRFUNC) return stat;
    modname = mark + 1;                                            
  }
  openfunc = gvts_pushfstring(L, GVTS_POF"%s", modname);
  return lookforfunc(L, filename, openfunc);
}

static int searcher_C (gvts_State *L) {
  const char *name = gvtsG_checkstring(L, 1);
  const char *filename = findfile(L, name, "cpath", GVTS_CSUBSEP);
  if (filename == NULL) return 1;                                     
  return checkload(L, (loadfunc(L, filename, name) == 0), filename);
}

static int searcher_Croot (gvts_State *L) {
  const char *filename;
  const char *name = gvtsG_checkstring(L, 1);
  const char *p = strchr(name, '.');
  int stat;
  if (p == NULL) return 0;               
  gvts_pushlstring(L, name, p - name);
  filename = findfile(L, gvts_tostring(L, -1), "cpath", GVTS_CSUBSEP);
  if (filename == NULL) return 1;                      
  if ((stat = loadfunc(L, filename, name)) != 0) {
    if (stat != ERRFUNC)
      return checkload(L, 0, filename);                  
    else {                               
      gvts_pushfstring(L, "no module '%s' in file '%s'", name, filename);
      return 1;
    }
  }
  gvts_pushstring(L, filename);                                      
  return 2;
}

static int searcher_preload (gvts_State *L) {
  const char *name = gvtsG_checkstring(L, 1);
  gvts_getfield(L, GVTS_REGISTRYINDEX, GVTS_PRELOAD_TABLE);
  if (gvts_getfield(L, -1, name) == GVTS_TNIL) {                  
    gvts_pushfstring(L, "no field package.preload['%s']", name);
    return 1;
  }
  else {
    gvts_pushliteral(L, ":preload:");
    return 2;
  }
}

static void findloader (gvts_State *L, const char *name) {
  int i;
  gvtsG_Buffer msg;                              

  if (g_unlikely(gvts_getfield(L, gvts_upvalueindex(1), "searchers")
                 != GVTS_TTABLE))
    gvtsG_error(L, "'package.searchers' must be a table");
  gvtsG_buffinit(L, &msg);

  for (i = 1; ; i++) {
    gvtsG_addstring(&msg, "\n\t");                            
    if (g_unlikely(gvts_rawgeti(L, 3, i) == GVTS_TNIL)) {                          
      gvts_pop(L, 1);                  
      gvtsG_buffsub(&msg, 2);                     
      gvtsG_pushresult(&msg);                            
      gvtsG_error(L, "module '%s' not found:%s", name, gvts_tostring(L, -1));
    }
    gvts_pushstring(L, name);
    gvts_call(L, 1, 2);               
    if (gvts_isfunction(L, -2))                             
      return;                           
    else if (gvts_isstring(L, -2)) {                                        
      gvts_pop(L, 1);                           
      gvtsG_addvalue(&msg);                                 
    }
    else {                        
      gvts_pop(L, 2);                           
      gvtsG_buffsub(&msg, 2);                     
    }
  }
}

static int ll_require (gvts_State *L) {
  const char *name = gvtsG_checkstring(L, 1);
  gvts_settop(L, 1);                                       
  gvts_getfield(L, GVTS_REGISTRYINDEX, GVTS_LOADED_TABLE);
  gvts_getfield(L, 2, name);                    
  if (gvts_toboolean(L, -1))                    
    return 1;                                 

  gvts_pop(L, 1);                                
  findloader(L, name);
  gvts_rotate(L, -2, 1);                                
  gvts_pushvalue(L, 1);                                             
  gvts_pushvalue(L, -3);                                   

  gvts_call(L, 2, 1);                                 

  if (!gvts_isnil(L, -1))                       
    gvts_setfield(L, 2, name);                                     
  else
    gvts_pop(L, 1);               
  if (gvts_getfield(L, 2, name) == GVTS_TNIL) {                             
    gvts_pushboolean(L, 1);                          
    gvts_copy(L, -1, -2);                             
    gvts_setfield(L, 2, name);                           
  }
  gvts_rotate(L, -2, 1);                                      
  return 2;                                            
}

static const gvtsG_Reg pk_funcs[] = {
  {"loadlib", ll_loadlib},
  {"searchpath", ll_searchpath},

  {"preload", NULL},
  {"cpath", NULL},
  {"path", NULL},
  {"searchers", NULL},
  {"loaded", NULL},
  {NULL, NULL}
};

static const gvtsG_Reg ll_funcs[] = {
  {"require", ll_require},
  {NULL, NULL}
};

static void createsearcherstable (gvts_State *L) {
  static const gvts_CFunction searchers[] = {
    searcher_preload,
    searcher_Gvts,
    searcher_C,
    searcher_Croot,
    NULL
  };
  int i;

  gvts_createtable(L, sizeof(searchers)/sizeof(searchers[0]) - 1, 0);

  for (i=0; searchers[i] != NULL; i++) {
    gvts_pushvalue(L, -2);                                                  
    gvts_pushcclosure(L, searchers[i], 1);
    gvts_rawseti(L, -2, i+1);
  }
  gvts_setfield(L, -2, "searchers");                                   
}

static void createclibstable (gvts_State *L) {
  gvtsG_getsubtable(L, GVTS_REGISTRYINDEX, CLIBS);                          
  gvts_createtable(L, 0, 1);                                  
  gvts_pushcfunction(L, gctm);
  gvts_setfield(L, -2, "__gc");                                     
  gvts_setmetatable(L, -2);
}

GVTSMOD_API int gvtsopen_package (gvts_State *L) {
  createclibstable(L);
  gvtsG_newlib(L, pk_funcs);                              
  createsearcherstable(L);

  setpath(L, "path", GVTS_PATH_VAR, GVTS_PATH_DEFAULT);
  setpath(L, "cpath", GVTS_CPATH_VAR, GVTS_CPATH_DEFAULT);

  gvts_pushliteral(L, GVTS_DIRSEP "\n" GVTS_PATH_SEP "\n" GVTS_PATH_MARK "\n"
                     GVTS_EXEC_DIR "\n" GVTS_IGMARK "\n");
  gvts_setfield(L, -2, "config");

  gvtsG_getsubtable(L, GVTS_REGISTRYINDEX, GVTS_LOADED_TABLE);
  gvts_setfield(L, -2, "loaded");

  gvtsG_getsubtable(L, GVTS_REGISTRYINDEX, GVTS_PRELOAD_TABLE);
  gvts_setfield(L, -2, "preload");
  gvts_pushglobaltable(L);
  gvts_pushvalue(L, -2);                                             
  gvtsG_setfuncs(L, ll_funcs, 1);                                  
  gvts_pop(L, 1);                        
  return 1;                              
}
