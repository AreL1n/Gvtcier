#define liolib_c
#define GVTS_LIB

#include "Gprefix.h"

#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gvts.h"

#include "Gauxlib.h"
#include "gvtslib.h"

#if !defined(g_checkmode)

#if !defined(L_MODEEXT)
#define L_MODEEXT	"b"
#endif

static int g_checkmode (const char *mode) {
  return (*mode != '\0' && strchr("rwa", *(mode++)) != NULL &&
         (*mode != '+' || ((void)(++mode), 1)) &&                           
         (strspn(mode, L_MODEEXT) == strlen(mode)));                        
}

#endif

#if !defined(g_popen)		       

#if defined(GVTS_USE_POSIX)	       

#define g_popen(L,c,m)		(fflush(NULL), popen(c,m))
#define g_pclose(L,file)	(pclose(file))

#elif defined(GVTS_USE_WINDOWS)	        

#define g_popen(L,c,m)		(_popen(c,m))
#define g_pclose(L,file)	(_pclose(file))

#if !defined(g_checkmodep)

#define g_checkmodep(m)	((m[0] == 'r' || m[0] == 'w') && \
  (m[1] == '\0' || ((m[1] == 'b' || m[1] == 't') && m[2] == '\0')))
#endif

#else				        

#define g_popen(L,c,m)  \
	  ((void)c, (void)m, \
	  gvtsG_error(L, "'popen' not supported"), \
	  (FILE*)0)
#define g_pclose(L,file)		((void)L, (void)file, -1)

#endif				       

#endif				       

#if !defined(g_checkmodep)

#define g_checkmodep(m)        ((m[0] == 'r' || m[0] == 'w') && m[1] == '\0')
#endif

#if !defined(g_getc)		       

#if defined(GVTS_USE_POSIX)
#define g_getc(f)		getc_unlocked(f)
#define g_lockfile(f)		flockfile(f)
#define g_unlockfile(f)		funlockfile(f)
#else
#define g_getc(f)		getc(f)
#define g_lockfile(f)		((void)0)
#define g_unlockfile(f)		((void)0)
#endif

#endif				       

#if !defined(g_fseek)		       

#if defined(GVTS_USE_POSIX)	       

#include <sys/types.h>

#define g_fseek(f,o,w)		fseeko(f,o,w)
#define g_ftell(f)		ftello(f)
#define g_seeknum		off_t

#elif defined(GVTS_USE_WINDOWS) && !defined(_CRTIMP_TYPEINFO) \
   && defined(_MSC_VER) && (_MSC_VER >= 1400)	        

#define g_fseek(f,o,w)		_fseeki64(f,o,w)
#define g_ftell(f)		_ftelli64(f)
#define g_seeknum		__int64

#else				        

#define g_fseek(f,o,w)		fseek(f,o,w)
#define g_ftell(f)		ftell(f)
#define g_seeknum		long

#endif				       

#endif				       

#define IO_PREFIX	"_IO_"
#define IOPREF_LEN	(sizeof(IO_PREFIX)/sizeof(char) - 1)
#define IO_INPUT	(IO_PREFIX "input")
#define IO_OUTPUT	(IO_PREFIX "output")

typedef gvtsG_Stream LStream;

#define tolstream(L)	((LStream *)gvtsG_checkudata(L, 1, GVTS_FILEHANDLE))

#define isclosed(p)	((p)->closef == NULL)

static int io_type (gvts_State *L) {
  LStream *p;
  gvtsG_checkany(L, 1);
  p = (LStream *)gvtsG_testudata(L, 1, GVTS_FILEHANDLE);
  if (p == NULL)
    gvtsG_pushfail(L);                  
  else if (isclosed(p))
    gvts_pushliteral(L, "closed file");
  else
    gvts_pushliteral(L, "file");
  return 1;
}

static int f_tostring (gvts_State *L) {
  LStream *p = tolstream(L);
  if (isclosed(p))
    gvts_pushliteral(L, "file (closed)");
  else
    gvts_pushfstring(L, "file (%p)", p->f);
  return 1;
}

static FILE *tofile (gvts_State *L) {
  LStream *p = tolstream(L);
  if (g_unlikely(isclosed(p)))
    gvtsG_error(L, "attempt to use a closed file");
  gvts_assert(p->f);
  return p->f;
}

