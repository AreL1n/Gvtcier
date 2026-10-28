#define lutf8lib_c
#define GVTS_LIB

#include "Gprefix.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "gvts.h"

#include "Gauxlib.h"
#include "gvtslib.h"

#define MAXUNICODE	0x10FFFFu

#define MAXUTF		0x7FFFFFFFu

#define MSGInvalid	"invalid UTF-8 code"

#if (UINT_MAX >> 30) >= 1
typedef unsigned int utfint;
#else
typedef unsigned long utfint;
#endif

#define iscont(c)	(((c) & 0xC0) == 0x80)
#define iscontp(p)	iscont(*(p))

static gvts_Integer u_posrelat (gvts_Integer pos, size_t len) {
  if (pos >= 0) return pos;
  else if (0u - (size_t)pos > len) return 0;
  else return (gvts_Integer)len + pos + 1;
}

static const char *utf8_decode (const char *s, utfint *val, int strict) {
  static const utfint limits[] =
        {~(utfint)0, 0x80, 0x800, 0x10000u, 0x200000u, 0x4000000u};
  unsigned int c = (unsigned char)s[0];
  utfint res = 0;                    
  if (c < 0x80)              
    res = c;
  else {
    int count = 0;                                             
    for (; c & 0x40; c <<= 1) {                                            
      unsigned int cc = (unsigned char)s[++count];                      
      if (!iscont(cc))                                
        return NULL;                             
      res = (res << 6) | (cc & 0x3F);                                        
    }
    res |= ((utfint)(c & 0x7F) << (count * 5));                      
    if (count > 5 || res > MAXUTF || res < limits[count])
      return NULL;                             
    s += count;                                    
  }
  if (strict) {

    if (res > MAXUNICODE || (0xD800u <= res && res <= 0xDFFFu))
      return NULL;
  }
  if (val) *val = res;
  return s + 1;                                
}

static int utflen (gvts_State *L) {
  gvts_Integer n = 0;                                            
  size_t len;                              
  const char *s = gvtsG_checklstring(L, 1, &len);
  gvts_Integer posi = u_posrelat(gvtsG_optinteger(L, 2, 1), len);
  gvts_Integer posj = u_posrelat(gvtsG_optinteger(L, 3, -1), len);
  int lax = gvts_toboolean(L, 4);
  gvtsG_argcheck(L, 1 <= posi && --posi <= (gvts_Integer)len, 2,
                   "initial position out of bounds");
  gvtsG_argcheck(L, --posj < (gvts_Integer)len, 3,
                   "final position out of bounds");
  while (posi <= posj) {
    const char *s1 = utf8_decode(s + posi, NULL, !lax);
    if (s1 == NULL) {                         
      gvtsG_pushfail(L);                       
      gvts_pushinteger(L, posi + 1);                                
      return 2;
    }
    posi = s1 - s;
    n++;
  }
  gvts_pushinteger(L, n);
  return 1;
}

static int codepoint (gvts_State *L) {
  size_t len;
  const char *s = gvtsG_checklstring(L, 1, &len);
  gvts_Integer posi = u_posrelat(gvtsG_optinteger(L, 2, 1), len);
  gvts_Integer pose = u_posrelat(gvtsG_optinteger(L, 3, posi), len);
  int lax = gvts_toboolean(L, 4);
  int n;
  const char *se;
  gvtsG_argcheck(L, posi >= 1, 2, "out of bounds");
  gvtsG_argcheck(L, pose <= (gvts_Integer)len, 3, "out of bounds");
  if (posi > pose) return 0;                                        
  if (pose - posi >= INT_MAX)                                      
    return gvtsG_error(L, "string slice too long");
  n = (int)(pose -  posi) + 1;                                         
  gvtsG_checkstack(L, n, "string slice too long");
  n = 0;                                   
  se = s + pose;                  
  for (s += posi - 1; s < se;) {
    utfint code;
    s = utf8_decode(s, &code, !lax);
    if (s == NULL)
      return gvtsG_error(L, MSGInvalid);
    gvts_pushinteger(L, code);
    n++;
  }
  return n;
}

