#define lstrlib_c
#define GVTS_LIB

#include "Gprefix.h"

#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gvts.h"

#include "Gauxlib.h"
#include "gvtslib.h"

#if !defined(GVTS_MAXCAPTURES)
#define GVTS_MAXCAPTURES		32
#endif

#define uchar(c)	((unsigned char)(c))

#define MAX_SIZET	((size_t)(~(size_t)0))

#define MAXSIZE  \
	(sizeof(size_t) < sizeof(int) ? MAX_SIZET : (size_t)(INT_MAX))

static int str_len (gvts_State *L) {
  size_t l;
  gvtsG_checklstring(L, 1, &l);
  gvts_pushinteger(L, (gvts_Integer)l);
  return 1;
}

static size_t posrelatI (gvts_Integer pos, size_t len) {
  if (pos > 0)
    return (size_t)pos;
  else if (pos == 0)
    return 1;
  else if (pos < -(gvts_Integer)len)                           
    return 1;                 
  else return len + (size_t)pos + 1;
}

static size_t getendpos (gvts_State *L, int arg, gvts_Integer def,
                         size_t len) {
  gvts_Integer pos = gvtsG_optinteger(L, arg, def);
  if (pos > (gvts_Integer)len)
    return len;
  else if (pos >= 0)
    return (size_t)pos;
  else if (pos < -(gvts_Integer)len)
    return 0;
  else return len + (size_t)pos + 1;
}

static int str_sub (gvts_State *L) {
  size_t l;
  const char *s = gvtsG_checklstring(L, 1, &l);
  size_t start = posrelatI(gvtsG_checkinteger(L, 2), l);
  size_t end = getendpos(L, 3, -1, l);
  if (start <= end)
    gvts_pushlstring(L, s + start - 1, (end - start) + 1);
  else gvts_pushliteral(L, "");
  return 1;
}

static int str_reverse (gvts_State *L) {
  size_t l, i;
  gvtsG_Buffer b;
  const char *s = gvtsG_checklstring(L, 1, &l);
  char *p = gvtsG_buffinitsize(L, &b, l);
  for (i = 0; i < l; i++)
    p[i] = s[l - i - 1];
  gvtsG_pushresultsize(&b, l);
  return 1;
}

static int str_lower (gvts_State *L) {
  size_t l;
  size_t i;
  gvtsG_Buffer b;
  const char *s = gvtsG_checklstring(L, 1, &l);
  char *p = gvtsG_buffinitsize(L, &b, l);
  for (i=0; i<l; i++)
    p[i] = tolower(uchar(s[i]));
  gvtsG_pushresultsize(&b, l);
  return 1;
}

static int str_upper (gvts_State *L) {
  size_t l;
  size_t i;
  gvtsG_Buffer b;
  const char *s = gvtsG_checklstring(L, 1, &l);
  char *p = gvtsG_buffinitsize(L, &b, l);
  for (i=0; i<l; i++)
    p[i] = toupper(uchar(s[i]));
  gvtsG_pushresultsize(&b, l);
  return 1;
}

static int str_rep (gvts_State *L) {
  size_t l, lsep;
  const char *s = gvtsG_checklstring(L, 1, &l);
  gvts_Integer n = gvtsG_checkinteger(L, 2);
  const char *sep = gvtsG_optlstring(L, 3, "", &lsep);
  if (n <= 0)
    gvts_pushliteral(L, "");
  else if (g_unlikely(l + lsep < l || l + lsep > MAXSIZE / n))
    return gvtsG_error(L, u8"结果字符串太长");
  else {
    size_t totallen = (size_t)n * l + (size_t)(n - 1) * lsep;
    gvtsG_Buffer b;
    char *p = gvtsG_buffinitsize(L, &b, totallen);
    while (n-- > 1) {                                                
      memcpy(p, s, l * sizeof(char)); p += l;
      if (lsep > 0) {                                        
        memcpy(p, sep, lsep * sizeof(char));
        p += lsep;
      }
    }
    memcpy(p, s, l * sizeof(char));                                             
    gvtsG_pushresultsize(&b, totallen);
  }
  return 1;
}

static int str_byte (gvts_State *L) {
  size_t l;
  const char *s = gvtsG_checklstring(L, 1, &l);
  gvts_Integer pi = gvtsG_optinteger(L, 2, 1);
  size_t posi = posrelatI(pi, l);
  size_t pose = getendpos(L, 3, pi, l);
  int n, i;
  if (posi > pose) return 0;                                        
  if (g_unlikely(pose - posi >= (size_t)INT_MAX))                            
    return gvtsG_error(L, u8"字符串切片太长");
  n = (int)(pose -  posi) + 1;
  gvtsG_checkstack(L, n, u8"字符串切片太长");
  for (i=0; i<n; i++)
    gvts_pushinteger(L, uchar(s[posi+i-1]));
  return n;
}

static int str_char (gvts_State *L) {
  int n = gvts_gettop(L);                           
  int i;
  gvtsG_Buffer b;
  char *p = gvtsG_buffinitsize(L, &b, n);
  for (i=1; i<=n; i++) {
    gvts_Unsigned c = (gvts_Unsigned)gvtsG_checkinteger(L, i);
    gvtsG_argcheck(L, c <= (gvts_Unsigned)UCHAR_MAX, i, u8"值超出范围");
    p[i - 1] = uchar(c);
  }
  gvtsG_pushresultsize(&b, n);
  return 1;
}

struct str_Writer {
  int init;                                            
  gvtsG_Buffer B;
};

static int writer (gvts_State *L, const void *b, size_t size, void *ud) {
  struct str_Writer *state = (struct str_Writer *)ud;
  if (!state->init) {
    state->init = 1;
    gvtsG_buffinit(L, &state->B);
  }
  gvtsG_addlstring(&state->B, (const char *)b, size);
  return 0;
}

static int str_dump (gvts_State *L) {
  struct str_Writer state;
  int strip = gvts_toboolean(L, 2);
  gvtsG_checktype(L, 1, GVTS_TFUNCTION);
  gvts_settop(L, 1);                                                  
  state.init = 0;
  if (g_unlikely(gvts_dump(L, writer, &state, strip) != 0))
    return gvtsG_error(L, u8"无法导出给定函数");
  gvtsG_pushresult(&state.B);
  return 1;
}

#if defined(GVTS_NOCVTS2N)	       

static const gvtsG_Reg stringmetamethods[] = {
  {"__index", NULL},                   
  {NULL, NULL}
};

#else		        

