#define ltablib_c
#define GVTS_LIB

#include "Gprefix.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "gvts.h"

#include "Gauxlib.h"
#include "gvtslib.h"

#define TAB_R	1			          
#define TAB_W	2			           
#define TAB_L	4			            
#define TAB_RW	(TAB_R | TAB_W)		                

#define aux_getn(L,n,w)	(checktab(L, n, (w) | TAB_L), gvtsG_len(L, n))

static int checkfield (gvts_State *L, const char *key, int n) {
  gvts_pushstring(L, key);
  return (gvts_rawget(L, -n) != GVTS_TNIL);
}

static void checktab (gvts_State *L, int arg, int what) {
  if (gvts_type(L, arg) != GVTS_TTABLE) {                          
    int n = 1;                                 
    if (gvts_getmetatable(L, arg) &&                           
        (!(what & TAB_R) || checkfield(L, "__index", ++n)) &&
        (!(what & TAB_W) || checkfield(L, "__newindex", ++n)) &&
        (!(what & TAB_L) || checkfield(L, "__len", ++n))) {
      gvts_pop(L, n);                                            
    }
    else
      gvtsG_checktype(L, arg, GVTS_TTABLE);                      
  }
}

static int tinsert (gvts_State *L) {
  gvts_Integer pos;                                   
  gvts_Integer e = aux_getn(L, 1, TAB_RW);
  e = gvtsG_intop(+, e, 1);                           
  switch (gvts_gettop(L)) {
    case 2: {                                    
      pos = e;                                     
      break;
    }
    case 3: {
      gvts_Integer i;
      pos = gvtsG_checkinteger(L, 2);                                    

      gvtsG_argcheck(L, (gvts_Unsigned)pos - 1u < (gvts_Unsigned)e, 2,
                       u8"位置超出范围");
      for (i = e; i > pos; i--) {                        
        gvts_geti(L, 1, i - 1);
        gvts_seti(L, 1, i);                       
      }
      break;
    }
    default: {
      return gvtsG_error(L, u8"调用「insert」时参数数量错误");
    }
  }
  gvts_seti(L, 1, pos);                  
  return 0;
}

static int tremove (gvts_State *L) {
  gvts_Integer size = aux_getn(L, 1, TAB_RW);
  gvts_Integer pos = gvtsG_optinteger(L, 2, size);
  if (pos != size)                               

    gvtsG_argcheck(L, (gvts_Unsigned)pos - 1u <= (gvts_Unsigned)size, 2,
                     u8"位置超出范围");
  gvts_geti(L, 1, pos);                       
  for ( ; pos < size; pos++) {
    gvts_geti(L, 1, pos + 1);
    gvts_seti(L, 1, pos);                           
  }
  gvts_pushnil(L);
  gvts_seti(L, 1, pos);                           
  return 1;
}

static int tmove (gvts_State *L) {
  gvts_Integer f = gvtsG_checkinteger(L, 2);
  gvts_Integer e = gvtsG_checkinteger(L, 3);
  gvts_Integer t = gvtsG_checkinteger(L, 4);
  int tt = !gvts_isnoneornil(L, 5) ? 5 : 1;                         
  checktab(L, 1, TAB_R);
  checktab(L, tt, TAB_W);
  if (e >= f) {                                  
    gvts_Integer n, i;
    gvtsG_argcheck(L, f > 0 || e < GVTS_MAXINTEGER + f, 3,
                  u8"要移动的元素太多");
    n = e - f + 1;                                  
    gvtsG_argcheck(L, t <= GVTS_MAXINTEGER - n + 1, 4,
                  u8"目标位置回绕");
    if (t > e || t <= f || (tt != 1 && !gvts_compare(L, 1, tt, GVTS_OPEQ))) {
      for (i = 0; i < n; i++) {
        gvts_geti(L, 1, f + i);
        gvts_seti(L, tt, t + i);
      }
    }
    else {
      for (i = n - 1; i >= 0; i--) {
        gvts_geti(L, 1, f + i);
        gvts_seti(L, tt, t + i);
      }
    }
  }
  gvts_pushvalue(L, tt);                                
  return 1;
}

static void addfield (gvts_State *L, gvtsG_Buffer *b, gvts_Integer i) {
  gvts_geti(L, 1, i);
  if (g_unlikely(!gvts_isstring(L, -1)))
    gvtsG_error(L, u8"「concat」的表在索引 %I 处的值（%s）无效",
                  gvtsG_typename(L, -1), (GVTSI_UACINT)i);
  gvtsG_addvalue(b);
}

static int tconcat (gvts_State *L) {
  gvtsG_Buffer b;
  gvts_Integer last = aux_getn(L, 1, TAB_R);
  size_t lsep;
  const char *sep = gvtsG_optlstring(L, 2, "", &lsep);
  gvts_Integer i = gvtsG_optinteger(L, 3, 1);
  last = gvtsG_optinteger(L, 4, last);
  gvtsG_buffinit(L, &b);
  for (; i < last; i++) {
    addfield(L, &b, i);
    gvtsG_addlstring(&b, sep, lsep);
  }
  if (i == last)                                                  
    addfield(L, &b, i);
  gvtsG_pushresult(&b);
  return 1;
}

static int tpack (gvts_State *L) {
  int i;
  int n = gvts_gettop(L);                                  
  gvts_createtable(L, n, 1);                           
  gvts_insert(L, 1);                         
  for (i = n; i >= 1; i--)                       
    gvts_seti(L, 1, i);
  gvts_pushinteger(L, n);
  gvts_setfield(L, 1, "n");                                
  return 1;                    
}

