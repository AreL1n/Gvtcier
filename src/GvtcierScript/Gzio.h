#ifndef lzio_h
#define lzio_h

#include "gvts.h"

#include "Gmem.h"

#define EOZ	(-1)			                   

typedef struct Zio ZIO;

#define zgetc(z)  (((z)->n--)>0 ?  cast_uchar(*(z)->p++) : gvtsZ_fill(z))

typedef struct Mbuffer {
  char *buffer;
  size_t n;
  size_t buffsize;
} Mbuffer;

#define gvtsZ_initbuffer(L, buff) ((buff)->buffer = NULL, (buff)->buffsize = 0)

#define gvtsZ_buffer(buff)	((buff)->buffer)
#define gvtsZ_sizebuffer(buff)	((buff)->buffsize)
#define gvtsZ_bufflen(buff)	((buff)->n)

#define gvtsZ_buffremove(buff,i)	((buff)->n -= (i))
#define gvtsZ_resetbuffer(buff) ((buff)->n = 0)

#define gvtsZ_resizebuffer(L, buff, size) \
	((buff)->buffer = gvtsM_reallocvchar(L, (buff)->buffer, \
				(buff)->buffsize, size), \
	(buff)->buffsize = size)

#define gvtsZ_freebuffer(L, buff)	gvtsZ_resizebuffer(L, buff, 0)

GVTSI_FUNC void gvtsZ_init (gvts_State *L, ZIO *z, gvts_Reader reader,
                                        void *data);
GVTSI_FUNC size_t gvtsZ_read (ZIO* z, void *b, size_t n);	                       

struct Zio {
  size_t n;			                        
  const char *p;		                                
  gvts_Reader reader;		                     
  void *data;			                     
  gvts_State *L;			                            
};

GVTSI_FUNC int gvtsZ_fill (ZIO *z);

#endif
