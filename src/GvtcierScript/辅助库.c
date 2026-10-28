#define lauxlib_c
#define GVTS_LIB

#include "Gprefix.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gvts.h"

#include "Gauxlib.h"

#if !defined(MAX_SIZET)

#define MAX_SIZET	((size_t)(~(size_t)0))
#endif

#define LEVELS1	10	                                         
#define LEVELS2	11	                                          

static int findfield (gvts_State *L, int objidx, int level) {
  if (level == 0 || !gvts_istable(L, -1))
    return 0;                 
  gvts_pushnil(L);                         
  while (gvts_next(L, -2)) {                              
    if (gvts_type(L, -2) == GVTS_TSTRING) {                              
      if (gvts_rawequal(L, objidx, -1)) {                     
        gvts_pop(L, 1);                                    
        return 1;
      }
      else if (findfield(L, objidx, level - 1)) {                       

        gvts_pushliteral(L, ".");                                       
        gvts_replace(L, -3);                                       
        gvts_concat(L, 3);                           
        return 1;
      }
    }
    gvts_pop(L, 1);                    
  }
  return 0;                 
}

static int pushglobalfuncname (gvts_State *L, gvts_Debug *ar) {
  int top = gvts_gettop(L);
  gvts_getinfo(L, "f", ar);                     
  gvts_getfield(L, GVTS_REGISTRYINDEX, GVTS_LOADED_TABLE);
  gvtsG_checkstack(L, 6, u8"栈空间不足");                             
  if (findfield(L, top + 1, 2)) {
    const char *name = gvts_tostring(L, -1);
    if (strncmp(name, GVTS_GNAME ".", 3) == 0) {                              
      gvts_pushstring(L, name + 3);                                
      gvts_remove(L, -2);                            
    }
    gvts_copy(L, -1, top + 1);                                 
    gvts_settop(L, top + 1);                                           
    return 1;
  }
  else {
    gvts_settop(L, top);                                        
    return 0;
  }
}

static void pushfuncname (gvts_State *L, gvts_Debug *ar) {
  if (pushglobalfuncname(L, ar)) {                               
    gvts_pushfstring(L, u8"函数 '%s'", gvts_tostring(L, -1));
    gvts_remove(L, -2);                   
  }
  else if (*ar->namewhat != '\0')                                  
    gvts_pushfstring(L, u8"%s '%s'", ar->namewhat, ar->name);              
  else if (*ar->what == 'm')             
      gvts_pushliteral(L, u8"主代码块");
  else if (*ar->what != 'C')                                          
    gvts_pushfstring(L, u8"函数 <%s:%d>", ar->short_src, ar->linedefined);
  else                       
    gvts_pushliteral(L, "?");
}

static int lastlevel (gvts_State *L) {
  gvts_Debug ar;
  int li = 1, le = 1;

  while (gvts_getstack(L, le, &ar)) { li = le; le *= 2; }

  while (li < le) {
    int m = (li + le)/2;
    if (gvts_getstack(L, m, &ar)) li = m + 1;
    else le = m;
  }
  return le - 1;
}

GVTSLIB_API void gvtsG_traceback (gvts_State *L, gvts_State *L1,
                                const char *msg, int level) {
  gvtsG_Buffer b;
  gvts_Debug ar;
  int last = lastlevel(L1);
  int limit2show = (last - level > LEVELS1 + LEVELS2) ? LEVELS1 : -1;
  gvtsG_buffinit(L, &b);
  if (msg) {
    gvtsG_addstring(&b, msg);
    gvtsG_addchar(&b, '\n');
  }
  gvtsG_addstring(&b, u8"栈回溯：");
  while (gvts_getstack(L1, level++, &ar)) {
    if (limit2show-- == 0) {                        
      int n = last - level - LEVELS2 + 1;                                
      gvts_pushfstring(L, u8"\n\t...\t(跳过 %d 层)", n);
      gvtsG_addvalue(&b);                              
      level += n;                               
    }
    else {
      gvts_getinfo(L1, "Slnt", &ar);
      if (ar.currentline <= 0)
        gvts_pushfstring(L, u8"\n\t%s: 于 ", ar.short_src);
      else
        gvts_pushfstring(L, u8"\n\t%s:%d: 于 ", ar.short_src, ar.currentline);
      gvtsG_addvalue(&b);
      pushfuncname(L, &ar);
      gvtsG_addvalue(&b);
      if (ar.istailcall)
        gvtsG_addstring(&b, u8"\n\t(...尾调用...)");
    }
  }
  gvtsG_pushresult(&b);
}

