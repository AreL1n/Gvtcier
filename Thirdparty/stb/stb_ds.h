#ifdef STBDS_UNIT_TESTS
#define _CRT_SECURE_NO_WARNINGS
#endif

#ifndef INCLUDE_STB_DS_H
#define INCLUDE_STB_DS_H

#include <stddef.h>
#include <string.h>

#ifndef STBDS_NO_SHORT_NAMES
#define arrlen      stbds_arrlen
#define arrlenu     stbds_arrlenu
#define arrput      stbds_arrput
#define arrpush     stbds_arrput
#define arrpop      stbds_arrpop
#define arrfree     stbds_arrfree
#define arraddn     stbds_arraddn 
#define arraddnptr  stbds_arraddnptr
#define arraddnindex stbds_arraddnindex
#define arrsetlen   stbds_arrsetlen
#define arrlast     stbds_arrlast
#define arrins      stbds_arrins
#define arrinsn     stbds_arrinsn
#define arrdel      stbds_arrdel
#define arrdeln     stbds_arrdeln
#define arrdelswap  stbds_arrdelswap
#define arrcap      stbds_arrcap
#define arrsetcap   stbds_arrsetcap

#define hmput       stbds_hmput
#define hmputs      stbds_hmputs
#define hmget       stbds_hmget
#define hmget_ts    stbds_hmget_ts
#define hmgets      stbds_hmgets
#define hmgetp      stbds_hmgetp
#define hmgetp_ts   stbds_hmgetp_ts
#define hmgetp_null stbds_hmgetp_null
#define hmgeti      stbds_hmgeti
#define hmgeti_ts   stbds_hmgeti_ts
#define hmdel       stbds_hmdel
#define hmlen       stbds_hmlen
#define hmlenu      stbds_hmlenu
#define hmfree      stbds_hmfree
#define hmdefault   stbds_hmdefault
#define hmdefaults  stbds_hmdefaults

#define shput       stbds_shput
#define shputi      stbds_shputi
#define shputs      stbds_shputs
#define shget       stbds_shget
#define shgeti      stbds_shgeti
#define shgets      stbds_shgets
#define shgetp      stbds_shgetp
#define shgetp_null stbds_shgetp_null
#define shdel       stbds_shdel
#define shlen       stbds_shlen
#define shlenu      stbds_shlenu
#define shfree      stbds_shfree
#define shdefault   stbds_shdefault
#define shdefaults  stbds_shdefaults
#define sh_new_arena  stbds_sh_new_arena
#define sh_new_strdup stbds_sh_new_strdup

#define stralloc    stbds_stralloc
#define strreset    stbds_strreset
#endif

#if defined(STBDS_REALLOC) && !defined(STBDS_FREE) || !defined(STBDS_REALLOC) && defined(STBDS_FREE)
#error "You must define both STBDS_REALLOC and STBDS_FREE, or neither."
#endif
#if !defined(STBDS_REALLOC) && !defined(STBDS_FREE)
#include <stdlib.h>
#define STBDS_REALLOC(c,p,s) realloc(p,s)
#define STBDS_FREE(c,p)      free(p)
#endif

#ifdef _MSC_VER
#define STBDS_NOTUSED(v)  (void)(v)
#else
#define STBDS_NOTUSED(v)  (void)sizeof(v)
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern void stbds_rand_seed(size_t seed);

extern size_t stbds_hash_bytes(void *p, size_t len, size_t seed);
extern size_t stbds_hash_string(char *str, size_t seed);

typedef struct stbds_string_arena stbds_string_arena;
extern char * stbds_stralloc(stbds_string_arena *a, char *str);
extern void   stbds_strreset(stbds_string_arena *a);

extern void stbds_unit_tests(void);

extern void * stbds_arrgrowf(void *a, size_t elemsize, size_t addlen, size_t min_cap);
extern void   stbds_arrfreef(void *a);
extern void   stbds_hmfree_func(void *p, size_t elemsize);
extern void * stbds_hmget_key(void *a, size_t elemsize, void *key, size_t keysize, int mode);
extern void * stbds_hmget_key_ts(void *a, size_t elemsize, void *key, size_t keysize, ptrdiff_t *temp, int mode);
extern void * stbds_hmput_default(void *a, size_t elemsize);
extern void * stbds_hmput_key(void *a, size_t elemsize, void *key, size_t keysize, int mode);
extern void * stbds_hmdel_key(void *a, size_t elemsize, void *key, size_t keysize, size_t keyoffset, int mode);
extern void * stbds_shmode_func(size_t elemsize, int mode);

#ifdef __cplusplus
}
#endif

#if defined(__GNUC__) || defined(__clang__)
#define STBDS_HAS_TYPEOF
#ifdef __cplusplus

#endif
#endif

#if !defined(__cplusplus)
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define STBDS_HAS_LITERAL_ARRAY
#endif
#endif

#if defined(STBDS_HAS_LITERAL_ARRAY) && defined(STBDS_HAS_TYPEOF)
  #if __clang__
  #define STBDS_ADDRESSOF(typevar, value)     ((__typeof__(typevar)[1]){value}) 
  #else
  #define STBDS_ADDRESSOF(typevar, value)     ((typeof(typevar)[1]){value}) 
  #endif
#else
#define STBDS_ADDRESSOF(typevar, value)     &(value)
#endif

#define STBDS_OFFSETOF(var,field)           ((char *) &(var)->field - (char *) (var))

#define stbds_header(t)  ((stbds_array_header *) (t) - 1)
#define stbds_temp(t)    stbds_header(t)->temp
#define stbds_temp_key(t) (*(char **) stbds_header(t)->hash_table)

#define stbds_arrsetcap(a,n)   (stbds_arrgrow(a,0,n))
#define stbds_arrsetlen(a,n)   ((stbds_arrcap(a) < (size_t) (n) ? stbds_arrsetcap((a),(size_t)(n)),0 : 0), (a) ? stbds_header(a)->length = (size_t) (n) : 0)
#define stbds_arrcap(a)        ((a) ? stbds_header(a)->capacity : 0)
#define stbds_arrlen(a)        ((a) ? (ptrdiff_t) stbds_header(a)->length : 0)
#define stbds_arrlenu(a)       ((a) ?             stbds_header(a)->length : 0)
#define stbds_arrput(a,v)      (stbds_arrmaybegrow(a,1), (a)[stbds_header(a)->length++] = (v))
#define stbds_arrpush          stbds_arrput  
#define stbds_arrpop(a)        (stbds_header(a)->length--, (a)[stbds_header(a)->length])
#define stbds_arraddn(a,n)     ((void)(stbds_arraddnindex(a, n)))    
#define stbds_arraddnptr(a,n)  (stbds_arrmaybegrow(a,n), (n) ? (stbds_header(a)->length += (n), &(a)[stbds_header(a)->length-(n)]) : (a))
#define stbds_arraddnindex(a,n)(stbds_arrmaybegrow(a,n), (n) ? (stbds_header(a)->length += (n), stbds_header(a)->length-(n)) : stbds_arrlen(a))
#define stbds_arraddnoff       stbds_arraddnindex
#define stbds_arrlast(a)       ((a)[stbds_header(a)->length-1])
#define stbds_arrfree(a)       ((void) ((a) ? STBDS_FREE(NULL,stbds_header(a)) : (void)0), (a)=NULL)
#define stbds_arrdel(a,i)      stbds_arrdeln(a,i,1)
#define stbds_arrdeln(a,i,n)   (memmove(&(a)[i], &(a)[(i)+(n)], sizeof *(a) * (stbds_header(a)->length-(n)-(i))), stbds_header(a)->length -= (n))
#define stbds_arrdelswap(a,i)  ((a)[i] = stbds_arrlast(a), stbds_header(a)->length -= 1)
#define stbds_arrinsn(a,i,n)   (stbds_arraddn((a),(n)), memmove(&(a)[(i)+(n)], &(a)[i], sizeof *(a) * (stbds_header(a)->length-(n)-(i))))
#define stbds_arrins(a,i,v)    (stbds_arrinsn((a),(i),1), (a)[i]=(v))