static LStream *newprefile (gvts_State *L) {
  LStream *p = (LStream *)gvts_newuserdatauv(L, sizeof(LStream), 0);
  p->closef = NULL;                                    
  gvtsG_setmetatable(L, GVTS_FILEHANDLE);
  return p;
}

static int aux_close (gvts_State *L) {
  LStream *p = tolstream(L);
  volatile gvts_CFunction cf = p->closef;
  p->closef = NULL;                             
  return (*cf)(L);                
}

static int f_close (gvts_State *L) {
  tofile(L);                                            
  return aux_close(L);
}

static int io_close (gvts_State *L) {
  if (gvts_isnone(L, 1))                    
    gvts_getfield(L, GVTS_REGISTRYINDEX, IO_OUTPUT);                          
  return f_close(L);
}

static int f_gc (gvts_State *L) {
  LStream *p = tolstream(L);
  if (!isclosed(p) && p->f != NULL)
    aux_close(L);                                                 
  return 0;
}

static int io_fclose (gvts_State *L) {
  LStream *p = tolstream(L);
  errno = 0;
  return gvtsG_fileresult(L, (fclose(p->f) == 0), NULL);
}

static LStream *newfile (gvts_State *L) {
  LStream *p = newprefile(L);
  p->f = NULL;
  p->closef = &io_fclose;
  return p;
}

static void opencheck (gvts_State *L, const char *fname, const char *mode) {
  LStream *p = newfile(L);
  p->f = fopen(fname, mode);
  if (g_unlikely(p->f == NULL))
    gvtsG_error(L, "cannot open file '%s' (%s)", fname, strerror(errno));
}

static int io_open (gvts_State *L) {
  const char *filename = gvtsG_checkstring(L, 1);
  const char *mode = gvtsG_optstring(L, 2, "r");
  LStream *p = newfile(L);
  const char *md = mode;                              
  gvtsG_argcheck(L, g_checkmode(md), 2, "invalid mode");
  errno = 0;
  p->f = fopen(filename, mode);
  return (p->f == NULL) ? gvtsG_fileresult(L, 0, filename) : 1;
}

static int io_pclose (gvts_State *L) {
  LStream *p = tolstream(L);
  errno = 0;
  return gvtsG_execresult(L, g_pclose(L, p->f));
}

static int io_popen (gvts_State *L) {
  const char *filename = gvtsG_checkstring(L, 1);
  const char *mode = gvtsG_optstring(L, 2, "r");
  LStream *p = newprefile(L);
  gvtsG_argcheck(L, g_checkmodep(mode), 2, "invalid mode");
  errno = 0;
  p->f = g_popen(L, filename, mode);
  p->closef = &io_pclose;
  return (p->f == NULL) ? gvtsG_fileresult(L, 0, filename) : 1;
}

static int io_tmpfile (gvts_State *L) {
  LStream *p = newfile(L);
  errno = 0;
  p->f = tmpfile();
  return (p->f == NULL) ? gvtsG_fileresult(L, 0, NULL) : 1;
}

static FILE *getiofile (gvts_State *L, const char *findex) {
  LStream *p;
  gvts_getfield(L, GVTS_REGISTRYINDEX, findex);
  p = (LStream *)gvts_touserdata(L, -1);
  if (g_unlikely(isclosed(p)))
    gvtsG_error(L, "default %s file is closed", findex + IOPREF_LEN);
  return p->f;
}

static int g_iofile (gvts_State *L, const char *f, const char *mode) {
  if (!gvts_isnoneornil(L, 1)) {
    const char *filename = gvts_tostring(L, 1);
    if (filename)
      opencheck(L, filename, mode);
    else {
      tofile(L);                                           
      gvts_pushvalue(L, 1);
    }
    gvts_setfield(L, GVTS_REGISTRYINDEX, f);
  }

  gvts_getfield(L, GVTS_REGISTRYINDEX, f);
  return 1;
}

static int io_input (gvts_State *L) {
  return g_iofile(L, IO_INPUT, "r");
}

static int io_output (gvts_State *L) {
  return g_iofile(L, IO_OUTPUT, "w");
}

static int io_readline (gvts_State *L);

#define MAXARGLINE	250

static void aux_lines (gvts_State *L, int toclose) {
  int n = gvts_gettop(L) - 1;                                   
  gvtsG_argcheck(L, n <= MAXARGLINE, MAXARGLINE + 2, "too many arguments");
  gvts_pushvalue(L, 1);            
  gvts_pushinteger(L, n);                                   
  gvts_pushboolean(L, toclose);                                          
  gvts_rotate(L, 2, 3);                                                
  gvts_pushcclosure(L, io_readline, 3 + n);
}