GVTSLIB_API int gvtsG_argerror (gvts_State *L, int arg, const char *extramsg) {
  gvts_Debug ar;
  if (!gvts_getstack(L, 0, &ar))                       
    return gvtsG_error(L, u8"第 %d 个参数错误（%s）", arg, extramsg);
  gvts_getinfo(L, "n", &ar);
  if (strcmp(ar.namewhat, "method") == 0) {
    arg--;                           
    if (arg == 0)                                             
      return gvtsG_error(L, u8"调用「%s」时自身参数有误（%s）",
                           ar.name, extramsg);
  }
  if (ar.name == NULL)
    ar.name = (pushglobalfuncname(L, &ar)) ? gvts_tostring(L, -1) : "?";
  return gvtsG_error(L, u8"调用「%s」时第 %d 个参数错误（%s）",
                        ar.name, arg, extramsg);
}

GVTSLIB_API int gvtsG_typeerror (gvts_State *L, int arg, const char *tname) {
  const char *msg;
  const char *typearg;                                                
  if (gvtsG_getmetafield(L, arg, "__name") == GVTS_TSTRING)
    typearg = gvts_tostring(L, -1);                               
  else if (gvts_type(L, arg) == GVTS_TLIGHTUSERDATA)
    typearg = u8"轻量用户数据";                                 
  else
    typearg = gvtsG_typename(L, arg);                     
  msg = gvts_pushfstring(L, u8"需要%s，实得%s", tname, typearg);
  return gvtsG_argerror(L, arg, msg);
}

static void tag_error (gvts_State *L, int arg, int tag) {
  gvtsG_typeerror(L, arg, gvts_typename(L, tag));
}

GVTSLIB_API void gvtsG_where (gvts_State *L, int level) {
  gvts_Debug ar;
  if (gvts_getstack(L, level, &ar)) {                               
    gvts_getinfo(L, "Sl", &ar);                         
    if (ar.currentline > 0) {                      
      gvts_pushfstring(L, "%s:%d: ", ar.short_src, ar.currentline);
      return;
    }
  }
  gvts_pushfstring(L, "");                                         
}

GVTSLIB_API int gvtsG_error (gvts_State *L, const char *fmt, ...) {
  va_list argp;
  va_start(argp, fmt);
  gvtsG_where(L, 1);
  gvts_pushvfstring(L, fmt, argp);
  va_end(argp);
  gvts_concat(L, 2);
  return gvts_error(L);
}

GVTSLIB_API int gvtsG_fileresult (gvts_State *L, int stat, const char *fname) {
  int en = errno;                                              
  if (stat) {
    gvts_pushboolean(L, 1);
    return 1;
  }
  else {
    const char *msg;
    gvtsG_pushfail(L);
    msg = (en != 0) ? strerror(en) : u8"(无额外信息)";
    if (fname)
      gvts_pushfstring(L, "%s: %s", fname, msg);
    else
      gvts_pushstring(L, msg);
    gvts_pushinteger(L, en);
    return 3;
  }
}

#if !defined(g_inspectstat)	       

#if defined(GVTS_USE_POSIX)

#include <sys/wait.h>

#define g_inspectstat(stat,what)  \
   if (WIFEXITED(stat)) { stat = WEXITSTATUS(stat); } \
   else if (WIFSIGNALED(stat)) { stat = WTERMSIG(stat); what = "signal"; }