#define stbds_arrmaybegrow(a,n)  ((!(a) || stbds_header(a)->length + (n) > stbds_header(a)->capacity) \
                                  ? (stbds_arrgrow(a,n,0),0) : 0)

#define stbds_arrgrow(a,b,c)   ((a) = stbds_arrgrowf_wrapper((a), sizeof *(a), (b), (c)))

#define stbds_hmput(t, k, v) \
    ((t) = stbds_hmput_key_wrapper((t), sizeof *(t), (void*) STBDS_ADDRESSOF((t)->key, (k)), sizeof (t)->key, 0),   \
     (t)[stbds_temp((t)-1)].key = (k),    \
     (t)[stbds_temp((t)-1)].value = (v))

#define stbds_hmputs(t, s) \
    ((t) = stbds_hmput_key_wrapper((t), sizeof *(t), &(s).key, sizeof (s).key, STBDS_HM_BINARY), \
     (t)[stbds_temp((t)-1)] = (s))

#define stbds_hmgeti(t,k) \
    ((t) = stbds_hmget_key_wrapper((t), sizeof *(t), (void*) STBDS_ADDRESSOF((t)->key, (k)), sizeof (t)->key, STBDS_HM_BINARY), \
      stbds_temp((t)-1))

#define stbds_hmgeti_ts(t,k,temp) \
    ((t) = stbds_hmget_key_ts_wrapper((t), sizeof *(t), (void*) STBDS_ADDRESSOF((t)->key, (k)), sizeof (t)->key, &(temp), STBDS_HM_BINARY), \
      (temp))

#define stbds_hmgetp(t, k) \
    ((void) stbds_hmgeti(t,k), &(t)[stbds_temp((t)-1)])

#define stbds_hmgetp_ts(t, k, temp) \
    ((void) stbds_hmgeti_ts(t,k,temp), &(t)[temp])

#define stbds_hmdel(t,k) \
    (((t) = stbds_hmdel_key_wrapper((t),sizeof *(t), (void*) STBDS_ADDRESSOF((t)->key, (k)), sizeof (t)->key, STBDS_OFFSETOF((t),key), STBDS_HM_BINARY)),(t)?stbds_temp((t)-1):0)

#define stbds_hmdefault(t, v) \
    ((t) = stbds_hmput_default_wrapper((t), sizeof *(t)), (t)[-1].value = (v))

#define stbds_hmdefaults(t, s) \
    ((t) = stbds_hmput_default_wrapper((t), sizeof *(t)), (t)[-1] = (s))

#define stbds_hmfree(p)        \
    ((void) ((p) != NULL ? stbds_hmfree_func((p)-1,sizeof*(p)),0 : 0),(p)=NULL)

#define stbds_hmgets(t, k)    (*stbds_hmgetp(t,k))
#define stbds_hmget(t, k)     (stbds_hmgetp(t,k)->value)
#define stbds_hmget_ts(t, k, temp)  (stbds_hmgetp_ts(t,k,temp)->value)
#define stbds_hmlen(t)        ((t) ? (ptrdiff_t) stbds_header((t)-1)->length-1 : 0)
#define stbds_hmlenu(t)       ((t) ?             stbds_header((t)-1)->length-1 : 0)
#define stbds_hmgetp_null(t,k)  (stbds_hmgeti(t,k) == -1 ? NULL : &(t)[stbds_temp((t)-1)])

#define stbds_shput(t, k, v) \
    ((t) = stbds_hmput_key_wrapper((t), sizeof *(t), (void*) (k), sizeof (t)->key, STBDS_HM_STRING),   \
     (t)[stbds_temp((t)-1)].value = (v))

#define stbds_shputi(t, k, v) \
    ((t) = stbds_hmput_key_wrapper((t), sizeof *(t), (void*) (k), sizeof (t)->key, STBDS_HM_STRING),   \
     (t)[stbds_temp((t)-1)].value = (v), stbds_temp((t)-1))

#define stbds_shputs(t, s) \
    ((t) = stbds_hmput_key_wrapper((t), sizeof *(t), (void*) (s).key, sizeof (s).key, STBDS_HM_STRING), \
     (t)[stbds_temp((t)-1)] = (s), \
     (t)[stbds_temp((t)-1)].key = stbds_temp_key((t)-1)) 

#define stbds_pshput(t, p) \
    ((t) = stbds_hmput_key_wrapper((t), sizeof *(t), (void*) (p)->key, sizeof (p)->key, STBDS_HM_PTR_TO_STRING), \
     (t)[stbds_temp((t)-1)] = (p))

#define stbds_shgeti(t,k) \
     ((t) = stbds_hmget_key_wrapper((t), sizeof *(t), (void*) (k), sizeof (t)->key, STBDS_HM_STRING), \
      stbds_temp((t)-1))

#define stbds_pshgeti(t,k) \
     ((t) = stbds_hmget_key_wrapper((t), sizeof *(t), (void*) (k), sizeof (*(t))->key, STBDS_HM_PTR_TO_STRING), \
      stbds_temp((t)-1))

#define stbds_shgetp(t, k) \
    ((void) stbds_shgeti(t,k), &(t)[stbds_temp((t)-1)])

#define stbds_pshget(t, k) \
    ((void) stbds_pshgeti(t,k), (t)[stbds_temp((t)-1)])

#define stbds_shdel(t,k) \
    (((t) = stbds_hmdel_key_wrapper((t),sizeof *(t), (void*) (k), sizeof (t)->key, STBDS_OFFSETOF((t),key), STBDS_HM_STRING)),(t)?stbds_temp((t)-1):0)
#define stbds_pshdel(t,k) \
    (((t) = stbds_hmdel_key_wrapper((t),sizeof *(t), (void*) (k), sizeof (*(t))->key, STBDS_OFFSETOF(*(t),key), STBDS_HM_PTR_TO_STRING)),(t)?stbds_temp((t)-1):0)

#define stbds_sh_new_arena(t)  \
    ((t) = stbds_shmode_func_wrapper(t, sizeof *(t), STBDS_SH_ARENA))
#define stbds_sh_new_strdup(t) \
    ((t) = stbds_shmode_func_wrapper(t, sizeof *(t), STBDS_SH_STRDUP))

#define stbds_shdefault(t, v)  stbds_hmdefault(t,v)
#define stbds_shdefaults(t, s) stbds_hmdefaults(t,s)

#define stbds_shfree       stbds_hmfree
#define stbds_shlenu       stbds_hmlenu

#define stbds_shgets(t, k) (*stbds_shgetp(t,k))
#define stbds_shget(t, k)  (stbds_shgetp(t,k)->value)
#define stbds_shgetp_null(t,k)  (stbds_shgeti(t,k) == -1 ? NULL : &(t)[stbds_temp((t)-1)])
#define stbds_shlen        stbds_hmlen