static int tonum (gvts_State *L, int arg) {
  if (gvts_type(L, arg) == GVTS_TNUMBER) {                         
    gvts_pushvalue(L, arg);
    return 1;
  }
  else {                                              
    size_t len;
    const char *s = gvts_tolstring(L, arg, &len);
    return (s != NULL && gvts_stringtonumber(L, s) == len + 1);
  }
}

static void trymt (gvts_State *L, const char *mtname) {
  gvts_settop(L, 2);                                      
  if (g_unlikely(gvts_type(L, 2) == GVTS_TSTRING ||
                 !gvtsG_getmetafield(L, 2, mtname)))
    gvtsG_error(L, u8"尝试对「%s」和「%s」执行 %s", mtname + 2,
                  gvtsG_typename(L, -2), gvtsG_typename(L, -1));
  gvts_insert(L, -3);                                       
  gvts_call(L, 2, 1);                       
}

static int arith (gvts_State *L, int op, const char *mtname) {
  if (tonum(L, 1) && tonum(L, 2))
    gvts_arith(L, op);                                 
  else
    trymt(L, mtname);
  return 1;
}

static int arith_add (gvts_State *L) {
  return arith(L, GVTS_OPADD, "__add");
}

static int arith_sub (gvts_State *L) {
  return arith(L, GVTS_OPSUB, "__sub");
}

static int arith_mul (gvts_State *L) {
  return arith(L, GVTS_OPMUL, "__mul");
}

static int arith_mod (gvts_State *L) {
  return arith(L, GVTS_OPMOD, "__mod");
}

static int arith_pow (gvts_State *L) {
  return arith(L, GVTS_OPPOW, "__pow");
}

static int arith_div (gvts_State *L) {
  return arith(L, GVTS_OPDIV, "__div");
}

static int arith_idiv (gvts_State *L) {
  return arith(L, GVTS_OPIDIV, "__idiv");
}

static int arith_unm (gvts_State *L) {
  return arith(L, GVTS_OPUNM, "__unm");
}

static const gvtsG_Reg stringmetamethods[] = {
  {"__add", arith_add},
  {"__sub", arith_sub},
  {"__mul", arith_mul},
  {"__mod", arith_mod},
  {"__pow", arith_pow},
  {"__div", arith_div},
  {"__idiv", arith_idiv},
  {"__unm", arith_unm},
  {"__index", NULL},                   
  {NULL, NULL}
};

#endif		       

#define CAP_UNFINISHED	(-1)
#define CAP_POSITION	(-2)

typedef struct MatchState {
  const char *src_init;                             
  const char *src_end;                                   
  const char *p_end;                             
  gvts_State *L;
  int matchdepth;                                                               
  unsigned char level;                                                         
  struct {
    const char *init;
    ptrdiff_t len;
  } capture[GVTS_MAXCAPTURES];
} MatchState;

static const char *match (MatchState *ms, const char *s, const char *p);

#if !defined(MAXCCALLS)
#define MAXCCALLS	200
#endif

#define L_ESC		'%'
#define SPECIALS	"^$*+?.([%-"

static int check_capture (MatchState *ms, int l) {
  l -= '1';
  if (g_unlikely(l < 0 || l >= ms->level ||
                 ms->capture[l].len == CAP_UNFINISHED))
    return gvtsG_error(ms->L, u8"捕获索引 %%%d 无效", l + 1);
  return l;
}

static int capture_to_close (MatchState *ms) {
  int level = ms->level;
  for (level--; level>=0; level--)
    if (ms->capture[level].len == CAP_UNFINISHED) return level;
  return gvtsG_error(ms->L, u8"模式捕获无效");
}

static const char *classend (MatchState *ms, const char *p) {
  switch (*p++) {
    case L_ESC: {
      if (g_unlikely(p == ms->p_end))
        gvtsG_error(ms->L, u8"模式格式错误（以 '%%' 结尾）");
      return p+1;
    }
    case '[': {
      if (*p == '^') p++;
      do {                      
        if (g_unlikely(p == ms->p_end))
          gvtsG_error(ms->L, u8"模式格式错误（缺少 ']'）");
        if (*(p++) == L_ESC && p < ms->p_end)
          p++;                                
      } while (*p != ']');
      return p+1;
    }
    default: {
      return p;
    }
  }
}

static int match_class (int c, int cl) {
  int res;
  switch (tolower(cl)) {
    case 'a' : res = isalpha(c); break;
    case 'c' : res = iscntrl(c); break;
    case 'd' : res = isdigit(c); break;
    case 'g' : res = isgraph(c); break;
    case 'l' : res = islower(c); break;
    case 'p' : res = ispunct(c); break;
    case 's' : res = isspace(c); break;
    case 'u' : res = isupper(c); break;
    case 'w' : res = isalnum(c); break;
    case 'x' : res = isxdigit(c); break;
    case 'z' : res = (c == 0); break;                         
    default: return (cl == c);
  }
  return (islower(cl) ? res : !res);
}

static int matchbracketclass (int c, const char *p, const char *ec) {
  int sig = 1;
  if (*(p+1) == '^') {
    sig = 0;
    p++;                    
  }
  while (++p < ec) {
    if (*p == L_ESC) {
      p++;
      if (match_class(c, uchar(*p)))
        return sig;
    }
    else if ((*(p+1) == '-') && (p+2 < ec)) {
      p+=2;
      if (uchar(*(p-2)) <= c && c <= uchar(*p))
        return sig;
    }
    else if (uchar(*p) == c) return sig;
  }
  return !sig;
}

static int singlematch (MatchState *ms, const char *s, const char *p,
                        const char *ep) {
  if (s >= ms->src_end)
    return 0;
  else {
    int c = uchar(*s);
    switch (*p) {
      case '.': return 1;                        
      case L_ESC: return match_class(c, uchar(*(p+1)));
      case '[': return matchbracketclass(c, p, ep-1);
      default:  return (uchar(*p) == c);
    }
  }
}

static const char *matchbalance (MatchState *ms, const char *s,
                                   const char *p) {
  if (g_unlikely(p >= ms->p_end - 1))
    gvtsG_error(ms->L, u8"模式格式错误（'%%b' 缺少参数）");
  if (*s != *p) return NULL;
  else {
    int b = *p;
    int e = *(p+1);
    int cont = 1;
    while (++s < ms->src_end) {
      if (*s == e) {
        if (--cont == 0) return s+1;
      }
      else if (*s == b) cont++;
    }
  }
  return NULL;                                  
}

static const char *max_expand (MatchState *ms, const char *s,
                                 const char *p, const char *ep) {
  ptrdiff_t i = 0;                                      
  while (singlematch(ms, s + i, p, ep))
    i++;

  while (i>=0) {
    const char *res = match(ms, (s+i), ep+1);
    if (res) return res;
    i--;                                                           
  }
  return NULL;
}

