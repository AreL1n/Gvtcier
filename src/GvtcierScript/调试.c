#define ldebug_c
#define GVTS_CORE

#include "Gprefix.h"

#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#include "gvts.h"

#include "Gapi.h"
#include "Gcode.h"
#include "Gdebug.h"
#include "Gdo.h"
#include "Gfunc.h"
#include "Gobject.h"
#include "Gopcodes.h"
#include "Gstate.h"
#include "Gstring.h"
#include "Gtable.h"
#include "Gtm.h"
#include "Gvm.h"

#define GvtsClosure(f)		((f) != NULL && (f)->c.tt == GVTS_VLCL)

static const char *funcnamefromcall (gvts_State *L, CallInfo *ci,
                                                   const char **name);

static int currentpc (CallInfo *ci) {
  gvts_assert(isGvts(ci));
  return pcRel(ci->u.l.savedpc, ci_func(ci)->p);
}

static int getbaseline (const Proto *f, int pc, int *basepc) {
  if (f->sizeabslineinfo == 0 || pc < f->abslineinfo[0].pc) {
    *basepc = -1;                                
    return f->linedefined;
  }
  else {
    int i = cast_uint(pc) / MAXIWTHABS - 1;                       

    gvts_assert(i < 0 ||
              (i < f->sizeabslineinfo && f->abslineinfo[i].pc <= pc));
    while (i + 1 < f->sizeabslineinfo && pc >= f->abslineinfo[i + 1].pc)
      i++;                               
    *basepc = f->abslineinfo[i].pc;
    return f->abslineinfo[i].line;
  }
}

int gvtsG_getfuncline (const Proto *f, int pc) {
  if (f->lineinfo == NULL)                             
    return -1;
  else {
    int basepc;
    int baseline = getbaseline(f, pc, &basepc);
    while (basepc++ < pc) {                                    
      gvts_assert(f->lineinfo[basepc] != ABSLINEINFO);
      baseline += f->lineinfo[basepc];                    
    }
    return baseline;
  }
}

static int getcurrentline (CallInfo *ci) {
  return gvtsG_getfuncline(ci_func(ci)->p, currentpc(ci));
}

static void settraps (CallInfo *ci) {
  for (; ci != NULL; ci = ci->previous)
    if (isGvts(ci))
      ci->u.l.trap = 1;
}

GVTS_API void gvts_sethook (gvts_State *L, gvts_Hook func, int mask, int count) {
  if (func == NULL || mask == 0) {                       
    mask = 0;
    func = NULL;
  }
  L->hook = func;
  L->basehookcount = count;
  resethookcount(L);
  L->hookmask = cast_byte(mask);
  if (mask)
    settraps(L->ci);                                      
}

GVTS_API gvts_Hook gvts_gethook (gvts_State *L) {
  return L->hook;
}

GVTS_API int gvts_gethookmask (gvts_State *L) {
  return L->hookmask;
}

GVTS_API int gvts_gethookcount (gvts_State *L) {
  return L->basehookcount;
}

GVTS_API int gvts_getstack (gvts_State *L, int level, gvts_Debug *ar) {
  int status;
  CallInfo *ci;
  if (level < 0) return 0;                                
  gvts_lock(L);
  for (ci = L->ci; level > 0 && ci != &L->base_ci; ci = ci->previous)
    level--;
  if (level == 0 && ci != &L->base_ci) {                    
    status = 1;
    ar->i_ci = ci;
  }
  else status = 0;                     
  gvts_unlock(L);
  return status;
}

static const char *upvalname (const Proto *p, int uv) {
  TString *s = check_exp(uv < p->sizeupvalues, p->upvalues[uv].name);
  if (s == NULL) return "?";
  else return getstr(s);
}

static const char *findvararg (CallInfo *ci, int n, StkId *pos) {
  if (clLvalue(s2v(ci->func.p))->p->is_vararg) {
    int nextra = ci->u.l.nextraargs;
    if (n >= -nextra) {                       
      *pos = ci->func.p - nextra - (n + 1);
      return "(vararg)";                                   
    }
  }
  return NULL;                      
}

