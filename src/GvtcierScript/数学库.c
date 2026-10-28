#define lmathlib_c
#define GVTS_LIB

#include "Gprefix.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>

#include "gvts.h"

#include "Gauxlib.h"
#include "gvtslib.h"

#undef PI
#define PI	(g_mathop(3.141592653589793238462643383279502884))

static int math_abs (gvts_State *L) {
  if (gvts_isinteger(L, 1)) {
    gvts_Integer n = gvts_tointeger(L, 1);
    if (n < 0) n = (gvts_Integer)(0u - (gvts_Unsigned)n);
    gvts_pushinteger(L, n);
  }
  else
    gvts_pushnumber(L, g_mathop(fabs)(gvtsG_checknumber(L, 1)));
  return 1;
}

static int math_sin (gvts_State *L) {
  gvts_pushnumber(L, g_mathop(sin)(gvtsG_checknumber(L, 1)));
  return 1;
}

static int math_cos (gvts_State *L) {
  gvts_pushnumber(L, g_mathop(cos)(gvtsG_checknumber(L, 1)));
  return 1;
}

static int math_tan (gvts_State *L) {
  gvts_pushnumber(L, g_mathop(tan)(gvtsG_checknumber(L, 1)));
  return 1;
}

static int math_asin (gvts_State *L) {
  gvts_pushnumber(L, g_mathop(asin)(gvtsG_checknumber(L, 1)));
  return 1;
}

static int math_acos (gvts_State *L) {
  gvts_pushnumber(L, g_mathop(acos)(gvtsG_checknumber(L, 1)));
  return 1;
}

static int math_atan (gvts_State *L) {
  gvts_Number y = gvtsG_checknumber(L, 1);
  gvts_Number x = gvtsG_optnumber(L, 2, 1);
  gvts_pushnumber(L, g_mathop(atan2)(y, x));
  return 1;
}

static int math_toint (gvts_State *L) {
  int valid;
  gvts_Integer n = gvts_tointegerx(L, 1, &valid);
  if (g_likely(valid))
    gvts_pushinteger(L, n);
  else {
    gvtsG_checkany(L, 1);
    gvtsG_pushfail(L);                                           
  }
  return 1;
}

static void pushnumint (gvts_State *L, gvts_Number d) {
  gvts_Integer n;
  if (gvts_numbertointeger(d, &n))                                   
    gvts_pushinteger(L, n);                         
  else
    gvts_pushnumber(L, d);                       
}

static int math_floor (gvts_State *L) {
  if (gvts_isinteger(L, 1))
    gvts_settop(L, 1);                                
  else {
    gvts_Number d = g_mathop(floor)(gvtsG_checknumber(L, 1));
    pushnumint(L, d);
  }
  return 1;
}

static int math_ceil (gvts_State *L) {
  if (gvts_isinteger(L, 1))
    gvts_settop(L, 1);                               
  else {
    gvts_Number d = g_mathop(ceil)(gvtsG_checknumber(L, 1));
    pushnumint(L, d);
  }
  return 1;
}

static int math_fmod (gvts_State *L) {
  if (gvts_isinteger(L, 1) && gvts_isinteger(L, 2)) {
    gvts_Integer d = gvts_tointeger(L, 2);
    if ((gvts_Unsigned)d + 1u <= 1u) {                              
      gvtsG_argcheck(L, d != 0, 2, u8"零");
      gvts_pushinteger(L, 0);                                           
    }
    else
      gvts_pushinteger(L, gvts_tointeger(L, 1) % d);
  }
  else
    gvts_pushnumber(L, g_mathop(fmod)(gvtsG_checknumber(L, 1),
                                     gvtsG_checknumber(L, 2)));
  return 1;
}

