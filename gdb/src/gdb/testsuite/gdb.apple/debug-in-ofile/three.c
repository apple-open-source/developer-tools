#include "dbg-in-ofile.h"

void
three (int inval, int *outval)
{
  int tmpval = inval * 3;
  four (tmpval, &tmpval);
  *outval = tmpval;
}