const char *gvtsG_findlocal (gvts_State *L, CallInfo *ci, int n, StkId *pos) {
  StkId base = ci->func.p + 1;
  const char *name = NULL;
  if (isGvts(ci)) {
    if (n < 0)                                
      return findvararg(ci, n, pos);
    else
      name = gvtsF_getlocalname(ci_func(ci)->p, n, currentpc(ci));
  }
  if (name == NULL) {                           
    StkId limit = (ci == L->ci) ? L->top.p : ci->next->func.p;
    if (limit - base >= n && n > 0) {                                 

      name = isGvts(ci) ? "(temporary)" : "(C temporary)";
    }
    else
      return NULL;               
  }
  if (pos)
    *pos = base + (n - 1);
  return name;
}

GVTS_API const char *gvts_getlocal (gvts_State *L, const gvts_Debug *ar, int n) {
  const char *name;
  gvts_lock(L);
  if (ar == NULL) {                                              
    if (!isLfunction(s2v(L->top.p - 1)))                           
      name = NULL;
    else                                                              
      name = gvtsF_getlocalname(clLvalue(s2v(L->top.p - 1))->p, n, 0);
  }
  else {                                                     
    StkId pos = NULL;                         
    name = gvtsG_findlocal(L, ar->i_ci, n, &pos);
    if (name) {
      setobjs2s(L, L->top.p, pos);
      api_incr_top(L);
    }
  }
  gvts_unlock(L);
  return name;
}

GVTS_API const char *gvts_setlocal (gvts_State *L, const gvts_Debug *ar, int n) {
  StkId pos = NULL;                         
  const char *name;
  gvts_lock(L);
  name = gvtsG_findlocal(L, ar->i_ci, n, &pos);
  if (name) {
    setobjs2s(L, pos, L->top.p - 1);
    L->top.p--;                 
  }
  gvts_unlock(L);
  return name;
}

static void funcinfo (gvts_Debug *ar, Closure *cl) {
  if (!GvtsClosure(cl)) {
    ar->source = "=[C]";
    ar->srclen = LL("=[C]");
    ar->linedefined = -1;
    ar->lastlinedefined = -1;
    ar->what = "C";
  }
  else {
    const Proto *p = cl->l.p;
    if (p->source) {
      ar->source = getstr(p->source);
      ar->srclen = tsslen(p->source);
    }
    else {
      ar->source = "=?";
      ar->srclen = LL("=?");
    }
    ar->linedefined = p->linedefined;
    ar->lastlinedefined = p->lastlinedefined;
    ar->what = (ar->linedefined == 0) ? "main" : "Gvts";
  }
  gvtsO_chunkid(ar->short_src, ar->source, ar->srclen);
}

static int nextline (const Proto *p, int currentline, int pc) {
  if (p->lineinfo[pc] != ABSLINEINFO)
    return currentline + p->lineinfo[pc];
  else
    return gvtsG_getfuncline(p, pc);
}

static void collectvalidlines (gvts_State *L, Closure *f) {
  if (!GvtsClosure(f)) {
    setnilvalue(s2v(L->top.p));
    api_incr_top(L);
  }
  else {
    const Proto *p = f->l.p;
    int currentline = p->linedefined;
    Table *t = gvtsH_new(L);                                       
    sethvalue2s(L, L->top.p, t);                        
    api_incr_top(L);
    if (p->lineinfo != NULL) {                                     
      int i;
      TValue v;
      setbtvalue(&v);                                                     
      if (!p->is_vararg)                         
        i = 0;                                 
      else {                       
        gvts_assert(GET_OPCODE(p->code[0]) == OP_VARARGPREP);
        currentline = nextline(p, currentline, 0);
        i = 1;                                              
      }
      for (; i < p->sizelineinfo; i++) {                            
        currentline = nextline(p, currentline, i);                    
        gvtsH_setint(L, t, currentline, &v);                          
      }
    }
  }
}

static const char *getfuncname (gvts_State *L, CallInfo *ci, const char **name) {

  if (ci != NULL && !(ci->callstatus & CIST_TAIL))
    return funcnamefromcall(L, ci->previous, name);
  else return NULL;                             
}

