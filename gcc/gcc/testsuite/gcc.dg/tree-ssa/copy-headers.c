/* { dg-do compile } */ 
/* { dg-options "-O2 -fdump-tree-dom1 -ftree-loop-optimize" } */

extern void link_error (void);

void bla (void)
{
  int i, j = 1;

  for (i = 0; i < 100; i++)
    j = 0;

  if (j)
    link_error ();
}

/* There should be no link_error call in the dom1 dump.  */
/* { dg-final { scan-tree-dump-times "link_error" 0 "dom1"} } */