#else

#define g_inspectstat(stat,what)             

#endif

#endif				       

GVTSLIB_API int gvtsG_execresult (gvts_State *L, int stat) {
  if (stat != 0 && errno != 0)                              
    return gvtsG_fileresult(L, 0, NULL);
  else {
    const char *what = u8"退出";                           
    g_inspectstat(stat, what);                        
    if (*what == 'e' && stat == 0)                               
      gvts_pushboolean(L, 1);
    else
      gvtsG_pushfail(L);
    gvts_pushstring(L, what);
    gvts_pushinteger(L, stat);
    return 3;                                  
  }
}

GVTSLIB_API int gvtsG_newmetatable (gvts_State *L, const char *tname) {
  if (gvtsG_getmetatable(L, tname) != GVTS_TNIL)                            
    return 0;                                                 
  gvts_pop(L, 1);
  gvts_createtable(L, 0, 2);                        
  gvts_pushstring(L, tname);
  gvts_setfield(L, -2, "__name");                                
  gvts_pushvalue(L, -1);
  gvts_setfield(L, GVTS_REGISTRYINDEX, tname);                                 
  return 1;
}

GVTSLIB_API void gvtsG_setmetatable (gvts_State *L, const char *tname) {
  gvtsG_getmetatable(L, tname);
  gvts_setmetatable(L, -2);
}

GVTSLIB_API void *gvtsG_testudata (gvts_State *L, int ud, const char *tname) {
  void *p = gvts_touserdata(L, ud);
  if (p != NULL) {                            
    if (gvts_getmetatable(L, ud)) {                                 
      gvtsG_getmetatable(L, tname);                             
      if (!gvts_rawequal(L, -1, -2))                     
        p = NULL;                                                
      gvts_pop(L, 2);                              
      return p;
    }
  }
  return NULL;                                                
}

GVTSLIB_API void *gvtsG_checkudata (gvts_State *L, int ud, const char *tname) {
  void *p = gvtsG_testudata(L, ud, tname);
  gvtsG_argexpected(L, p != NULL, ud, tname);
  return p;
}

GVTSLIB_API int gvtsG_checkoption (gvts_State *L, int arg, const char *def,
                                 const char *const lst[]) {
  const char *name = (def) ? gvtsG_optstring(L, arg, def) :
                             gvtsG_checkstring(L, arg);
  int i;
  for (i=0; lst[i]; i++)
    if (strcmp(lst[i], name) == 0)
      return i;
  return gvtsG_argerror(L, arg,
                       gvts_pushfstring(L, u8"选项「%s」无效", name));
}

GVTSLIB_API void gvtsG_checkstack (gvts_State *L, int space, const char *msg) {
  if (g_unlikely(!gvts_checkstack(L, space))) {
    if (msg)
      gvtsG_error(L, u8"栈溢出（%s）", msg);
    else
      gvtsG_error(L, u8"栈溢出");
  }
}

GVTSLIB_API void gvtsG_checktype (gvts_State *L, int arg, int t) {
  if (g_unlikely(gvts_type(L, arg) != t))
    tag_error(L, arg, t);
}

GVTSLIB_API void gvtsG_checkany (gvts_State *L, int arg) {
  if (g_unlikely(gvts_type(L, arg) == GVTS_TNONE))
    gvtsG_argerror(L, arg, u8"需要一个值");
}

GVTSLIB_API const char *gvtsG_checklstring (gvts_State *L, int arg, size_t *len) {
  const char *s = gvts_tolstring(L, arg, len);
  if (g_unlikely(!s)) tag_error(L, arg, GVTS_TSTRING);
  return s;
}

GVTSLIB_API const char *gvtsG_optlstring (gvts_State *L, int arg,
                                        const char *def, size_t *len) {
  if (gvts_isnoneornil(L, arg)) {
    if (len)
      *len = (def ? strlen(def) : 0);
    return def;
  }
  else return gvtsG_checklstring(L, arg, len);
}