static int f_lines (gvts_State *L) {
  tofile(L);                                           
  aux_lines(L, 0);
  return 1;
}

static int io_lines (gvts_State *L) {
  int toclose;
  if (gvts_isnone(L, 1)) gvts_pushnil(L);                             
  if (gvts_isnil(L, 1)) {                     
    gvts_getfield(L, GVTS_REGISTRYINDEX, IO_INPUT);                         
    gvts_replace(L, 1);                         
    tofile(L);                                           
    toclose = 0;                                       
  }
  else {                       
    const char *filename = gvtsG_checkstring(L, 1);
    opencheck(L, filename, "r");
    gvts_replace(L, 1);                           
    toclose = 1;                                
  }
  aux_lines(L, toclose);                               
  if (toclose) {
    gvts_pushnil(L);             
    gvts_pushnil(L);               
    gvts_pushvalue(L, 1);                                                      
    return 4;
  }
  else
    return 1;
}

#if !defined (L_MAXLENNUM)
#define L_MAXLENNUM     200
#endif

typedef struct {
  FILE *f;                       
  int c;                                      
  int n;                                           
  char buff[L_MAXLENNUM + 1];                          
} RN;

static int nextc (RN *rn) {
  if (g_unlikely(rn->n >= L_MAXLENNUM)) {                        
    rn->buff[0] = '\0';                         
    return 0;            
  }
  else {
    rn->buff[rn->n++] = rn->c;                         
    rn->c = g_getc(rn->f);                     
    return 1;
  }
}

static int test2 (RN *rn, const char *set) {
  if (rn->c == set[0] || rn->c == set[1])
    return nextc(rn);
  else return 0;
}

static int readdigits (RN *rn, int hex) {
  int count = 0;
  while ((hex ? isxdigit(rn->c) : isdigit(rn->c)) && nextc(rn))
    count++;
  return count;
}

static int read_number (gvts_State *L, FILE *f) {
  RN rn;
  int count = 0;
  int hex = 0;
  char decp[2];
  rn.f = f; rn.n = 0;
  decp[0] = gvts_getlocaledecpoint();                                     
  decp[1] = '.';                           
  g_lockfile(rn.f);
  do { rn.c = g_getc(rn.f); } while (isspace(rn.c));                   
  test2(&rn, "-+");                     
  if (test2(&rn, "00")) {
    if (test2(&rn, "xX")) hex = 1;                              
    else count = 1;                                          
  }
  count += readdigits(&rn, hex);                     
  if (test2(&rn, decp))                      
    count += readdigits(&rn, hex);                       
  if (count > 0 && test2(&rn, (hex ? "pP" : "eE"))) {                      
    test2(&rn, "-+");                     
    readdigits(&rn, 0);                       
  }
  ungetc(rn.c, rn.f);                              
  g_unlockfile(rn.f);
  rn.buff[rn.n] = '\0';                     
  if (g_likely(gvts_stringtonumber(L, rn.buff)))
    return 1;                                
  else {                      
   gvts_pushnil(L);                              
   return 0;                  
  }
}

static int test_eof (gvts_State *L, FILE *f) {
  int c = getc(f);
  ungetc(c, f);                           
  gvts_pushliteral(L, "");
  return (c != EOF);
}

static int read_line (gvts_State *L, FILE *f, int chop) {
  gvtsG_Buffer b;
  int c;
  gvtsG_buffinit(L, &b);
  do {                                                         
    char *buff = gvtsG_prepbuffer(&b);                                
    int i = 0;
    g_lockfile(f);                                                   
    while (i < GVTSG_BUFFERSIZE && (c = g_getc(f)) != EOF && c != '\n')
      buff[i++] = c;                                              
    g_unlockfile(f);
    gvtsG_addsize(&b, i);
  } while (c != EOF && c != '\n');                                
  if (!chop && c == '\n')                                    
    gvtsG_addchar(&b, c);                                    
  gvtsG_pushresult(&b);                    

  return (c == '\n' || gvts_rawlen(L, -1) > 0);
}

