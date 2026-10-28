#define lobject_c
#define GVTS_CORE

#include "Gprefix.h"

#include <locale.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gvts.h"

#include "Gctype.h"
#include "Gdebug.h"
#include "Gdo.h"
#include "Gmem.h"
#include "Gobject.h"
#include "Gstate.h"
#include "Gstring.h"
#include "Gvm.h"

int gvtsO_ceillog2 (unsigned int x) {
  static const lu_byte log_2[256] = {                                    
    0,1,2,2,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
    6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8
  };
  int l = 0;
  x--;
  while (x >= 256) { l += 8; x >>= 8; }
  return l + log_2[x];
}

static gvts_Integer intarith (gvts_State *L, int op, gvts_Integer v1,
                                                   gvts_Integer v2) {
  switch (op) {
    case GVTS_OPADD: return intop(+, v1, v2);
    case GVTS_OPSUB:return intop(-, v1, v2);
    case GVTS_OPMUL:return intop(*, v1, v2);
    case GVTS_OPMOD: return gvtsV_mod(L, v1, v2);
    case GVTS_OPIDIV: return gvtsV_idiv(L, v1, v2);
    case GVTS_OPBAND: return intop(&, v1, v2);
    case GVTS_OPBOR: return intop(|, v1, v2);
    case GVTS_OPBXOR: return intop(^, v1, v2);
    case GVTS_OPSHL: return gvtsV_shiftl(v1, v2);
    case GVTS_OPSHR: return gvtsV_shiftr(v1, v2);
    case GVTS_OPUNM: return intop(-, 0, v1);
    case GVTS_OPBNOT: return intop(^, ~g_castS2U(0), v1);
    default: gvts_assert(0); return 0;
  }
}

static gvts_Number numarith (gvts_State *L, int op, gvts_Number v1,
                                                  gvts_Number v2) {
  switch (op) {
    case GVTS_OPADD: return gvtsi_numadd(L, v1, v2);
    case GVTS_OPSUB: return gvtsi_numsub(L, v1, v2);
    case GVTS_OPMUL: return gvtsi_nummul(L, v1, v2);
    case GVTS_OPDIV: return gvtsi_numdiv(L, v1, v2);
    case GVTS_OPPOW: return gvtsi_numpow(L, v1, v2);
    case GVTS_OPIDIV: return gvtsi_numidiv(L, v1, v2);
    case GVTS_OPUNM: return gvtsi_numunm(L, v1);
    case GVTS_OPMOD: return gvtsV_modf(L, v1, v2);
    default: gvts_assert(0); return 0;
  }
}

int gvtsO_rawarith (gvts_State *L, int op, const TValue *p1, const TValue *p2,
                   TValue *res) {
  switch (op) {
    case GVTS_OPBAND: case GVTS_OPBOR: case GVTS_OPBXOR:
    case GVTS_OPSHL: case GVTS_OPSHR:
    case GVTS_OPBNOT: {                                
      gvts_Integer i1; gvts_Integer i2;
      if (tointegerns(p1, &i1) && tointegerns(p2, &i2)) {
        setivalue(res, intarith(L, op, i1, i2));
        return 1;
      }
      else return 0;            
    }
    case GVTS_OPDIV: case GVTS_OPPOW: {                              
      gvts_Number n1; gvts_Number n2;
      if (tonumberns(p1, n1) && tonumberns(p2, n2)) {
        setfltvalue(res, numarith(L, op, n1, n2));
        return 1;
      }
      else return 0;            
    }
    default: {                        
      gvts_Number n1; gvts_Number n2;
      if (ttisinteger(p1) && ttisinteger(p2)) {
        setivalue(res, intarith(L, op, ivalue(p1), ivalue(p2)));
        return 1;
      }
      else if (tonumberns(p1, n1) && tonumberns(p2, n2)) {
        setfltvalue(res, numarith(L, op, n1, n2));
        return 1;
      }
      else return 0;            
    }
  }
}

void gvtsO_arith (gvts_State *L, int op, const TValue *p1, const TValue *p2,
                 StkId res) {
  if (!gvtsO_rawarith(L, op, p1, p2, s2v(res))) {

    gvtsT_trybinTM(L, p1, p2, res, cast(TMS, (op - GVTS_OPADD) + TM_ADD));
  }
}

