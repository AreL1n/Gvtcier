#define gvts_c

#include "Gprefix.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <signal.h>

#include "gvts.h"

#include "Gauxlib.h"
#include "gvtslib.h"

#if !defined(GVTS_PROGNAME)
#define GVTS_PROGNAME		"gvts"
#endif

#if !defined(GVTS_INIT_VAR)
#define GVTS_INIT_VAR		"GVTS_INIT"
#endif

#define GVTS_INITVARVERSION	GVTS_INIT_VAR GVTS_VERSUFFIX

static gvts_State *globalL = NULL;

static const char *progname = GVTS_PROGNAME;

#if defined(GVTS_USE_POSIX)          

static void setsignal (int sig, void (*handler)(int)) {
  struct sigaction sa;
  sa.sa_handler = handler;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);                              
  sigaction(sig, &sa, NULL);
}

#else                   

#define setsignal            signal

#endif                                      

static void lstop (gvts_State *L, gvts_Debug *ar) {
  (void)ar;                   
  gvts_sethook(L, NULL, 0, 0);                  
  gvtsG_error(L, "interrupted!");
}

static void laction (int i) {
  int flag = GVTS_MASKCALL | GVTS_MASKRET | GVTS_MASKLINE | GVTS_MASKCOUNT;
  setsignal(i, SIG_DFL);                                                   
  gvts_sethook(globalL, lstop, flag, 1);
}

static void print_usage (const char *badoption) {
  gvts_writestringerror("%s: ", progname);
  if (badoption[1] == 'e' || badoption[1] == 'l')
    gvts_writestringerror("'%s' needs argument\n", badoption);
  else
    gvts_writestringerror("unrecognized option '%s'\n", badoption);
  gvts_writestringerror(
  "usage: %s [options] [script [args]]\n"
  "Available options are:\n"
  "  -e stat   execute string 'stat'\n"
  "  -i        enter interactive mode after executing 'script'\n"
  "  -l mod    require library 'mod' into global 'mod'\n"
  "  -l g=mod  require library 'mod' into global 'g'\n"
  "  -v        show version information\n"
  "  -E        ignore environment variables\n"
  "  -W        turn warnings on\n"
  "  --        stop handling options\n"
  "  -         stop handling options and execute stdin\n"
  ,
  progname);
}

static void g_message (const char *pname, const char *msg) {
  if (pname) gvts_writestringerror("%s: ", pname);
  gvts_writestringerror("%s\n", msg);
}

static int report (gvts_State *L, int status) {
  if (status != GVTS_OK) {
    const char *msg = gvts_tostring(L, -1);
    if (msg == NULL)
      msg = "(error message not a string)";
    g_message(progname, msg);
    gvts_pop(L, 1);                      
  }
  return status;
}

static int msghandler (gvts_State *L) {
  const char *msg = gvts_tostring(L, 1);
  if (msg == NULL) {                                     
    if (gvtsG_callmeta(L, 1, "__tostring") &&                                 
        gvts_type(L, -1) == GVTS_TSTRING)                               
      return 1;                           
    else
      msg = gvts_pushfstring(L, "(error object is a %s value)",
                               gvtsG_typename(L, 1));
  }
  gvtsG_traceback(L, L, msg, 1);                                   
  return 1;                            
}

static int docall (gvts_State *L, int narg, int nres) {
  int status;
  int base = gvts_gettop(L) - narg;                      
  gvts_pushcfunction(L, msghandler);                            
  gvts_insert(L, base);                                      
  globalL = L;                                    
  setsignal(SIGINT, laction);                            
  status = gvts_pcall(L, narg, nres, base);
  setsignal(SIGINT, SIG_DFL);                             
  gvts_remove(L, base);                                             
  return status;
}

static void print_version (void) {
  gvts_writestring(GVTS_COPYRIGHT, strlen(GVTS_COPYRIGHT));
  gvts_writeline();
}

static void createargtable (gvts_State *L, char **argv, int argc, int script) {
  int i, narg;
  narg = argc - (script + 1);                                  
  gvts_createtable(L, narg, script + 1);
  for (i = 0; i < argc; i++) {
    gvts_pushstring(L, argv[i]);
    gvts_rawseti(L, -2, i - script);
  }
  gvts_setglobal(L, "arg");
}

