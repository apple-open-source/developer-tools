#include "dbg-in-ofile.h"

void
four (int inval, int *outval)
{
  int tmpval = inval * 4;
  five (tmpval, &tmpval);
  *outval = tmpval;
}