static int tunpack (gvts_State *L) {
  gvts_Unsigned n;
  gvts_Integer i = gvtsG_optinteger(L, 2, 1);
  gvts_Integer e = gvtsG_opt(L, gvtsG_checkinteger, 3, gvtsG_len(L, 1));
  if (i > e) return 0;                   
  n = (gvts_Unsigned)e - i;                                                    
  if (g_unlikely(n >= (unsigned int)INT_MAX  ||
                 !gvts_checkstack(L, (int)(++n))))
    return gvtsG_error(L, u8"要展开的结果太多");
  for (; i < e; i++) {                                               
    gvts_geti(L, 1, i);
  }
  gvts_geti(L, 1, e);                         
  return (int)n;
}

typedef unsigned int IdxT;

#if !defined(g_randomizePivot)		       

#include <time.h>

#define sof(e)		(sizeof(e) / sizeof(unsigned int))

static unsigned int g_randomizePivot (void) {
  clock_t c = clock();
  time_t t = time(NULL);
  unsigned int buff[sof(c) + sof(t)];
  unsigned int i, rnd = 0;
  memcpy(buff, &c, sof(c) * sizeof(unsigned int));
  memcpy(buff + sof(c), &t, sof(t) * sizeof(unsigned int));
  for (i = 0; i < sof(buff); i++)
    rnd += buff[i];
  return rnd;
}

#endif					       

#define RANLIMIT	100u

static void set2 (gvts_State *L, IdxT i, IdxT j) {
  gvts_seti(L, 1, i);
  gvts_seti(L, 1, j);
}

static int sort_comp (gvts_State *L, int a, int b) {
  if (gvts_isnil(L, 2))                    
    return gvts_compare(L, a, b, GVTS_OPLT);             
  else {                
    int res;
    gvts_pushvalue(L, 2);                       
    gvts_pushvalue(L, a-1);                                 
    gvts_pushvalue(L, b-2);                                         
    gvts_call(L, 2, 1);                         
    res = gvts_toboolean(L, -1);                  
    gvts_pop(L, 1);                          
    return res;
  }
}

static IdxT partition (gvts_State *L, IdxT lo, IdxT up) {
  IdxT i = lo;                                            
  IdxT j = up - 1;                                            

  for (;;) {

    while ((void)gvts_geti(L, 1, ++i), sort_comp(L, -1, -2)) {
      if (g_unlikely(i == up - 1))                                        
        gvtsG_error(L, u8"排序的比较函数顺序无效");
      gvts_pop(L, 1);                   
    }

    while ((void)gvts_geti(L, 1, --j), sort_comp(L, -3, -1)) {
      if (g_unlikely(j < i))                               
        gvtsG_error(L, u8"排序的比较函数顺序无效");
      gvts_pop(L, 1);                   
    }

    if (j < i) {                                 

      gvts_pop(L, 1);                

      set2(L, up - 1, i);
      return i;
    }

    set2(L, i, j);
  }
}

static IdxT choosePivot (IdxT lo, IdxT up, unsigned int rnd) {
  IdxT r4 = (up - lo) / 4;               
  IdxT p = rnd % (r4 * 2) + (lo + r4);
  gvts_assert(lo + r4 <= p && p <= up - r4);
  return p;
}

static void auxsort (gvts_State *L, IdxT lo, IdxT up,
                                   unsigned int rnd) {
  while (lo < up) {                               
    IdxT p;                   
    IdxT n;                        

    gvts_geti(L, 1, lo);
    gvts_geti(L, 1, up);
    if (sort_comp(L, -1, -2))                      
      set2(L, lo, up);                          
    else
      gvts_pop(L, 2);                          
    if (up - lo == 1)                        
      return;                      
    if (up - lo < RANLIMIT || rnd == 0)                                       
      p = (lo + up)/2;                                      
    else                                                        
      p = choosePivot(lo, up, rnd);
    gvts_geti(L, 1, p);
    gvts_geti(L, 1, lo);
    if (sort_comp(L, -2, -1))                     
      set2(L, p, lo);                         
    else {
      gvts_pop(L, 1);                    
      gvts_geti(L, 1, up);
      if (sort_comp(L, -1, -2))                     
        set2(L, p, up);                         
      else
        gvts_pop(L, 2);
    }
    if (up - lo == 2)                        
      return;                      
    gvts_geti(L, 1, p);                                  
    gvts_pushvalue(L, -1);                  
    gvts_geti(L, 1, up - 1);                      
    set2(L, p, up - 1);                                        
    p = partition(L, lo, up);

    if (p - lo < up - p) {                                  
      auxsort(L, lo, p - 1, rnd);                                           
      n = p - lo;                                
      lo = p + 1;                                                    
    }
    else {
      auxsort(L, p + 1, up, rnd);                                           
      n = up - p;                                
      up = p - 1;                                                     
    }
    if ((up - lo) / 128 > n)                                
      rnd = g_randomizePivot();                               
  }                                         
}

static int sort (gvts_State *L) {
  gvts_Integer n = aux_getn(L, 1, TAB_RW);
  if (n > 1) {                             
    gvtsG_argcheck(L, n < INT_MAX, 1, u8"数组太大");
    if (!gvts_isnoneornil(L, 2))                                
      gvtsG_checktype(L, 2, GVTS_TFUNCTION);                          
    gvts_settop(L, 2);                                         
    auxsort(L, 1, (IdxT)n, 0);
  }
  return 0;
}

static const gvtsG_Reg tab_funcs[] = {
  {"concat", tconcat},
  {"insert", tinsert},
  {"pack", tpack},
  {"unpack", tunpack},
  {"remove", tremove},
  {"move", tmove},
  {"sort", sort},
  {NULL, NULL}
};

GVTSMOD_API int gvtsopen_table (gvts_State *L) {
  gvtsG_newlib(L, tab_funcs);
  return 1;
}
