#define llex_c
#define GVTS_CORE

#include "Gprefix.h"

#include <locale.h>
#include <string.h>

#include "gvts.h"

#include "Gctype.h"
#include "Gdebug.h"
#include "Gdo.h"
#include "Ggc.h"
#include "Glex.h"
#include "Gobject.h"
#include "Gparser.h"
#include "Gstate.h"
#include "Gstring.h"
#include "Gtable.h"
#include "Gzio.h"

#define next(ls)	(ls->current = gv_nextchar(ls))

#define currIsNewline(ls)	(ls->current == '\n' || ls->current == '\r')

static int gv_nextchar (LexState *ls) {
  ZIO *z = ls->z;
  int c = zgetc(z);
  if (ls->fwidthOff) return c;
  if (c != 0xEF && c != 0xE2 && c != 0xE3) return c;
  size_t sn = z->n;
  const char *sp = z->p;
  int c2 = zgetc(z);
  int c3 = zgetc(z);
  int mapped = -1;
  if (c == 0xEF && (c2 == 0xBC || c2 == 0xBD)) {
    int cp = 0xFF00 + (c2 - 0xBC) * 0x40 + (c3 - 0x80);
    if (cp >= 0xFF01 && cp <= 0xFF5E) mapped = cp - 0xFEE0;
  }
  else if (c == 0xE2 && c2 == 0x80) {
    if (c3 == 0x9C || c3 == 0x9D) mapped = '"';
    else if (c3 == 0x98 || c3 == 0x99) mapped = '\'';
  }
  else if (c == 0xE3 && c2 == 0x80) {
    if (c3 == 0x80) mapped = ' ';
    else if (c3 == 0x81) mapped = ',';
    else if (c3 == 0x82) mapped = '.';
    else if (c3 == 0x8C || c3 == 0x8D || c3 == 0x8E || c3 == 0x8F) mapped = '"';
  }
  if (mapped >= 0) return mapped;
  z->n = sn;
  z->p = sp;
  return c;
}

static const char *const gvtsX_tokens [] = {
    "且", "跳出", "执行", "否则", "否则如果",
    "假", "循环", "函数", "跳转", "如果",
    "在", "局部", "空", "非", "或", "重复",
    "返回", "那么", "真", "直到", "当", "继续",
    "全局",
    "当二维开始时",
    "当三维开始时",
    "//", "..", "...", "==", ">=", "<=", "~=",
    "<<", ">>", "::", "<eof>",
    "<number>", "<integer>", "<name>", "<string>"
};

#define save_and_next(ls) (save(ls, ls->current), next(ls))

static g_noret lexerror (LexState *ls, const char *msg, int token);

static void save (LexState *ls, int c) {
  Mbuffer *b = ls->buff;
  if (gvtsZ_bufflen(b) + 1 > gvtsZ_sizebuffer(b)) {
    size_t newsize;
    if (gvtsZ_sizebuffer(b) >= MAX_SIZE/2)
      lexerror(ls, "lexical element too long", 0);
    newsize = gvtsZ_sizebuffer(b) * 2;
    gvtsZ_resizebuffer(ls->L, b, newsize);
  }
  b->buffer[gvtsZ_bufflen(b)++] = cast_char(c);
}

void gvtsX_init (gvts_State *L) {
  int i;
  TString *e = gvtsS_newliteral(L, GVTS_ENV);                       
  gvtsC_fix(L, obj2gco(e));                               
  for (i=0; i<NUM_RESERVED; i++) {
    TString *ts = gvtsS_new(L, gvtsX_tokens[i]);
    gvtsC_fix(L, obj2gco(ts));                                          
    ts->extra = cast_byte(i+1);                     
  }
}

const char *gvtsX_token2str (LexState *ls, int token) {
  if (token < FIRST_RESERVED) {                            
    if (lisprint(token))
      return gvtsO_pushfstring(ls->L, "'%c'", token);
    else                         
      return gvtsO_pushfstring(ls->L, "'<\\%d>'", token);
  }
  else {
    const char *s = gvtsX_tokens[token - FIRST_RESERVED];
    if (token < TK_EOS)                                                  
      return gvtsO_pushfstring(ls->L, "'%s'", s);
    else                                    
      return s;
  }
}

static const char *txtToken (LexState *ls, int token) {
  switch (token) {
    case TK_NAME: case TK_STRING:
    case TK_FLT: case TK_INT:
      save(ls, '\0');
      return gvtsO_pushfstring(ls->L, "'%s'", gvtsZ_buffer(ls->buff));
    default:
      return gvtsX_token2str(ls, token);
  }
}

static g_noret lexerror (LexState *ls, const char *msg, int token) {
  msg = gvtsG_addinfo(ls->L, msg, ls->source, ls->linenumber);
  if (token)
    gvtsO_pushfstring(ls->L, u8"%s，靠近 %s", msg, txtToken(ls, token));
  gvtsD_throw(ls->L, GVTS_ERRSYNTAX);
}

