#ifndef INCLUDE_STB_DIVIDE_H
#define INCLUDE_STB_DIVIDE_H

#ifdef __cplusplus
extern "C" {
#endif

extern int stb_div_trunc(int value_to_be_divided, int value_to_divide_by);
extern int stb_div_floor(int value_to_be_divided, int value_to_divide_by);
extern int stb_div_eucl (int value_to_be_divided, int value_to_divide_by);
extern int stb_mod_trunc(int value_to_be_divided, int value_to_divide_by);
extern int stb_mod_floor(int value_to_be_divided, int value_to_divide_by);
extern int stb_mod_eucl (int value_to_be_divided, int value_to_divide_by);

#ifdef __cplusplus
}
#endif

#ifdef STB_DIVIDE_IMPLEMENTATION

#if defined(__STDC_VERSION) && __STDC_VERSION__ >= 19901
   #ifndef C_INTEGER_DIVISION_TRUNCATES
      #define C_INTEGER_DIVISION_TRUNCATES
   #endif
#endif

#ifndef INT_MIN
#include <limits.h> 
#endif

#ifndef STB_DIVIDE_TEST_FLOOR
   #define stb__div(a,b)  ((a)/(b))
   #define stb__mod(a,b)  ((a)%(b))
#else

   #ifndef C_INTEGER_DIVISION_TRUNCATES
   #error "floor test requires truncating division"
   #endif
   #undef C_INTEGER_DIVISION_TRUNCATES
   int stb__div(int v1, int v2)
   {
      int q = v1/v2, r = v1%v2;
      if ((r > 0 && v2 < 0) || (r < 0 && v2 > 0))
         return q-1;
      else
         return q;
   }

   int stb__mod(int v1, int v2)
   {
      int r = v1%v2;
      if ((r > 0 && v2 < 0) || (r < 0 && v2 > 0))
         return r+v2;
      else
         return r;
   }
#endif

int stb_div_trunc(int v1, int v2)
{
   #ifdef C_INTEGER_DIVISION_TRUNCATES
   return v1/v2;
   #else
   if (v1 >= 0 && v2 <= 0)
      return -stb__div(-v1,v2);  
   if (v1 <= 0 && v2 >= 0)
      if (v1 != INT_MIN)
         return -stb__div(v1,-v2);    
      else
         return -stb__div(v1+v2,-v2)-1; 
   else
      return v1/v2;            
   #endif
}

int stb_div_floor(int v1, int v2)
{
   #ifdef C_INTEGER_DIVISION_FLOORS
   return v1/v2;
   #else
   if (v1 >= 0 && v2 < 0) {
      if (v2 + 1 >= INT_MIN + v1) 
         return -stb__div((v2+1)-v1,v2); 
      else
         return -stb__div(-v1,v2) + ((-v1)%v2 ? -1 : 0);
   }
   if (v1 < 0 && v2 >= 0) {
      if (v1 != INT_MIN) {
         if (v1 + 1 >= INT_MIN + v2) 
            return -stb__div((v1+1)-v2,-v2); 
         else
            return -stb__div(-v1,v2) + (stb__mod(v1,-v2) ? -1 : 0);
      } else 
         return -stb__div(-(v1+v2),v2) + (stb__mod(-(v1+v2),v2) ? -2 : -1);
   } else
      return v1/v2;           
   #endif
}

int stb_div_eucl(int v1, int v2)
{
   int q,r;
   #ifdef C_INTEGER_DIVISION_TRUNCATES
   q = v1/v2;
   r = v1%v2;
   #else

   if (v1 >= 0)
      if (v2 >= 0)
         return stb__div(v1,v2);
      else if (v2 != INT_MIN)
         q = -stb__div(v1,-v2), r = stb__mod(v1,-v2);
      else
         q = 0, r = v1;
   else if (v1 != INT_MIN)
      if (v2 >= 0)
         q = -stb__div(-v1,v2), r = -stb__mod(-v1,v2);
      else if (v2 != INT_MIN)
         q = stb__div(-v1,-v2), r = -stb__mod(-v1,-v2);
      else 
         q = 1, r = v1-q*v2;
   else 
      if (v2 >= 0)
         q = -stb__div(-(v1+v2),v2)-1, r = -stb__mod(-(v1+v2),v2);
      else if (v2 != INT_MIN)
         q = stb__div(-(v1-v2),-v2)+1, r = -stb__mod(-(v1-v2),-v2);
      else 
         q = 1, r = 0;
   #endif
   if (r >= 0)
      return q;
   else
      return q + (v2 > 0 ? -1 : 1);
}

int stb_mod_trunc(int v1, int v2)
{
   #ifdef C_INTEGER_DIVISION_TRUNCATES
   return v1%v2;
   #else
   if (v1 >= 0) { 
      int r = stb__mod(v1,v2);
      if (r >= 0)
         return r;
      else
         return r - (v2 < 0 ? v2 : -v2);
   } else {    
      int r = stb__mod(v1,v2);
      if (r <= 0)
         return r;
      else
         return r + (v2 < 0 ? v2 : -v2);
   }
   #endif
}

