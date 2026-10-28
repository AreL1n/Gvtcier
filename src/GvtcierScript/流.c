#define lzio_c
#define GVTS_CORE

#include "Gprefix.h"

#include <string.h>

#include "gvts.h"

#include "Glimits.h"
#include "Gmem.h"
#include "Gstate.h"
#include "Gzio.h"

int gvtsZ_fill (ZIO *z) {
  size_t size;
  gvts_State *L = z->L;
  const char *buff;
  gvts_unlock(L);
  buff = z->reader(L, z->data, &size);
  gvts_lock(L);
  if (buff == NULL || size == 0)
    return EOZ;
  z->n = size - 1;                                    
  z->p = buff;
  return cast_uchar(*(z->p++));
}

void gvtsZ_init (gvts_State *L, ZIO *z, gvts_Reader reader, void *data) {
  z->L = L;
  z->reader = reader;
  z->data = data;
  z->n = 0;
  z->p = NULL;
}

size_t gvtsZ_read (ZIO *z, void *b, size_t n) {
  while (n) {
    size_t m;
    if (z->n == 0) {                           
      if (gvtsZ_fill(z) == EOZ)                        
        return n;                                                     
      else {
        z->n++;                                                  
        z->p--;
      }
    }
    m = (n <= z->n) ? n : z->n;                               
    memcpy(b, z->p, m);
    z->n -= m;
    z->p += m;
    b = (char *)b + m;
    n -= m;
  }
  return 0;
}
