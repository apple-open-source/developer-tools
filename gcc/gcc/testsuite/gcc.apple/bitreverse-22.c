/* APPLE LOCAL file 4431497 */
extern void abort();
#include <stdio.h>
/* { dg-do run { target powerpc*-*-darwin* } } */
/* { dg-options "-std=gnu99" } */

#pragma reverse_bitfields on
#pragma ms_struct on

typedef struct _S5
{
 unsigned int l1 : 16;
 unsigned int l2 : 16;
 unsigned short s1;
 unsigned int l3 : 16;
 unsigned int l4 : 16;
 unsigned short s2;
} S5;
/* s1 and l3 overlap.  s2 and l4 overlap. */

int TestS5(void)
{
 S5 s5 = {0};
 
 s5.s1 = 0x5678;
 s5.s2 = 0xABCD;

  if (sizeof(S5) != 12 
  || s5.s1 != 0x5678
  || s5.l3 != 0x5678
  || s5.s2 != 0xabcd
  || s5.l4 != 0xabcd)
    abort();
  return 0;

#if 0
 printf("size %d\n", sizeof(S5)); 
 printf("s5.l3 = 0x%x, s5.l4 = 0x%x\n", s5.l3, s5.l4);
#endif
}

int main()
{
 return TestS5();
}
