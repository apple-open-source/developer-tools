/* { dg-do compile } */ 
/* { dg-options "-O1 -fscalar-evolutions -fdump-tree-scev-details" } */

int 
foo (int i, 
     int precision)
{
  i = precision - i - 1;
  
  /* At this point the analyzer is confused by the initialisation of "i".
     It keeps the initial condition under a symbolic form: "i_1".  */
  
  while (--i);
}

/* i  ->  {i_1, +, -1}_1  */

/* FIXME. */