static const char *min_expand (MatchState *ms, const char *s,
                                 const char *p, const char *ep) {
  for (;;) {
    const char *res = match(ms, s, ep+1);
    if (res != NULL)
      return res;
    else if (singlematch(ms, s, p, ep))
      s++;                                    
    else return NULL;
  }
}

static const char *start_capture (MatchState *ms, const char *s,
                                    const char *p, int what) {
  const char *res;
  int level = ms->level;
  if (level >= GVTS_MAXCAPTURES) gvtsG_error(ms->L, u8"捕获数量太多");
  ms->capture[level].init = s;
  ms->capture[level].len = what;
  ms->level = level+1;
  if ((res=match(ms, s, p)) == NULL)                     
    ms->level--;                    
  return res;
}

static const char *end_capture (MatchState *ms, const char *s,
                                  const char *p) {
  int l = capture_to_close(ms);
  const char *res;
  ms->capture[l].len = s - ms->capture[l].init;                     
  if ((res = match(ms, s, p)) == NULL)                     
    ms->capture[l].len = CAP_UNFINISHED;                    
  return res;
}

static const char *match_capture (MatchState *ms, const char *s, int l) {
  size_t len;
  l = check_capture(ms, l);
  len = ms->capture[l].len;
  if ((size_t)(ms->src_end-s) >= len &&
      memcmp(ms->capture[l].init, s, len) == 0)
    return s+len;
  else return NULL;
}

static const char *match (MatchState *ms, const char *s, const char *p) {
  if (g_unlikely(ms->matchdepth-- == 0))
    gvtsG_error(ms->L, u8"模式太复杂");
  init:                                            
  if (p != ms->p_end) {                       
    switch (*p) {
      case '(': {                     
        if (*(p + 1) == ')')                         
          s = start_capture(ms, s, p + 2, CAP_POSITION);
        else
          s = start_capture(ms, s, p + 1, CAP_UNFINISHED);
        break;
      }
      case ')': {                   
        s = end_capture(ms, s, p + 1);
        break;
      }
      case '$': {
        if ((p + 1) != ms->p_end)                                            
          goto dflt;                         
        s = (s == ms->src_end) ? s : NULL;                           
        break;
      }
      case L_ESC: {                                                        
        switch (*(p + 1)) {
          case 'b': {                        
            s = matchbalance(ms, s, p + 2);
            if (s != NULL) {
              p += 4; goto init;                                   
            }                             
            break;
          }
          case 'f': {                 
            const char *ep; char previous;
            p += 2;
            if (g_unlikely(*p != '['))
              gvtsG_error(ms->L, u8"模式中 '%%f' 后缺少 '['");
            ep = classend(ms, p);                              
            previous = (s == ms->src_init) ? '\0' : *(s - 1);
            if (!matchbracketclass(uchar(previous), p, ep - 1) &&
               matchbracketclass(uchar(*s), p, ep - 1)) {
              p = ep; goto init;                                
            }
            s = NULL;                    
            break;
          }
          case '0': case '1': case '2': case '3':
          case '4': case '5': case '6': case '7':
          case '8': case '9': {                                
            s = match_capture(ms, s, uchar(*(p + 1)));
            if (s != NULL) {
              p += 2; goto init;                                  
            }
            break;
          }
          default: goto dflt;
        }
        break;
      }
      default: dflt: {                                          
        const char *ep = classend(ms, p);                                 

        if (!singlematch(ms, s, p, ep)) {
          if (*ep == '*' || *ep == '?' || *ep == '-') {                     
            p = ep + 1; goto init;                                    
          }
          else                        
            s = NULL;            
        }
        else {                    
          switch (*ep) {                              
            case '?': {                
              const char *res;
              if ((res = match(ms, s + 1, ep + 1)) != NULL)
                s = res;
              else {
                p = ep + 1; goto init;                                         
              }
              break;
            }
            case '+':                             
              s++;                            

            case '*':                             
              s = max_expand(ms, s, p, ep);
              break;
            case '-':                                       
              s = min_expand(ms, s, p, ep);
              break;
            default:                 
              s++; p = ep; goto init;                                    
          }
        }
        break;
      }
    }
  }
  ms->matchdepth++;
  return s;
}

static const char *lmemfind (const char *s1, size_t l1,
                               const char *s2, size_t l2) {
  if (l2 == 0) return s1;                                    
  else if (l2 > l1) return NULL;                              
  else {
    const char *init;                                         
    l2--;                                            
    l1 = l1-l2;                                       
    while (l1 > 0 && (init = (const char *)memchr(s1, *s2, l1)) != NULL) {
      init++;                                    
      if (memcmp(init, s2+1, l2) == 0)
        return init-1;
      else {                                          
        l1 -= init-s1;
        s1 = init;
      }
    }
    return NULL;                 
  }
}

static size_t get_onecapture (MatchState *ms, int i, const char *s,
                              const char *e, const char **cap) {
  if (i >= ms->level) {
    if (g_unlikely(i != 0))
      gvtsG_error(ms->L, u8"捕获索引 %%%d 无效", i + 1);
    *cap = s;
    return e - s;
  }
  else {
    ptrdiff_t capl = ms->capture[i].len;
    *cap = ms->capture[i].init;
    if (g_unlikely(capl == CAP_UNFINISHED))
      gvtsG_error(ms->L, u8"未完成的捕获");
    else if (capl == CAP_POSITION)
      gvts_pushinteger(ms->L, (ms->capture[i].init - ms->src_init) + 1);
    return capl;
  }
}

static void push_onecapture (MatchState *ms, int i, const char *s,
                                                    const char *e) {
  const char *cap;
  ptrdiff_t l = get_onecapture(ms, i, s, e, &cap);
  if (l != CAP_POSITION)
    gvts_pushlstring(ms->L, cap, l);

}

static int push_captures (MatchState *ms, const char *s, const char *e) {
  int i;
  int nlevels = (ms->level == 0 && s) ? 1 : ms->level;
  gvtsG_checkstack(ms->L, nlevels, u8"捕获数量太多");
  for (i = 0; i < nlevels; i++)
    push_onecapture(ms, i, s, e);
  return nlevels;                                
}

static int nospecials (const char *p, size_t l) {
  size_t upto = 0;
  do {
    if (strpbrk(p + upto, SPECIALS))
      return 0;                                       
    upto += strlen(p + upto) + 1;                              
  } while (upto <= l);
  return 1;                              
}

static void prepstate (MatchState *ms, gvts_State *L,
                       const char *s, size_t ls, const char *p, size_t lp) {
  ms->L = L;
  ms->matchdepth = MAXCCALLS;
  ms->src_init = s;
  ms->src_end = s + ls;
  ms->p_end = p + lp;
}

