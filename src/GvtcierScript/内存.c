#define lmem_c
#define GVTS_CORE

#include "Gprefix.h"

#include <stddef.h>

#include "gvts.h"

#include "Gdebug.h"
#include "Gdo.h"
#include "Ggc.h"
#include "Gmem.h"
#include "Gobject.h"
#include "Gstate.h"

#define callfrealloc(g,block,os,ns)    ((*g->frealloc)(g->ud, block, os, ns))

#define cantryagain(g)	(completestate(g) && !g->gcstopem)

#if defined(EMERGENCYGCTESTS)

static void *firsttry (global_State *g, void *block, size_t os, size_t ns) {
  if (ns > 0 && cantryagain(g))
    return NULL;            
  else                         
    return callfrealloc(g, block, os, ns);
}
#else
#define firsttry(g,block,os,ns)    callfrealloc(g, block, os, ns)
#endif

#define MINSIZEARRAY	4

void *gvtsM_growaux_ (gvts_State *L, void *block, int nelems, int *psize,
                     int size_elems, int limit, const char *what) {
  void *newblock;
  int size = *psize;
  if (nelems + 1 <= size)                                         
    return block;                          
  if (size >= limit / 2) {                         
    if (g_unlikely(size >= limit))                                  
      gvtsG_runerror(L, u8"%s过多（上限 %d）", what, limit);
    size = limit;                                          
  }
  else {
    size *= 2;
    if (size < MINSIZEARRAY)
      size = MINSIZEARRAY;                    
  }
  gvts_assert(nelems + 1 <= size && size <= limit);

  newblock = gvtsM_saferealloc_(L, block, cast_sizet(*psize) * size_elems,
                                         cast_sizet(size) * size_elems);
  *psize = size;                                              
  return newblock;
}

void *gvtsM_shrinkvector_ (gvts_State *L, void *block, int *size,
                          int final_n, int size_elem) {
  void *newblock;
  size_t oldsize = cast_sizet((*size) * size_elem);
  size_t newsize = cast_sizet(final_n * size_elem);
  gvts_assert(newsize <= oldsize);
  newblock = gvtsM_saferealloc_(L, block, oldsize, newsize);
  *size = final_n;
  return newblock;
}

g_noret gvtsM_toobig (gvts_State *L) {
  gvtsG_runerror(L, u8"内存分配失败：数据块过大");
}

void gvtsM_free_ (gvts_State *L, void *block, size_t osize) {
  global_State *g = G(L);
  gvts_assert((osize == 0) == (block == NULL));
  callfrealloc(g, block, osize, 0);
  g->GCdebt -= osize;
}

static void *tryagain (gvts_State *L, void *block,
                       size_t osize, size_t nsize) {
  global_State *g = G(L);
  if (cantryagain(g)) {
    gvtsC_fullgc(L, 1);                                  
    return callfrealloc(g, block, osize, nsize);                 
  }
  else return NULL;                                          
}

void *gvtsM_realloc_ (gvts_State *L, void *block, size_t osize, size_t nsize) {
  void *newblock;
  global_State *g = G(L);
  gvts_assert((osize == 0) == (block == NULL));
  newblock = firsttry(g, block, osize, nsize);
  if (g_unlikely(newblock == NULL && nsize > 0)) {
    newblock = tryagain(L, block, osize, nsize);
    if (newblock == NULL)                        
      return NULL;                              
  }
  gvts_assert((nsize == 0) == (newblock == NULL));
  g->GCdebt = (g->GCdebt + nsize) - osize;
  return newblock;
}

void *gvtsM_saferealloc_ (gvts_State *L, void *block, size_t osize,
                                                    size_t nsize) {
  void *newblock = gvtsM_realloc_(L, block, osize, nsize);
  if (g_unlikely(newblock == NULL && nsize > 0))                          
    gvtsM_error(L);
  return newblock;
}

void *gvtsM_malloc_ (gvts_State *L, size_t size, int tag) {
  if (size == 0)
    return NULL;                  
  else {
    global_State *g = G(L);
    void *newblock = firsttry(g, NULL, tag, size);
    if (g_unlikely(newblock == NULL)) {
      newblock = tryagain(L, NULL, tag, size);
      if (newblock == NULL)
        gvtsM_error(L);
    }
    g->GCdebt += size;
    return newblock;
  }
}
