#define lcorolib_c
#define GVTS_LIB

#include "Gprefix.h"

#include <stdlib.h>

#include "gvts.h"

#include "Gauxlib.h"
#include "gvtslib.h"

static gvts_State *getco (gvts_State *L) {
  gvts_State *co = gvts_tothread(L, 1);
  gvtsG_argexpected(L, co, 1, "thread");
  return co;
}

static int auxresume (gvts_State *L, gvts_State *co, int narg) {
  int status, nres;
  if (g_unlikely(!gvts_checkstack(co, narg))) {
    gvts_pushliteral(L, "too many arguments to resume");
    return -1;                  
  }
  gvts_xmove(L, co, narg);
  status = gvts_resume(co, L, narg, &nres);
  if (g_likely(status == GVTS_OK || status == GVTS_YIELD)) {
    if (g_unlikely(!gvts_checkstack(L, nres + 1))) {
      gvts_pop(co, nres);                             
      gvts_pushliteral(L, "too many results to resume");
      return -1;                  
    }
    gvts_xmove(co, L, nres);                           
    return nres;
  }
  else {
    gvts_xmove(co, L, 1);                          
    return -1;                  
  }
}

static int gvtsB_coresume (gvts_State *L) {
  gvts_State *co = getco(L);
  int r;
  r = auxresume(L, co, gvts_gettop(L) - 1);
  if (g_unlikely(r < 0)) {
    gvts_pushboolean(L, 0);
    gvts_insert(L, -2);
    return 2;                                    
  }
  else {
    gvts_pushboolean(L, 1);
    gvts_insert(L, -(r + 1));
    return r + 1;                                      
  }
}

static int gvtsB_auxwrap (gvts_State *L) {
  gvts_State *co = gvts_tothread(L, gvts_upvalueindex(1));
  int r = auxresume(L, co, gvts_gettop(L));
  if (g_unlikely(r < 0)) {              
    int stat = gvts_status(co);
    if (stat != GVTS_OK && stat != GVTS_YIELD) {                               
      stat = gvts_closethread(co, L);                               
      gvts_assert(stat != GVTS_OK);
      gvts_xmove(co, L, 1);                                        
    }
    if (stat != GVTS_ERRMEM &&                                  
        gvts_type(L, -1) == GVTS_TSTRING) {                                     
      gvtsG_where(L, 1);                                    
      gvts_insert(L, -2);
      gvts_concat(L, 2);
    }
    return gvts_error(L);                       
  }
  return r;
}

static int gvtsB_cocreate (gvts_State *L) {
  gvts_State *NL;
  gvtsG_checktype(L, 1, GVTS_TFUNCTION);
  NL = gvts_newthread(L);
  gvts_pushvalue(L, 1);                            
  gvts_xmove(L, NL, 1);                                  
  return 1;
}

static int gvtsB_cowrap (gvts_State *L) {
  gvtsB_cocreate(L);
  gvts_pushcclosure(L, gvtsB_auxwrap, 1);
  return 1;
}

static int gvtsB_yield (gvts_State *L) {
  return gvts_yield(L, gvts_gettop(L));
}

#define COS_RUN		0
#define COS_DEAD	1
#define COS_YIELD	2
#define COS_NORM	3

static const char *const statname[] =
  {"running", "dead", "suspended", "normal"};

static int auxstatus (gvts_State *L, gvts_State *co) {
  if (L == co) return COS_RUN;
  else {
    switch (gvts_status(co)) {
      case GVTS_YIELD:
        return COS_YIELD;
      case GVTS_OK: {
        gvts_Debug ar;
        if (gvts_getstack(co, 0, &ar))                            
          return COS_NORM;                     
        else if (gvts_gettop(co) == 0)
            return COS_DEAD;
        else
          return COS_YIELD;                     
      }
      default:                           
        return COS_DEAD;
    }
  }
}

static int gvtsB_costatus (gvts_State *L) {
  gvts_State *co = getco(L);
  gvts_pushstring(L, statname[auxstatus(L, co)]);
  return 1;
}

static int gvtsB_yieldable (gvts_State *L) {
  gvts_State *co = gvts_isnone(L, 1) ? L : getco(L);
  gvts_pushboolean(L, gvts_isyieldable(co));
  return 1;
}

static int gvtsB_corunning (gvts_State *L) {
  int ismain = gvts_pushthread(L);
  gvts_pushboolean(L, ismain);
  return 2;
}

static int gvtsB_close (gvts_State *L) {
  gvts_State *co = getco(L);
  int status = auxstatus(L, co);
  switch (status) {
    case COS_DEAD: case COS_YIELD: {
      status = gvts_closethread(co, L);
      if (status == GVTS_OK) {
        gvts_pushboolean(L, 1);
        return 1;
      }
      else {
        gvts_pushboolean(L, 0);
        gvts_xmove(co, L, 1);                          
        return 2;
      }
    }
    default:                                   
      return gvtsG_error(L, "cannot close a %s coroutine", statname[status]);
  }
}

static const gvtsG_Reg co_funcs[] = {
  {"create", gvtsB_cocreate},
  {"resume", gvtsB_coresume},
  {"running", gvtsB_corunning},
  {"status", gvtsB_costatus},
  {"wrap", gvtsB_cowrap},
  {"yield", gvtsB_yield},
  {"isyieldable", gvtsB_yieldable},
  {"close", gvtsB_close},
  {NULL, NULL}
};

GVTSMOD_API int gvtsopen_coroutine (gvts_State *L) {
  gvtsG_newlib(L, co_funcs);
  return 1;
}