static void pushutfchar (gvts_State *L, int arg) {
  gvts_Unsigned code = (gvts_Unsigned)gvtsG_checkinteger(L, arg);
  gvtsG_argcheck(L, code <= MAXUTF, arg, "value out of range");
  gvts_pushfstring(L, "%U", (long)code);
}

static int utfchar (gvts_State *L) {
  int n = gvts_gettop(L);                           
  if (n == 1)                                           
    pushutfchar(L, 1);
  else {
    int i;
    gvtsG_Buffer b;
    gvtsG_buffinit(L, &b);
    for (i = 1; i <= n; i++) {
      pushutfchar(L, i);
      gvtsG_addvalue(&b);
    }
    gvtsG_pushresult(&b);
  }
  return 1;
}

static int byteoffset (gvts_State *L) {
  size_t len;
  const char *s = gvtsG_checklstring(L, 1, &len);
  gvts_Integer n  = gvtsG_checkinteger(L, 2);
  gvts_Integer posi = (n >= 0) ? 1 : len + 1;
  posi = u_posrelat(gvtsG_optinteger(L, 3, posi), len);
  gvtsG_argcheck(L, 1 <= posi && --posi <= (gvts_Integer)len, 3,
                   "position out of bounds");
  if (n == 0) {

    while (posi > 0 && iscontp(s + posi)) posi--;
  }
  else {
    if (iscontp(s + posi))
      return gvtsG_error(L, "initial position is a continuation byte");
    if (n < 0) {
       while (n < 0 && posi > 0) {                 
         do {                                            
           posi--;
         } while (posi > 0 && iscontp(s + posi));
         n++;
       }
     }
     else {
       n--;                                     
       while (n > 0 && posi < (gvts_Integer)len) {
         do {                                        
           posi++;
         } while (iscontp(s + posi));                                
         n--;
       }
     }
  }
  if (n == 0)                                    
    gvts_pushinteger(L, posi + 1);
  else                         
    gvtsG_pushfail(L);
  return 1;
}

static int iter_aux (gvts_State *L, int strict) {
  size_t len;
  const char *s = gvtsG_checklstring(L, 1, &len);
  gvts_Unsigned n = (gvts_Unsigned)gvts_tointeger(L, 2);
  if (n < len) {
    while (iscontp(s + n)) n++;                            
  }
  if (n >= len)                                                  
    return 0;                          
  else {
    utfint code;
    const char *next = utf8_decode(s + n, &code, strict);
    if (next == NULL || iscontp(next))
      return gvtsG_error(L, MSGInvalid);
    gvts_pushinteger(L, n + 1);
    gvts_pushinteger(L, code);
    return 2;
  }
}

static int iter_auxstrict (gvts_State *L) {
  return iter_aux(L, 1);
}

static int iter_auxlax (gvts_State *L) {
  return iter_aux(L, 0);
}

static int iter_codes (gvts_State *L) {
  int lax = gvts_toboolean(L, 2);
  const char *s = gvtsG_checkstring(L, 1);
  gvtsG_argcheck(L, !iscontp(s), 1, MSGInvalid);
  gvts_pushcfunction(L, lax ? iter_auxlax : iter_auxstrict);
  gvts_pushvalue(L, 1);
  gvts_pushinteger(L, 0);
  return 3;
}

#define UTF8PATT	"[\0-\x7F\xC2-\xFD][\x80-\xBF]*"

static const gvtsG_Reg funcs[] = {
  {"offset", byteoffset},
  {"codepoint", codepoint},
  {"char", utfchar},
  {"len", utflen},
  {"codes", iter_codes},

  {"charpattern", NULL},
  {NULL, NULL}
};

GVTSMOD_API int gvtsopen_utf8 (gvts_State *L) {
  gvtsG_newlib(L, funcs);
  gvts_pushlstring(L, UTF8PATT, sizeof(UTF8PATT)/sizeof(char) - 1);
  gvts_setfield(L, -2, "charpattern");
  return 1;
}