static int math_modf (gvts_State *L) {
  if (gvts_isinteger(L ,1)) {
    gvts_settop(L, 1);                                      
    gvts_pushnumber(L, 0);                          
  }
  else {
    gvts_Number n = gvtsG_checknumber(L, 1);

    gvts_Number ip = (n < 0) ? g_mathop(ceil)(n) : g_mathop(floor)(n);
    pushnumint(L, ip);

    gvts_pushnumber(L, (n == ip) ? g_mathop(0.0) : (n - ip));
  }
  return 2;
}

static int math_sqrt (gvts_State *L) {
  gvts_pushnumber(L, g_mathop(sqrt)(gvtsG_checknumber(L, 1)));
  return 1;
}

static int math_ult (gvts_State *L) {
  gvts_Integer a = gvtsG_checkinteger(L, 1);
  gvts_Integer b = gvtsG_checkinteger(L, 2);
  gvts_pushboolean(L, (gvts_Unsigned)a < (gvts_Unsigned)b);
  return 1;
}

static int math_log (gvts_State *L) {
  gvts_Number x = gvtsG_checknumber(L, 1);
  gvts_Number res;
  if (gvts_isnoneornil(L, 2))
    res = g_mathop(log)(x);
  else {
    gvts_Number base = gvtsG_checknumber(L, 2);
#if !defined(GVTS_USE_C89)
    if (base == g_mathop(2.0))
      res = g_mathop(log2)(x);
    else
#endif
    if (base == g_mathop(10.0))
      res = g_mathop(log10)(x);
    else
      res = g_mathop(log)(x)/g_mathop(log)(base);
  }
  gvts_pushnumber(L, res);
  return 1;
}

static int math_exp (gvts_State *L) {
  gvts_pushnumber(L, g_mathop(exp)(gvtsG_checknumber(L, 1)));
  return 1;
}

static int math_deg (gvts_State *L) {
  gvts_pushnumber(L, gvtsG_checknumber(L, 1) * (g_mathop(180.0) / PI));
  return 1;
}

static int math_rad (gvts_State *L) {
  gvts_pushnumber(L, gvtsG_checknumber(L, 1) * (PI / g_mathop(180.0)));
  return 1;
}

static int math_min (gvts_State *L) {
  int n = gvts_gettop(L);                           
  int imin = 1;                                      
  int i;
  gvtsG_argcheck(L, n >= 1, 1, u8"需要一个值");
  for (i = 2; i <= n; i++) {
    if (gvts_compare(L, i, imin, GVTS_OPLT))
      imin = i;
  }
  gvts_pushvalue(L, imin);
  return 1;
}

static int math_max (gvts_State *L) {
  int n = gvts_gettop(L);                           
  int imax = 1;                                      
  int i;
  gvtsG_argcheck(L, n >= 1, 1, u8"需要一个值");
  for (i = 2; i <= n; i++) {
    if (gvts_compare(L, imax, i, GVTS_OPLT))
      imax = i;
  }
  gvts_pushvalue(L, imax);
  return 1;
}

static int math_type (gvts_State *L) {
  if (gvts_type(L, 1) == GVTS_TNUMBER)
    gvts_pushstring(L, (gvts_isinteger(L, 1)) ? "integer" : "float");
  else {
    gvtsG_checkany(L, 1);
    gvtsG_pushfail(L);
  }
  return 1;
}

#define FIGS	g_floatatt(MANT_DIG)

#if FIGS > 64

#undef FIGS
#define FIGS	64
#endif

#if !defined(GVTS_RAND32) && !defined(Rand64)

#if ((ULONG_MAX >> 31) >> 31) >= 3

#define Rand64		unsigned long
#define SRand64		long

#elif !defined(GVTS_USE_C89) && defined(LLONG_MAX)

#define Rand64		unsigned long long
#define SRand64		long long

#elif ((GVTS_MAXUNSIGNED >> 31) >> 31) >= 3

#define Rand64		gvts_Unsigned
#define SRand64		gvts_Integer

#endif

#endif

#if defined(Rand64)         

#define trim64(x)	((x) & 0xffffffffffffffffu)

static Rand64 rotl (Rand64 x, int n) {
  return (x << n) | (trim64(x) >> (64 - n));
}

