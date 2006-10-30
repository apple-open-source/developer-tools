#include "dbg-in-ofile.h"

void
two (int inval, int *outval)
{
  int tmpval = inval * 2;
  three (tmpval, &tmpval);
  *outval = tmpval;
}