int gvtsO_hexavalue (int c) {
  if (lisdigit(c)) return c - '0';
  else return (ltolower(c) - 'a') + 10;
}

static int isneg (const char **s) {
  if (**s == '-') { (*s)++; return 1; }
  else if (**s == '+') (*s)++;
  return 0;
}

#if !defined(gvts_strx2number)

#define MAXSIGDIG	30

static gvts_Number gvts_strx2number (const char *s, char **endptr) {
  int dot = gvts_getlocaledecpoint();
  gvts_Number r = g_mathop(0.0);                            
  int sigdig = 0;                                    
  int nosigdig = 0;                                        
  int e = 0;                           
  int neg;                               
  int hasdot = 0;                             
  *endptr = cast_charp(s);                            
  while (lisspace(cast_uchar(*s))) s++;                           
  neg = isneg(&s);                  
  if (!(*s == '0' && (*(s + 1) == 'x' || *(s + 1) == 'X')))                  
    return g_mathop(0.0);                                
  for (s += 2; ; s++) {                                  
    if (*s == dot) {
      if (hasdot) break;                             
      else hasdot = 1;
    }
    else if (lisxdigit(cast_uchar(*s))) {
      if (sigdig == 0 && *s == '0')                                     
        nosigdig++;
      else if (++sigdig <= MAXSIGDIG)                                     
          r = (r * g_mathop(16.0)) + gvtsO_hexavalue(*s);
      else e++;                                                            
      if (hasdot) e--;                                       
    }
    else break;                                 
  }
  if (nosigdig + sigdig == 0)                  
    return g_mathop(0.0);                      
  *endptr = cast_charp(s);                        
  e *= 4;                                                  
  if (*s == 'p' || *s == 'P') {                      
    int exp1 = 0;                      
    int neg1;                     
    s++;                
    neg1 = isneg(&s);            
    if (!lisdigit(cast_uchar(*s)))
      return g_mathop(0.0);                                             
    while (lisdigit(cast_uchar(*s)))                     
      exp1 = exp1 * 10 + *(s++) - '0';
    if (neg1) exp1 = -exp1;
    e += exp1;
    *endptr = cast_charp(s);                        
  }
  if (neg) r = -r;
  return g_mathop(ldexp)(r, e);
}

#endif

#if !defined (L_MAXLENNUM)
#define L_MAXLENNUM	200
#endif

static const char *g_str2dloc (const char *s, gvts_Number *result, int mode) {
  char *endptr;
  *result = (mode == 'x') ? gvts_strx2number(s, &endptr)                      
                          : gvts_str2number(s, &endptr);
  if (endptr == s) return NULL;                           
  while (lisspace(cast_uchar(*endptr))) endptr++;                            
  return (*endptr == '\0') ? endptr : NULL;                                
}

static const char *g_str2d (const char *s, gvts_Number *result) {
  const char *endptr;
  const char *pmode = strpbrk(s, ".xXnN");                              
  int mode = pmode ? ltolower(cast_uchar(*pmode)) : 0;
  if (mode == 'n')                              
    return NULL;
  endptr = g_str2dloc(s, result, mode);                      
  if (endptr == NULL) {                                         
    char buff[L_MAXLENNUM + 1];
    const char *pdot = strchr(s, '.');
    if (pdot == NULL || strlen(s) > L_MAXLENNUM)
      return NULL;                                       
    strcpy(buff, s);                             
    buff[pdot - s] = gvts_getlocaledecpoint();                             
    endptr = g_str2dloc(buff, result, mode);                 
    if (endptr != NULL)
      endptr = s + (endptr - buff);                            
  }
  return endptr;
}

#define MAXBY10		cast(gvts_Unsigned, GVTS_MAXINTEGER / 10)
#define MAXLASTD	cast_int(GVTS_MAXINTEGER % 10)