typedef struct
{
  size_t      length;
  size_t      capacity;
  void      * hash_table;
  ptrdiff_t   temp;
} stbds_array_header;

typedef struct stbds_string_block
{
  struct stbds_string_block *next;
  char storage[8];
} stbds_string_block;

struct stbds_string_arena
{
  stbds_string_block *storage;
  size_t remaining;
  unsigned char block;
  unsigned char mode;  
};

#define STBDS_HM_BINARY         0
#define STBDS_HM_STRING         1

enum
{
   STBDS_SH_NONE,
   STBDS_SH_DEFAULT,
   STBDS_SH_STRDUP,
   STBDS_SH_ARENA
};

#ifdef __cplusplus

template<class T> static T * stbds_arrgrowf_wrapper(T *a, size_t elemsize, size_t addlen, size_t min_cap) {
  return (T*)stbds_arrgrowf((void *)a, elemsize, addlen, min_cap);
}
template<class T> static T * stbds_hmget_key_wrapper(T *a, size_t elemsize, void *key, size_t keysize, int mode) {
  return (T*)stbds_hmget_key((void*)a, elemsize, key, keysize, mode);
}
template<class T> static T * stbds_hmget_key_ts_wrapper(T *a, size_t elemsize, void *key, size_t keysize, ptrdiff_t *temp, int mode) {
  return (T*)stbds_hmget_key_ts((void*)a, elemsize, key, keysize, temp, mode);
}
template<class T> static T * stbds_hmput_default_wrapper(T *a, size_t elemsize) {
  return (T*)stbds_hmput_default((void *)a, elemsize);
}
template<class T> static T * stbds_hmput_key_wrapper(T *a, size_t elemsize, void *key, size_t keysize, int mode) {
  return (T*)stbds_hmput_key((void*)a, elemsize, key, keysize, mode);
}
template<class T> static T * stbds_hmdel_key_wrapper(T *a, size_t elemsize, void *key, size_t keysize, size_t keyoffset, int mode){
  return (T*)stbds_hmdel_key((void*)a, elemsize, key, keysize, keyoffset, mode);
}
template<class T> static T * stbds_shmode_func_wrapper(T *, size_t elemsize, int mode) {
  return (T*)stbds_shmode_func(elemsize, mode);
}
#else
#define stbds_arrgrowf_wrapper            stbds_arrgrowf
#define stbds_hmget_key_wrapper           stbds_hmget_key
#define stbds_hmget_key_ts_wrapper        stbds_hmget_key_ts
#define stbds_hmput_default_wrapper       stbds_hmput_default
#define stbds_hmput_key_wrapper           stbds_hmput_key
#define stbds_hmdel_key_wrapper           stbds_hmdel_key
#define stbds_shmode_func_wrapper(t,e,m)  stbds_shmode_func(e,m)
#endif

#endif 

#ifdef STB_DS_IMPLEMENTATION
#include <assert.h>
#include <string.h>

#ifndef STBDS_ASSERT
#define STBDS_ASSERT_WAS_UNDEFINED
#define STBDS_ASSERT(x)   ((void) 0)
#endif

#ifdef STBDS_STATISTICS
#define STBDS_STATS(x)   x
size_t stbds_array_grow;
size_t stbds_hash_grow;
size_t stbds_hash_shrink;
size_t stbds_hash_rebuild;
size_t stbds_hash_probes;
size_t stbds_hash_alloc;
size_t stbds_rehash_probes;
size_t stbds_rehash_items;
#else
#define STBDS_STATS(x)
#endif

void *stbds_arrgrowf(void *a, size_t elemsize, size_t addlen, size_t min_cap)
{
  stbds_array_header temp={0}; 
  void *b;
  size_t min_len = stbds_arrlen(a) + addlen;
  (void) sizeof(temp);

  if (min_len > min_cap)
    min_cap = min_len;

  if (min_cap <= stbds_arrcap(a))
    return a;

  if (min_cap < 2 * stbds_arrcap(a))
    min_cap = 2 * stbds_arrcap(a);
  else if (min_cap < 4)
    min_cap = 4;

  b = STBDS_REALLOC(NULL, (a) ? stbds_header(a) : 0, elemsize * min_cap + sizeof(stbds_array_header));

  b = (char *) b + sizeof(stbds_array_header);
  if (a == NULL) {
    stbds_header(b)->length = 0;
    stbds_header(b)->hash_table = 0;
    stbds_header(b)->temp = 0;
  } else {
    STBDS_STATS(++stbds_array_grow);
  }
  stbds_header(b)->capacity = min_cap;

  return b;
}

void stbds_arrfreef(void *a)
{
  STBDS_FREE(NULL, stbds_header(a));
}

#ifdef STBDS_INTERNAL_SMALL_BUCKET
#define STBDS_BUCKET_LENGTH      4
#else
#define STBDS_BUCKET_LENGTH      8
#endif

#define STBDS_BUCKET_SHIFT      (STBDS_BUCKET_LENGTH == 8 ? 3 : 2)
#define STBDS_BUCKET_MASK       (STBDS_BUCKET_LENGTH-1)
#define STBDS_CACHE_LINE_SIZE   64

#define STBDS_ALIGN_FWD(n,a)   (((n) + (a) - 1) & ~((a)-1))

typedef struct
{
   size_t    hash [STBDS_BUCKET_LENGTH];
   ptrdiff_t index[STBDS_BUCKET_LENGTH];
} stbds_hash_bucket; 

typedef struct
{
  char * temp_key; 
  size_t slot_count;
  size_t used_count;
  size_t used_count_threshold;
  size_t used_count_shrink_threshold;
  size_t tombstone_count;
  size_t tombstone_count_threshold;
  size_t seed;
  size_t slot_count_log2;
  stbds_string_arena string;
  stbds_hash_bucket *storage; 
} stbds_hash_index;

#define STBDS_INDEX_EMPTY    -1
#define STBDS_INDEX_DELETED  -2
#define STBDS_INDEX_IN_USE(x)  ((x) >= 0)

#define STBDS_HASH_EMPTY      0
#define STBDS_HASH_DELETED    1

static size_t stbds_hash_seed=0x31415926;

void stbds_rand_seed(size_t seed)
{
  stbds_hash_seed = seed;
}

#define stbds_load_32_or_64(var, temp, v32, v64_hi, v64_lo)                                          \
  temp = v64_lo ^ v32, temp <<= 16, temp <<= 16, temp >>= 16, temp >>= 16,                           \
  var = v64_hi, var <<= 16, var <<= 16,                                                              \
  var ^= temp ^ v32

#define STBDS_SIZE_T_BITS           ((sizeof (size_t)) * 8)

static size_t stbds_probe_position(size_t hash, size_t slot_count, size_t slot_log2)
{
  size_t pos;
  STBDS_NOTUSED(slot_log2);
  pos = hash & (slot_count-1);
  #ifdef STBDS_INTERNAL_BUCKET_START
  pos &= ~STBDS_BUCKET_MASK;
  #endif
  return pos;
}

static size_t stbds_log2(size_t slot_count)
{
  size_t n=0;
  while (slot_count > 1) {
    slot_count >>= 1;
    ++n;
  }
  return n;
}

