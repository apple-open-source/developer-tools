/* Induction variable canonicalization.
   Copyright (C) 2004 Free Software Foundation, Inc.
   
This file is part of GCC.
   
GCC is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.
   
GCC is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.
   
You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 59 Temple Place - Suite 330, Boston, MA
02111-1307, USA.  */

/* This pass detects the loops that iterate a constant number of times,
   adds a canonical induction variable (step -1, tested against 0) 
   and replaces the exit test.  This enables the less powerful rtl
   level analysis to use this information.

   This might spoil the code in some cases (by increasing register pressure).
   Note that in the case the new variable is not needed, ivopts will get rid
   of it, so it might only be a problem when there are no other linear induction
   variables.  In that case the created optimization possibilities are likely
   to pay up.

   Additionally in case we detect that it is benefitial to unroll the
   loop completely, we do it right here to expose the optimization
   possibilities to the following passes.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "rtl.h"
#include "tm_p.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "output.h"
#include "diagnostic.h"
#include "tree-flow.h"
#include "tree-dump.h"
#include "cfgloop.h"
#include "tree-pass.h"
#include "ggc.h"
#include "tree-fold-const.h"
#include "tree-chrec.h"
#include "tree-scalar-evolution.h"
#include "params.h"
#include "flags.h"
#include "tree-inline.h"

/* Adds a canonical induction variable to LOOP iterating NITER times.  EXIT
   is the exit edge whose condition is replaced.  */

static void
create_canonical_iv (struct loop *loop, edge exit, tree niter)
{
  edge in;
  tree cond, type, var;
  block_stmt_iterator incr_at;
  enum tree_code cmp;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Added canonical iv to loop %d, ", loop->num);
      print_generic_expr (dump_file, niter, TDF_SLIM);
      fprintf (dump_file, " iterations.\n");
    }

  cond = last_stmt (exit->src);
  in = exit->src->succ;
  if (in == exit)
    in = in->succ_next;

  type = TREE_TYPE (niter);
  niter = fold (build (PLUS_EXPR, type,
		       niter, convert (type, integer_one_node)));
  incr_at = bsi_last (in->src);
  create_iv (niter, convert (type, integer_minus_one_node), NULL_TREE, loop,
	     &incr_at, false, NULL, &var);

  cmp = (exit->flags & EDGE_TRUE_VALUE) ? EQ_EXPR : NE_EXPR;
  COND_EXPR_COND (cond) = build (cmp, boolean_type_node,
				 var, convert (type, integer_zero_node));
  modify_stmt (cond);
}

/* Computes an estimated number of insns in LOOP.  */

unsigned
estimate_loop_size (struct loop *loop)
{
  basic_block *body = get_loop_body (loop);
  block_stmt_iterator bsi;
  unsigned size = 0, i;

  for (i = 0; i < loop->num_nodes; i++)
    for (bsi = bsi_start (body[i]); !bsi_end_p (bsi); bsi_next (&bsi))
      size += estimate_num_insns (bsi_stmt (bsi));
  free (body);

  return size;
}

/* Tries to unroll LOOP completely, i.e. NITER times.  LOOPS is the
   loop tree.  COMPLETELY_UNROLL is true if we should unroll the loop
   even if it may cause code growth.  EXIT is the exit of the loop
   that should be eliminated.  */

static bool
try_unroll_loop_completely (struct loops *loops, struct loop *loop,
			    edge exit, tree niter,
			    bool completely_unroll)
{
  tree max_unroll = build_int_2 (PARAM_VALUE (PARAM_MAX_COMPLETELY_PEEL_TIMES),
				 0);
  unsigned n_unroll, ninsns;
  tree cond, dont_exit, do_exit;

  if (loop->inner)
    return false;

  if (!integer_nonzerop (fold (build (LE_EXPR, boolean_type_node,
				      niter, max_unroll))))
    return false;
  n_unroll = tree_low_cst (niter, 1);

  if (n_unroll && !completely_unroll)
    return false;

  ninsns = estimate_loop_size (loop);

  if (n_unroll * ninsns
      > (unsigned) PARAM_VALUE (PARAM_MAX_COMPLETELY_PEELED_INSNS))
    return false;

  if (exit->flags & EDGE_TRUE_VALUE)
    {
      dont_exit = boolean_false_node;
      do_exit = boolean_true_node;
    }
  else
    {
      dont_exit = boolean_true_node;
      do_exit = boolean_false_node;
    }
  cond = last_stmt (exit->src);
    
  if (n_unroll)
    {
      if (!flag_unroll_loops)
	return false;

      COND_EXPR_COND (cond) = dont_exit;
      modify_stmt (cond);

      if (!tree_duplicate_loop_to_header_edge (loop, loop_preheader_edge (loop),
					       loops, n_unroll, NULL,
					       NULL, NULL, NULL, 0))
	return false;
    }
  
  COND_EXPR_COND (cond) = do_exit;
  modify_stmt (cond);

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "Unrolled loop %d completely.\n", loop->num);

  return true;
}

/* Adds a canonical induction variable to LOOP if suitable.  LOOPS is the loops
   tree.  CREATE_IV is true if we may create a new iv.  COMPLETELY_UNROLL is
   true if we should do complete unrolling even if it may cause the code
   growth.  */

static void
canonicalize_loop_induction_variables (struct loops *loops, struct loop *loop,
				       bool create_iv, bool completely_unroll)
{
  edge exit = NULL;
  tree niter;

  /* ??? Why is this needed?  I.e. from where comes the invalid info?  */
  loop->nb_iterations = NULL;

  niter = number_of_iterations_in_loop (loop);

  if (TREE_CODE (niter) == INTEGER_CST)
    {
#ifdef ENABLE_CHECKING
      tree nit;
      edge ex;
#endif

      exit = loop_exit_edge (loop, 0);
      if (!just_once_each_iteration_p (loop, exit->src))
	return;

      /* The result of number_of_iterations_in_loop is by one higher than
	 we expect (i.e. it returns number of executions of the exit
	 condition, not of the loop latch edge).  */
      niter = fold (build (PLUS_EXPR, TREE_TYPE (niter), niter,
			   convert (TREE_TYPE (niter),
				    integer_minus_one_node)));

#ifdef ENABLE_CHECKING
      nit = find_loop_niter_by_eval (loop, &ex);

      if (ex == exit
	  && TREE_CODE (nit) == INTEGER_CST
	  && !operand_equal_p (niter, convert (TREE_TYPE (niter), nit), 0))
	abort ();
#endif
    }
  else
    niter = find_loop_niter_by_eval (loop, &exit);

  if (TREE_CODE (niter) != INTEGER_CST)
    return;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Loop %d iterates ", loop->num);
      print_generic_expr (dump_file, niter, TDF_SLIM);
      fprintf (dump_file, " times.\n");
    }

  if (try_unroll_loop_completely (loops, loop, exit, niter, completely_unroll))
    return;

  if (create_iv)
    create_canonical_iv (loop, exit, niter);
}

/* The main entry point of the pass.  Adds canonical induction variables
   to the suitable LOOPS.  */

void
canonicalize_induction_variables (struct loops *loops)
{
  unsigned i;
  struct loop *loop;
  bool create_ivs = flag_unroll_loops || flag_branch_on_count_reg;
  bool completely_unroll_loops = flag_unroll_loops;

  for (i = 1; i < loops->num; i++)
    {
      loop = loops->parray[i];

      if (loop)
	canonicalize_loop_induction_variables (loops, loop, create_ivs,
					       completely_unroll_loops);
    }
}