g_noret gvtsX_syntaxerror (LexState *ls, const char *msg) {
  lexerror(ls, msg, ls->t.token);
}

TString *gvtsX_newstring (LexState *ls, const char *str, size_t l) {
  gvts_State *L = ls->L;
  TString *ts = gvtsS_newlstr(L, str, l);                         
  const TValue *o = gvtsH_getstr(ls->h, ts);
  if (!ttisnil(o))                               
    ts = keystrval(nodefromval(o));                      
  else {                      
    TValue *stv = s2v(L->top.p++);                                      
    setsvalue(L, stv, ts);                                     
    gvtsH_finishset(L, ls->h, stv, o, stv);                          

    gvtsC_checkGC(L);
    L->top.p--;                                
  }
  return ts;
}

static void inclinenumber (LexState *ls) {
  int old = ls->current;
  gvts_assert(currIsNewline(ls));
  next(ls);                         
  if (currIsNewline(ls) && ls->current != old)
    next(ls);                             
  if (++ls->linenumber >= MAX_INT)
    lexerror(ls, "chunk has too many lines", 0);
}

void gvtsX_setinput (gvts_State *L, LexState *ls, ZIO *z, TString *source,
                    int firstchar) {
  ls->t.token = 0;
  ls->L = L;
  ls->current = firstchar;
  ls->lookahead.token = TK_EOS;                           
  ls->z = z;
  ls->fs = NULL;
  ls->linenumber = 1;
  ls->lastline = 1;
  ls->source = source;
  ls->envn = gvtsS_newliteral(L, GVTS_ENV);                    
  ls->fwidthOff = 0;
  gvtsZ_resizebuffer(ls->L, ls->buff, GVTS_MINBUFFER);                         
}

static int check_next1 (LexState *ls, int c) {
  if (ls->current == c) {
    next(ls);
    return 1;
  }
  else return 0;
}

static int check_next2 (LexState *ls, const char *set) {
  gvts_assert(set[2] == '\0');
  if (ls->current == set[0] || ls->current == set[1]) {
    save_and_next(ls);
    return 1;
  }
  else return 0;
}

static int read_numeral (LexState *ls, SemInfo *seminfo) {
  TValue obj;
  const char *expo = "Ee";
  int first = ls->current;
  gvts_assert(lisdigit(ls->current));
  save_and_next(ls);
  if (first == '0' && check_next2(ls, "xX"))                    
    expo = "Pp";
  for (;;) {
    if (check_next2(ls, expo))                      
      check_next2(ls, "-+");                              
    else if (lisxdigit(ls->current) || ls->current == '.')               
      save_and_next(ls);
    else break;
  }
  if (lislalpha(ls->current))                                     
    save_and_next(ls);                      
  save(ls, '\0');
  if (gvtsO_str2num(gvtsZ_buffer(ls->buff), &obj) == 0)                     
    lexerror(ls, "malformed number", TK_FLT);
  if (ttisinteger(&obj)) {
    seminfo->i = ivalue(&obj);
    return TK_INT;
  }
  else {
    gvts_assert(ttisfloat(&obj));
    seminfo->r = fltvalue(&obj);
    return TK_FLT;
  }
}

static size_t skip_sep (LexState *ls) {
  size_t count = 0;
  int s = ls->current;
  gvts_assert(s == '[' || s == ']');
  save_and_next(ls);
  while (ls->current == '=') {
    save_and_next(ls);
    count++;
  }
  return (ls->current == s) ? count + 2
         : (count == 0) ? 1
         : 0;
}

static void read_long_string (LexState *ls, SemInfo *seminfo, size_t sep) {
  int line = ls->linenumber;                                        
  int savedOff = ls->fwidthOff;
  ls->fwidthOff = 1;
  save_and_next(ls);                    
  if (currIsNewline(ls))                                     
    inclinenumber(ls);               
  for (;;) {
    switch (ls->current) {
      case EOZ: {             
        const char *what = (seminfo ? "string" : "comment");
        const char *msg = gvtsO_pushfstring(ls->L,
                     "unfinished long %s (starting at line %d)", what, line);
        lexerror(ls, msg, TK_EOS);
        break;                         
      }
      case ']': {
        if (skip_sep(ls) == sep) {
          save_and_next(ls);                    
          goto endloop;
        }
        break;
      }
      case '\n': case '\r': {
        save(ls, '\n');
        inclinenumber(ls);
        if (!seminfo) gvtsZ_resetbuffer(ls->buff);                           
        break;
      }
      default: {
        if (seminfo) save_and_next(ls);
        else next(ls);
      }
    }
  } endloop:
  ls->fwidthOff = savedOff;
  if (seminfo)
    seminfo->ts = gvtsX_newstring(ls, gvtsZ_buffer(ls->buff) + sep,
                                     gvtsZ_bufflen(ls->buff) - 2 * sep);
}