GVTSLIB_API gvts_Number gvtsG_checknumber (gvts_State *L, int arg) {
  int isnum;
  gvts_Number d = gvts_tonumberx(L, arg, &isnum);
  if (g_unlikely(!isnum))
    tag_error(L, arg, GVTS_TNUMBER);
  return d;
}

GVTSLIB_API gvts_Number gvtsG_optnumber (gvts_State *L, int arg, gvts_Number def) {
  return gvtsG_opt(L, gvtsG_checknumber, arg, def);
}

static void interror (gvts_State *L, int arg) {
  if (gvts_isnumber(L, arg))
    gvtsG_argerror(L, arg, u8"数字没有整数表示");
  else
    tag_error(L, arg, GVTS_TNUMBER);
}

GVTSLIB_API gvts_Integer gvtsG_checkinteger (gvts_State *L, int arg) {
  int isnum;
  gvts_Integer d = gvts_tointegerx(L, arg, &isnum);
  if (g_unlikely(!isnum)) {
    interror(L, arg);
  }
  return d;
}

GVTSLIB_API gvts_Integer gvtsG_optinteger (gvts_State *L, int arg,
                                                      gvts_Integer def) {
  return gvtsG_opt(L, gvtsG_checkinteger, arg, def);
}

typedef struct UBox {
  void *box;
  size_t bsize;
} UBox;

static void *resizebox (gvts_State *L, int idx, size_t newsize) {
  void *ud;
  gvts_Alloc allocf = gvts_getallocf(L, &ud);
  UBox *box = (UBox *)gvts_touserdata(L, idx);
  void *temp = allocf(ud, box->box, box->bsize, newsize);
  if (g_unlikely(temp == NULL && newsize > 0)) {                         
    gvts_pushliteral(L, u8"内存不足");
    gvts_error(L);                            
  }
  box->box = temp;
  box->bsize = newsize;
  return temp;
}

static int boxgc (gvts_State *L) {
  resizebox(L, 1, 0);
  return 0;
}

static const gvtsG_Reg boxmt[] = {                       
  {"__gc", boxgc},
  {"__close", boxgc},
  {NULL, NULL}
};

static void newbox (gvts_State *L) {
  UBox *box = (UBox *)gvts_newuserdatauv(L, sizeof(UBox), 0);
  box->box = NULL;
  box->bsize = 0;
  if (gvtsG_newmetatable(L, "_UBOX*"))                           
    gvtsG_setfuncs(L, boxmt, 0);                           
  gvts_setmetatable(L, -2);
}

#define buffonstack(B)	((B)->b != (B)->init.b)

#define checkbufferlevel(B,idx)  \
  gvts_assert(buffonstack(B) ? gvts_touserdata(B->L, idx) != NULL  \
                            : gvts_touserdata(B->L, idx) == (void*)B)

static size_t newbuffsize (gvtsG_Buffer *B, size_t sz) {
  size_t newsize = (B->size / 2) * 3;                         
  if (g_unlikely(MAX_SIZET - sz < B->n))                                
    return gvtsG_error(B->L, u8"缓冲区太大");
  if (newsize < B->n + sz)                       
    newsize = B->n + sz;
  return newsize;
}

static char *prepbuffsize (gvtsG_Buffer *B, size_t sz, int boxidx) {
  checkbufferlevel(B, boxidx);
  if (B->size - B->n >= sz)                     
    return B->b + B->n;
  else {
    gvts_State *L = B->L;
    char *newbuff;
    size_t newsize = newbuffsize(B, sz);

    if (buffonstack(B))                                 
      newbuff = (char *)resizebox(L, boxidx, newsize);                 
    else {                  
      gvts_remove(L, boxidx);                          
      newbox(L);                        
      gvts_insert(L, boxidx);                                         
      gvts_toclose(L, boxidx);
      newbuff = (char *)resizebox(L, boxidx, newsize);
      memcpy(newbuff, B->b, B->n * sizeof(char));                             
    }
    B->b = newbuff;
    B->size = newsize;
    return newbuff + B->n;
  }
}