static Rand64 nextrand (Rand64 *state) {
  Rand64 state0 = state[0];
  Rand64 state1 = state[1];
  Rand64 state2 = state[2] ^ state0;
  Rand64 state3 = state[3] ^ state1;
  Rand64 res = rotl(state1 * 5, 7) * 9;
  state[0] = state0 ^ state3;
  state[1] = state1 ^ state2;
  state[2] = state2 ^ (state1 << 17);
  state[3] = rotl(state3, 45);
  return res;
}

#define shift64_FIG	(64 - FIGS)

#define scaleFIG	(g_mathop(0.5) / ((Rand64)1 << (FIGS - 1)))

static gvts_Number I2d (Rand64 x) {
  SRand64 sx = (SRand64)(trim64(x) >> shift64_FIG);
  gvts_Number res = (gvts_Number)(sx) * scaleFIG;
  if (sx < 0)
    res += g_mathop(1.0);                                                
  gvts_assert(0 <= res && res < 1);
  return res;
}

#define I2UInt(x)	((gvts_Unsigned)trim64(x))

#define Int2I(x)	((Rand64)(x))

#else	                      

#if GVTSI_IS32INT
typedef unsigned int lu_int32;
#else
typedef unsigned long lu_int32;
#endif

typedef struct Rand64 {
  lu_int32 h;                   
  lu_int32 l;                  
} Rand64;

#define trim32(x)	((x) & 0xffffffffu)

static Rand64 packI (lu_int32 h, lu_int32 l) {
  Rand64 result;
  result.h = h;
  result.l = l;
  return result;
}

static Rand64 Ishl (Rand64 i, int n) {
  gvts_assert(n > 0 && n < 32);
  return packI((i.h << n) | (trim32(i.l) >> (32 - n)), i.l << n);
}

static void Ixor (Rand64 *i1, Rand64 i2) {
  i1->h ^= i2.h;
  i1->l ^= i2.l;
}

static Rand64 Iadd (Rand64 i1, Rand64 i2) {
  Rand64 result = packI(i1.h + i2.h, i1.l + i2.l);
  if (trim32(result.l) < trim32(i1.l))              
    result.h++;
  return result;
}

static Rand64 times5 (Rand64 i) {
  return Iadd(Ishl(i, 2), i);                             
}

static Rand64 times9 (Rand64 i) {
  return Iadd(Ishl(i, 3), i);                             
}

static Rand64 rotl (Rand64 i, int n) {
  gvts_assert(n > 0 && n < 32);
  return packI((i.h << n) | (trim32(i.l) >> (32 - n)),
               (trim32(i.h) >> (32 - n)) | (i.l << n));
}

static Rand64 rotl1 (Rand64 i, int n) {
  gvts_assert(n > 32 && n < 64);
  n = 64 - n;
  return packI((trim32(i.h) >> n) | (i.l << (32 - n)),
               (i.h << (32 - n)) | (trim32(i.l) >> n));
}

static Rand64 nextrand (Rand64 *state) {
  Rand64 res = times9(rotl(times5(state[1]), 7));
  Rand64 t = Ishl(state[1], 17);
  Ixor(&state[2], state[0]);
  Ixor(&state[3], state[1]);
  Ixor(&state[1], state[2]);
  Ixor(&state[0], state[3]);
  Ixor(&state[2], t);
  state[3] = rotl1(state[3], 45);
  return res;
}

#define UONE		((lu_int32)1)

#if FIGS <= 32

#define scaleFIG       (g_mathop(0.5) / (UONE << (FIGS - 1)))

static gvts_Number I2d (Rand64 x) {
  gvts_Number h = (gvts_Number)(trim32(x.h) >> (32 - FIGS));
  return h * scaleFIG;
}

#else	                     

#define scaleFIG  \
    (g_mathop(1.0) / (UONE << 30) / g_mathop(8.0) / (UONE << (FIGS - 33)))