static int dochunk (gvts_State *L, int status) {
  if (status == GVTS_OK) status = docall(L, 0, 0);
  return report(L, status);
}

static int dofile (gvts_State *L, const char *name) {
  return dochunk(L, gvtsG_loadfile(L, name));
}

static int dostring (gvts_State *L, const char *s, const char *name) {
  return dochunk(L, gvtsG_loadbuffer(L, s, strlen(s), name));
}

static int dolibrary (gvts_State *L, char *globname) {
  int status;
  char *suffix = NULL;
  char *modname = strchr(globname, '=');
  if (modname == NULL) {                         
    modname = globname;                                           
    suffix = strchr(modname, *GVTS_IGMARK);                              
  }
  else {
    *modname = '\0';                             
    modname++;                                        
  }
  gvts_getglobal(L, "require");
  gvts_pushstring(L, modname);
  status = docall(L, 1, 1);                               
  if (status == GVTS_OK) {
    if (suffix != NULL)                               
      *suffix = '\0';                                      
    gvts_setglobal(L, globname);                                   
  }
  return report(L, status);
}

static int pushargs (gvts_State *L) {
  int i, n;
  if (gvts_getglobal(L, "arg") != GVTS_TTABLE)
    gvtsG_error(L, "'arg' is not a table");
  n = (int)gvtsG_len(L, -1);
  gvtsG_checkstack(L, n + 3, "too many arguments to script");
  for (i = 1; i <= n; i++)
    gvts_rawgeti(L, -i, i);
  gvts_remove(L, -i);                                   
  return n;
}

static int handle_script (gvts_State *L, char **argv) {
  int status;
  const char *fname = argv[0];
  if (strcmp(fname, "-") == 0 && strcmp(argv[-1], "--") != 0)
    fname = NULL;             
  status = gvtsG_loadfile(L, fname);
  if (status == GVTS_OK) {
    int n = pushargs(L);                                
    status = docall(L, n, GVTS_MULTRET);
  }
  return report(L, status);
}

#define has_error	1	                
#define has_i		2	        
#define has_v		4	        
#define has_e		8	        
#define has_E		16	        

static int collectargs (char **argv, int *first) {
  int args = 0;
  int i;
  if (argv[0] != NULL) {                                
    if (argv[0][0])                  
      progname = argv[0];               
  }
  else {                       
    *first = -1;
    return 0;
  }
  for (i = 1; argv[i] != NULL; i++) {                        
    *first = i;
    if (argv[i][0] != '-')                      
        return args;                             
    switch (argv[i][1]) {                         
      case '-':            
        if (argv[i][2] != '\0')                                    
          return has_error;                      
        *first = i + 1;
        return args;
      case '\0':           
        return args;                            
      case 'E':
        if (argv[i][2] != '\0')                         
          return has_error;                      
        args |= has_E;
        break;
      case 'W':
        if (argv[i][2] != '\0')                         
          return has_error;                      
        break;
      case 'i':
        args |= has_i;                                        
      case 'v':
        if (argv[i][2] != '\0')                         
          return has_error;                      
        args |= has_v;
        break;
      case 'e':
        args |= has_e;                   
      case 'l':                                     
        if (argv[i][2] == '\0') {                                 
          i++;                       
          if (argv[i] == NULL || argv[i][0] == '-')
            return has_error;                                                
        }
        break;
      default:                      
        return has_error;
    }
  }
  *first = 0;                      
  return args;
}

static int runargs (gvts_State *L, char **argv, int n) {
  int i;
  for (i = 1; i < n; i++) {
    int option = argv[i][1];
    gvts_assert(argv[i][0] == '-');                       
    switch (option) {
      case 'e':  case 'l': {
        int status;
        char *extra = argv[i] + 2;                                     
        if (*extra == '\0') extra = argv[++i];
        gvts_assert(extra != NULL);
        status = (option == 'e')
                 ? dostring(L, extra, "=(command line)")
                 : dolibrary(L, extra);
        if (status != GVTS_OK) return 0;
        break;
      }
      case 'W':
        gvts_warning(L, "@on", 0);                   
        break;
    }
  }
  return 1;
}