static void read_all (gvts_State *L, FILE *f) {
  size_t nr;
  gvtsG_Buffer b;
  gvtsG_buffinit(L, &b);
  do {                                                    
    char *p = gvtsG_prepbuffer(&b);
    nr = fread(p, sizeof(char), GVTSG_BUFFERSIZE, f);
    gvtsG_addsize(&b, nr);
  } while (nr == GVTSG_BUFFERSIZE);
  gvtsG_pushresult(&b);                    
}

static int read_chars (gvts_State *L, FILE *f, size_t n) {
  size_t nr;                                     
  char *p;
  gvtsG_Buffer b;
  gvtsG_buffinit(L, &b);
  p = gvtsG_prepbuffsize(&b, n);                                          
  nr = fread(p, sizeof(char), n, f);                             
  gvtsG_addsize(&b, nr);
  gvtsG_pushresult(&b);                    
  return (nr > 0);                               
}

static int g_read (gvts_State *L, FILE *f, int first) {
  int nargs = gvts_gettop(L) - 1;
  int n, success;
  clearerr(f);
  errno = 0;
  if (nargs == 0) {                     
    success = read_line(L, f, 1);
    n = first + 1;                          
  }
  else {

    gvtsG_checkstack(L, nargs+GVTS_MINSTACK, "too many arguments");
    success = 1;
    for (n = first; nargs-- && success; n++) {
      if (gvts_type(L, n) == GVTS_TNUMBER) {
        size_t l = (size_t)gvtsG_checkinteger(L, n);
        success = (l == 0) ? test_eof(L, f) : read_chars(L, f, l);
      }
      else {
        const char *p = gvtsG_checkstring(L, n);
        if (*p == '*') p++;                                             
        switch (*p) {
          case 'n':              
            success = read_number(L, f);
            break;
          case 'l':            
            success = read_line(L, f, 1);
            break;
          case 'L':                             
            success = read_line(L, f, 0);
            break;
          case 'a':            
            read_all(L, f);                        
            success = 1;                     
            break;
          default:
            return gvtsG_argerror(L, n, "invalid format");
        }
      }
    }
  }
  if (ferror(f))
    return gvtsG_fileresult(L, 0, NULL);
  if (!success) {
    gvts_pop(L, 1);                          
    gvtsG_pushfail(L);                        
  }
  return n - first;
}

static int io_read (gvts_State *L) {
  return g_read(L, getiofile(L, IO_INPUT), 1);
}

static int f_read (gvts_State *L) {
  return g_read(L, tofile(L), 2);
}

static int io_readline (gvts_State *L) {
  LStream *p = (LStream *)gvts_touserdata(L, gvts_upvalueindex(1));
  int i;
  int n = (int)gvts_tointeger(L, gvts_upvalueindex(2));
  if (isclosed(p))                               
    return gvtsG_error(L, "file is already closed");
  gvts_settop(L , 1);
  gvtsG_checkstack(L, n, "too many arguments");
  for (i = 1; i <= n; i++)                                  
    gvts_pushvalue(L, gvts_upvalueindex(3 + i));
  n = g_read(L, p->f, 2);                                
  gvts_assert(n > 0);                                    
  if (gvts_toboolean(L, -n))                                
    return n;                   
  else {                                           
    if (n > 1) {                                   

      return gvtsG_error(L, "%s", gvts_tostring(L, -n + 1));
    }
    if (gvts_toboolean(L, gvts_upvalueindex(3))) {                               
      gvts_settop(L, 0);                   
      gvts_pushvalue(L, gvts_upvalueindex(1));                            
      aux_close(L);                
    }
    return 0;
  }
}

static int g_write (gvts_State *L, FILE *f, int arg) {
  int nargs = gvts_gettop(L) - arg;
  int status = 1;
  errno = 0;
  for (; nargs--; arg++) {
    if (gvts_type(L, arg) == GVTS_TNUMBER) {

      int len = gvts_isinteger(L, arg)
                ? fprintf(f, GVTS_INTEGER_FMT,
                             (GVTSI_UACINT)gvts_tointeger(L, arg))
                : fprintf(f, GVTS_NUMBER_FMT,
                             (GVTSI_UACNUMBER)gvts_tonumber(L, arg));
      status = status && (len > 0);
    }
    else {
      size_t l;
      const char *s = gvtsG_checklstring(L, arg, &l);
      status = status && (fwrite(s, sizeof(char), l, f) == l);
    }
  }
  if (g_likely(status))
    return 1;                                        
  else
    return gvtsG_fileresult(L, status, NULL);
}