static const char *g_str2int (const char *s, gvts_Integer *result) {
  gvts_Unsigned a = 0;
  int empty = 1;
  int neg;
  while (lisspace(cast_uchar(*s))) s++;                           
  neg = isneg(&s);
  if (s[0] == '0' &&
      (s[1] == 'x' || s[1] == 'X')) {            
    s += 2;                 
    for (; lisxdigit(cast_uchar(*s)); s++) {
      a = a * 16 + gvtsO_hexavalue(*s);
      empty = 0;
    }
  }
  else {               
    for (; lisdigit(cast_uchar(*s)); s++) {
      int d = *s - '0';
      if (a >= MAXBY10 && (a > MAXBY10 || d > MAXLASTD + neg))                 
        return NULL;                                     
      a = a * 10 + d;
      empty = 0;
    }
  }
  while (lisspace(cast_uchar(*s))) s++;                            
  if (empty || *s != '\0') return NULL;                                      
  else {
    *result = g_castU2S((neg) ? 0u - a : a);
    return s;
  }
}

size_t gvtsO_str2num (const char *s, TValue *o) {
  gvts_Integer i; gvts_Number n;
  const char *e;
  if ((e = g_str2int(s, &i)) != NULL) {                         
    setivalue(o, i);
  }
  else if ((e = g_str2d(s, &n)) != NULL) {                           
    setfltvalue(o, n);
  }
  else
    return 0;                         
  return (e - s) + 1;                                   
}

int gvtsO_utf8esc (char *buff, unsigned long x) {
  int n = 1;                                                 
  gvts_assert(x <= 0x7FFFFFFFu);
  if (x < 0x80)              
    buff[UTF8BUFFSZ - 1] = cast_char(x);
  else {                               
    unsigned int mfb = 0x3f;                                       
    do {                              
      buff[UTF8BUFFSZ - (n++)] = cast_char(0x80 | (x & 0x3f));
      x >>= 6;                         
      mfb >>= 1;                                                         
    } while (x > mfb);                                      
    buff[UTF8BUFFSZ - n] = cast_char((~mfb << 1) | x);                      
  }
  return n;
}

#define MAXNUMBER2STR	44

static int tostringbuff (TValue *obj, char *buff) {
  int len;
  gvts_assert(ttisnumber(obj));
  if (ttisinteger(obj))
    len = gvts_integer2str(buff, MAXNUMBER2STR, ivalue(obj));
  else {
    len = gvts_number2str(buff, MAXNUMBER2STR, fltvalue(obj));
    if (buff[strspn(buff, "-0123456789")] == '\0') {                          
      buff[len++] = gvts_getlocaledecpoint();
      buff[len++] = '0';                           
    }
  }
  return len;
}

void gvtsO_tostring (gvts_State *L, TValue *obj) {
  char buff[MAXNUMBER2STR];
  int len = tostringbuff(obj, buff);
  setsvalue(L, obj, gvtsS_newlstr(L, buff, len));
}

#define BUFVFS		(GVTS_IDSIZE + MAXNUMBER2STR + 95)

typedef struct BuffFS {
  gvts_State *L;
  int pushed;                                                          
  int blen;                                           
  char space[BUFVFS];                                     
} BuffFS;

static void pushstr (BuffFS *buff, const char *str, size_t lstr) {
  gvts_State *L = buff->L;
  setsvalue2s(L, L->top.p, gvtsS_newlstr(L, str, lstr));
  L->top.p++;                                         
  if (!buff->pushed)                                        
    buff->pushed = 1;                        
  else                                         
    gvtsV_concat(L, 2);
}

static void clearbuff (BuffFS *buff) {
  pushstr(buff, buff->space, buff->blen);                            
  buff->blen = 0;                          
}

static char *getbuff (BuffFS *buff, int sz) {
  gvts_assert(buff->blen <= BUFVFS); gvts_assert(sz <= BUFVFS);
  if (sz > BUFVFS - buff->blen)                         
    clearbuff(buff);
  return buff->space + buff->blen;
}

#define addsize(b,sz)	((b)->blen += (sz))

static void addstr2buff (BuffFS *buff, const char *str, size_t slen) {
  if (slen <= BUFVFS) {                                    
    char *bf = getbuff(buff, cast_int(slen));
    memcpy(bf, str, slen);                            
    addsize(buff, cast_int(slen));
  }
  else {                                 
    clearbuff(buff);                                           
    pushstr(buff, str, slen);                   
  }
}

