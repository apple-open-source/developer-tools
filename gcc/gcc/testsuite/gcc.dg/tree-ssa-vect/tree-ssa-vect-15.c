/* { dg-do run { target powerpc*-*-* } } */
/* { dg-do compile { target i?86-*-* } } */
/* { dg-options "-O2 -ftree-vectorize -fdump-tree-vect-stats -maltivec" { target powerpc*-*-* } } */
/* { dg-options "-O2 -ftree-vectorize -fdump-tree-vect-stats -msse2" { target i?86-*-* } } */

#include <stdarg.h>
#include <signal.h>

#define N 16

int main1 ()
{
  int i;
  int a[N];
  int b[N] = {0,3,6,9,12,15,18,21,24,27,30,33,36,39,42,45};

  /* Not vetorizable yet (reverse access and forward access).  */
  for (i = N; i > 0; i--)
    {
      a[N-i] = b[i-1];
    }

  /* check results:  */
  for (i = 0; i <N; i++)
    {
      if (a[i] != b[N-1-i])
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

/* { dg-final { scan-tree-dump-times "vectorized 1 loops" 1 "vect" { xfail *-*-* } } } */