GVTSLIB_API char *gvtsG_prepbuffsize (gvtsG_Buffer *B, size_t sz) {
  return prepbuffsize(B, sz, -1);
}

GVTSLIB_API void gvtsG_addlstring (gvtsG_Buffer *B, const char *s, size_t l) {
  if (l > 0) {                                           
    char *b = prepbuffsize(B, l, -1);
    memcpy(b, s, l * sizeof(char));
    gvtsG_addsize(B, l);
  }
}

GVTSLIB_API void gvtsG_addstring (gvtsG_Buffer *B, const char *s) {
  gvtsG_addlstring(B, s, strlen(s));
}

GVTSLIB_API void gvtsG_pushresult (gvtsG_Buffer *B) {
  gvts_State *L = B->L;
  checkbufferlevel(B, -1);
  gvts_pushlstring(L, B->b, B->n);
  if (buffonstack(B))
    gvts_closeslot(L, -2);                     
  gvts_remove(L, -2);                                                
}

GVTSLIB_API void gvtsG_pushresultsize (gvtsG_Buffer *B, size_t sz) {
  gvtsG_addsize(B, sz);
  gvtsG_pushresult(B);
}

GVTSLIB_API void gvtsG_addvalue (gvtsG_Buffer *B) {
  gvts_State *L = B->L;
  size_t len;
  const char *s = gvts_tolstring(L, -1, &len);
  char *b = prepbuffsize(B, len, -2);
  memcpy(b, s, len * sizeof(char));
  gvtsG_addsize(B, len);
  gvts_pop(L, 1);                  
}

GVTSLIB_API void gvtsG_buffinit (gvts_State *L, gvtsG_Buffer *B) {
  B->L = L;
  B->b = B->init.b;
  B->n = 0;
  B->size = GVTSG_BUFFERSIZE;
  gvts_pushlightuserdata(L, (void*)B);                        
}

GVTSLIB_API char *gvtsG_buffinitsize (gvts_State *L, gvtsG_Buffer *B, size_t sz) {
  gvtsG_buffinit(L, B);
  return prepbuffsize(B, sz, -1);
}

#define freelist	(GVTS_RIDX_LAST + 1)

GVTSLIB_API int gvtsG_ref (gvts_State *L, int t) {
  int ref;
  if (gvts_isnil(L, -1)) {
    gvts_pop(L, 1);                         
    return GVTS_REFNIL;                                          
  }
  t = gvts_absindex(L, t);
  if (gvts_rawgeti(L, t, freelist) == GVTS_TNIL) {                     
    ref = 0;                     
    gvts_pushinteger(L, 0);                                   
    gvts_rawseti(L, t, freelist);                             
  }
  else {                           
    gvts_assert(gvts_isinteger(L, -1));
    ref = (int)gvts_tointeger(L, -1);                         
  }
  gvts_pop(L, 1);                                 
  if (ref != 0) {                         
    gvts_rawgeti(L, t, ref);                           
    gvts_rawseti(L, t, freelist);                              
  }
  else                        
    ref = (int)gvts_rawlen(L, t) + 1;                           
  gvts_rawseti(L, t, ref);
  return ref;
}

GVTSLIB_API void gvtsG_unref (gvts_State *L, int t, int ref) {
  if (ref >= 0) {
    t = gvts_absindex(L, t);
    gvts_rawgeti(L, t, freelist);
    gvts_assert(gvts_isinteger(L, -1));
    gvts_rawseti(L, t, ref);                            
    gvts_pushinteger(L, ref);
    gvts_rawseti(L, t, freelist);                         
  }
}

typedef struct LoadF {
  int n;                                     
  FILE *f;                       
  char buff[BUFSIZ];                             
} LoadF;

