/* { dg-do run { target powerpc*-*-* } } */
/* { dg-do compile { target i?86-*-* } } */
/* { dg-options "-O2 -ftree-vectorize -fdump-tree-vect-stats -maltivec" { target powerpc*-*-* } } */
/* { dg-options "-O2 -ftree-vectorize -fdump-tree-vect-stats -msse2" { target i?86-*-* } } */

#include <stdarg.h>
#include <signal.h>

#define N 128

int main1 (int n, int *p)
{
  int i;
  int ib[N];
  int ia[N];
  int k;

  for (i = 0; i < N; i++)
    {
      ia[i] = n;
    }

  /* check results:  */
  for (i = 0; i < N; i++)
    {
      if (ia[i] != n)
        abort ();
    }

  k = *p;
  for (i = 0; i < N; i++)
    {
      ib[i] = k;
    }

  /* check results:  */
  for (i = 0; i < N; i++)
    {
      if (ib[i] != k)
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
  int m = 8;

  /* Exit on systems without altivec.  */
  signal (SIGILL, sig_ill_handler);
  /* Altivec instruction, 'vor %v0,%v0,%v0'.  */
  asm volatile (".long 0x10000484");
  signal (SIGILL, SIG_DFL);
  
  return main1 (m, &m);
}

/* { dg-final { scan-tree-dump-times "vectorized 2 loops" 1 "vect"} } */