int stb_mod_floor(int v1, int v2)
{
   #ifdef C_INTEGER_DIVISION_FLOORS
   return v1%v2;
   #else
   if (v2 >= 0) { 
      int r = stb__mod(v1,v2);
      if (r >= 0)
         return r;
      else
         return r + v2;
   } else { 
      int r = stb__mod(v1,v2);
      if (r <= 0)
         return r;
      else
         return r + v2;
   }
   #endif
}

int stb_mod_eucl(int v1, int v2)
{
   int r = stb__mod(v1,v2);

   if (r >= 0)
      return r;
   else
      return r - (v2 < 0 ? v2 : -v2); 
}

#ifdef STB_DIVIDE_TEST
#include <stdio.h>
#include <math.h>
#include <limits.h>

int show=0;
int err=0;

void stbdiv_check(int q, int r, int a, int b, char *type, int dir)
{
   if ((dir > 0 && r < 0) || (dir < 0 && r > 0)) {
      fprintf(stderr, "FAILED: %s(%d,%d) remainder %d in wrong direction\n", type,a,b,r);
      err++;
   } else
      if (b != INT_MIN) 
         if (r <= -abs(b) || r >= abs(b)) {
            fprintf(stderr, "FAILED: %s(%d,%d) remainder %d out of range\n", type,a,b,r);
            err++;
         }
   #ifdef STB_DIVIDE_TEST_64
   {
      STB_DIVIDE_TEST_64 q64 = q, r64=r, a64=a, b64=b;
      if (q64*b64+r64 != a64) {
         fprintf(stderr, "FAILED: %s(%d,%d) remainder %d doesn't match quotient %d\n", type,a,b,r,q);
         err++;
      }
   }
   #else
   if (q*b+r != a) {
      fprintf(stderr, "FAILED: %s(%d,%d) remainder %d doesn't match quotient %d\n", type,a,b,r,q);
      err++;
   }
   #endif
}

void test(int a, int b)
{
   int q,r;
   if (show) printf("(%+11d,%+d) |  ", a,b);
   q = stb_div_trunc(a,b), r = stb_mod_trunc(a,b);
   if (show) printf("(%+11d,%+2d)  ", q,r); stbdiv_check(q,r,a,b, "trunc",a);
   q = stb_div_floor(a,b), r = stb_mod_floor(a,b);
   if (show) printf("(%+11d,%+2d)  ", q,r); stbdiv_check(q,r,a,b, "floor",b);
   q = stb_div_eucl (a,b), r = stb_mod_eucl (a,b);
   if (show) printf("(%+11d,%+2d)\n", q,r); stbdiv_check(q,r,a,b, "euclidean",1);
}

void testh(int a, int b)
{
   int q,r;
   if (show) printf("(%08x,%08x) |\n", a,b);
   q = stb_div_trunc(a,b), r = stb_mod_trunc(a,b); stbdiv_check(q,r,a,b, "trunc",a);
   if (show) printf("             (%08x,%08x)", q,r);
   q = stb_div_floor(a,b), r = stb_mod_floor(a,b); stbdiv_check(q,r,a,b, "floor",b);
   if (show) printf("   (%08x,%08x)", q,r);
   q = stb_div_eucl (a,b), r = stb_mod_eucl (a,b); stbdiv_check(q,r,a,b, "euclidean",1);
   if (show) printf("   (%08x,%08x)\n ", q,r);
}

int main(int argc, char **argv)
{
   if (argc > 1) show=1;

   test(8,3);
   test(8,-3);
   test(-8,3);
   test(-8,-3);
   test(1,2);
   test(1,-2);
   test(-1,2);
   test(-1,-2);
   test(8,4);
   test(8,-4);
   test(-8,4);
   test(-8,-4);

   test(INT_MAX,1);
   test(INT_MIN,1);
   test(INT_MIN+1,1);
   test(INT_MAX,-1);

   test(INT_MIN+1,-1);
   test(INT_MIN,-2);
   test(INT_MIN+1,2);
   test(INT_MIN+1,-2);
   test(INT_MAX,2);
   test(INT_MAX,-2);
   test(INT_MIN+1,2);
   test(INT_MIN+1,-2);
   test(INT_MIN,2);
   test(INT_MIN,-2);
   test(INT_MIN,7);
   test(INT_MIN,-7);
   test(INT_MIN+1,4);
   test(INT_MIN+1,-4);

   testh(-7, INT_MIN);
   testh(-1, INT_MIN);
   testh(1, INT_MIN);
   testh(7, INT_MIN);

   testh(INT_MAX-1, INT_MIN);
   testh(INT_MAX,   INT_MIN);
   testh(INT_MIN,   INT_MIN);
   testh(INT_MIN+1, INT_MIN);

   testh(INT_MAX-1, INT_MAX);
   testh(INT_MAX  , INT_MAX);
   testh(INT_MIN  , INT_MAX);
   testh(INT_MIN+1, INT_MAX);

   return err > 0 ? 1 : 0;
}
#endif 
#endif 
#endif 