static void reprepstate (MatchState *ms) {
  ms->level = 0;
  gvts_assert(ms->matchdepth == MAXCCALLS);
}

static int str_find_aux (gvts_State *L, int find) {
  size_t ls, lp;
  const char *s = gvtsG_checklstring(L, 1, &ls);
  const char *p = gvtsG_checklstring(L, 2, &lp);
  size_t init = posrelatI(gvtsG_optinteger(L, 3, 1), ls) - 1;
  if (init > ls) {                                 
    gvtsG_pushfail(L);                            
    return 1;
  }

  if (find && (gvts_toboolean(L, 4) || nospecials(p, lp))) {

    const char *s2 = lmemfind(s + init, ls - init, p, lp);
    if (s2) {
      gvts_pushinteger(L, (s2 - s) + 1);
      gvts_pushinteger(L, (s2 - s) + lp);
      return 2;
    }
  }
  else {
    MatchState ms;
    const char *s1 = s + init;
    int anchor = (*p == '^');
    if (anchor) {
      p++; lp--;                             
    }
    prepstate(&ms, L, s, ls, p, lp);
    do {
      const char *res;
      reprepstate(&ms);
      if ((res=match(&ms, s1, p)) != NULL) {
        if (find) {
          gvts_pushinteger(L, (s1 - s) + 1);             
          gvts_pushinteger(L, res - s);            
          return push_captures(&ms, NULL, 0) + 2;
        }
        else
          return push_captures(&ms, s1, res);
      }
    } while (s1++ < ms.src_end && !anchor);
  }
  gvtsG_pushfail(L);                 
  return 1;
}

static int str_find (gvts_State *L) {
  return str_find_aux(L, 1);
}

static int str_match (gvts_State *L) {
  return str_find_aux(L, 0);
}

typedef struct GMatchState {
  const char *src;                        
  const char *p;               
  const char *lastmatch;                         
  MatchState ms;                   
} GMatchState;

static int gmatch_aux (gvts_State *L) {
  GMatchState *gm = (GMatchState *)gvts_touserdata(L, gvts_upvalueindex(3));
  const char *src;
  gm->ms.L = L;
  for (src = gm->src; src <= gm->ms.src_end; src++) {
    const char *e;
    reprepstate(&gm->ms);
    if ((e = match(&gm->ms, src, gm->p)) != NULL && e != gm->lastmatch) {
      gm->src = gm->lastmatch = e;
      return push_captures(&gm->ms, src, e);
    }
  }
  return 0;                 
}

static int gmatch (gvts_State *L) {
  size_t ls, lp;
  const char *s = gvtsG_checklstring(L, 1, &ls);
  const char *p = gvtsG_checklstring(L, 2, &lp);
  size_t init = posrelatI(gvtsG_optinteger(L, 3, 1), ls) - 1;
  GMatchState *gm;
  gvts_settop(L, 2);                                                        
  gm = (GMatchState *)gvts_newuserdatauv(L, sizeof(GMatchState), 0);
  if (init > ls)                                 
    init = ls + 1;                                     
  prepstate(&gm->ms, L, s, ls, p, lp);
  gm->src = s + init; gm->p = p; gm->lastmatch = NULL;
  gvts_pushcclosure(L, gmatch_aux, 3);
  return 1;
}

static void add_s (MatchState *ms, gvtsG_Buffer *b, const char *s,
                                                   const char *e) {
  size_t l;
  gvts_State *L = ms->L;
  const char *news = gvts_tolstring(L, 3, &l);
  const char *p;
  while ((p = (char *)memchr(news, L_ESC, l)) != NULL) {
    gvtsG_addlstring(b, news, p - news);
    p++;                
    if (*p == L_ESC)            
      gvtsG_addchar(b, *p);
    else if (*p == '0')            
        gvtsG_addlstring(b, s, e - s);
    else if (isdigit(uchar(*p))) {            
      const char *cap;
      ptrdiff_t resl = get_onecapture(ms, *p - '1', s, e, &cap);
      if (resl == CAP_POSITION)
        gvtsG_addvalue(b);                                          
      else
        gvtsG_addlstring(b, cap, resl);
    }
    else
      gvtsG_error(L, u8"替换字符串中 '%c' 的用法无效", L_ESC);
    l -= p + 1 - news;
    news = p + 1;
  }
  gvtsG_addlstring(b, news, l);
}

static int add_value (MatchState *ms, gvtsG_Buffer *b, const char *s,
                                      const char *e, int tr) {
  gvts_State *L = ms->L;
  switch (tr) {
    case GVTS_TFUNCTION: {                         
      int n;
      gvts_pushvalue(L, 3);                         
      n = push_captures(ms, s, e);                                 
      gvts_call(L, n, 1);               
      break;
    }
    case GVTS_TTABLE: {                       
      push_onecapture(ms, 0, s, e);                                  
      gvts_gettable(L, 3);
      break;
    }
    default: {                                  
      add_s(ms, b, s, e);                               
      return 1;                         
    }
  }
  if (!gvts_toboolean(L, -1)) {                     
    gvts_pop(L, 1);                    
    gvtsG_addlstring(b, s, e - s);                          
    return 0;                  
  }
  else if (g_unlikely(!gvts_isstring(L, -1)))
    return gvtsG_error(L, u8"替换值无效（%s）",
                         gvtsG_typename(L, -1));
  else {
    gvtsG_addvalue(b);                                 
    return 1;                         
  }
}

static int str_gsub (gvts_State *L) {
  size_t srcl, lp;
  const char *src = gvtsG_checklstring(L, 1, &srcl);               
  const char *p = gvtsG_checklstring(L, 2, &lp);               
  const char *lastmatch = NULL;                         
  int tr = gvts_type(L, 3);                        
  gvts_Integer max_s = gvtsG_optinteger(L, 4, srcl + 1);                        
  int anchor = (*p == '^');
  gvts_Integer n = 0;                         
  int changed = 0;                   
  MatchState ms;
  gvtsG_Buffer b;
  gvtsG_argexpected(L, tr == GVTS_TNUMBER || tr == GVTS_TSTRING ||
                   tr == GVTS_TFUNCTION || tr == GVTS_TTABLE, 3,
                      u8"字符串/函数/表");
  gvtsG_buffinit(L, &b);
  if (anchor) {
    p++; lp--;                             
  }
  prepstate(&ms, L, src, srcl, p, lp);
  while (n < max_s) {
    const char *e;
    reprepstate(&ms);                                       
    if ((e = match(&ms, src, p)) != NULL && e != lastmatch) {              
      n++;
      changed = add_value(&ms, &b, src, e, tr) | changed;
      src = lastmatch = e;
    }
    else if (src < ms.src_end)                                     
      gvtsG_addchar(&b, *src++);
    else break;                      
    if (anchor) break;
  }
  if (!changed)                   
    gvts_pushvalue(L, 1);                              
  else {                         
    gvtsG_addlstring(&b, src, ms.src_end-src);
    gvtsG_pushresult(&b);                                    
  }
  gvts_pushinteger(L, n);                               
  return 2;
}