static int handle_gvtsinit (gvts_State *L) {
  const char *name = "=" GVTS_INITVARVERSION;
  const char *init = getenv(name + 1);
  if (init == NULL) {
    name = "=" GVTS_INIT_VAR;
    init = getenv(name + 1);                            
  }
  if (init == NULL) return GVTS_OK;
  else if (init[0] == '@')
    return dofile(L, init+1);
  else
    return dostring(L, init, name);
}

#if !defined(GVTS_PROMPT)
#define GVTS_PROMPT		"> "
#define GVTS_PROMPT2		">> "
#endif

#if !defined(GVTS_MAXINPUT)
#define GVTS_MAXINPUT		512
#endif

#if !defined(gvts_stdin_is_tty)	       

#if defined(GVTS_USE_POSIX)	       

#include <unistd.h>
#define gvts_stdin_is_tty()	isatty(0)

#elif defined(GVTS_USE_WINDOWS)	        

#include <io.h>
#include <windows.h>

#define gvts_stdin_is_tty()	_isatty(_fileno(stdin))

#else				        

#define gvts_stdin_is_tty()	1                             

#endif				       

#endif				       

#if !defined(gvts_readline)	       

#if defined(GVTS_USE_READLINE)	       

#include <readline/readline.h>
#include <readline/history.h>
#define gvts_initreadline(L)	((void)L, rl_readline_name="gvts")
#define gvts_readline(L,b,p)	((void)L, ((b)=readline(p)) != NULL)
#define gvts_saveline(L,line)	((void)L, add_history(line))
#define gvts_freeline(L,b)	((void)L, free(b))

#else				        

#define gvts_initreadline(L)  ((void)L)
#define gvts_readline(L,b,p) \
        ((void)L, fputs(p, stdout), fflush(stdout),                    \
        fgets(b, GVTS_MAXINPUT, stdin) != NULL)                
#define gvts_saveline(L,line)	{ (void)L; (void)line; }
#define gvts_freeline(L,b)	{ (void)L; (void)b; }

#endif				       

#endif				       

static const char *get_prompt (gvts_State *L, int firstline) {
  if (gvts_getglobal(L, firstline ? "_PROMPT" : "_PROMPT2") == GVTS_TNIL)
    return (firstline ? GVTS_PROMPT : GVTS_PROMPT2);                       
  else {                                       
    const char *p = gvtsG_tolstring(L, -1, NULL);
    gvts_remove(L, -2);                             
    return p;
  }
}

#define EOFMARK		"<eof>"
#define marklen		(sizeof(EOFMARK)/sizeof(char) - 1)

static int incomplete (gvts_State *L, int status) {
  if (status == GVTS_ERRSYNTAX) {
    size_t lmsg;
    const char *msg = gvts_tolstring(L, -1, &lmsg);
    if (lmsg >= marklen && strcmp(msg + lmsg - marklen, EOFMARK) == 0) {
      gvts_pop(L, 1);
      return 1;
    }
  }
  return 0;               
}

static int pushline (gvts_State *L, int firstline) {
  char buffer[GVTS_MAXINPUT];
  char *b = buffer;
  size_t l;
  const char *prmt = get_prompt(L, firstline);
  int readstatus = gvts_readline(L, b, prmt);
  if (readstatus == 0)
    return 0;                                                  
  gvts_pop(L, 1);                     
  l = strlen(b);
  if (l > 0 && b[l-1] == '\n')                               
    b[--l] = '\0';                 
  if (firstline && b[0] == '=')                                       
    gvts_pushfstring(L, "return %s", b + 1);                              
  else
    gvts_pushlstring(L, b, l);
  gvts_freeline(L, b);
  return 1;
}

static int addreturn (gvts_State *L) {
  const char *line = gvts_tostring(L, -1);                     
  const char *retline = gvts_pushfstring(L, "return %s;", line);
  int status = gvtsG_loadbuffer(L, retline, strlen(retline), "=stdin");
  if (status == GVTS_OK) {
    gvts_remove(L, -2);                            
    if (line[0] != '\0')                  
      gvts_saveline(L, line);                    
  }
  else
    gvts_pop(L, 2);                                                           
  return status;
}

