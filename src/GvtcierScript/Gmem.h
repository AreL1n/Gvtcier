#ifndef lmem_h
#define lmem_h

#include <stddef.h>

#include "Glimits.h"
#include "gvts.h"

#define gvtsM_error(L)	gvtsD_throw(L, GVTS_ERRMEM)

#define gvtsM_testsize(n,e)  \
	(sizeof(n) >= sizeof(size_t) && cast_sizet((n)) + 1 > MAX_SIZET/(e))

#define gvtsM_checksize(L,n,e)  \
	(gvtsM_testsize(n,e) ? gvtsM_toobig(L) : cast_void(0))

#define gvtsM_limitN(n,t)  \
  ((cast_sizet(n) <= MAX_SIZET/sizeof(t)) ? (n) :  \
     cast_uint((MAX_SIZET/sizeof(t))))

#define gvtsM_reallocvchar(L,b,on,n)  \
  cast_charp(gvtsM_saferealloc_(L, (b), (on)*sizeof(char), (n)*sizeof(char)))

#define gvtsM_freemem(L, b, s)	gvtsM_free_(L, (b), (s))
#define gvtsM_free(L, b)		gvtsM_free_(L, (b), sizeof(*(b)))
#define gvtsM_freearray(L, b, n)   gvtsM_free_(L, (b), (n)*sizeof(*(b)))

#define gvtsM_new(L,t)		cast(t*, gvtsM_malloc_(L, sizeof(t), 0))
#define gvtsM_newvector(L,n,t)	cast(t*, gvtsM_malloc_(L, (n)*sizeof(t), 0))
#define gvtsM_newvectorchecked(L,n,t) \
  (gvtsM_checksize(L,n,sizeof(t)), gvtsM_newvector(L,n,t))

#define gvtsM_newobject(L,tag,s)	gvtsM_malloc_(L, (s), tag)

#define gvtsM_growvector(L,v,nelems,size,t,limit,e) \
	((v)=cast(t *, gvtsM_growaux_(L,v,nelems,&(size),sizeof(t), \
                         gvtsM_limitN(limit,t),e)))

#define gvtsM_reallocvector(L, v,oldn,n,t) \
   (cast(t *, gvtsM_realloc_(L, v, cast_sizet(oldn) * sizeof(t), \
                                  cast_sizet(n) * sizeof(t))))

#define gvtsM_shrinkvector(L,v,size,fs,t) \
   ((v)=cast(t *, gvtsM_shrinkvector_(L, v, &(size), fs, sizeof(t))))

GVTSI_FUNC g_noret gvtsM_toobig (gvts_State *L);

GVTSI_FUNC void *gvtsM_realloc_ (gvts_State *L, void *block, size_t oldsize,
                                                          size_t size);
GVTSI_FUNC void *gvtsM_saferealloc_ (gvts_State *L, void *block, size_t oldsize,
                                                              size_t size);
GVTSI_FUNC void gvtsM_free_ (gvts_State *L, void *block, size_t osize);
GVTSI_FUNC void *gvtsM_growaux_ (gvts_State *L, void *block, int nelems,
                               int *size, int size_elem, int limit,
                               const char *what);
GVTSI_FUNC void *gvtsM_shrinkvector_ (gvts_State *L, void *block, int *nelem,
                                    int final_n, int size_elem);
GVTSI_FUNC void *gvtsM_malloc_ (gvts_State *L, size_t size, int tag);

#endif