#if !defined(gvts_number2strx)	       

#define SIZELENMOD	(sizeof(GVTS_NUMBER_FRMLEN)/sizeof(char))

#define L_NBFD		((g_floatatt(MANT_DIG) - 1)%4 + 1)

static gvts_Number adddigit (char *buff, int n, gvts_Number x) {
  gvts_Number dd = g_mathop(floor)(x);                                 
  int d = (int)dd;
  buff[n] = (d < 10 ? d + '0' : d - 10 + 'a');                     
  return x - dd;                           
}

static int num2straux (char *buff, int sz, gvts_Number x) {

  if (x != x || x == (gvts_Number)HUGE_VAL || x == -(gvts_Number)HUGE_VAL)
    return g_sprintf(buff, sz, GVTS_NUMBER_FMT, (GVTSI_UACNUMBER)x);
  else if (x == 0) {                    

    return g_sprintf(buff, sz, GVTS_NUMBER_FMT "x0p+0", (GVTSI_UACNUMBER)x);
  }
  else {
    int e;
    gvts_Number m = g_mathop(frexp)(x, &e);                                 
    int n = 0;                       
    if (m < 0) {                           
      buff[n++] = '-';                
      m = -m;                        
    }
    buff[n++] = '0'; buff[n++] = 'x';                
    m = adddigit(buff, n++, m * (1 << L_NBFD));                       
    e -= L_NBFD;                                              
    if (m > 0) {                    
      buff[n++] = gvts_getlocaledecpoint();                       
      do {                                    
        m = adddigit(buff, n++, m * 16);
      } while (m > 0);
    }
    n += g_sprintf(buff + n, sz - n, "p%+d", e);                    
    gvts_assert(n < sz);
    return n;
  }
}

static int gvts_number2strx (gvts_State *L, char *buff, int sz,
                            const char *fmt, gvts_Number x) {
  int n = num2straux(buff, sz, x);
  if (fmt[SIZELENMOD] == 'A') {
    int i;
    for (i = 0; i < n; i++)
      buff[i] = toupper(uchar(buff[i]));
  }
  else if (g_unlikely(fmt[SIZELENMOD] != 'a'))
    return gvtsG_error(L, u8"格式 '%%a'/'%%A' 的修饰符尚未实现");
  return n;
}

#endif				       

#define MAX_ITEMF	(110 + g_floatatt(MAX_10_EXP))

#define MAX_ITEM	120

#if !defined(L_FMTFLAGSF)

#define L_FMTFLAGSF	"-+#0 "

#define L_FMTFLAGSX	"-#0"

#define L_FMTFLAGSI	"-+0 "

#define L_FMTFLAGSU	"-0"

#define L_FMTFLAGSC	"-"

#endif

#define MAX_FORMAT	32

static void addquoted (gvtsG_Buffer *b, const char *s, size_t len) {
  gvtsG_addchar(b, '"');
  while (len--) {
    if (*s == '"' || *s == '\\' || *s == '\n') {
      gvtsG_addchar(b, '\\');
      gvtsG_addchar(b, *s);
    }
    else if (iscntrl(uchar(*s))) {
      char buff[10];
      if (!isdigit(uchar(*(s+1))))
        g_sprintf(buff, sizeof(buff), "\\%d", (int)uchar(*s));
      else
        g_sprintf(buff, sizeof(buff), "\\%03d", (int)uchar(*s));
      gvtsG_addstring(b, buff);
    }
    else
      gvtsG_addchar(b, *s);
    s++;
  }
  gvtsG_addchar(b, '"');
}

static int quotefloat (gvts_State *L, char *buff, gvts_Number n) {
  const char *s;                                     
  if (n == (gvts_Number)HUGE_VAL)            
    s = "1e9999";
  else if (n == -(gvts_Number)HUGE_VAL)             
    s = "-1e9999";
  else if (n != n)            
    s = "(0/0)";
  else {                                    
    int  nb = gvts_number2strx(L, buff, MAX_ITEM,
                                 "%" GVTS_NUMBER_FRMLEN "a", n);

    if (memchr(buff, '.', nb) == NULL) {               
      char point = gvts_getlocaledecpoint();                        
      char *ppoint = (char *)memchr(buff, point, nb);
      if (ppoint) *ppoint = '.';                          
    }
    return nb;
  }

  return g_sprintf(buff, MAX_ITEM, "%s", s);
}

static void addliteral (gvts_State *L, gvtsG_Buffer *b, int arg) {
  switch (gvts_type(L, arg)) {
    case GVTS_TSTRING: {
      size_t len;
      const char *s = gvts_tolstring(L, arg, &len);
      addquoted(b, s, len);
      break;
    }
    case GVTS_TNUMBER: {
      char *buff = gvtsG_prepbuffsize(b, MAX_ITEM);
      int nb;
      if (!gvts_isinteger(L, arg))              
        nb = quotefloat(L, buff, gvts_tonumber(L, arg));
      else {                
        gvts_Integer n = gvts_tointeger(L, arg);
        const char *format = (n == GVTS_MININTEGER)                    
                           ? "0x%" GVTS_INTEGER_FRMLEN "x"               
                           : GVTS_INTEGER_FMT;                               
        nb = g_sprintf(buff, MAX_ITEM, format, (GVTSI_UACINT)n);
      }
      gvtsG_addsize(b, nb);
      break;
    }
    case GVTS_TNIL: case GVTS_TBOOLEAN: {
      gvtsG_tolstring(L, arg, NULL);
      gvtsG_addvalue(b);
      break;
    }
    default: {
      gvtsG_argerror(L, arg, u8"值没有字面形式");
    }
  }
}

static const char *get2digits (const char *s) {
  if (isdigit(uchar(*s))) {
    s++;
    if (isdigit(uchar(*s))) s++;                          
  }
  return s;
}

static void checkformat (gvts_State *L, const char *form, const char *flags,
                                       int precision) {
  const char *spec = form + 1;                
  spec += strspn(spec, flags);                  
  if (*spec != '0') {                                     
    spec = get2digits(spec);                  
    if (*spec == '.' && precision) {
      spec++;
      spec = get2digits(spec);                      
    }
  }
  if (!isalpha(uchar(*spec)))                              
    gvtsG_error(L, u8"转换格式说明符无效：'%s'", form);
}