static int auxgetinfo (gvts_State *L, const char *what, gvts_Debug *ar,
                       Closure *f, CallInfo *ci) {
  int status = 1;
  for (; *what; what++) {
    switch (*what) {
      case 'S': {
        funcinfo(ar, f);
        break;
      }
      case 'l': {
        ar->currentline = (ci && isGvts(ci)) ? getcurrentline(ci) : -1;
        break;
      }
      case 'u': {
        ar->nups = (f == NULL) ? 0 : f->c.nupvalues;
        if (!GvtsClosure(f)) {
          ar->isvararg = 1;
          ar->nparams = 0;
        }
        else {
          ar->isvararg = f->l.p->is_vararg;
          ar->nparams = f->l.p->numparams;
        }
        break;
      }
      case 't': {
        ar->istailcall = (ci) ? ci->callstatus & CIST_TAIL : 0;
        break;
      }
      case 'n': {
        ar->namewhat = getfuncname(L, ci, &ar->name);
        if (ar->namewhat == NULL) {
          ar->namewhat = "";                 
          ar->name = NULL;
        }
        break;
      }
      case 'r': {
        if (ci == NULL || !(ci->callstatus & CIST_TRAN))
          ar->ftransfer = ar->ntransfer = 0;
        else {
          ar->ftransfer = ci->u2.transferinfo.ftransfer;
          ar->ntransfer = ci->u2.transferinfo.ntransfer;
        }
        break;
      }
      case 'L':
      case 'f':                              
        break;
      default: status = 0;                      
    }
  }
  return status;
}

GVTS_API int gvts_getinfo (gvts_State *L, const char *what, gvts_Debug *ar) {
  int status;
  Closure *cl;
  CallInfo *ci;
  TValue *func;
  gvts_lock(L);
  if (*what == '>') {
    ci = NULL;
    func = s2v(L->top.p - 1);
    api_check(L, ttisfunction(func), "function expected");
    what++;                    
    L->top.p--;                    
  }
  else {
    ci = ar->i_ci;
    func = s2v(ci->func.p);
    gvts_assert(ttisfunction(func));
  }
  cl = ttisclosure(func) ? clvalue(func) : NULL;
  status = auxgetinfo(L, what, ar, cl, ci);
  if (strchr(what, 'f')) {
    setobj2s(L, L->top.p, func);
    api_incr_top(L);
  }
  if (strchr(what, 'L'))
    collectvalidlines(L, cl);
  gvts_unlock(L);
  return status;
}

static int filterpc (int pc, int jmptarget) {
  if (pc < jmptarget)                                            
    return -1;                                          
  else return pc;                                           
}

static int findsetreg (const Proto *p, int lastpc, int reg) {
  int pc;
  int setreg = -1;                                                
  int jmptarget = 0;                                                   
  if (testMMMode(GET_OPCODE(p->code[lastpc])))
    lastpc--;                                                      
  for (pc = 0; pc < lastpc; pc++) {
    Instruction i = p->code[pc];
    OpCode op = GET_OPCODE(i);
    int a = GETARG_A(i);
    int change;                                                 
    switch (op) {
      case OP_LOADNIL: {                                       
        int b = GETARG_B(i);
        change = (a <= reg && reg <= a + b);
        break;
      }
      case OP_TFORCALL: {                                      
        change = (reg >= a + 2);
        break;
      }
      case OP_CALL:
      case OP_TAILCALL: {                                       
        change = (reg >= a);
        break;
      }
      case OP_JMP: {                                                         
        int b = GETARG_sJ(i);
        int dest = pc + 1 + b;

        if (dest <= lastpc && dest > jmptarget)
          jmptarget = dest;                          
        change = 0;
        break;
      }
      default:                                   
        change = (testAMode(op) && reg == a);
        break;
    }
    if (change)
      setreg = filterpc(pc, jmptarget);
  }
  return setreg;
}

static const char *kname (const Proto *p, int index, const char **name) {
  TValue *kvalue = &p->k[index];
  if (ttisstring(kvalue)) {
    *name = getstr(tsvalue(kvalue));
    return u8"常量";
  }
  else {
    *name = "?";
    return NULL;
  }
}