static stbds_hash_index *stbds_make_hash_index(size_t slot_count, stbds_hash_index *ot)
{
  stbds_hash_index *t;
  t = (stbds_hash_index *) STBDS_REALLOC(NULL,0,(slot_count >> STBDS_BUCKET_SHIFT) * sizeof(stbds_hash_bucket) + sizeof(stbds_hash_index) + STBDS_CACHE_LINE_SIZE-1);
  t->storage = (stbds_hash_bucket *) STBDS_ALIGN_FWD((size_t) (t+1), STBDS_CACHE_LINE_SIZE);
  t->slot_count = slot_count;
  t->slot_count_log2 = stbds_log2(slot_count);
  t->tombstone_count = 0;
  t->used_count = 0;

  #if 0 
  t->used_count_threshold        = slot_count*12/16; 
  t->tombstone_count_threshold   = slot_count* 2/16; 
  t->used_count_shrink_threshold = slot_count* 4/16; 
  #elif 1 

  t->used_count_threshold        = slot_count - (slot_count>>2);
  t->tombstone_count_threshold   = (slot_count>>3) + (slot_count>>4);
  t->used_count_shrink_threshold = slot_count >> 2;

  #elif 0 
  t->used_count_threshold        = slot_count*13/16; 
  t->tombstone_count_threshold   = slot_count* 2/16; 
  t->used_count_shrink_threshold = slot_count* 5/16; 
  #else 
  t->used_count_threshold        = slot_count*14/16; 
  t->tombstone_count_threshold   = slot_count* 2/16; 
  t->used_count_shrink_threshold = slot_count* 6/16; 
  #endif

  if (slot_count <= STBDS_BUCKET_LENGTH)
    t->used_count_shrink_threshold = 0;

  STBDS_ASSERT(t->used_count_threshold + t->tombstone_count_threshold < t->slot_count);
  STBDS_STATS(++stbds_hash_alloc);
  if (ot) {
    t->string = ot->string;

    t->seed = ot->seed;
  } else {
    size_t a,b,temp;
    memset(&t->string, 0, sizeof(t->string));
    t->seed = stbds_hash_seed;

    stbds_load_32_or_64(a,temp, 2147001325, 0x27bb2ee6, 0x87b0b0fd);
    stbds_load_32_or_64(b,temp,  715136305,          0, 0xb504f32d);
    stbds_hash_seed = stbds_hash_seed  * a + b;
  }

  {
    size_t i,j;
    for (i=0; i < slot_count >> STBDS_BUCKET_SHIFT; ++i) {
      stbds_hash_bucket *b = &t->storage[i];
      for (j=0; j < STBDS_BUCKET_LENGTH; ++j)
        b->hash[j] = STBDS_HASH_EMPTY;
      for (j=0; j < STBDS_BUCKET_LENGTH; ++j)
        b->index[j] = STBDS_INDEX_EMPTY;
    }
  }

  if (ot) {
    size_t i,j;
    t->used_count = ot->used_count;
    for (i=0; i < ot->slot_count >> STBDS_BUCKET_SHIFT; ++i) {
      stbds_hash_bucket *ob = &ot->storage[i];
      for (j=0; j < STBDS_BUCKET_LENGTH; ++j) {
        if (STBDS_INDEX_IN_USE(ob->index[j])) {
          size_t hash = ob->hash[j];
          size_t pos = stbds_probe_position(hash, t->slot_count, t->slot_count_log2);
          size_t step = STBDS_BUCKET_LENGTH;
          STBDS_STATS(++stbds_rehash_items);
          for (;;) {
            size_t limit,z;
            stbds_hash_bucket *bucket;
            bucket = &t->storage[pos >> STBDS_BUCKET_SHIFT];
            STBDS_STATS(++stbds_rehash_probes);

            for (z=pos & STBDS_BUCKET_MASK; z < STBDS_BUCKET_LENGTH; ++z) {
              if (bucket->hash[z] == 0) {
                bucket->hash[z] = hash;
                bucket->index[z] = ob->index[j];
                goto done;
              }
            }

            limit = pos & STBDS_BUCKET_MASK;
            for (z = 0; z < limit; ++z) {
              if (bucket->hash[z] == 0) {
                bucket->hash[z] = hash;
                bucket->index[z] = ob->index[j];
                goto done;
              }
            }

            pos += step;                  
            step += STBDS_BUCKET_LENGTH;
            pos &= (t->slot_count-1);
          }
        }
       done:
        ;
      }
    }
  }

  return t;
}

#define STBDS_ROTATE_LEFT(val, n)   (((val) << (n)) | ((val) >> (STBDS_SIZE_T_BITS - (n))))
#define STBDS_ROTATE_RIGHT(val, n)  (((val) >> (n)) | ((val) << (STBDS_SIZE_T_BITS - (n))))

size_t stbds_hash_string(char *str, size_t seed)
{
  size_t hash = seed;
  while (*str)
     hash = STBDS_ROTATE_LEFT(hash, 9) + (unsigned char) *str++;

  hash ^= seed;
  hash = (~hash) + (hash << 18);
  hash ^= hash ^ STBDS_ROTATE_RIGHT(hash,31);
  hash = hash * 21;
  hash ^= hash ^ STBDS_ROTATE_RIGHT(hash,11);
  hash += (hash << 6);
  hash ^= STBDS_ROTATE_RIGHT(hash,22);
  return hash+seed;
}

#ifdef STBDS_SIPHASH_2_4
#define STBDS_SIPHASH_C_ROUNDS 2
#define STBDS_SIPHASH_D_ROUNDS 4
typedef int STBDS_SIPHASH_2_4_can_only_be_used_in_64_bit_builds[sizeof(size_t) == 8 ? 1 : -1];
#endif

#ifndef STBDS_SIPHASH_C_ROUNDS
#define STBDS_SIPHASH_C_ROUNDS 1
#endif
#ifndef STBDS_SIPHASH_D_ROUNDS
#define STBDS_SIPHASH_D_ROUNDS 1
#endif

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4127) 
#endif

