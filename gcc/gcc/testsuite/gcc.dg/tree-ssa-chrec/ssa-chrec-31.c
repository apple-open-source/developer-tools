/* { dg-do compile } */ 
/* { dg-options "-O1 -fscalar-evolutions -fdump-tree-scev-details -fall-data-deps -fdump-tree-ddall" } */

void bar (short);

#define N 100
foo (){
  short a[N];
  short b[N]; 
  short c[N];
  int i;
  
  for (i=0; i<N; i++){  
    a[i] = b[i] + c[i];
  }
  bar (a[2]);
}

/* { dg-final { diff-tree-dumps "ddall" } } */