static const char *getformat (gvts_State *L, const char *strfrmt,
                                            char *form) {

  size_t len = strspn(strfrmt, L_FMTFLAGSF "123456789.");
  len++;                                                          

  if (len >= MAX_FORMAT - 10)
    gvtsG_error(L, u8"格式无效（太长）");
  *(form++) = '%';
  memcpy(form, strfrmt, len * sizeof(char));
  *(form + len) = '\0';
  return strfrmt + len - 1;
}

static void addlenmod (char *form, const char *lenmod) {
  size_t l = strlen(form);
  size_t lm = strlen(lenmod);
  char spec = form[l - 1];
  strcpy(form + l - 1, lenmod);
  form[l + lm - 1] = spec;
  form[l + lm] = '\0';
}

static int str_format (gvts_State *L) {
  int top = gvts_gettop(L);
  int arg = 1;
  size_t sfl;
  const char *strfrmt = gvtsG_checklstring(L, arg, &sfl);
  const char *strfrmt_end = strfrmt+sfl;
  const char *flags;
  gvtsG_Buffer b;
  gvtsG_buffinit(L, &b);
  while (strfrmt < strfrmt_end) {
    if (*strfrmt != L_ESC)
      gvtsG_addchar(&b, *strfrmt++);
    else if (*++strfrmt == L_ESC)
      gvtsG_addchar(&b, *strfrmt++);          
    else {                  
      char form[MAX_FORMAT];                                    
      int maxitem = MAX_ITEM;                                     
      char *buff = gvtsG_prepbuffsize(&b, maxitem);                     
      int nb = 0;                                 
      if (++arg > top)
        return gvtsG_argerror(L, arg, u8"没有值");
      strfrmt = getformat(L, strfrmt, form);
      switch (*strfrmt++) {
        case 'c': {
          checkformat(L, form, L_FMTFLAGSC, 0);
          nb = g_sprintf(buff, maxitem, form, (int)gvtsG_checkinteger(L, arg));
          break;
        }
        case 'd': case 'i':
          flags = L_FMTFLAGSI;
          goto intcase;
        case 'u':
          flags = L_FMTFLAGSU;
          goto intcase;
        case 'o': case 'x': case 'X':
          flags = L_FMTFLAGSX;
         intcase: {
          gvts_Integer n = gvtsG_checkinteger(L, arg);
          checkformat(L, form, flags, 1);
          addlenmod(form, GVTS_INTEGER_FRMLEN);
          nb = g_sprintf(buff, maxitem, form, (GVTSI_UACINT)n);
          break;
        }
        case 'a': case 'A':
          checkformat(L, form, L_FMTFLAGSF, 1);
          addlenmod(form, GVTS_NUMBER_FRMLEN);
          nb = gvts_number2strx(L, buff, maxitem, form,
                                  gvtsG_checknumber(L, arg));
          break;
        case 'f':
          maxitem = MAX_ITEMF;                            
          buff = gvtsG_prepbuffsize(&b, maxitem);

        case 'e': case 'E': case 'g': case 'G': {
          gvts_Number n = gvtsG_checknumber(L, arg);
          checkformat(L, form, L_FMTFLAGSF, 1);
          addlenmod(form, GVTS_NUMBER_FRMLEN);
          nb = g_sprintf(buff, maxitem, form, (GVTSI_UACNUMBER)n);
          break;
        }
        case 'p': {
          const void *p = gvts_topointer(L, arg);
          checkformat(L, form, L_FMTFLAGSC, 0);
          if (p == NULL) {                                                 
            p = "(null)";              
            form[strlen(form) - 1] = 's';                             
          }
          nb = g_sprintf(buff, maxitem, form, p);
          break;
        }
        case 'q': {
          if (form[2] != '\0')                  
            return gvtsG_error(L, u8"说明符 '%%q' 不能带修饰符");
          addliteral(L, &b, arg);
          break;
        }
        case 's': {
          size_t l;
          const char *s = gvtsG_tolstring(L, arg, &l);
          if (form[2] == '\0')                     
            gvtsG_addvalue(&b);                          
          else {
            gvtsG_argcheck(L, l == strlen(s), arg, u8"字符串包含零字符");
            checkformat(L, form, L_FMTFLAGSC, 1);
            if (strchr(form, '.') == NULL && l >= 100) {

              gvtsG_addvalue(&b);                          
            }
            else {                                     
              nb = g_sprintf(buff, maxitem, form, s);
              gvts_pop(L, 1);                                           
            }
          }
          break;
        }
        default: {                                
          return gvtsG_error(L, u8"无效转换 '%s' 用于 'format'", form);
        }
      }
      gvts_assert(nb < maxitem);
      gvtsG_addsize(&b, nb);
    }
  }
  gvtsG_pushresult(&b);
  return 1;
}

#if !defined(GVTSG_PACKPADBYTE)
#define GVTSG_PACKPADBYTE		0x00
#endif

#define MAXINTSIZE	16

#define NB	CHAR_BIT

#define MC	((1 << NB) - 1)

#define SZINT	((int)sizeof(gvts_Integer))

static const union {
  int dummy;
  char little;                                         
} nativeendian = {1};

typedef struct Header {
  gvts_State *L;
  int islittle;
  int maxalign;
} Header;

typedef enum KOption {
  Kint,		                     
  Kuint,	                       
  Kfloat,	                                             
  Knumber,	                                         
  Kdouble,	                                             
  Kchar,	                          
  Kstring,	                                  
  Kzstr,	                             
  Kpadding,	             
  Kpaddalign,	                           
  Knop		                                     
} KOption;

static int digit (int c) { return '0' <= c && c <= '9'; }

static int getnum (const char **fmt, int df) {
  if (!digit(**fmt))                  
    return df;                            
  else {
    int a = 0;
    do {
      a = a*10 + (*((*fmt)++) - '0');
    } while (digit(**fmt) && a <= ((int)MAXSIZE - 9)/10);
    return a;
  }
}

static int getnumlimit (Header *h, const char **fmt, int df) {
  int sz = getnum(fmt, df);
  if (g_unlikely(sz > MAXINTSIZE || sz <= 0))
    return gvtsG_error(h->L, u8"整型大小（%d）超出范围 [1,%d]",
                            sz, MAXINTSIZE);
  return sz;
}

static void initheader (gvts_State *L, Header *h) {
  h->L = L;
  h->islittle = nativeendian.little;
  h->maxalign = 1;
}

