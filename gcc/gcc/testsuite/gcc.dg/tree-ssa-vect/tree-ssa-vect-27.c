/* { dg-do run { target powerpc*-*-* } } */
/* { dg-do compile { target i?86-*-* } } */
/* { dg-options "-O2 -ftree-vectorize -fdump-tree-vect-stats -maltivec" { target powerpc*-*-* } } */
/* { dg-options "-O2 -ftree-vectorize -fdump-tree-vect-stats -msse" { target i?86-*-* } } */

#include <stdarg.h>
#include <signal.h>

#define N 128

/* unaligned load.  */

int main1 ()
{
  int i;
  int ia[N];
  int ib[N+1];

  for (i=0; i < N; i++)
    {
      ib[i] = i;
    }

  for (i = 1; i <= N; i++)
    {
      ia[i-1] = ib[i];
    }

  /* check results:  */
  for (i = 1; i <= N; i++)
    {
      if (ia[i-1] != ib[i])
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

/* APPLE LOCAL -haifa  */
/* { dg-final { scan-tree-dump-times "vectorized 1 loops" 1 "vect" } } */

