/* { dg-do compile } */ 
/* { dg-options "-O1 -fscalar-evolutions -fdump-tree-scev-details" } */

int 
foo (int *c)
{
  int i;
  int j = 10;
  
  for (i = 0; i < 5; i++)
    {
      for (j = 10;; j--)
	{
	  if (j == 0)
	    break;
	  
	  *(c + j) = *(c + j) - 1;
	}
    }
  
  return j;
}

/* 
   j  ->  {10, +, -1}_2  
   i  ->  {0, +, 1}_1
*/

/* FIXME. */