static const char *basicgetobjname (const Proto *p, int *ppc, int reg,
                                    const char **name) {
  int pc = *ppc;
  *name = gvtsF_getlocalname(p, reg + 1, pc);
  if (*name)                   
    return u8"局部";

  *ppc = pc = findsetreg(p, pc, reg);
  if (pc != -1) {                               
    Instruction i = p->code[pc];
    OpCode op = GET_OPCODE(i);
    switch (op) {
      case OP_MOVE: {
        int b = GETARG_B(i);                            
        if (b < GETARG_A(i))
          return basicgetobjname(p, ppc, b, name);                        
        break;
      }
      case OP_GETUPVAL: {
        *name = upvalname(p, GETARG_B(i));
        return u8"上值";
      }
      case OP_LOADK: return kname(p, GETARG_Bx(i), name);
      case OP_LOADKX: return kname(p, GETARG_Ax(p->code[pc + 1]), name);
      default: break;
    }
  }
  return NULL;                                      
}

static void rname (const Proto *p, int pc, int c, const char **name) {
  const char *what = basicgetobjname(p, &pc, c, name);                     
  if (!(what && *what == 'c'))                                     
    *name = "?";
}

static void rkname (const Proto *p, int pc, Instruction i, const char **name) {
  int c = GETARG_C(i);                 
  if (GETARG_k(i))                          
    kname(p, c, name);
  else                         
    rname(p, pc, c, name);
}

static const char *isEnv (const Proto *p, int pc, Instruction i, int isup) {
  int t = GETARG_B(i);                   
  const char *name;                                
  if (isup)                          
    name = upvalname(p, t);
  else                         
    basicgetobjname(p, &pc, t, &name);
  return (name && strcmp(name, GVTS_ENV) == 0) ? u8"全局" : u8"字段";
}

static const char *getobjname (const Proto *p, int lastpc, int reg,
                               const char **name) {
  const char *kind = basicgetobjname(p, &lastpc, reg, name);
  if (kind != NULL)
    return kind;
  else if (lastpc != -1) {                               
    Instruction i = p->code[lastpc];
    OpCode op = GET_OPCODE(i);
    switch (op) {
      case OP_GETTABUP: {
        int k = GETARG_C(i);                 
        kname(p, k, name);
        return isEnv(p, lastpc, i, 1);
      }
      case OP_GETTABLE: {
        int k = GETARG_C(i);                 
        rname(p, lastpc, k, name);
        return isEnv(p, lastpc, i, 0);
      }
      case OP_GETI: {
        *name = u8"整数索引";
        return u8"字段";
      }
      case OP_GETFIELD: {
        int k = GETARG_C(i);                 
        kname(p, k, name);
        return isEnv(p, lastpc, i, 0);
      }
      case OP_SELF: {
        rkname(p, lastpc, i, name);
        return u8"方法";
      }
      default: break;                                 
    }
  }
  return NULL;                                      
}

static const char *funcnamefromcode (gvts_State *L, const Proto *p,
                                     int pc, const char **name) {
  TMS tm = (TMS)0;                                       
  Instruction i = p->code[pc];                           
  switch (GET_OPCODE(i)) {
    case OP_CALL:
    case OP_TAILCALL:
      return getobjname(p, pc, GETARG_A(i), name);                         
    case OP_TFORCALL: {                    
      *name = u8"迭代器";
       return u8"迭代器";
    }

    case OP_SELF: case OP_GETTABUP: case OP_GETTABLE:
    case OP_GETI: case OP_GETFIELD:
      tm = TM_INDEX;
      break;
    case OP_SETTABUP: case OP_SETTABLE: case OP_SETI: case OP_SETFIELD:
      tm = TM_NEWINDEX;
      break;
    case OP_MMBIN: case OP_MMBINI: case OP_MMBINK: {
      tm = cast(TMS, GETARG_C(i));
      break;
    }
    case OP_UNM: tm = TM_UNM; break;
    case OP_BNOT: tm = TM_BNOT; break;
    case OP_LEN: tm = TM_LEN; break;
    case OP_CONCAT: tm = TM_CONCAT; break;
    case OP_EQ: tm = TM_EQ; break;

    case OP_LT: case OP_LTI: case OP_GTI: tm = TM_LT; break;
    case OP_LE: case OP_LEI: case OP_GEI: tm = TM_LE; break;
    case OP_CLOSE: case OP_RETURN: tm = TM_CLOSE; break;
    default:
      return NULL;                                     
  }
  *name = getshrstr(G(L)->tmname[tm]) + 2;
  return u8"元方法";
}