static size_t stbds_siphash_bytes(void *p, size_t len, size_t seed)
{
  unsigned char *d = (unsigned char *) p;
  size_t i,j;
  size_t v0,v1,v2,v3, data;

  v0 = ((((size_t) 0x736f6d65 << 16) << 16) + 0x70736575) ^  seed;
  v1 = ((((size_t) 0x646f7261 << 16) << 16) + 0x6e646f6d) ^ ~seed;
  v2 = ((((size_t) 0x6c796765 << 16) << 16) + 0x6e657261) ^  seed;
  v3 = ((((size_t) 0x74656462 << 16) << 16) + 0x79746573) ^ ~seed;

  #ifdef STBDS_TEST_SIPHASH_2_4

  v0 ^= 0x0706050403020100ull ^  seed;
  v1 ^= 0x0f0e0d0c0b0a0908ull ^ ~seed;
  v2 ^= 0x0706050403020100ull ^  seed;
  v3 ^= 0x0f0e0d0c0b0a0908ull ^ ~seed;
  #endif

  #define STBDS_SIPROUND() \
    do {                   \
      v0 += v1; v1 = STBDS_ROTATE_LEFT(v1, 13);  v1 ^= v0; v0 = STBDS_ROTATE_LEFT(v0,STBDS_SIZE_T_BITS/2); \
      v2 += v3; v3 = STBDS_ROTATE_LEFT(v3, 16);  v3 ^= v2;                                                 \
      v2 += v1; v1 = STBDS_ROTATE_LEFT(v1, 17);  v1 ^= v2; v2 = STBDS_ROTATE_LEFT(v2,STBDS_SIZE_T_BITS/2); \
      v0 += v3; v3 = STBDS_ROTATE_LEFT(v3, 21);  v3 ^= v0;                                                 \
    } while (0)

  for (i=0; i+sizeof(size_t) <= len; i += sizeof(size_t), d += sizeof(size_t)) {
    data = d[0] | (d[1] << 8) | (d[2] << 16) | (d[3] << 24);
    data |= (size_t) (d[4] | (d[5] << 8) | (d[6] << 16) | (d[7] << 24)) << 16 << 16; 

    v3 ^= data;
    for (j=0; j < STBDS_SIPHASH_C_ROUNDS; ++j)
      STBDS_SIPROUND();
    v0 ^= data;
  }
  data = len << (STBDS_SIZE_T_BITS-8);
  switch (len - i) {
    case 7: data |= ((size_t) d[6] << 24) << 24; 
    case 6: data |= ((size_t) d[5] << 20) << 20; 
    case 5: data |= ((size_t) d[4] << 16) << 16; 
    case 4: data |= (d[3] << 24); 
    case 3: data |= (d[2] << 16); 
    case 2: data |= (d[1] << 8); 
    case 1: data |= d[0]; 
    case 0: break;
  }
  v3 ^= data;
  for (j=0; j < STBDS_SIPHASH_C_ROUNDS; ++j)
    STBDS_SIPROUND();
  v0 ^= data;
  v2 ^= 0xff;
  for (j=0; j < STBDS_SIPHASH_D_ROUNDS; ++j)
    STBDS_SIPROUND();

#ifdef STBDS_SIPHASH_2_4
  return v0^v1^v2^v3;
#else
  return v1^v2^v3; 
#endif
}