static const char *getF (gvts_State *L, void *ud, size_t *size) {
  LoadF *lf = (LoadF *)ud;
  (void)L;                
  if (lf->n > 0) {                                                 
    *size = lf->n;                                             
    lf->n = 0;                                   
  }
  else {                              

    if (feof(lf->f)) return NULL;
    *size = fread(lf->buff, 1, sizeof(lf->buff), lf->f);                  
  }
  return lf->buff;
}

static int errfile (gvts_State *L, const char *what, int fnameindex) {
  int err = errno;
  const char *filename = gvts_tostring(L, fnameindex) + 1;
  if (err != 0)
    gvts_pushfstring(L, u8"无法 %s %s：%s", what, filename, strerror(err));
  else
    gvts_pushfstring(L, u8"无法 %s %s", what, filename);
  gvts_remove(L, fnameindex);
  return GVTS_ERRFILE;
}

static int skipBOM (FILE *f) {
  int c = getc(f);                            
  if (c == 0xEF && getc(f) == 0xBB && getc(f) == 0xBF)                    
    return getc(f);                                       
  else                      
    return c;                              
}

static int skipcomment (FILE *f, int *cp) {
  int c = *cp = skipBOM(f);
  if (c == '#') {                                                  
    do {                       
      c = getc(f);
    } while (c != EOF && c != '\n');
    *cp = getc(f);                                                
    return 1;                           
  }
  else return 0;                  
}

GVTSLIB_API int gvtsG_loadfilex (gvts_State *L, const char *filename,
                                             const char *mode) {
  LoadF lf;
  int status, readstatus;
  int c;
  int fnameindex = gvts_gettop(L) + 1;                                      
  if (filename == NULL) {
    gvts_pushliteral(L, "=stdin");
    lf.f = stdin;
  }
  else {
    gvts_pushfstring(L, "@%s", filename);
    errno = 0;
    lf.f = fopen(filename, "r");
    if (lf.f == NULL) return errfile(L, u8"打开", fnameindex);
  }
  lf.n = 0;
  if (skipcomment(lf.f, &c))                            
    lf.buff[lf.n++] = '\n';                                           
  if (c == GVTS_SIGNATURE[0]) {                    
    lf.n = 0;                               
    if (filename) {                    
      errno = 0;
      lf.f = freopen(filename, "rb", lf.f);                             
      if (lf.f == NULL) return errfile(L, u8"重新打开", fnameindex);
      skipcomment(lf.f, &c);                               
    }
  }
  if (c != EOF)
    lf.buff[lf.n++] = c;                                                
  errno = 0;
  status = gvts_load(L, getF, &lf, gvts_tostring(L, -1), mode);
  readstatus = ferror(lf.f);
  if (filename) fclose(lf.f);                                           
  if (readstatus) {
    gvts_settop(L, fnameindex);                                      
    return errfile(L, u8"读取", fnameindex);
  }
  gvts_remove(L, fnameindex);
  return status;
}

typedef struct LoadS {
  const char *s;
  size_t size;
} LoadS;

static const char *getS (gvts_State *L, void *ud, size_t *size) {
  LoadS *ls = (LoadS *)ud;
  (void)L;                
  if (ls->size == 0) return NULL;
  *size = ls->size;
  ls->size = 0;
  return ls->s;
}

GVTSLIB_API int gvtsG_loadbufferx (gvts_State *L, const char *buff, size_t size,
                                 const char *name, const char *mode) {
  LoadS ls;
  ls.s = buff;
  ls.size = size;
  return gvts_load(L, getS, &ls, name, mode);
}

GVTSLIB_API int gvtsG_loadstring (gvts_State *L, const char *s) {
  return gvtsG_loadbuffer(L, s, strlen(s), s);
}

GVTSLIB_API int gvtsG_getmetafield (gvts_State *L, int obj, const char *event) {
  if (!gvts_getmetatable(L, obj))                     
    return GVTS_TNIL;
  else {
    int tt;
    gvts_pushstring(L, event);
    tt = gvts_rawget(L, -2);
    if (tt == GVTS_TNIL)                         
      gvts_pop(L, 2);                                      
    else
      gvts_remove(L, -2);                             
    return tt;                             
  }
}