#define shiftLOW	(64 - FIGS)

#define shiftHI		((gvts_Number)(UONE << (FIGS - 33)) * g_mathop(2.0))

static gvts_Number I2d (Rand64 x) {
  gvts_Number h = (gvts_Number)trim32(x.h) * shiftHI;
  gvts_Number l = (gvts_Number)(trim32(x.l) >> shiftLOW);
  return (h + l) * scaleFIG;
}

#endif

static gvts_Unsigned I2UInt (Rand64 x) {
  return (((gvts_Unsigned)trim32(x.h) << 31) << 1) | (gvts_Unsigned)trim32(x.l);
}

static Rand64 Int2I (gvts_Unsigned n) {
  return packI((lu_int32)((n >> 31) >> 1), (lu_int32)n);
}

#endif         

typedef struct {
  Rand64 s[4];
} RanState;

static gvts_Unsigned project (gvts_Unsigned ran, gvts_Unsigned n,
                             RanState *state) {
  if ((n & (n + 1)) == 0)                                
    return ran & n;               
  else {
    gvts_Unsigned lim = n;

    lim |= (lim >> 1);
    lim |= (lim >> 2);
    lim |= (lim >> 4);
    lim |= (lim >> 8);
    lim |= (lim >> 16);
#if (GVTS_MAXUNSIGNED >> 31) >= 3
    lim |= (lim >> 32);                                          
#endif
    gvts_assert((lim & (lim + 1)) == 0                                  
      && lim >= n                             
      && (lim >> 1) < n);                                  
    while ((ran &= lim) > n)                                   
      ran = I2UInt(nextrand(state->s));                                    
    return ran;
  }
}

static int math_random (gvts_State *L) {
  gvts_Integer low, up;
  gvts_Unsigned p;
  RanState *state = (RanState *)gvts_touserdata(L, gvts_upvalueindex(1));
  Rand64 rv = nextrand(state->s);                                
  switch (gvts_gettop(L)) {                                 
    case 0: {                    
      gvts_pushnumber(L, I2d(rv));                             
      return 1;
    }
    case 1: {                        
      low = 1;
      up = gvtsG_checkinteger(L, 1);
      if (up == 0) {                             
        gvts_pushinteger(L, I2UInt(rv));                           
        return 1;
      }
      break;
    }
    case 2: {                              
      low = gvtsG_checkinteger(L, 1);
      up = gvtsG_checkinteger(L, 2);
      break;
    }
    default: return gvtsG_error(L, u8"参数数量错误");
  }

  gvtsG_argcheck(L, low <= up, 1, u8"区间为空");

  p = project(I2UInt(rv), (gvts_Unsigned)up - (gvts_Unsigned)low, state);
  gvts_pushinteger(L, p + (gvts_Unsigned)low);
  return 1;
}

static void setseed (gvts_State *L, Rand64 *state,
                     gvts_Unsigned n1, gvts_Unsigned n2) {
  int i;
  state[0] = Int2I(n1);
  state[1] = Int2I(0xff);                          
  state[2] = Int2I(n2);
  state[3] = Int2I(0);
  for (i = 0; i < 16; i++)
    nextrand(state);                                               
  gvts_pushinteger(L, n1);
  gvts_pushinteger(L, n2);
}

static void randseed (gvts_State *L, RanState *state) {
  gvts_Unsigned seed1 = (gvts_Unsigned)time(NULL);
  gvts_Unsigned seed2 = (gvts_Unsigned)(size_t)L;
  setseed(L, state->s, seed1, seed2);
}

static int math_randomseed (gvts_State *L) {
  RanState *state = (RanState *)gvts_touserdata(L, gvts_upvalueindex(1));
  if (gvts_isnone(L, 1)) {
    randseed(L, state);
  }
  else {
    gvts_Integer n1 = gvtsG_checkinteger(L, 1);
    gvts_Integer n2 = gvtsG_optinteger(L, 2, 0);
    setseed(L, state->s, n1, n2);
  }
  return 2;                    
}