size_t stbds_hash_bytes(void *p, size_t len, size_t seed)
{
#ifdef STBDS_SIPHASH_2_4
  return stbds_siphash_bytes(p,len,seed);
#else
  unsigned char *d = (unsigned char *) p;

  if (len == 4) {
    unsigned int hash = d[0] | (d[1] << 8) | (d[2] << 16) | (d[3] << 24);
    #if 0

    hash ^= seed;
    hash -= (hash<<6);
    hash ^= (hash>>17);
    hash -= (hash<<9);
    hash ^= seed;
    hash ^= (hash<<4);
    hash -= (hash<<3);
    hash ^= (hash<<10);
    hash ^= (hash>>15);
    #elif 1

    hash ^= seed;
    hash = (hash ^ 61) ^ (hash >> 16);
    hash = hash + (hash << 3);
    hash = hash ^ (hash >> 4);
    hash = hash * 0x27d4eb2d;
    hash ^= seed;
    hash = hash ^ (hash >> 15);
    #else  
    hash ^= seed;
    hash *= 0xcc9e2d51;
    hash = (hash << 17) | (hash >> 15);
    hash *= 0x1b873593;
    hash ^= seed;
    hash = (hash << 19) | (hash >> 13);
    hash = hash*5 + 0xe6546b64;
    hash ^= hash >> 16;
    hash *= 0x85ebca6b;
    hash ^= seed;
    hash ^= hash >> 13;
    hash *= 0xc2b2ae35;
    hash ^= hash >> 16;
    #endif

    return (((size_t) hash << 16 << 16) | hash) ^ seed;
  } else if (len == 8 && sizeof(size_t) == 8) {
    size_t hash = d[0] | (d[1] << 8) | (d[2] << 16) | (d[3] << 24);
    hash |= (size_t) (d[4] | (d[5] << 8) | (d[6] << 16) | (d[7] << 24)) << 16 << 16; 
    hash ^= seed;
    hash = (~hash) + (hash << 21);
    hash ^= STBDS_ROTATE_RIGHT(hash,24);
    hash *= 265;
    hash ^= STBDS_ROTATE_RIGHT(hash,14);
    hash ^= seed;
    hash *= 21;
    hash ^= STBDS_ROTATE_RIGHT(hash,28);
    hash += (hash << 31);
    hash = (~hash) + (hash << 18);
    return hash;
  } else {
    return stbds_siphash_bytes(p,len,seed);
  }
#endif
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

static int stbds_is_key_equal(void *a, size_t elemsize, void *key, size_t keysize, size_t keyoffset, int mode, size_t i)
{
  if (mode >= STBDS_HM_STRING)
    return 0==strcmp((char *) key, * (char **) ((char *) a + elemsize*i + keyoffset));
  else
    return 0==memcmp(key, (char *) a + elemsize*i + keyoffset, keysize);
}

#define STBDS_HASH_TO_ARR(x,elemsize) ((char*) (x) - (elemsize))
#define STBDS_ARR_TO_HASH(x,elemsize) ((char*) (x) + (elemsize))

#define stbds_hash_table(a)  ((stbds_hash_index *) stbds_header(a)->hash_table)

void stbds_hmfree_func(void *a, size_t elemsize)
{
  if (a == NULL) return;
  if (stbds_hash_table(a) != NULL) {
    if (stbds_hash_table(a)->string.mode == STBDS_SH_STRDUP) {
      size_t i;

      for (i=1; i < stbds_header(a)->length; ++i)
        STBDS_FREE(NULL, *(char**) ((char *) a + elemsize*i));
    }
    stbds_strreset(&stbds_hash_table(a)->string);
  }
  STBDS_FREE(NULL, stbds_header(a)->hash_table);
  STBDS_FREE(NULL, stbds_header(a));
}

static ptrdiff_t stbds_hm_find_slot(void *a, size_t elemsize, void *key, size_t keysize, size_t keyoffset, int mode)
{
  void *raw_a = STBDS_HASH_TO_ARR(a,elemsize);
  stbds_hash_index *table = stbds_hash_table(raw_a);
  size_t hash = mode >= STBDS_HM_STRING ? stbds_hash_string((char*)key,table->seed) : stbds_hash_bytes(key, keysize,table->seed);
  size_t step = STBDS_BUCKET_LENGTH;
  size_t limit,i;
  size_t pos;
  stbds_hash_bucket *bucket;

  if (hash < 2) hash += 2; 

  pos = stbds_probe_position(hash, table->slot_count, table->slot_count_log2);

  for (;;) {
    STBDS_STATS(++stbds_hash_probes);
    bucket = &table->storage[pos >> STBDS_BUCKET_SHIFT];

    for (i=pos & STBDS_BUCKET_MASK; i < STBDS_BUCKET_LENGTH; ++i) {
      if (bucket->hash[i] == hash) {
        if (stbds_is_key_equal(a, elemsize, key, keysize, keyoffset, mode, bucket->index[i])) {
          return (pos & ~STBDS_BUCKET_MASK)+i;
        }
      } else if (bucket->hash[i] == STBDS_HASH_EMPTY) {
        return -1;
      }
    }

    limit = pos & STBDS_BUCKET_MASK;
    for (i = 0; i < limit; ++i) {
      if (bucket->hash[i] == hash) {
        if (stbds_is_key_equal(a, elemsize, key, keysize, keyoffset, mode, bucket->index[i])) {
          return (pos & ~STBDS_BUCKET_MASK)+i;
        }
      } else if (bucket->hash[i] == STBDS_HASH_EMPTY) {
        return -1;
      }
    }

    pos += step;
    step += STBDS_BUCKET_LENGTH;
    pos &= (table->slot_count-1);
  }

}

void * stbds_hmget_key_ts(void *a, size_t elemsize, void *key, size_t keysize, ptrdiff_t *temp, int mode)
{
  size_t keyoffset = 0;
  if (a == NULL) {

    a = stbds_arrgrowf(0, elemsize, 0, 1);
    stbds_header(a)->length += 1;
    memset(a, 0, elemsize);
    *temp = STBDS_INDEX_EMPTY;

    return STBDS_ARR_TO_HASH(a,elemsize);
  } else {
    stbds_hash_index *table;
    void *raw_a = STBDS_HASH_TO_ARR(a,elemsize);

    table = (stbds_hash_index *) stbds_header(raw_a)->hash_table;
    if (table == 0) {
      *temp = -1;
    } else {
      ptrdiff_t slot = stbds_hm_find_slot(a, elemsize, key, keysize, keyoffset, mode);
      if (slot < 0) {
        *temp = STBDS_INDEX_EMPTY;
      } else {
        stbds_hash_bucket *b = &table->storage[slot >> STBDS_BUCKET_SHIFT];
        *temp = b->index[slot & STBDS_BUCKET_MASK];
      }
    }
    return a;
  }
}

void * stbds_hmget_key(void *a, size_t elemsize, void *key, size_t keysize, int mode)
{
  ptrdiff_t temp;
  void *p = stbds_hmget_key_ts(a, elemsize, key, keysize, &temp, mode);
  stbds_temp(STBDS_HASH_TO_ARR(p,elemsize)) = temp;
  return p;
}

void * stbds_hmput_default(void *a, size_t elemsize)
{

  if (a == NULL || stbds_header(STBDS_HASH_TO_ARR(a,elemsize))->length == 0) {
    a = stbds_arrgrowf(a ? STBDS_HASH_TO_ARR(a,elemsize) : NULL, elemsize, 0, 1);
    stbds_header(a)->length += 1;
    memset(a, 0, elemsize);
    a=STBDS_ARR_TO_HASH(a,elemsize);
  }
  return a;
}

static char *stbds_strdup(char *str);

void *stbds_hmput_key(void *a, size_t elemsize, void *key, size_t keysize, int mode)
{
  size_t keyoffset=0;
  void *raw_a;
  stbds_hash_index *table;

  if (a == NULL) {
    a = stbds_arrgrowf(0, elemsize, 0, 1);
    memset(a, 0, elemsize);
    stbds_header(a)->length += 1;

    a = STBDS_ARR_TO_HASH(a,elemsize);
  }

  raw_a = a;
  a = STBDS_HASH_TO_ARR(a,elemsize);

  table = (stbds_hash_index *) stbds_header(a)->hash_table;

  if (table == NULL || table->used_count >= table->used_count_threshold) {
    stbds_hash_index *nt;
    size_t slot_count;

    slot_count = (table == NULL) ? STBDS_BUCKET_LENGTH : table->slot_count*2;
    nt = stbds_make_hash_index(slot_count, table);
    if (table)
      STBDS_FREE(NULL, table);
    else
      nt->string.mode = mode >= STBDS_HM_STRING ? STBDS_SH_DEFAULT : 0;
    stbds_header(a)->hash_table = table = nt;
    STBDS_STATS(++stbds_hash_grow);
  }

  {
    size_t hash = mode >= STBDS_HM_STRING ? stbds_hash_string((char*)key,table->seed) : stbds_hash_bytes(key, keysize,table->seed);
    size_t step = STBDS_BUCKET_LENGTH;
    size_t pos;
    ptrdiff_t tombstone = -1;
    stbds_hash_bucket *bucket;

    if (hash < 2) hash += 2;

    pos = stbds_probe_position(hash, table->slot_count, table->slot_count_log2);

    for (;;) {
      size_t limit, i;
      STBDS_STATS(++stbds_hash_probes);
      bucket = &table->storage[pos >> STBDS_BUCKET_SHIFT];

      for (i=pos & STBDS_BUCKET_MASK; i < STBDS_BUCKET_LENGTH; ++i) {
        if (bucket->hash[i] == hash) {
          if (stbds_is_key_equal(raw_a, elemsize, key, keysize, keyoffset, mode, bucket->index[i])) {
            stbds_temp(a) = bucket->index[i];
            if (mode >= STBDS_HM_STRING)
              stbds_temp_key(a) = * (char **) ((char *) raw_a + elemsize*bucket->index[i] + keyoffset);
            return STBDS_ARR_TO_HASH(a,elemsize);
          }
        } else if (bucket->hash[i] == 0) {
          pos = (pos & ~STBDS_BUCKET_MASK) + i;
          goto found_empty_slot;
        } else if (tombstone < 0) {
          if (bucket->index[i] == STBDS_INDEX_DELETED)
            tombstone = (ptrdiff_t) ((pos & ~STBDS_BUCKET_MASK) + i);
        }
      }

      limit = pos & STBDS_BUCKET_MASK;
      for (i = 0; i < limit; ++i) {
        if (bucket->hash[i] == hash) {
          if (stbds_is_key_equal(raw_a, elemsize, key, keysize, keyoffset, mode, bucket->index[i])) {
            stbds_temp(a) = bucket->index[i];
            return STBDS_ARR_TO_HASH(a,elemsize);
          }
        } else if (bucket->hash[i] == 0) {
          pos = (pos & ~STBDS_BUCKET_MASK) + i;
          goto found_empty_slot;
        } else if (tombstone < 0) {
          if (bucket->index[i] == STBDS_INDEX_DELETED)
            tombstone = (ptrdiff_t) ((pos & ~STBDS_BUCKET_MASK) + i);
        }
      }

      pos += step;
      step += STBDS_BUCKET_LENGTH;
      pos &= (table->slot_count-1);
    }
   found_empty_slot:
    if (tombstone >= 0) {
      pos = tombstone;
      --table->tombstone_count;
    }
    ++table->used_count;

    {
      ptrdiff_t i = (ptrdiff_t) stbds_arrlen(a);

      if ((size_t) i+1 > stbds_arrcap(a))
        *(void **) &a = stbds_arrgrowf(a, elemsize, 1, 0);
      raw_a = STBDS_ARR_TO_HASH(a,elemsize);

      STBDS_ASSERT((size_t) i+1 <= stbds_arrcap(a));
      stbds_header(a)->length = i+1;
      bucket = &table->storage[pos >> STBDS_BUCKET_SHIFT];
      bucket->hash[pos & STBDS_BUCKET_MASK] = hash;
      bucket->index[pos & STBDS_BUCKET_MASK] = i-1;
      stbds_temp(a) = i-1;

      switch (table->string.mode) {
         case STBDS_SH_STRDUP:  stbds_temp_key(a) = *(char **) ((char *) a + elemsize*i) = stbds_strdup((char*) key); break;
         case STBDS_SH_ARENA:   stbds_temp_key(a) = *(char **) ((char *) a + elemsize*i) = stbds_stralloc(&table->string, (char*)key); break;
         case STBDS_SH_DEFAULT: stbds_temp_key(a) = *(char **) ((char *) a + elemsize*i) = (char *) key; break;
         default:                memcpy((char *) a + elemsize*i, key, keysize); break;
      }
    }
    return STBDS_ARR_TO_HASH(a,elemsize);
  }
}

void * stbds_shmode_func(size_t elemsize, int mode)
{
  void *a = stbds_arrgrowf(0, elemsize, 0, 1);
  stbds_hash_index *h;
  memset(a, 0, elemsize);
  stbds_header(a)->length = 1;
  stbds_header(a)->hash_table = h = (stbds_hash_index *) stbds_make_hash_index(STBDS_BUCKET_LENGTH, NULL);
  h->string.mode = (unsigned char) mode;
  return STBDS_ARR_TO_HASH(a,elemsize);
}

void * stbds_hmdel_key(void *a, size_t elemsize, void *key, size_t keysize, size_t keyoffset, int mode)
{
  if (a == NULL) {
    return 0;
  } else {
    stbds_hash_index *table;
    void *raw_a = STBDS_HASH_TO_ARR(a,elemsize);
    table = (stbds_hash_index *) stbds_header(raw_a)->hash_table;
    stbds_temp(raw_a) = 0;
    if (table == 0) {
      return a;
    } else {
      ptrdiff_t slot;
      slot = stbds_hm_find_slot(a, elemsize, key, keysize, keyoffset, mode);
      if (slot < 0)
        return a;
      else {
        stbds_hash_bucket *b = &table->storage[slot >> STBDS_BUCKET_SHIFT];
        int i = slot & STBDS_BUCKET_MASK;
        ptrdiff_t old_index = b->index[i];
        ptrdiff_t final_index = (ptrdiff_t) stbds_arrlen(raw_a)-1-1; 
        STBDS_ASSERT(slot < (ptrdiff_t) table->slot_count);
        --table->used_count;
        ++table->tombstone_count;
        stbds_temp(raw_a) = 1;
        STBDS_ASSERT(table->used_count >= 0);

        b->hash[i] = STBDS_HASH_DELETED;
        b->index[i] = STBDS_INDEX_DELETED;

        if (mode == STBDS_HM_STRING && table->string.mode == STBDS_SH_STRDUP)
          STBDS_FREE(NULL, *(char**) ((char *) a+elemsize*old_index));

        if (old_index != final_index) {

          memmove((char*) a + elemsize*old_index, (char*) a + elemsize*final_index, elemsize);

          if (mode == STBDS_HM_STRING)
            slot = stbds_hm_find_slot(a, elemsize, *(char**) ((char *) a+elemsize*old_index + keyoffset), keysize, keyoffset, mode);
          else
            slot = stbds_hm_find_slot(a, elemsize,  (char* ) a+elemsize*old_index + keyoffset, keysize, keyoffset, mode);
          STBDS_ASSERT(slot >= 0);
          b = &table->storage[slot >> STBDS_BUCKET_SHIFT];
          i = slot & STBDS_BUCKET_MASK;
          STBDS_ASSERT(b->index[i] == final_index);
          b->index[i] = old_index;
        }
        stbds_header(raw_a)->length -= 1;

        if (table->used_count < table->used_count_shrink_threshold && table->slot_count > STBDS_BUCKET_LENGTH) {
          stbds_header(raw_a)->hash_table = stbds_make_hash_index(table->slot_count>>1, table);
          STBDS_FREE(NULL, table);
          STBDS_STATS(++stbds_hash_shrink);
        } else if (table->tombstone_count > table->tombstone_count_threshold) {
          stbds_header(raw_a)->hash_table = stbds_make_hash_index(table->slot_count   , table);
          STBDS_FREE(NULL, table);
          STBDS_STATS(++stbds_hash_rebuild);
        }

        return a;
      }
    }
  }

}

static char *stbds_strdup(char *str)
{

  size_t len = strlen(str)+1;
  char *p = (char*) STBDS_REALLOC(NULL, 0, len);
  memmove(p, str, len);
  return p;
}

#ifndef STBDS_STRING_ARENA_BLOCKSIZE_MIN
#define STBDS_STRING_ARENA_BLOCKSIZE_MIN  512u
#endif
#ifndef STBDS_STRING_ARENA_BLOCKSIZE_MAX
#define STBDS_STRING_ARENA_BLOCKSIZE_MAX  (1u<<20)
#endif

char *stbds_stralloc(stbds_string_arena *a, char *str)
{
  char *p;
  size_t len = strlen(str)+1;
  if (len > a->remaining) {

    size_t blocksize = a->block;

    blocksize = (size_t) (STBDS_STRING_ARENA_BLOCKSIZE_MIN) << (blocksize>>1);

    if (blocksize < (size_t)(STBDS_STRING_ARENA_BLOCKSIZE_MAX))
      ++a->block;

    if (len > blocksize) {

      stbds_string_block *sb = (stbds_string_block *) STBDS_REALLOC(NULL, 0, sizeof(*sb)-8 + len);
      memmove(sb->storage, str, len);
      if (a->storage) {

        sb->next = a->storage->next;
        a->storage->next = sb;
      } else {
        sb->next = 0;
        a->storage = sb;
        a->remaining = 0; 
      }
      return sb->storage;
    } else {
      stbds_string_block *sb = (stbds_string_block *) STBDS_REALLOC(NULL, 0, sizeof(*sb)-8 + blocksize);
      sb->next = a->storage;
      a->storage = sb;
      a->remaining = blocksize;
    }
  }

  STBDS_ASSERT(len <= a->remaining);
  p = a->storage->storage + a->remaining - len;
  a->remaining -= len;
  memmove(p, str, len);
  return p;
}

void stbds_strreset(stbds_string_arena *a)
{
  stbds_string_block *x,*y;
  x = a->storage;
  while (x) {
    y = x->next;
    STBDS_FREE(NULL, x);
    x = y;
  }
  memset(a, 0, sizeof(*a));
}

#endif

#ifdef STBDS_UNIT_TESTS
#include <stdio.h>
#ifdef STBDS_ASSERT_WAS_UNDEFINED
#undef STBDS_ASSERT
#endif
#ifndef STBDS_ASSERT
#define STBDS_ASSERT assert
#include <assert.h>
#endif

typedef struct { int key,b,c,d; } stbds_struct;
typedef struct { int key[2],b,c,d; } stbds_struct2;

static char buffer[256];
char *strkey(int n)
{
#if defined(_WIN32) && defined(__STDC_WANT_SECURE_LIB__)
   sprintf_s(buffer, sizeof(buffer), "test_%d", n);
#else
   sprintf(buffer, "test_%d", n);
#endif
   return buffer;
}

void stbds_unit_tests(void)
{
#if defined(_MSC_VER) && _MSC_VER <= 1200 && defined(__cplusplus)

  STBDS_ASSERT(0);
#else
  const int testsize = 100000;
  const int testsize2 = testsize/20;
  int *arr=NULL;
  struct { int   key;        int value; }  *intmap  = NULL;
  struct { char *key;        int value; }  *strmap  = NULL, s;
  struct { stbds_struct key; int value; }  *map     = NULL;
  stbds_struct                             *map2    = NULL;
  stbds_struct2                            *map3    = NULL;
  stbds_string_arena                        sa      = { 0 };
  int key3[2] = { 1,2 };
  ptrdiff_t temp;

  int i,j;

  STBDS_ASSERT(arrlen(arr)==0);
  for (i=0; i < 20000; i += 50) {
    for (j=0; j < i; ++j)
      arrpush(arr,j);
    arrfree(arr);
  }

  for (i=0; i < 4; ++i) {
    arrpush(arr,1); arrpush(arr,2); arrpush(arr,3); arrpush(arr,4);
    arrdel(arr,i);
    arrfree(arr);
    arrpush(arr,1); arrpush(arr,2); arrpush(arr,3); arrpush(arr,4);
    arrdelswap(arr,i);
    arrfree(arr);
  }

  for (i=0; i < 5; ++i) {
    arrpush(arr,1); arrpush(arr,2); arrpush(arr,3); arrpush(arr,4);
    stbds_arrins(arr,i,5);
    STBDS_ASSERT(arr[i] == 5);
    if (i < 4)
      STBDS_ASSERT(arr[4] == 4);
    arrfree(arr);
  }

  i = 1;
  STBDS_ASSERT(hmgeti(intmap,i) == -1);
  hmdefault(intmap, -2);
  STBDS_ASSERT(hmgeti(intmap, i) == -1);
  STBDS_ASSERT(hmget (intmap, i) == -2);
  for (i=0; i < testsize; i+=2)
    hmput(intmap, i, i*5);
  for (i=0; i < testsize; i+=1) {
    if (i & 1) STBDS_ASSERT(hmget(intmap, i) == -2 );
    else       STBDS_ASSERT(hmget(intmap, i) == i*5);
    if (i & 1) STBDS_ASSERT(hmget_ts(intmap, i, temp) == -2 );
    else       STBDS_ASSERT(hmget_ts(intmap, i, temp) == i*5);
  }
  for (i=0; i < testsize; i+=2)
    hmput(intmap, i, i*3);
  for (i=0; i < testsize; i+=1)
    if (i & 1) STBDS_ASSERT(hmget(intmap, i) == -2 );
    else       STBDS_ASSERT(hmget(intmap, i) == i*3);
  for (i=2; i < testsize; i+=4)
    hmdel(intmap, i); 
  for (i=0; i < testsize; i+=1)
    if (i & 3) STBDS_ASSERT(hmget(intmap, i) == -2 );
    else       STBDS_ASSERT(hmget(intmap, i) == i*3);
  for (i=0; i < testsize; i+=1)
    hmdel(intmap, i); 
  for (i=0; i < testsize; i+=1)
    STBDS_ASSERT(hmget(intmap, i) == -2 );
  hmfree(intmap);
  for (i=0; i < testsize; i+=2)
    hmput(intmap, i, i*3);
  hmfree(intmap);

  #if defined(__clang__) || defined(__GNUC__)
  #ifndef __cplusplus
  intmap = NULL;
  hmput(intmap, 15, 7);
  hmput(intmap, 11, 3);
  hmput(intmap,  9, 5);
  STBDS_ASSERT(hmget(intmap, 9) == 5);
  STBDS_ASSERT(hmget(intmap, 11) == 3);
  STBDS_ASSERT(hmget(intmap, 15) == 7);
  #endif
  #endif

  for (i=0; i < testsize; ++i)
    stralloc(&sa, strkey(i));
  strreset(&sa);

  {
    s.key = "a", s.value = 1;
    shputs(strmap, s);
    STBDS_ASSERT(*strmap[0].key == 'a');
    STBDS_ASSERT(strmap[0].key == s.key);
    STBDS_ASSERT(strmap[0].value == s.value);
    shfree(strmap);
  }

  {
    s.key = "a", s.value = 1;
    sh_new_strdup(strmap);
    shputs(strmap, s);
    STBDS_ASSERT(*strmap[0].key == 'a');
    STBDS_ASSERT(strmap[0].key != s.key);
    STBDS_ASSERT(strmap[0].value == s.value);
    shfree(strmap);
  }

  {
    s.key = "a", s.value = 1;
    sh_new_arena(strmap);
    shputs(strmap, s);
    STBDS_ASSERT(*strmap[0].key == 'a');
    STBDS_ASSERT(strmap[0].key != s.key);
    STBDS_ASSERT(strmap[0].value == s.value);
    shfree(strmap);
  }

  for (j=0; j < 2; ++j) {
    STBDS_ASSERT(shgeti(strmap,"foo") == -1);
    if (j == 0)
      sh_new_strdup(strmap);
    else
      sh_new_arena(strmap);
    STBDS_ASSERT(shgeti(strmap,"foo") == -1);
    shdefault(strmap, -2);
    STBDS_ASSERT(shgeti(strmap,"foo") == -1);
    for (i=0; i < testsize; i+=2)
      shput(strmap, strkey(i), i*3);
    for (i=0; i < testsize; i+=1)
      if (i & 1) STBDS_ASSERT(shget(strmap, strkey(i)) == -2 );
      else       STBDS_ASSERT(shget(strmap, strkey(i)) == i*3);
    for (i=2; i < testsize; i+=4)
      shdel(strmap, strkey(i)); 
    for (i=0; i < testsize; i+=1)
      if (i & 3) STBDS_ASSERT(shget(strmap, strkey(i)) == -2 );
      else       STBDS_ASSERT(shget(strmap, strkey(i)) == i*3);
    for (i=0; i < testsize; i+=1)
      shdel(strmap, strkey(i)); 
    for (i=0; i < testsize; i+=1)
      STBDS_ASSERT(shget(strmap, strkey(i)) == -2 );
    shfree(strmap);
  }

  {
    struct { char *key; char value; } *hash = NULL;
    char name[4] = "jen";
    shput(hash, "bob"   , 'h');
    shput(hash, "sally" , 'e');
    shput(hash, "fred"  , 'l');
    shput(hash, "jen"   , 'x');
    shput(hash, "doug"  , 'o');

    shput(hash, name    , 'l');
    shfree(hash);
  }

  for (i=0; i < testsize; i += 2) {
    stbds_struct s = { i,i*2,i*3,i*4 };
    hmput(map, s, i*5);
  }

  for (i=0; i < testsize; i += 1) {
    stbds_struct s = { i,i*2,i*3  ,i*4 };
    stbds_struct t = { i,i*2,i*3+1,i*4 };
    if (i & 1) STBDS_ASSERT(hmget(map, s) == 0);
    else       STBDS_ASSERT(hmget(map, s) == i*5);
    if (i & 1) STBDS_ASSERT(hmget_ts(map, s, temp) == 0);
    else       STBDS_ASSERT(hmget_ts(map, s, temp) == i*5);

  }

  for (i=0; i < testsize; i += 2) {
    stbds_struct s = { i,i*2,i*3,i*4 };
    hmputs(map2, s);
  }
  hmfree(map);

  for (i=0; i < testsize; i += 1) {
    stbds_struct s = { i,i*2,i*3,i*4 };
    stbds_struct t = { i,i*2,i*3+1,i*4 };
    if (i & 1) STBDS_ASSERT(hmgets(map2, s.key).d == 0);
    else       STBDS_ASSERT(hmgets(map2, s.key).d == i*4);

  }
  hmfree(map2);

  for (i=0; i < testsize; i += 2) {
    stbds_struct2 s = { { i,i*2 }, i*3,i*4, i*5 };
    hmputs(map3, s);
  }
  for (i=0; i < testsize; i += 1) {
    stbds_struct2 s = { { i,i*2}, i*3, i*4, i*5 };
    stbds_struct2 t = { { i,i*2}, i*3+1, i*4, i*5 };
    if (i & 1) STBDS_ASSERT(hmgets(map3, s.key).d == 0);
    else       STBDS_ASSERT(hmgets(map3, s.key).d == i*5);

  }
#endif
}
#endif