GVTSLIB_API int gvtsG_callmeta (gvts_State *L, int obj, const char *event) {
  obj = gvts_absindex(L, obj);
  if (gvtsG_getmetafield(L, obj, event) == GVTS_TNIL)                     
    return 0;
  gvts_pushvalue(L, obj);
  gvts_call(L, 1, 1);
  return 1;
}

GVTSLIB_API gvts_Integer gvtsG_len (gvts_State *L, int idx) {
  gvts_Integer l;
  int isnum;
  gvts_len(L, idx);
  l = gvts_tointegerx(L, -1, &isnum);
  if (g_unlikely(!isnum))
    gvtsG_error(L, u8"对象长度不是整数");
  gvts_pop(L, 1);                     
  return l;
}

GVTSLIB_API const char *gvtsG_tolstring (gvts_State *L, int idx, size_t *len) {
  idx = gvts_absindex(L,idx);
  if (gvtsG_callmeta(L, idx, "__tostring")) {                  
    if (!gvts_isstring(L, -1))
      gvtsG_error(L, u8"「__tostring」必须返回字符串");
  }
  else {
    switch (gvts_type(L, idx)) {
      case GVTS_TNUMBER: {
        if (gvts_isinteger(L, idx))
          gvts_pushfstring(L, "%I", (GVTSI_UACINT)gvts_tointeger(L, idx));
        else
          gvts_pushfstring(L, "%f", (GVTSI_UACNUMBER)gvts_tonumber(L, idx));
        break;
      }
      case GVTS_TSTRING:
        gvts_pushvalue(L, idx);
        break;
      case GVTS_TBOOLEAN:
        gvts_pushstring(L, (gvts_toboolean(L, idx) ? "true" : "false"));
        break;
      case GVTS_TNIL:
        gvts_pushliteral(L, "nil");
        break;
      default: {
        int tt = gvtsG_getmetafield(L, idx, "__name");                
        const char *kind = (tt == GVTS_TSTRING) ? gvts_tostring(L, -1) :
                                                 gvtsG_typename(L, idx);
        gvts_pushfstring(L, "%s: %p", kind, gvts_topointer(L, idx));
        if (tt != GVTS_TNIL)
          gvts_remove(L, -2);                       
        break;
      }
    }
  }
  return gvts_tolstring(L, -1, len);
}

GVTSLIB_API void gvtsG_setfuncs (gvts_State *L, const gvtsG_Reg *l, int nup) {
  gvtsG_checkstack(L, nup, u8"上值太多");
  for (; l->name != NULL; l++) {                                           
    if (l->func == NULL)                    
      gvts_pushboolean(L, 0);
    else {
      int i;
      for (i = 0; i < nup; i++)                                
        gvts_pushvalue(L, -nup);
      gvts_pushcclosure(L, l->func, nup);                                   
    }
    gvts_setfield(L, -(nup + 2), l->name);
  }
  gvts_pop(L, nup);                       
}

GVTSLIB_API int gvtsG_getsubtable (gvts_State *L, int idx, const char *fname) {
  if (gvts_getfield(L, idx, fname) == GVTS_TTABLE)
    return 1;                           
  else {
    gvts_pop(L, 1);                              
    idx = gvts_absindex(L, idx);
    gvts_newtable(L);
    gvts_pushvalue(L, -1);                              
    gvts_setfield(L, idx, fname);                                 
    return 0;                                               
  }
}

GVTSLIB_API void gvtsG_requiref (gvts_State *L, const char *modname,
                               gvts_CFunction openf, int glb) {
  gvtsG_getsubtable(L, GVTS_REGISTRYINDEX, GVTS_LOADED_TABLE);
  gvts_getfield(L, -1, modname);                       
  if (!gvts_toboolean(L, -1)) {                                   
    gvts_pop(L, 1);                    
    gvts_pushcfunction(L, openf);
    gvts_pushstring(L, modname);                                 
    gvts_call(L, 1, 1);                                   
    gvts_pushvalue(L, -1);                                         
    gvts_setfield(L, -3, modname);                                
  }
  gvts_remove(L, -2);                           
  if (glb) {
    gvts_pushvalue(L, -1);                      
    gvts_setglobal(L, modname);                            
  }
}

