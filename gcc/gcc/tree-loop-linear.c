/* Linear Loop transforms
   Copyright (C) 2003, 2004 Free Software Foundation, Inc.
   Contributed by Daniel Berlin <dberlin@dberlin.org>.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 59 Temple Place - Suite 330, Boston, MA
02111-1307, USA.  */


#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "errors.h"
#include "ggc.h"
#include "tree.h"
#include "target.h"

#include "rtl.h"
#include "basic-block.h"
#include "diagnostic.h"
#include "tree-flow.h"
#include "tree-dump.h"
#include "timevar.h"
#include "cfgloop.h"
#include "tree-fold-const.h"
#include "expr.h"
#include "optabs.h"
#include "tree-chrec.h"
#include "tree-data-ref.h"
#include "tree-scalar-evolution.h"
#include "tree-pass.h"
#include "varray.h"
#include "lambda.h"

/* Linear loop transforms include any composition of interchange,
   scaling, skewing, and reversal.  They are used to change the
   iteration order of loop nests in order to optimize data locality of
   traversals, or remove dependences that prevent
   parallelization/vectorization/etc.  

   TODO: Determine reuse vectors/matrix and use it to determine optimal
   transform matrix for locality purposes.
   TODO: Add dependence matrix collection and approriate matrix
   calculations so we can determine if a given transformation matrix
   is legal for a loop. 
   TODO: Completion of partial transforms.
*/

/* Perform a set of linear transforms on LOOPS.  */

void
linear_transform_loops (struct loops *loops)
{
  unsigned int i;  
  for (i = 1; i < loops->num; i++)
    {
      struct loop *loop_nest = loops->parray[i];
      varray_type oldivs;
      varray_type invariants;
      lambda_loopnest before, after;
      lambda_trans_matrix trans;

      if (!loop_nest->inner)
	continue;
      flow_loop_scan (loop_nest, LOOP_ALL);
      flow_loop_scan (loop_nest->inner, LOOP_ALL);
#if 0
      if (loop_nest->num_pre_header_edges != 1 
	  || loop_nest->inner->num_pre_header_edges != 1)
	  continue;
#endif 
      before = gcc_loopnest_to_lambda_loopnest (loop_nest, &oldivs, &invariants);
      if (!before)
	continue;
      if (dump_file)
	{
	  fprintf (dump_file, "Before:\n");
	  print_lambda_loopnest (dump_file, before, 'i');
	}
      trans = lambda_trans_matrix_new (LN_DEPTH (before), LN_DEPTH (before));
#if 1
      lambda_matrix_id (LTM_MATRIX (trans), LN_DEPTH (before));
#else
      /* This is a 2x2 interchange matrix.  */
      LTM_MATRIX (trans)[0][0] = 0;
      LTM_MATRIX (trans)[0][1] = 1;
      LTM_MATRIX (trans)[1][0] = 1;
      LTM_MATRIX (trans)[1][1] = 0;
#endif
      after = lambda_loopnest_transform (before, trans);
      if (dump_file)
	{
	  fprintf (dump_file, "After:\n");
	  print_lambda_loopnest (dump_file, after, 'u');
	}
      lambda_loopnest_to_gcc_loopnest (loop_nest, oldivs, invariants,
				       after, trans);
    }
}
