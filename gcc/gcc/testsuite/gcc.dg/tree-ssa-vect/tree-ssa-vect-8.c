/* { dg-do run { target powerpc*-*-* } } */
/* { dg-do compile { target i?86-*-* } } */
/* { dg-options "-O2 -ftree-vectorize -fdump-tree-vect-stats -maltivec" { target powerpc*-*-* } } */
/* { dg-options "-O2 -ftree-vectorize -fdump-tree-vect-stats -msse2" { target i?86-*-* } } */

#include <stdarg.h>
#include <signal.h>

#define N 16

float b[N] = {0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30};
float a[N];

int main1 (int n)
{
  int i;

  /* Vectorized: unknown loop bound).  */
  for (i = 0; i < n; i++){
    a[i] = b[i];
  }

  /* check results:  */
  for (i = 0; i < n; i++)
    {
      if (a[i] != b[i])
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
  
  return main1 (N);
}

/* { dg-final { scan-tree-dump-times "vectorized 1 loops" 1 "vect" } } */