static int multiline (gvts_State *L) {
  for (;;) {                                              
    size_t len;
    const char *line = gvts_tolstring(L, 1, &len);                       
    int status = gvtsG_loadbuffer(L, line, len, "=stdin");              
    if (!incomplete(L, status) || !pushline(L, 0)) {
      gvts_saveline(L, line);                    
      return status;                                                         
    }
    gvts_pushliteral(L, "\n");                      
    gvts_insert(L, -2);                                
    gvts_concat(L, 3);                 
  }
}

static int loadline (gvts_State *L) {
  int status;
  gvts_settop(L, 0);
  if (!pushline(L, 1))
    return -1;                
  if ((status = addreturn(L)) != GVTS_OK)                                  
    status = multiline(L);                                                     
  gvts_remove(L, 1);                                  
  gvts_assert(gvts_gettop(L) == 1);
  return status;
}

static void g_print (gvts_State *L) {
  int n = gvts_gettop(L);
  if (n > 0) {                                 
    gvtsG_checkstack(L, GVTS_MINSTACK, "too many results to print");
    gvts_getglobal(L, "print");
    gvts_insert(L, 1);
    if (gvts_pcall(L, n, 0, 0) != GVTS_OK)
      g_message(progname, gvts_pushfstring(L, "error calling 'print' (%s)",
                                             gvts_tostring(L, -1)));
  }
}

static void doREPL (gvts_State *L) {
  int status;
  const char *oldprogname = progname;
  progname = NULL;                                                   
  gvts_initreadline(L);
  while ((status = loadline(L)) != -1) {
    if (status == GVTS_OK)
      status = docall(L, 0, GVTS_MULTRET);
    if (status == GVTS_OK) g_print(L);
    else report(L, status);
  }
  gvts_settop(L, 0);                   
  gvts_writeline();
  progname = oldprogname;
}

static int pmain (gvts_State *L) {
  int argc = (int)gvts_tointeger(L, 1);
  char **argv = (char **)gvts_touserdata(L, 2);
  int script;
  int args = collectargs(argv, &script);
  int optlim = (script > 0) ? script : argc;                               
  gvtsG_checkversion(L);                                                  
  if (args == has_error) {                
    print_usage(argv[script]);                                      
    return 0;
  }
  if (args & has_v)                    
    print_version();
  if (args & has_E) {                    
    gvts_pushboolean(L, 1);                                                 
    gvts_setfield(L, GVTS_REGISTRYINDEX, "GVTS_NOENV");
  }
  gvtsG_openlibs(L);                               
  createargtable(L, argv, argc, script);                          
  gvts_gc(L, GVTS_GCRESTART);                   
  gvts_gc(L, GVTS_GCGEN, 0, 0);                               
  if (!(args & has_E)) {                       
    if (handle_gvtsinit(L) != GVTS_OK)                    
      return 0;                              
  }
  if (!runargs(L, argv, optlim))                                   
    return 0;                        
  if (script > 0) {                                             
    if (handle_script(L, argv + script) != GVTS_OK)
      return 0;                                  
  }
  if (args & has_i)                  
    doREPL(L);                               
  else if (script < 1 && !(args & (has_e | has_v))) {                        
    if (gvts_stdin_is_tty()) {                                    
      print_version();
      doREPL(L);                               
    }
    else dofile(L, NULL);                                
  }
  gvts_pushboolean(L, 1);                        
  return 1;
}

int main (int argc, char **argv) {
  int status, result;
  gvts_State *L = gvtsG_newstate();                    
  if (L == NULL) {
    g_message(argv[0], "cannot create state: not enough memory");
    return EXIT_FAILURE;
  }
  gvts_gc(L, GVTS_GCSTOP);                                    
  gvts_pushcfunction(L, &pmain);                                         
  gvts_pushinteger(L, argc);                    
  gvts_pushlightuserdata(L, argv);                   
  status = gvts_pcall(L, 2, 1, 0);                   
  result = gvts_toboolean(L, -1);                  
  report(L, status);
  gvts_close(L);
  return (result && status == GVTS_OK) ? EXIT_SUCCESS : EXIT_FAILURE;
}
