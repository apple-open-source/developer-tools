/* { dg-do compile } */ 
/* { dg-options "-O1 -fscalar-evolutions -fdump-tree-scev-details" } */


int main ()
{
  int a = 1;
  int b = 1;
  
  while (a)
    {
      a += b;
      b *= 2;
    }
}

/* 
   b  ->  {1, *, 2}_1
   a  ->  {1, +, {1, *, 2}_1}_1
*/

/* FIXME. */
