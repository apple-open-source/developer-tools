#include "dbg-in-ofile.h"

void
one (int inval, int *outval)
{
  int tmpval = inval * 1;
  two (tmpval, &tmpval);
  *outval = tmpval;
}