static KOption getoption (Header *h, const char **fmt, int *size) {

  struct cD { char c; union { GVTSI_MAXALIGN; } u; };
  int opt = *((*fmt)++);
  *size = 0;               
  switch (opt) {
    case 'b': *size = sizeof(char); return Kint;
    case 'B': *size = sizeof(char); return Kuint;
    case 'h': *size = sizeof(short); return Kint;
    case 'H': *size = sizeof(short); return Kuint;
    case 'l': *size = sizeof(long); return Kint;
    case 'L': *size = sizeof(long); return Kuint;
    case 'j': *size = sizeof(gvts_Integer); return Kint;
    case 'J': *size = sizeof(gvts_Integer); return Kuint;
    case 'T': *size = sizeof(size_t); return Kuint;
    case 'f': *size = sizeof(float); return Kfloat;
    case 'n': *size = sizeof(gvts_Number); return Knumber;
    case 'd': *size = sizeof(double); return Kdouble;
    case 'i': *size = getnumlimit(h, fmt, sizeof(int)); return Kint;
    case 'I': *size = getnumlimit(h, fmt, sizeof(int)); return Kuint;
    case 's': *size = getnumlimit(h, fmt, sizeof(size_t)); return Kstring;
    case 'c':
      *size = getnum(fmt, -1);
      if (g_unlikely(*size == -1))
        gvtsG_error(h->L, u8"格式选项 'c' 缺少大小");
      return Kchar;
    case 'z': return Kzstr;
    case 'x': *size = 1; return Kpadding;
    case 'X': return Kpaddalign;
    case ' ': break;
    case '<': h->islittle = 1; break;
    case '>': h->islittle = 0; break;
    case '=': h->islittle = nativeendian.little; break;
    case '!': {
      const int maxalign = offsetof(struct cD, u);
      h->maxalign = getnumlimit(h, fmt, maxalign);
      break;
    }
    default: gvtsG_error(h->L, u8"格式选项 '%c' 无效", opt);
  }
  return Knop;
}

static KOption getdetails (Header *h, size_t totalsize,
                           const char **fmt, int *psize, int *ntoalign) {
  KOption opt = getoption(h, fmt, psize);
  int align = *psize;                                       
  if (opt == Kpaddalign) {                                                
    if (**fmt == '\0' || getoption(h, fmt, &align) == Kchar || align == 0)
      gvtsG_argerror(h->L, 1, u8"选项 'X' 的下一个选项无效");
  }
  if (align <= 1 || opt == Kchar)                          
    *ntoalign = 0;
  else {
    if (align > h->maxalign)                                 
      align = h->maxalign;
    if (g_unlikely((align & (align - 1)) != 0))                         
      gvtsG_argerror(h->L, 1, u8"格式要求的对齐不是 2 的幂");
    *ntoalign = (align - (int)(totalsize & (align - 1))) & (align - 1);
  }
  return opt;
}

static void packint (gvtsG_Buffer *b, gvts_Unsigned n,
                     int islittle, int size, int neg) {
  char *buff = gvtsG_prepbuffsize(b, size);
  int i;
  buff[islittle ? 0 : size - 1] = (char)(n & MC);                  
  for (i = 1; i < size; i++) {
    n >>= NB;
    buff[islittle ? i : size - 1 - i] = (char)(n & MC);
  }
  if (neg && size > SZINT) {                                            
    for (i = SZINT; i < size; i++)                           
      buff[islittle ? i : size - 1 - i] = (char)MC;
  }
  gvtsG_addsize(b, size);                            
}

static void copywithendian (char *dest, const char *src,
                            int size, int islittle) {
  if (islittle == nativeendian.little)
    memcpy(dest, src, size);
  else {
    dest += size - 1;
    while (size-- != 0)
      *(dest--) = *(src++);
  }
}

static int str_pack (gvts_State *L) {
  gvtsG_Buffer b;
  Header h;
  const char *fmt = gvtsG_checkstring(L, 1);                     
  int arg = 1;                                
  size_t totalsize = 0;                                       
  initheader(L, &h);
  gvts_pushnil(L);                                                     
  gvtsG_buffinit(L, &b);
  while (*fmt != '\0') {
    int size, ntoalign;
    KOption opt = getdetails(&h, totalsize, &fmt, &size, &ntoalign);
    totalsize += ntoalign + size;
    while (ntoalign-- > 0)
     gvtsG_addchar(&b, GVTSG_PACKPADBYTE);                      
    arg++;
    switch (opt) {
      case Kint: {                       
        gvts_Integer n = gvtsG_checkinteger(L, arg);
        if (size < SZINT) {                            
          gvts_Integer lim = (gvts_Integer)1 << ((size * NB) - 1);
          gvtsG_argcheck(L, -lim <= n && n < lim, arg, u8"整数溢出");
        }
        packint(&b, (gvts_Unsigned)n, h.islittle, size, (n < 0));
        break;
      }
      case Kuint: {                         
        gvts_Integer n = gvtsG_checkinteger(L, arg);
        if (size < SZINT)                            
          gvtsG_argcheck(L, (gvts_Unsigned)n < ((gvts_Unsigned)1 << (size * NB)),
                           arg, u8"无符号溢出");
        packint(&b, (gvts_Unsigned)n, h.islittle, size, 0);
        break;
      }
      case Kfloat: {               
        float f = (float)gvtsG_checknumber(L, arg);                    
        char *buff = gvtsG_prepbuffsize(&b, sizeof(f));

        copywithendian(buff, (char *)&f, sizeof(f), h.islittle);
        gvtsG_addsize(&b, size);
        break;
      }
      case Knumber: {                 
        gvts_Number f = gvtsG_checknumber(L, arg);                    
        char *buff = gvtsG_prepbuffsize(&b, sizeof(f));

        copywithendian(buff, (char *)&f, sizeof(f), h.islittle);
        gvtsG_addsize(&b, size);
        break;
      }
      case Kdouble: {                
        double f = (double)gvtsG_checknumber(L, arg);                    
        char *buff = gvtsG_prepbuffsize(&b, sizeof(f));

        copywithendian(buff, (char *)&f, sizeof(f), h.islittle);
        gvtsG_addsize(&b, size);
        break;
      }
      case Kchar: {                         
        size_t len;
        const char *s = gvtsG_checklstring(L, arg, &len);
        gvtsG_argcheck(L, len <= (size_t)size, arg,
                         u8"字符串长度超出指定大小");
        gvtsG_addlstring(&b, s, len);                  
        while (len++ < (size_t)size)                       
          gvtsG_addchar(&b, GVTSG_PACKPADBYTE);
        break;
      }
      case Kstring: {                                 
        size_t len;
        const char *s = gvtsG_checklstring(L, arg, &len);
        gvtsG_argcheck(L, size >= (int)sizeof(size_t) ||
                         len < ((size_t)1 << (size * NB)),
                         arg, u8"字符串长度不适合指定大小");
        packint(&b, (gvts_Unsigned)len, h.islittle, size, 0);                   
        gvtsG_addlstring(&b, s, len);
        totalsize += len;
        break;
      }
      case Kzstr: {                              
        size_t len;
        const char *s = gvtsG_checklstring(L, arg, &len);
        gvtsG_argcheck(L, strlen(s) == len, arg, u8"字符串包含零字符");
        gvtsG_addlstring(&b, s, len);
        gvtsG_addchar(&b, '\0');                           
        totalsize += len + 1;
        break;
      }
      case Kpadding: gvtsG_addchar(&b, GVTSG_PACKPADBYTE);                   
      case Kpaddalign: case Knop:
        arg--;                      
        break;
    }
  }
  gvtsG_pushresult(&b);
  return 1;
}

