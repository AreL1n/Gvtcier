#ifndef lcode_h
#define lcode_h

#include "Glex.h"
#include "Gobject.h"
#include "Gopcodes.h"
#include "Gparser.h"

#define NO_JUMP (-1)

typedef enum BinOpr {

  OPR_ADD, OPR_SUB, OPR_MUL, OPR_MOD, OPR_POW,
  OPR_DIV, OPR_IDIV,

  OPR_BAND, OPR_BOR, OPR_BXOR,
  OPR_SHL, OPR_SHR,

  OPR_CONCAT,

  OPR_EQ, OPR_LT, OPR_LE,
  OPR_NE, OPR_GT, OPR_GE,

  OPR_AND, OPR_OR,
  OPR_NOBINOPR
} BinOpr;

#define foldbinop(op)	((op) <= OPR_SHR)

#define gvtsK_codeABC(fs,o,a,b,c)	gvtsK_codeABCk(fs,o,a,b,c,0)

typedef enum UnOpr { OPR_MINUS, OPR_BNOT, OPR_NOT, OPR_LEN, OPR_NOUNOPR } UnOpr;

#define getinstruction(fs,e)	((fs)->f->code[(e)->u.info])

#define gvtsK_setmultret(fs,e)	gvtsK_setreturns(fs, e, GVTS_MULTRET)

#define gvtsK_jumpto(fs,t)	gvtsK_patchlist(fs, gvtsK_jump(fs), t)

GVTSI_FUNC int gvtsK_code (FuncState *fs, Instruction i);
GVTSI_FUNC int gvtsK_codeABx (FuncState *fs, OpCode o, int A, unsigned int Bx);
GVTSI_FUNC int gvtsK_codeABCk (FuncState *fs, OpCode o, int A,
                                            int B, int C, int k);
GVTSI_FUNC int gvtsK_exp2const (FuncState *fs, const expdesc *e, TValue *v);
GVTSI_FUNC void gvtsK_fixline (FuncState *fs, int line);
GVTSI_FUNC void gvtsK_nil (FuncState *fs, int from, int n);
GVTSI_FUNC void gvtsK_reserveregs (FuncState *fs, int n);
GVTSI_FUNC void gvtsK_checkstack (FuncState *fs, int n);
GVTSI_FUNC void gvtsK_int (FuncState *fs, int reg, gvts_Integer n);
GVTSI_FUNC void gvtsK_dischargevars (FuncState *fs, expdesc *e);
GVTSI_FUNC int gvtsK_exp2anyreg (FuncState *fs, expdesc *e);
GVTSI_FUNC void gvtsK_exp2anyregup (FuncState *fs, expdesc *e);
GVTSI_FUNC void gvtsK_exp2nextreg (FuncState *fs, expdesc *e);
GVTSI_FUNC void gvtsK_exp2val (FuncState *fs, expdesc *e);
GVTSI_FUNC void gvtsK_self (FuncState *fs, expdesc *e, expdesc *key);
GVTSI_FUNC void gvtsK_indexed (FuncState *fs, expdesc *t, expdesc *k);
GVTSI_FUNC void gvtsK_goiftrue (FuncState *fs, expdesc *e);
GVTSI_FUNC void gvtsK_goiffalse (FuncState *fs, expdesc *e);
GVTSI_FUNC void gvtsK_storevar (FuncState *fs, expdesc *var, expdesc *e);
GVTSI_FUNC void gvtsK_setreturns (FuncState *fs, expdesc *e, int nresults);
GVTSI_FUNC void gvtsK_setoneret (FuncState *fs, expdesc *e);
GVTSI_FUNC int gvtsK_jump (FuncState *fs);
GVTSI_FUNC void gvtsK_ret (FuncState *fs, int first, int nret);
GVTSI_FUNC void gvtsK_patchlist (FuncState *fs, int list, int target);
GVTSI_FUNC void gvtsK_patchtohere (FuncState *fs, int list);
GVTSI_FUNC void gvtsK_concat (FuncState *fs, int *l1, int l2);
GVTSI_FUNC int gvtsK_getlabel (FuncState *fs);
GVTSI_FUNC void gvtsK_prefix (FuncState *fs, UnOpr op, expdesc *v, int line);
GVTSI_FUNC void gvtsK_infix (FuncState *fs, BinOpr op, expdesc *v);
GVTSI_FUNC void gvtsK_posfix (FuncState *fs, BinOpr op, expdesc *v1,
                            expdesc *v2, int line);
GVTSI_FUNC void gvtsK_settablesize (FuncState *fs, int pc,
                                  int ra, int asize, int hsize);
GVTSI_FUNC void gvtsK_setlist (FuncState *fs, int base, int nelems, int tostore);
GVTSI_FUNC void gvtsK_finish (FuncState *fs);
GVTSI_FUNC g_noret gvtsK_semerror (LexState *ls, const char *msg);

#endif
