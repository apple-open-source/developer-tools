/* { dg-do compile } */ 
/* { dg-options "-O1 -fscalar-evolutions -fdump-tree-scev-details" } */

int foo (void);

int main (void)
{
  int a = -100;
  int b = 2;
  int c = 3;
  
  while (a)
    {
      /* Exercises if-phi-nodes.  */
      if (foo ())
	a += b;
      else
	a += c;
      
      b++;
      c++;
    }
}

/* The analyzer has to detect the following evolution function:
   a  ->  {-100, +, {[2, 3], +, 1}_1}_1
*/

/* FIXME. */