GVTSLIB_API void gvtsG_addgsub (gvtsG_Buffer *b, const char *s,
                                     const char *p, const char *r) {
  const char *wild;
  size_t l = strlen(p);
  while ((wild = strstr(s, p)) != NULL) {
    gvtsG_addlstring(b, s, wild - s);                   
    gvtsG_addstring(b, r);                                            
    s = wild + l;                          
  }
  gvtsG_addstring(b, s);                        
}

GVTSLIB_API const char *gvtsG_gsub (gvts_State *L, const char *s,
                                  const char *p, const char *r) {
  gvtsG_Buffer b;
  gvtsG_buffinit(L, &b);
  gvtsG_addgsub(&b, s, p, r);
  gvtsG_pushresult(&b);
  return gvts_tostring(L, -1);
}

static void *g_alloc (void *ud, void *ptr, size_t osize, size_t nsize) {
  (void)ud; (void)osize;                
  if (nsize == 0) {
    free(ptr);
    return NULL;
  }
  else
    return realloc(ptr, nsize);
}

static int panic (gvts_State *L) {
  const char *msg = (gvts_type(L, -1) == GVTS_TSTRING)
                  ? gvts_tostring(L, -1)
                  : u8"错误对象不是字符串";
  gvts_writestringerror(u8"PANIC：调用 Gvts 接口时发生未保护错误（%s）\n",
                        msg);
  return 0;                              
}

static void warnfoff (void *ud, const char *message, int tocont);
static void warnfon (void *ud, const char *message, int tocont);
static void warnfcont (void *ud, const char *message, int tocont);

static int checkcontrol (gvts_State *L, const char *message, int tocont) {
  if (tocont || *(message++) != '@')                              
    return 0;
  else {
    if (strcmp(message, "off") == 0)
      gvts_setwarnf(L, warnfoff, L);                         
    else if (strcmp(message, "on") == 0)
      gvts_setwarnf(L, warnfon, L);                         
    return 1;                                
  }
}

static void warnfoff (void *ud, const char *message, int tocont) {
  checkcontrol((gvts_State *)ud, message, tocont);
}

static void warnfcont (void *ud, const char *message, int tocont) {
  gvts_State *L = (gvts_State *)ud;
  gvts_writestringerror("%s", message);                     
  if (tocont)                          
    gvts_setwarnf(L, warnfcont, L);                       
  else {                 
    gvts_writestringerror("%s", "\n");                                       
    gvts_setwarnf(L, warnfon, L);                                  
  }
}

static void warnfon (void *ud, const char *message, int tocont) {
  if (checkcontrol((gvts_State *)ud, message, tocont))                        
    return;                               
  gvts_writestringerror(u8"%s", u8"Gvts 警告： ");                           
  warnfcont(ud, message, tocont);                         
}

GVTSLIB_API gvts_State *gvtsG_newstate (void) {
  gvts_State *L = gvts_newstate(g_alloc, NULL);
  if (g_likely(L)) {
    gvts_atpanic(L, &panic);
    gvts_setwarnf(L, warnfoff, L);                               
  }
  return L;
}

GVTSLIB_API void gvtsG_checkversion_ (gvts_State *L, gvts_Number ver, size_t sz) {
  gvts_Number v = gvts_version(L);
  if (sz != GVTSG_NUMSIZES)                           
    gvtsG_error(L, u8"核心与库的数值类型不兼容");
  else if (v != ver)
    gvtsG_error(L, u8"版本不匹配：应用需要 %f，Gvts 核心提供 %f",
                  (GVTSI_UACNUMBER)ver, (GVTSI_UACNUMBER)v);
}