static void esccheck (LexState *ls, int c, const char *msg) {
  if (!c) {
    if (ls->current != EOZ)
      save_and_next(ls);                                               
    lexerror(ls, msg, TK_STRING);
  }
}

static int gethexa (LexState *ls) {
  save_and_next(ls);
  esccheck (ls, lisxdigit(ls->current), "hexadecimal digit expected");
  return gvtsO_hexavalue(ls->current);
}

static int readhexaesc (LexState *ls) {
  int r = gethexa(ls);
  r = (r << 4) + gethexa(ls);
  gvtsZ_buffremove(ls->buff, 2);                                      
  return r;
}

static unsigned long readutf8esc (LexState *ls) {
  unsigned long r;
  int i = 4;                                                           
  save_and_next(ls);                
  esccheck(ls, ls->current == '{', "missing '{'");
  r = gethexa(ls);                                    
  while (cast_void(save_and_next(ls)), lisxdigit(ls->current)) {
    i++;
    esccheck(ls, r <= (0x7FFFFFFFu >> 4), "UTF-8 value too large");
    r = (r << 4) + gvtsO_hexavalue(ls->current);
  }
  esccheck(ls, ls->current == '}', "missing '}'");
  next(ls);                
  gvtsZ_buffremove(ls->buff, i);                                      
  return r;
}

static void utf8esc (LexState *ls) {
  char buff[UTF8BUFFSZ];
  int n = gvtsO_utf8esc(buff, readutf8esc(ls));
  for (; n > 0; n--)                            
    save(ls, buff[UTF8BUFFSZ - n]);
}

static int readdecesc (LexState *ls) {
  int i;
  int r = 0;                          
  for (i = 0; i < 3 && lisdigit(ls->current); i++) {                           
    r = 10*r + ls->current - '0';
    save_and_next(ls);
  }
  esccheck(ls, r <= UCHAR_MAX, "decimal escape too large");
  gvtsZ_buffremove(ls->buff, i);                                      
  return r;
}

static int gv_isquotechar (int c, int del) {
  if (c == del) return 1;
  if (del == '"' && (c == 0x201C || c == 0x201D)) return 1;
  if (del == '\'' && (c == 0x2018 || c == 0x2019)) return 1;
  return 0;
}

static void read_string (LexState *ls, int del, SemInfo *seminfo) {
  int savedOff = ls->fwidthOff;
  ls->fwidthOff = 1;
  save_and_next(ls);
  for (;;) {
    if (ls->current == EOZ || ls->current == del) break;
    if (ls->current == '\n' || ls->current == '\r')
      lexerror(ls, "unfinished string", TK_STRING);
    if (ls->current >= 0x80) {
      int lead = ls->current;
      int cp = lead;
      int need = (lead >= 0xF0) ? 3 : (lead >= 0xE0) ? 2 : 1;
      cp = (need == 3) ? (lead & 0x07) : (need == 2) ? (lead & 0x0F) : (lead & 0x1F);
      int cont[3];
      int valid = 1;
      for (int i = 0; i < need; ++i) {
        cont[i] = zgetc(ls->z);
        if ((cont[i] & 0xC0) != 0x80) { valid = 0; break; }
        cp = (cp << 6) | (cont[i] & 0x3F);
      }
      if (valid && gv_isquotechar(cp, del)) {
        next(ls);
        break;
      }
      if (valid) {
        save(ls, cast_char(lead));
        for (int i = 0; i < need; ++i) save(ls, cast_char(cont[i]));
      }
      next(ls);
      continue;
    }
    switch (ls->current) {
      case EOZ:
        lexerror(ls, "unfinished string", TK_EOS);
        break;                         
      case '\n':
      case '\r':
        lexerror(ls, "unfinished string", TK_STRING);
        break;                         
      case '\\': {                        
        int c;                                   
        save_and_next(ls);                                    
        switch (ls->current) {
          case 'a': c = '\a'; goto read_save;
          case 'b': c = '\b'; goto read_save;
          case 'f': c = '\f'; goto read_save;
          case 'n': c = '\n'; goto read_save;
          case 'r': c = '\r'; goto read_save;
          case 't': c = '\t'; goto read_save;
          case 'v': c = '\v'; goto read_save;
          case 'x': c = readhexaesc(ls); goto read_save;
          case 'u': utf8esc(ls);  goto no_save;
          case '\n': case '\r':
            inclinenumber(ls); c = '\n'; goto only_save;
          case '\\': case '\"': case '\'':
            c = ls->current; goto read_save;
          case EOZ: goto no_save;                                     
          case 'z': {                                    
            gvtsZ_buffremove(ls->buff, 1);                   
            next(ls);                    
            while (lisspace(ls->current)) {
              if (currIsNewline(ls)) inclinenumber(ls);
              else next(ls);
            }
            goto no_save;
          }
          default: {
            esccheck(ls, lisdigit(ls->current), "invalid escape sequence");
            c = readdecesc(ls);                             
            goto only_save;
          }
        }
       read_save:
         next(ls);

       only_save:
         gvtsZ_buffremove(ls->buff, 1);                   
         save(ls, c);

       no_save: break;
      }
      default:
        save_and_next(ls);
    }
  }
  save_and_next(ls);                      
  ls->fwidthOff = savedOff;
  seminfo->ts = gvtsX_newstring(ls, gvtsZ_buffer(ls->buff) + 1,
                                   gvtsZ_bufflen(ls->buff) - 2);
}