static const char *funcnamefromcall (gvts_State *L, CallInfo *ci,
                                                   const char **name) {
  if (ci->callstatus & CIST_HOOKED) {                                    
    *name = "?";
    return u8"钩子";
  }
  else if (ci->callstatus & CIST_FIN) {                                     
    *name = "__gc";
    return u8"元方法";                         
  }
  else if (isGvts(ci))
    return funcnamefromcode(L, ci_func(ci)->p, currentpc(ci), name);
  else
    return NULL;
}

static int instack (CallInfo *ci, const TValue *o) {
  int pos;
  StkId base = ci->func.p + 1;
  for (pos = 0; base + pos < ci->top.p; pos++) {
    if (o == s2v(base + pos))
      return pos;
  }
  return -1;                 
}

static const char *getupvalname (CallInfo *ci, const TValue *o,
                                 const char **name) {
  LClosure *c = ci_func(ci);
  int i;
  for (i = 0; i < c->nupvalues; i++) {
    if (c->upvals[i]->v.p == o) {
      *name = upvalname(c->p, i);
      return u8"上值";
    }
  }
  return NULL;
}

static const char *formatvarinfo (gvts_State *L, const char *kind,
                                                const char *name) {
  if (kind == NULL)
    return "";                      
  else
    return gvtsO_pushfstring(L, u8"（%s「%s」）", kind, name);
}

static const char *varinfo (gvts_State *L, const TValue *o) {
  CallInfo *ci = L->ci;
  const char *name = NULL;                         
  const char *kind = NULL;
  if (ttisnil(o))
    return "";                      
  if (isGvts(ci)) {
    kind = getupvalname(ci, o, &name);                                       
    if (!kind) {                       
      int reg = instack(ci, o);                      
      if (reg >= 0)                          
        kind = getobjname(ci_func(ci)->p, currentpc(ci), reg, &name);
    }
  }
  return formatvarinfo(L, kind, name);
}

static g_noret typeerror (gvts_State *L, const TValue *o, const char *op,
                          const char *extra) {
  const char *t = gvtsT_objtypename(L, o);
  gvtsG_runerror(L, u8"不能对%s执行“%s”%s", t, op, extra);
}

g_noret gvtsG_typeerror (gvts_State *L, const TValue *o, const char *op) {
  typeerror(L, o, op, varinfo(L, o));
}

g_noret gvtsG_callerror (gvts_State *L, const TValue *o) {
  CallInfo *ci = L->ci;
  const char *name = NULL;                         
  const char *kind = funcnamefromcall(L, ci, &name);
  const char *extra = kind ? formatvarinfo(L, kind, name) : varinfo(L, o);
  typeerror(L, o, u8"调用", extra);
}

g_noret gvtsG_forerror (gvts_State *L, const TValue *o, const char *what) {
  gvtsG_runerror(L, u8"for 循环的“%s”应为数字，实得%s",
                   what, gvtsT_objtypename(L, o));
}

g_noret gvtsG_concaterror (gvts_State *L, const TValue *p1, const TValue *p2) {
  if (ttisstring(p1) || cvt2str(p1)) p1 = p2;
  gvtsG_typeerror(L, p1, u8"拼接");
}

g_noret gvtsG_opinterror (gvts_State *L, const TValue *p1,
                         const TValue *p2, const char *msg) {
  if (!ttisnumber(p1))                               
    p2 = p1;                           
  gvtsG_typeerror(L, p2, msg);
}

g_noret gvtsG_tointerror (gvts_State *L, const TValue *p1, const TValue *p2) {
  gvts_Integer temp;
  if (!gvtsV_tointegerns(p1, &temp, GVTS_FLOORN2I))
    p2 = p1;
  gvtsG_runerror(L, u8"数字%s没有整数表示", varinfo(L, p2));
}

g_noret gvtsG_ordererror (gvts_State *L, const TValue *p1, const TValue *p2) {
  const char *t1 = gvtsT_objtypename(L, p1);
  const char *t2 = gvtsT_objtypename(L, p2);
  if (strcmp(t1, t2) == 0)
    gvtsG_runerror(L, u8"不能比较两个%s值", t1);
  else
    gvtsG_runerror(L, u8"不能比较%s与%s", t1, t2);
}

const char *gvtsG_addinfo (gvts_State *L, const char *msg, TString *src,
                                        int line) {
  char buff[GVTS_IDSIZE];
  if (src)
    gvtsO_chunkid(buff, getstr(src), tsslen(src));
  else {                                            
    buff[0] = '?'; buff[1] = '\0';
  }
  return gvtsO_pushfstring(L, u8"%s:%d: 错误: %s", buff, line, msg);
}