static int str_packsize (gvts_State *L) {
  Header h;
  const char *fmt = gvtsG_checkstring(L, 1);                     
  size_t totalsize = 0;                                       
  initheader(L, &h);
  while (*fmt != '\0') {
    int size, ntoalign;
    KOption opt = getdetails(&h, totalsize, &fmt, &size, &ntoalign);
    gvtsG_argcheck(L, opt != Kstring && opt != Kzstr, 1,
                     u8"变长格式");
    size += ntoalign;                                  
    gvtsG_argcheck(L, totalsize <= MAXSIZE - size, 1,
                     u8"格式结果太大");
    totalsize += size;
  }
  gvts_pushinteger(L, (gvts_Integer)totalsize);
  return 1;
}

static gvts_Integer unpackint (gvts_State *L, const char *str,
                              int islittle, int size, int issigned) {
  gvts_Unsigned res = 0;
  int i;
  int limit = (size  <= SZINT) ? size : SZINT;
  for (i = limit - 1; i >= 0; i--) {
    res <<= NB;
    res |= (gvts_Unsigned)(unsigned char)str[islittle ? i : size - 1 - i];
  }
  if (size < SZINT) {                                           
    if (issigned) {                             
      gvts_Unsigned mask = (gvts_Unsigned)1 << (size*NB - 1);
      res = ((res ^ mask) - mask);                         
    }
  }
  else if (size > SZINT) {                               
    int mask = (!issigned || (gvts_Integer)res >= 0) ? 0 : MC;
    for (i = limit; i < size; i++) {
      if (g_unlikely((unsigned char)str[islittle ? i : size - 1 - i] != mask))
        gvtsG_error(L, u8"%d 字节整数无法放入 Gvts 整数", size);
    }
  }
  return (gvts_Integer)res;
}

static int str_unpack (gvts_State *L) {
  Header h;
  const char *fmt = gvtsG_checkstring(L, 1);
  size_t ld;
  const char *data = gvtsG_checklstring(L, 2, &ld);
  size_t pos = posrelatI(gvtsG_optinteger(L, 3, 1), ld) - 1;
  int n = 0;                         
  gvtsG_argcheck(L, pos <= ld, 3, u8"起始位置超出字符串范围");
  initheader(L, &h);
  while (*fmt != '\0') {
    int size, ntoalign;
    KOption opt = getdetails(&h, pos, &fmt, &size, &ntoalign);
    gvtsG_argcheck(L, (size_t)ntoalign + size <= ld - pos, 2,
                    u8"数据字符串太短");
    pos += ntoalign;                      

    gvtsG_checkstack(L, 2, u8"结果太多");
    n++;
    switch (opt) {
      case Kint:
      case Kuint: {
        gvts_Integer res = unpackint(L, data + pos, h.islittle, size,
                                       (opt == Kint));
        gvts_pushinteger(L, res);
        break;
      }
      case Kfloat: {
        float f;
        copywithendian((char *)&f, data + pos, sizeof(f), h.islittle);
        gvts_pushnumber(L, (gvts_Number)f);
        break;
      }
      case Knumber: {
        gvts_Number f;
        copywithendian((char *)&f, data + pos, sizeof(f), h.islittle);
        gvts_pushnumber(L, f);
        break;
      }
      case Kdouble: {
        double f;
        copywithendian((char *)&f, data + pos, sizeof(f), h.islittle);
        gvts_pushnumber(L, (gvts_Number)f);
        break;
      }
      case Kchar: {
        gvts_pushlstring(L, data + pos, size);
        break;
      }
      case Kstring: {
        size_t len = (size_t)unpackint(L, data + pos, h.islittle, size, 0);
        gvtsG_argcheck(L, len <= ld - pos - size, 2, u8"数据字符串太短");
        gvts_pushlstring(L, data + pos + size, len);
        pos += len;                   
        break;
      }
      case Kzstr: {
        size_t len = strlen(data + pos);
        gvtsG_argcheck(L, pos + len < ld, 2,
                         u8"格式 'z' 的字符串未结束");
        gvts_pushlstring(L, data + pos, len);
        pos += len + 1;                                   
        break;
      }
      case Kpaddalign: case Kpadding: case Knop:
        n--;                      
        break;
    }
    pos += size;
  }
  gvts_pushinteger(L, pos + 1);                     
  return n + 1;
}

static const gvtsG_Reg strlib[] = {
  {"byte", str_byte},
  {"char", str_char},
  {"dump", str_dump},
  {"find", str_find},
  {"format", str_format},
  {"gmatch", gmatch},
  {"gsub", str_gsub},
  {"len", str_len},
  {"lower", str_lower},
  {"match", str_match},
  {"rep", str_rep},
  {"reverse", str_reverse},
  {"sub", str_sub},
  {"upper", str_upper},
  {"pack", str_pack},
  {"packsize", str_packsize},
  {"unpack", str_unpack},
  {NULL, NULL}
};

static void createmetatable (gvts_State *L) {

  gvtsG_newlibtable(L, stringmetamethods);
  gvtsG_setfuncs(L, stringmetamethods, 0);
  gvts_pushliteral(L, "");                    
  gvts_pushvalue(L, -2);                  
  gvts_setmetatable(L, -2);                                          
  gvts_pop(L, 1);                        
  gvts_pushvalue(L, -2);                          
  gvts_setfield(L, -2, "__index");                                  
  gvts_pop(L, 1);                     
}

GVTSMOD_API int gvtsopen_string (gvts_State *L) {
  gvtsG_newlib(L, strlib);
  createmetatable(L);
  return 1;
}
