/* { dg-do run { target powerpc*-*-* } } */
/* { dg-do compile { target i?86-*-* } } */
/* { dg-options "-O2 -ftree-vectorize -fdump-tree-vect-stats -maltivec" { target powerpc*-*-* } } */
/* { dg-options "-O2 -ftree-vectorize -fdump-tree-vect-stats -msse2" { target i?86-*-* } } */
  
#include <stdarg.h>
#include <signal.h>

#define N 20

int
main1 ()
{
  int i;
  float a[N];
  float b[N] = {0,3,6,9,12,15,18,21,24,27,30,33,36,39,42,45,48,51,54,57};
  float c[N] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19};

  for (i = 0; i < N; i++)
    {
      a[i] = b[i] * c[i];
    }

  /* check results:  */
  for (i = 0; i <N; i++)
    {
      if (a[i] != b[i] * c[i])
        abort ();
    }

  return 0;
}

void
sig_ill_handler (int sig)
{
    exit(0);
}

int main (void)
{
  /* Exit on systems without altivec.  */
  signal (SIGILL, sig_ill_handler);
  /* Altivec instruction, 'vor %v0,%v0,%v0'.  */
  asm volatile (".long 0x10000484");
  signal (SIGILL, SIG_DFL);

  return main1 ();
}

/* { dg-final { scan-tree-dump-times "vectorized 1 loops" 1 "vect"} } */