static int io_write (gvts_State *L) {
  return g_write(L, getiofile(L, IO_OUTPUT), 1);
}

static int f_write (gvts_State *L) {
  FILE *f = tofile(L);
  gvts_pushvalue(L, 1);                                                   
  return g_write(L, f, 2);
}

static int f_seek (gvts_State *L) {
  static const int mode[] = {SEEK_SET, SEEK_CUR, SEEK_END};
  static const char *const modenames[] = {"set", "cur", "end", NULL};
  FILE *f = tofile(L);
  int op = gvtsG_checkoption(L, 2, "cur", modenames);
  gvts_Integer p3 = gvtsG_optinteger(L, 3, 0);
  g_seeknum offset = (g_seeknum)p3;
  gvtsG_argcheck(L, (gvts_Integer)offset == p3, 3,
                  "not an integer in proper range");
  errno = 0;
  op = g_fseek(f, offset, mode[op]);
  if (g_unlikely(op))
    return gvtsG_fileresult(L, 0, NULL);             
  else {
    gvts_pushinteger(L, (gvts_Integer)g_ftell(f));
    return 1;
  }
}

static int f_setvbuf (gvts_State *L) {
  static const int mode[] = {_IONBF, _IOFBF, _IOLBF};
  static const char *const modenames[] = {"no", "full", "line", NULL};
  FILE *f = tofile(L);
  int op = gvtsG_checkoption(L, 2, NULL, modenames);
  gvts_Integer sz = gvtsG_optinteger(L, 3, GVTSG_BUFFERSIZE);
  int res;
  errno = 0;
  res = setvbuf(f, NULL, mode[op], (size_t)sz);
  return gvtsG_fileresult(L, res == 0, NULL);
}

static int io_flush (gvts_State *L) {
  FILE *f = getiofile(L, IO_OUTPUT);
  errno = 0;
  return gvtsG_fileresult(L, fflush(f) == 0, NULL);
}

static int f_flush (gvts_State *L) {
  FILE *f = tofile(L);
  errno = 0;
  return gvtsG_fileresult(L, fflush(f) == 0, NULL);
}

static const gvtsG_Reg iolib[] = {
  {"close", io_close},
  {"flush", io_flush},
  {"input", io_input},
  {"lines", io_lines},
  {"open", io_open},
  {"output", io_output},
  {"popen", io_popen},
  {"read", io_read},
  {"tmpfile", io_tmpfile},
  {"type", io_type},
  {"write", io_write},
  {NULL, NULL}
};

static const gvtsG_Reg meth[] = {
  {"read", f_read},
  {"write", f_write},
  {"lines", f_lines},
  {"flush", f_flush},
  {"seek", f_seek},
  {"close", f_close},
  {"setvbuf", f_setvbuf},
  {NULL, NULL}
};

static const gvtsG_Reg metameth[] = {
  {"__index", NULL},                   
  {"__gc", f_gc},
  {"__close", f_gc},
  {"__tostring", f_tostring},
  {NULL, NULL}
};

static void createmeta (gvts_State *L) {
  gvtsG_newmetatable(L, GVTS_FILEHANDLE);                                  
  gvtsG_setfuncs(L, metameth, 0);                                        
  gvtsG_newlibtable(L, meth);                           
  gvtsG_setfuncs(L, meth, 0);                                        
  gvts_setfield(L, -2, "__index");                                        
  gvts_pop(L, 1);                     
}

static int io_noclose (gvts_State *L) {
  LStream *p = tolstream(L);
  p->closef = &io_noclose;                        
  gvtsG_pushfail(L);
  gvts_pushliteral(L, "cannot close standard file");
  return 2;
}

static void createstdfile (gvts_State *L, FILE *f, const char *k,
                           const char *fname) {
  LStream *p = newprefile(L);
  p->f = f;
  p->closef = &io_noclose;
  if (k != NULL) {
    gvts_pushvalue(L, -1);
    gvts_setfield(L, GVTS_REGISTRYINDEX, k);                            
  }
  gvts_setfield(L, -2, fname);                          
}

GVTSMOD_API int gvtsopen_io (gvts_State *L) {
  gvtsG_newlib(L, iolib);                  
  createmeta(L);

  createstdfile(L, stdin, IO_INPUT, "stdin");
  createstdfile(L, stdout, IO_OUTPUT, "stdout");
  createstdfile(L, stderr, NULL, "stderr");
  return 1;
}