g_noret gvtsG_errormsg (gvts_State *L) {
  if (L->errfunc != 0) {                                            
    StkId errfunc = restorestack(L, L->errfunc);
    gvts_assert(ttisfunction(s2v(errfunc)));
    setobjs2s(L, L->top.p, L->top.p - 1);                     
    setobjs2s(L, L->top.p - 1, errfunc);                     
    L->top.p++;                          
    gvtsD_callnoyield(L, L->top.p - 2, 1);               
  }
  gvtsD_throw(L, GVTS_ERRRUN);
}

g_noret gvtsG_runerror (gvts_State *L, const char *fmt, ...) {
  CallInfo *ci = L->ci;
  const char *msg;
  va_list argp;
  gvtsC_checkGC(L);                                 
  va_start(argp, fmt);
  msg = gvtsO_pushvfstring(L, fmt, argp);                      
  va_end(argp);
  if (isGvts(ci)) {                                                    
    gvtsG_addinfo(L, msg, ci_func(ci)->p->source, getcurrentline(ci));
    setobjs2s(L, L->top.p - 2, L->top.p - 1);                    
    L->top.p--;
  }
  gvtsG_errormsg(L);
}

static int changedline (const Proto *p, int oldpc, int newpc) {
  if (p->lineinfo == NULL)                             
    return 0;
  if (newpc - oldpc < MAXIWTHABS / 2) {                          
    int delta = 0;                       
    int pc = oldpc;
    for (;;) {
      int lineinfo = p->lineinfo[++pc];
      if (lineinfo == ABSLINEINFO)
        break;                                          
      delta += lineinfo;
      if (pc == newpc)
        return (delta != 0);                                   
    }
  }

  return (gvtsG_getfuncline(p, oldpc) != gvtsG_getfuncline(p, newpc));
}

int gvtsG_tracecall (gvts_State *L) {
  CallInfo *ci = L->ci;
  Proto *p = ci_func(ci)->p;
  ci->u.l.trap = 1;                                    
  if (ci->u.l.savedpc == p->code) {                                         
    if (p->is_vararg)
      return 0;                                                  
    else if (!(ci->callstatus & CIST_HOOKYIELD))                   
      gvtsD_hookcall(L, ci);                         
  }
  return 1;                      
}

int gvtsG_traceexec (gvts_State *L, const Instruction *pc) {
  CallInfo *ci = L->ci;
  lu_byte mask = L->hookmask;
  const Proto *p = ci_func(ci)->p;
  int counthook;
  if (!(mask & (GVTS_MASKLINE | GVTS_MASKCOUNT))) {                 
    ci->u.l.trap = 0;                                
    return 0;                       
  }
  pc++;                                            
  ci->u.l.savedpc = pc;                 
  counthook = (mask & GVTS_MASKCOUNT) && (--L->hookcount == 0);
  if (counthook)
    resethookcount(L);                   
  else if (!(mask & GVTS_MASKLINE))
    return 1;                                                           
  if (ci->callstatus & CIST_HOOKYIELD) {                               
    ci->callstatus &= ~CIST_HOOKYIELD;                  
    return 1;                                                               
  }
  if (!isIT(*(ci->u.l.savedpc - 1)))                           
    L->top.p = ci->top.p;                   
  if (counthook)
    gvtsD_hook(L, GVTS_HOOKCOUNT, -1, 0, 0);                       
  if (mask & GVTS_MASKLINE) {

    int oldpc = (L->oldpc < p->sizecode) ? L->oldpc : 0;
    int npci = pcRel(pc, p);
    if (npci <= oldpc ||                                        
        changedline(p, oldpc, npci)) {                              
      int newline = gvtsG_getfuncline(p, npci);
      gvtsD_hook(L, GVTS_HOOKLINE, newline, 0, 0);                      
    }
    L->oldpc = npci;                                      
  }
  if (L->status == GVTS_YIELD) {                       
    if (counthook)
      L->hookcount = 1;                              
    ci->callstatus |= CIST_HOOKYIELD;                            
    gvtsD_throw(L, GVTS_YIELD);
  }
  return 1;                      
}