static const gvtsG_Reg randfuncs[] = {
  {"random", math_random},
  {"randomseed", math_randomseed},
  {NULL, NULL}
};

static void setrandfunc (gvts_State *L) {
  RanState *state = (RanState *)gvts_newuserdatauv(L, sizeof(RanState), 0);
  randseed(L, state);                                       
  gvts_pop(L, 2);                           
  gvtsG_setfuncs(L, randfuncs, 1);
}

#if defined(GVTS_COMPAT_MATHLIB)

static int math_cosh (gvts_State *L) {
  gvts_pushnumber(L, g_mathop(cosh)(gvtsG_checknumber(L, 1)));
  return 1;
}

static int math_sinh (gvts_State *L) {
  gvts_pushnumber(L, g_mathop(sinh)(gvtsG_checknumber(L, 1)));
  return 1;
}

static int math_tanh (gvts_State *L) {
  gvts_pushnumber(L, g_mathop(tanh)(gvtsG_checknumber(L, 1)));
  return 1;
}

static int math_pow (gvts_State *L) {
  gvts_Number x = gvtsG_checknumber(L, 1);
  gvts_Number y = gvtsG_checknumber(L, 2);
  gvts_pushnumber(L, g_mathop(pow)(x, y));
  return 1;
}

static int math_frexp (gvts_State *L) {
  int e;
  gvts_pushnumber(L, g_mathop(frexp)(gvtsG_checknumber(L, 1), &e));
  gvts_pushinteger(L, e);
  return 2;
}

static int math_ldexp (gvts_State *L) {
  gvts_Number x = gvtsG_checknumber(L, 1);
  int ep = (int)gvtsG_checkinteger(L, 2);
  gvts_pushnumber(L, g_mathop(ldexp)(x, ep));
  return 1;
}

static int math_log10 (gvts_State *L) {
  gvts_pushnumber(L, g_mathop(log10)(gvtsG_checknumber(L, 1)));
  return 1;
}

#endif

static const gvtsG_Reg mathlib[] = {
  {"abs",   math_abs},
  {"acos",  math_acos},
  {"asin",  math_asin},
  {"atan",  math_atan},
  {"ceil",  math_ceil},
  {"cos",   math_cos},
  {"deg",   math_deg},
  {"exp",   math_exp},
  {"tointeger", math_toint},
  {"floor", math_floor},
  {"fmod",   math_fmod},
  {"ult",   math_ult},
  {"log",   math_log},
  {"max",   math_max},
  {"min",   math_min},
  {"modf",   math_modf},
  {"rad",   math_rad},
  {"sin",   math_sin},
  {"sqrt",  math_sqrt},
  {"tan",   math_tan},
  {"type", math_type},
#if defined(GVTS_COMPAT_MATHLIB)
  {"atan2", math_atan},
  {"cosh",   math_cosh},
  {"sinh",   math_sinh},
  {"tanh",   math_tanh},
  {"pow",   math_pow},
  {"frexp", math_frexp},
  {"ldexp", math_ldexp},
  {"log10", math_log10},
#endif

  {"random", NULL},
  {"randomseed", NULL},
  {"pi", NULL},
  {"huge", NULL},
  {"maxinteger", NULL},
  {"mininteger", NULL},
  {NULL, NULL}
};

GVTSMOD_API int gvtsopen_math (gvts_State *L) {
  gvtsG_newlib(L, mathlib);
  gvts_pushnumber(L, PI);
  gvts_setfield(L, -2, "pi");
  gvts_pushnumber(L, (gvts_Number)HUGE_VAL);
  gvts_setfield(L, -2, "huge");
  gvts_pushinteger(L, GVTS_MAXINTEGER);
  gvts_setfield(L, -2, "maxinteger");
  gvts_pushinteger(L, GVTS_MININTEGER);
  gvts_setfield(L, -2, "mininteger");
  setrandfunc(L);
  return 1;
}