static void addnum2buff (BuffFS *buff, TValue *num) {
  char *numbuff = getbuff(buff, MAXNUMBER2STR);
  int len = tostringbuff(num, numbuff);                                    
  addsize(buff, len);
}

const char *gvtsO_pushvfstring (gvts_State *L, const char *fmt, va_list argp) {
  BuffFS buff;                                     
  const char *e;                          
  buff.pushed = buff.blen = 0;
  buff.L = L;
  while ((e = strchr(fmt, '%')) != NULL) {
    addstr2buff(&buff, fmt, e - fmt);                           
    switch (*(e + 1)) {                            
      case 's': {                              
        const char *s = va_arg(argp, char *);
        if (s == NULL) s = "(null)";
        addstr2buff(&buff, s, strlen(s));
        break;
      }
      case 'c': {                               
        char c = cast_uchar(va_arg(argp, int));
        addstr2buff(&buff, &c, sizeof(char));
        break;
      }
      case 'd': {                
        TValue num;
        setivalue(&num, va_arg(argp, int));
        addnum2buff(&buff, &num);
        break;
      }
      case 'I': {                       
        TValue num;
        setivalue(&num, cast(gvts_Integer, va_arg(argp, g_uacInt)));
        addnum2buff(&buff, &num);
        break;
      }
      case 'f': {                      
        TValue num;
        setfltvalue(&num, cast_num(va_arg(argp, g_uacNumber)));
        addnum2buff(&buff, &num);
        break;
      }
      case 'p': {                 
        const int sz = 3 * sizeof(void*) + 8;                            
        char *bf = getbuff(&buff, sz);
        void *p = va_arg(argp, void *);
        int len = gvts_pointer2str(bf, sz, p);
        addsize(&buff, len);
        break;
      }
      case 'U': {                                    
        char bf[UTF8BUFFSZ];
        int len = gvtsO_utf8esc(bf, va_arg(argp, long));
        addstr2buff(&buff, bf + UTF8BUFFSZ - len, len);
        break;
      }
      case '%': {
        addstr2buff(&buff, "%", 1);
        break;
      }
      default: {
        gvtsG_runerror(L, u8"gvts_pushfstring 的格式选项 '%%%c' 无效",
                         *(e + 1));
      }
    }
    fmt = e + 2;                                  
  }
  addstr2buff(&buff, fmt, strlen(fmt));                     
  clearbuff(&buff);                                   
  gvts_assert(buff.pushed == 1);
  return getstr(tsvalue(s2v(L->top.p - 1)));
}

const char *gvtsO_pushfstring (gvts_State *L, const char *fmt, ...) {
  const char *msg;
  va_list argp;
  va_start(argp, fmt);
  msg = gvtsO_pushvfstring(L, fmt, argp);
  va_end(argp);
  return msg;
}

#define RETS	"..."
#define PRE	"[string \""
#define POS	"\"]"

#define addstr(a,b,l)	( memcpy(a,b,(l) * sizeof(char)), a += (l) )

void gvtsO_chunkid (char *out, const char *source, size_t srclen) {
  size_t bufflen = GVTS_IDSIZE;                            
  if (*source == '=') {                        
    if (srclen <= bufflen)                     
      memcpy(out, source + 1, srclen * sizeof(char));
    else {                   
      addstr(out, source + 1, bufflen - 1);
      *out = '\0';
    }
  }
  else if (*source == '@') {                 
    if (srclen <= bufflen)                     
      memcpy(out, source + 1, srclen * sizeof(char));
    else {                                     
      addstr(out, RETS, LL(RETS));
      bufflen -= LL(RETS);
      memcpy(out, source + 1 + srclen - bufflen, bufflen * sizeof(char));
    }
  }
  else {                                           
    const char *nl = strchr(source, '\n');                                    
    addstr(out, PRE, LL(PRE));                  
    bufflen -= LL(PRE RETS POS) + 1;                                         
    if (srclen < bufflen && nl == NULL) {                              
      addstr(out, source, srclen);               
    }
    else {
      if (nl != NULL) srclen = nl - source;                             
      if (srclen > bufflen) srclen = bufflen;
      addstr(out, source, srclen);
      addstr(out, RETS, LL(RETS));
    }
    memcpy(out, POS, (LL(POS) + 1) * sizeof(char));
  }
}