static int llex (LexState *ls, SemInfo *seminfo) {
  gvtsZ_resetbuffer(ls->buff);
  for (;;) {
    switch (ls->current) {
      case '\n': case '\r': {                   
        inclinenumber(ls);
        break;
      }
      case ' ': case '\f': case '\t': case '\v': {              
        next(ls);
        break;
      }
      case '-': {                             
        next(ls);
        if (ls->current != '-') return '-';

        next(ls);
        if (ls->current == '[') {                     
          size_t sep = skip_sep(ls);
          gvtsZ_resetbuffer(ls->buff);                                       
          if (sep >= 2) {
            read_long_string(ls, NULL, sep);                         
            gvtsZ_resetbuffer(ls->buff);                                         
            break;
          }
        }

        while (!currIsNewline(ls) && ls->current != EOZ)
          next(ls);                                               
        break;
      }
      case '[': {                                 
        size_t sep = skip_sep(ls);
        if (sep >= 2) {
          read_long_string(ls, seminfo, sep);
          return TK_STRING;
        }
        else if (sep == 0)                                       
          lexerror(ls, "invalid long string delimiter", TK_STRING);
        return '[';
      }
      case '=': {
        next(ls);
        if (check_next1(ls, '=')) return TK_EQ;            
        else return '=';
      }
      case '<': {
        next(ls);
        if (check_next1(ls, '=')) return TK_LE;            
        else if (check_next1(ls, '<')) return TK_SHL;            
        else return '<';
      }
      case '>': {
        next(ls);
        if (check_next1(ls, '=')) return TK_GE;            
        else if (check_next1(ls, '>')) return TK_SHR;            
        else return '>';
      }
      case '/': {
        next(ls);
        if (check_next1(ls, '/')) return TK_IDIV;            
        else return '/';
      }
      case '~': {
        next(ls);
        if (check_next1(ls, '=')) return TK_NE;            
        else return '~';
      }
      case ':': {
        next(ls);
        if (check_next1(ls, ':')) return TK_DBCOLON;            
        else return ':';
      }
      case '"': case '\'': {                             
        read_string(ls, ls->current, seminfo);
        return TK_STRING;
      }
      case '.': {                                   
        save_and_next(ls);
        if (check_next1(ls, '.')) {
          if (check_next1(ls, '.'))
            return TK_DOTS;              
          else return TK_CONCAT;             
        }
        else if (!lisdigit(ls->current)) return '.';
        else return read_numeral(ls, seminfo);
      }
      case '0': case '1': case '2': case '3': case '4':
      case '5': case '6': case '7': case '8': case '9': {
        return read_numeral(ls, seminfo);
      }
      case EOZ: {
        return TK_EOS;
      }
      default: {
        if (lislalpha(ls->current)) {                                    
          TString *ts;
          do {
            save_and_next(ls);
          } while (lislalnum(ls->current));
          ts = gvtsX_newstring(ls, gvtsZ_buffer(ls->buff),
                                  gvtsZ_bufflen(ls->buff));
          seminfo->ts = ts;
          if (isreserved(ts))                      
            return ts->extra - 1 + FIRST_RESERVED;
          else {
            return TK_NAME;
          }
        }
        else {                                                         
          int c = ls->current;
          next(ls);
          return c;
        }
      }
    }
  }
}

void gvtsX_next (LexState *ls) {
  ls->lastline = ls->linenumber;
  if (ls->lookahead.token != TK_EOS) {                                    
    ls->t = ls->lookahead;                    
    ls->lookahead.token = TK_EOS;                        
  }
  else
    ls->t.token = llex(ls, &ls->t.seminfo);                       
}

int gvtsX_lookahead (LexState *ls) {
  gvts_assert(ls->lookahead.token == TK_EOS);
  ls->lookahead.token = llex(ls, &ls->lookahead.seminfo);
  return ls->lookahead.token;
}
