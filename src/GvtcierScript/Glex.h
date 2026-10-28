#ifndef llex_h
#define llex_h

#include <limits.h>

#include "Gobject.h"
#include "Gzio.h"

#define FIRST_RESERVED	(UCHAR_MAX + 1)

#if !defined(GVTS_ENV)
#define GVTS_ENV		"_ENV"
#endif

enum RESERVED {

  TK_AND = FIRST_RESERVED, TK_BREAK,
  TK_DO, TK_ELSE, TK_ELSEIF, TK_FALSE, TK_FOR, TK_FUNCTION,
  TK_GOTO, TK_IF, TK_IN, TK_LOCAL, TK_NIL, TK_NOT, TK_OR, TK_REPEAT,
  TK_RETURN, TK_THEN, TK_TRUE, TK_UNTIL, TK_WHILE, TK_CONTINUE,
  TK_GLOBAL,
  TK_GAMESTART2D, TK_GAMESTART3D,

  TK_IDIV, TK_CONCAT, TK_DOTS, TK_EQ, TK_GE, TK_LE, TK_NE,
  TK_SHL, TK_SHR,
  TK_DBCOLON, TK_EOS,
  TK_FLT, TK_INT, TK_NAME, TK_STRING
};

#define NUM_RESERVED	(cast_int(TK_GAMESTART3D-FIRST_RESERVED + 1))

typedef union {
  gvts_Number r;
  gvts_Integer i;
  TString *ts;
} SemInfo;                             

typedef struct Token {
  int token;
  SemInfo seminfo;
} Token;

typedef struct LexState {
  int current;                                   
  int linenumber;                          
  int lastline;                                     
  Token t;                     
  Token lookahead;                        
  struct FuncState *fs;                                 
  struct gvts_State *L;
  ZIO *z;                    
  Mbuffer *buff;                         
  Table *h;                                         
  struct Dyndata *dyd;                                             
  TString *source;                           
  TString *envn;                                 
  int fwidthOff;                   
} LexState;

GVTSI_FUNC void gvtsX_init (gvts_State *L);
GVTSI_FUNC void gvtsX_setinput (gvts_State *L, LexState *ls, ZIO *z,
                              TString *source, int firstchar);
GVTSI_FUNC TString *gvtsX_newstring (LexState *ls, const char *str, size_t l);
GVTSI_FUNC void gvtsX_next (LexState *ls);
GVTSI_FUNC int gvtsX_lookahead (LexState *ls);
GVTSI_FUNC g_noret gvtsX_syntaxerror (LexState *ls, const char *s);
GVTSI_FUNC const char *gvtsX_token2str (LexState *ls, int token);

#endif
