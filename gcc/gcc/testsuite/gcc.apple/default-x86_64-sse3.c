/* APPLE LOCAL file 4515157 */
/* { dg-do compile { target x86_64-*-* } } */
/* { dg-options "-m64" } */
/* Insure that -m64 implies -msse3 on Darwin/x86.  */
#include <pmmintrin.h>
main ()
{
  static __m128 x, y, z;
  x = _mm_hadd_ps (y, z);	/* An SSE3 intrinsic.  */
  exit (0);
}
