/* If-conversion for vectorizer.
   Copyright (C) 2004 Free Software Foundation, Inc.
   Contributed by Devang Patel <dpatel@apple.com>

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

/* This pass implements if-conversion transformation of loops for
   vectorizer.

   A short description of if-conversion:

   TODO */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "errors.h"
#include "tree.h"
#include "c-common.h"
#include "flags.h"
#include "timevar.h"
#include "varray.h"
#include "rtl.h"
#include "basic-block.h"
#include "diagnostic.h"
#include "tree-flow.h"
#include "tree-dump.h"
#include "cfgloop.h"
#include "tree-fold-const.h"
#include "tree-chrec.h"
#include "tree-data-ref.h"
#include "tree-scalar-evolution.h"
#include "tree-pass.h"
#include "tree-dg.h"
#include "target.h"

/* local function prototypes */
static tree tree_if_convert_stmt (tree, tree, block_stmt_iterator *);
static void add_to_predicate_list (basic_block, tree);
static void clean_predicate_lists (struct loop *loop, basic_block *);
static bool is_appropriate_for_if_conv (struct loop *, bool);
static tree make_ifcvt_temp_variable (tree, tree, block_stmt_iterator *, bool);
static bool bb_with_exit_edge (basic_block);
static void collapse_blocks (struct loop *, basic_block *);
static void make_cond_modify_expr (tree, block_stmt_iterator *);

/* Make new temp variable of type TYPE. Add MODIFY_EXPR to assign EXPR 
   to the variable.  */

static tree
make_ifcvt_temp_variable (tree type, tree exp, block_stmt_iterator *bsi, bool before)
{
  const char *name = "__ifcvt";
  tree var, stmt, new_name;

  if (is_gimple_val (exp))
    return exp;

  /* Create new temporary variable.  */
  var = create_tmp_var (type, name);
  add_referenced_tmp_var (var);

  /* Build new statement to assigne EXP to new variable.  */
  stmt = build (MODIFY_EXPR, type, var, exp);
  
  /*bitmap_set_bit (vars_to_rename, var_ann (var)->uid);*/

  /* Get SSA name for the new variable and set make new statement
     its definition statment.  */
  new_name = make_ssa_name (var, stmt);
  TREE_OPERAND (stmt, 0) = new_name;
  SSA_NAME_DEF_STMT (new_name) = stmt;

  /* Insert new statement using iterator.  */
  if (before)
    bsi_insert_before (bsi, stmt, BSI_SAME_STMT);
  else
    bsi_insert_after (bsi, stmt, BSI_SAME_STMT);

  return new_name;
}

/* Add condition COND into predicate list of basic block BB.  */

static void
add_to_predicate_list (basic_block bb, tree new_cond)
{
  tree cond = bb->aux;
  
  if (cond)
    {
      /* Attempt to do simple boolean expression simplification.  */

      /* Replace 'A || !A' with TRUE.  */
      if (TREE_CODE (new_cond) == TRUTH_NOT_EXPR
	  && cond == TREE_OPERAND (new_cond, 0))
	cond = boolean_true_node;
      /* Replace '!A || A' with TRUE.  */
      else if (TREE_CODE (cond) == TRUTH_NOT_EXPR
	       && new_cond == TREE_OPERAND (cond, 0))
	cond = boolean_true_node;
      /* Replace 'A || TRUE' with TRUE.  */
      else if (new_cond == boolean_true_node)
 	cond = boolean_true_node;
      /* If we have TRUE then 'TRUE || A' is still TRUE.  */
      else if (cond != boolean_true_node)
	cond = build (TRUTH_OR_EXPR, boolean_type_node,
		      unshare_expr (cond), new_cond);
    }
  else
    cond = new_cond;

  bb->aux = cond;
}

/* Add condition COND into T's destination node's predicate list.  */

static void
add_to_dst_predicate_list (tree t, tree cond)
{
  basic_block bb;

#ifdef ENABLE_CHECKING
  if (TREE_CODE (t) != GOTO_EXPR)
    abort ();
#endif

  bb = label_to_block (TREE_OPERAND (t, 0));
  add_to_predicate_list (bb, cond);
}

/* Input T is a modify expr. Make it conditional modify expr.
   Condition is saved in basic block's aux field.  */

static void
make_cond_modify_expr (tree t, block_stmt_iterator *bsi)
{
  basic_block bb;
  tree cond, op0, op1;
  tree new_cond, new_op1, arg0, arg1;

#ifdef ENABLE_CHECKING
  if (TREE_CODE (t) != MODIFY_EXPR)
    abort ();
#endif

  /* Get the condition and operands */
  bb = bb_for_stmt (t);
  cond = bb->aux;
  op0 = TREE_OPERAND (t, 0);
  op1 = TREE_OPERAND (t, 1);

  if (!cond || cond == boolean_true_node)
    return;

  /* Create temporary for B in < A1 = COND_EXPR < C, B>>.  */
  new_op1 = make_ifcvt_temp_variable (TREE_TYPE (op1), op1, bsi, true);

  /* If condition is TRUTH_NOT_EXPR than switch 'if' and 'else' args.  */
  if (TREE_CODE (cond) == TRUTH_NOT_EXPR) 
    { 
      cond = TREE_OPERAND (cond, 0);
      arg0 = build_empty_stmt ();
      arg1 = new_op1;
    }
  else
    {
      arg0 = new_op1;
      arg1 = build_empty_stmt ();
    }

  /* Create temporary for C in < A = COND_EXPR < C, B>>.  */
  new_cond = make_ifcvt_temp_variable (boolean_type_node, 
				       unshare_expr (cond),
				       bsi, true);

  /* Replace RHS with new conditional expression.  */
  TREE_OPERAND (t, 1) = build (COND_EXPR, TREE_TYPE (op0),
			       unshare_expr (new_cond), arg0, arg1);
 
  TREE_TYPE (t) = TREE_TYPE (op0);
  modify_stmt (t);
}

/* Replace PHI node with conditional modify expr.  */
static void
replace_phi_with_cond_modify_expr (tree phi)
{
  tree new_stmt;
  basic_block bb;
  block_stmt_iterator bsi;
  tree rhs, cond;
  tree arg_0, arg_1;
  tree cond_0, cond_1;
  edge e_0, e_1;

#ifdef ENABLE_CHECKING
  if (TREE_CODE (phi) != PHI_NODE)
    abort ();
#endif

  /* Find basic block and initialize iterator.  */
  bb = bb_for_stmt (phi);
  bsi = bsi_start (bb);

  new_stmt = NULL_TREE;
  cond = NULL_TREE;
  arg_0 = NULL_TREE;
  arg_1 = NULL_TREE;
  
  /* If this is not filtered earlier, then now it is too late.  */
  if (PHI_NUM_ARGS (phi) != 2)
     abort ();

  /* Find PHI edges and if-cond associated with the edge.  */
  e_0 = PHI_ARG_EDGE (phi, 0);
  e_1 = PHI_ARG_EDGE (phi, 1);
  cond_0 = e_0->src->aux;
  cond_1 = e_1->src->aux;

  /* Use condition that is not TRUTH_NOT_EXPR in conditional modify expr.  */
  if (TREE_CODE (cond_0) == TRUTH_NOT_EXPR)
    /*      && cond_1 == TREE_OPERAND (cond_0, 0))*/
    {
      /* Use cond from edge e_0.  */
      cond  = cond_1;
      arg_0 = PHI_ARG_DEF (phi, 1);
      arg_1 = PHI_ARG_DEF (phi, 0);
    }
  else if (TREE_CODE (cond_0) != TRUTH_NOT_EXPR)
    /* else if (TREE_CODE (cond_1) == TRUTH_NOT_EXPR)
    	   && cond_0 == TREE_OPERAND (cond_1, 0))*/
    {
      /* Use cond from edge e_1.  */
      cond  = cond_0;
      arg_0 = PHI_ARG_DEF (phi, 0);
      arg_1 = PHI_ARG_DEF (phi, 1);
    }
  else
    {
      /* This is unexpected.  */
      abort ();
    }

  cond = make_ifcvt_temp_variable (TREE_TYPE (cond), unshare_expr (cond), &bsi, false);

  if (TREE_CODE (cond) == VAR_DECL)
    bitmap_set_bit (vars_to_rename, var_ann (cond)->uid);

  /* Build new RHS using selected condtion and arguments.  */
  rhs = build (COND_EXPR, TREE_TYPE (PHI_RESULT (phi)),
	       unshare_expr (cond), unshare_expr (arg_0),
	       unshare_expr (arg_1));
  
  /* Create new MODIFY expresstion using RHS.  */
  new_stmt = build (MODIFY_EXPR, TREE_TYPE (PHI_RESULT (phi)),
		    unshare_expr (PHI_RESULT (phi)), rhs);
  
  /* Make new statement definition of the original phi result.  */
  SSA_NAME_DEF_STMT (PHI_RESULT (phi)) = new_stmt;

  /* Set basic block and insert using iterator.  */
  set_bb_for_stmt (new_stmt, bb);

  bsi_insert_after (&bsi, new_stmt, BSI_SAME_STMT);
  modify_stmt (new_stmt);

}

/* if-convert stmt T */

static tree
tree_if_convert_stmt (tree t, tree cond, block_stmt_iterator *bsi)
{
 
  switch (TREE_CODE (t))
    {
      /* Labels are harmless here.  */
    case LABEL_EXPR:
      break;

      /* Convert modify expression into conditional modify expression.  */
    case MODIFY_EXPR:
      make_cond_modify_expr (t, bsi);
      break;

    case GOTO_EXPR:
      /* Unconditional goto */
      add_to_predicate_list (bb_for_stmt (TREE_OPERAND (t, 1)), cond);
      bsi_remove (bsi);
      cond = NULL_TREE;
      break;

    case COND_EXPR:

      /* Update destination blocks' predicate list and remove this
	 condition expression.  */

      /* Hey, do not if-convert exit condition!  */
      if (bb_with_exit_edge (bb_for_stmt (t)))
	{
	  cond = NULL_TREE;
	  break;
	}
      
      {
	tree dst1, dst2, c;

	c = TREE_OPERAND (t, 0);
	dst1 = TREE_OPERAND (t, 1);
	dst2 = TREE_OPERAND (t, 2);

	c = make_ifcvt_temp_variable (TREE_TYPE (c), unshare_expr (c), bsi, true);
	/* Add new condition into destination's predicate list.  */
	if (dst1)
	  {
	    /* if 'c' is true then dst1 is reached.  */
	    tree new_cond;
	    if (cond == boolean_true_node || !cond)
	      new_cond = unshare_expr (c);
	    else
	      new_cond = 
		make_ifcvt_temp_variable (boolean_type_node, 
					  build (TRUTH_AND_EXPR, boolean_type_node,
						 unshare_expr (cond), c),
					  bsi, true);
	    add_to_dst_predicate_list (dst1, new_cond);
	  }
	
	if (dst2)
	  {
	    /* if 'c' is false then dst2 is reached.  */
	    tree c2 = build1 (TRUTH_NOT_EXPR, 
			      boolean_type_node, 
			      unshare_expr (c));
	    tree new_cond;
	    if (cond == boolean_true_node || !cond)
		new_cond = c2;
	    else
	      new_cond = 
		make_ifcvt_temp_variable (boolean_type_node, 
					  build (TRUTH_AND_EXPR, boolean_type_node,
						 unshare_expr (cond), c2),
					  bsi, true);
	    
	    add_to_dst_predicate_list (dst2, new_cond);
	  }

	/* Now this conditional statement is redundent. Remove it.  */
	bsi_remove (bsi);
	cond = NULL_TREE;
      }
      break;
	
    default:    
      abort ();
      break;
    }
  return cond;
}

/* Return true if one of the basic block BB edge is loop exit.  */

static bool
bb_with_exit_edge (basic_block bb)
{
  edge e;
  bool exit_edge_found = false;

  for (e = bb->succ; e && !exit_edge_found ; e = e->succ_next)
    if (e->flags & EDGE_LOOP_EXIT)
      exit_edge_found = true;

  return exit_edge_found;
}

/* Filter for if-conversion.  */

static bool
is_appropriate_for_if_conv (struct loop *loop, bool for_vectorizer)
{
  basic_block bb;
  basic_block *bbs;
  block_stmt_iterator itr;
  unsigned int i;
  edge e;

  /* Handle only inner most loop.  */
  if (!loop || loop->inner)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "not inner most loop\n");
      return false;
    }
  
  /* If only one block, no need for if-conversion.  */
  if (loop->num_nodes <= 2)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "less thant 2 basic blocks\n");
      return false;
    }
  
  /* More than one loop exit is too much to handle.  */
  if (loop->num_exits > 1)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "multiple exits\n");
      return false;
    }

  /* If target does not support vector compare and select operations
     then do not if-convert loop for vectorizer.  */
  if (for_vectorizer &&
      (!targetm.vect.support_vector_compare_p () 
       || !targetm.vect.support_vector_select_p ()))
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "target does not support vector compare/select\n");
      return false;
    }

  /* If one of the loop header's edge is exit edge the do not apply
     if-conversion.  */
  for (e = loop->header->succ; e; e = e->succ_next)
    if ( e->flags & EDGE_LOOP_EXIT)
      return false;

  compute_immediate_uses (TDFA_USE_OPS, NULL);
  compute_immediate_uses (TDFA_USE_VOPS, NULL);

  calculate_dominance_info (CDI_DOMINATORS);
  calculate_dominance_info (CDI_POST_DOMINATORS);

  /* Allow statements that can be handled during if-conversion.  */
  bbs = get_loop_body_in_dom_order(loop);
  for (i = 0; i < loop->num_nodes; i++)
    {
      edge e;
      bb = bbs[i];

      if (dump_file && (dump_flags & TDF_DETAILS))
	  fprintf (dump_file, "----------[%d]-------------\n", bb->index);
      
      for (itr = bsi_start (bb); !bsi_end_p (itr); bsi_next (&itr))
	{
	  tree t = bsi_stmt (itr);
	  switch (TREE_CODE (t))
	    {
	    case LABEL_EXPR:
	      break;

	    case MODIFY_EXPR:

	      if (dump_file && (dump_flags & TDF_DETAILS))
		{
		  fprintf (dump_file, "-------------------------\n");
		  print_generic_stmt (dump_file, t, TDF_SLIM);
		}

	      /* Be conservative */
	      if (movement_possibility (t) == MOVE_IMPOSSIBLE)
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    fprintf (dump_file, "stmt is movable. Don't take risk\n");
		  free_dominance_info (CDI_POST_DOMINATORS);
		  return false;
		}
	      {
		int j;
		dataflow_t df = get_immediate_uses (t);
		int num_uses = num_immediate_uses (df);
		for (j = 0; j < num_uses; j++)
		  {
		    tree use = immediate_use (df, j);
		    if (TREE_CODE (use) == PHI_NODE)
		      {
			basic_block use_bb = bb_for_stmt (use);
			basic_block t_bb = bb_for_stmt (t);

			if (dump_file && (dump_flags & TDF_DETAILS))
			  fprintf (dump_file, "t_bb = %d, use_bb = %d (%d)\n", t_bb->index, 
				   use_bb->index,
				   flow_bb_inside_loop_p (loop, use_bb) ? 1 : 0);

			/* Case 1 */
			if (dominated_by_p (CDI_DOMINATORS, t_bb, use_bb))
			  {
			    if (dump_file && (dump_flags & TDF_DETAILS))
			      fprintf (dump_file,"t_bb is dominated by use_bb\n");
			  }
			/* Case 2 */
			else if (dominated_by_p (CDI_POST_DOMINATORS, t_bb, use_bb)
				     && !dominated_by_p (CDI_DOMINATORS, use_bb, t_bb))
			  {
			    if (dump_file && (dump_flags & TDF_DETAILS))
			      {
				fprintf (dump_file,"t_bb is post dominated by use_bb AND");
				fprintf (dump_file,"use_bb is dominated by t_bb\n");
			      }
			  }
			/* Case 3 */
			else if (dominated_by_p (CDI_DOMINATORS, use_bb, t_bb))
			  {
			    if (dump_file && (dump_flags & TDF_DETAILS))
			      {
				fprintf (dump_file,"use_bb is dominated by t_bb");
				fprintf (dump_file,"do not know how to handle this case");
			      }
			    free_dominance_info (CDI_POST_DOMINATORS);
			    return false;
			  }
			/* case 4 */
			else if (bb != loop->header && PHI_NUM_ARGS (use) != 2)
			  {
			    if (dump_file && (dump_flags & TDF_DETAILS))
				fprintf (dump_file, "More than two phi node args.\n");
			    free_dominance_info(CDI_POST_DOMINATORS);
			    return false;
			  }
			/* Case 5 */
			else
			  {
			    if (dump_file && (dump_flags & TDF_DETAILS))
			      {
				fprintf (dump_file, "dominance relationship between t_bb ");
				fprintf (dump_file, "and use_bb difficult to handle");
			      }
			    free_dominance_info (CDI_POST_DOMINATORS);
			    return false;
			  }
		      }
		  } /*		for (j = 0; j < num_uses; j++) */
	      }
	      break;

	    case GOTO_EXPR:
	    case COND_EXPR:
	      break;

	    default:
	      /* Don't know what to do with 'em so don't do anything.  */
	      if (dump_file && (dump_flags & TDF_DETAILS))
		{
		  fprintf (dump_file, "don't know what to do\n");
		  print_generic_stmt (dump_file, t, TDF_SLIM);
		}
	      free_dominance_info (CDI_POST_DOMINATORS);
	      return false;
	      break;
	    }

	  /* ??? Check data dependency for vectorizer.  */
	}
      
      {
	tree phi;
	
	for (phi = phi_nodes (bb); phi; phi = TREE_CHAIN (phi))
	  {
	     tree result = SSA_NAME_VAR (PHI_RESULT (phi));
	    var_ann_t ann = var_ann (result);

	    if (bb_with_exit_edge (bb))
	      {
		tree d = PHI_ARG_DEF (phi, 0);
		if (TREE_CODE (d) == SSA_NAME)
		  {
		    if (dump_file && (dump_flags & TDF_DETAILS))
		      {
			fprintf (dump_file,  "phi node where 1st argument is not constant,");
			fprintf (dump_file,  "in Exit block. Enough!\n");
		      }
		    free_dominance_info (CDI_POST_DOMINATORS);
		    return false;
		  }
	      }
	    if (ann 
		&& (ann->mem_tag_kind == TYPE_TAG || ann->mem_tag_kind == NAME_TAG))
	      {
		if (dump_file && (dump_flags & TDF_DETAILS))
		  fprintf (dump_file,"PHIs with memory tags\n");
		free_dominance_info (CDI_POST_DOMINATORS);
		return false;
	      }
	    if (bb != loop->header && PHI_NUM_ARGS (phi) != 2)
	      {
		if (dump_file && (dump_flags & TDF_DETAILS))
		  fprintf (dump_file, "More than two phi node args.\n");
		free_dominance_info (CDI_POST_DOMINATORS);
		return false;
	      }

	  }
      }

      /* Be less adveturous.  */
      for (e = bb->succ; e; e = e->succ_next)
	if (e->flags & (EDGE_ABNORMAL_CALL | EDGE_EH | EDGE_ABNORMAL))
	  {
	    if (dump_file && (dump_flags & TDF_DETAILS))
	      fprintf (dump_file,"Difficult to handle edges\n");
	    free_dominance_info (CDI_POST_DOMINATORS);
	    return false;
	  }
    }

  /* OK. Did not find any potential issues so go ahead in if-convert
     this loop. Now there is no looking back.  */
  if (dump_file)
    fprintf (dump_file,"Applying if-conversion\n");

  free (bbs);
  free_dominance_info (CDI_POST_DOMINATORS);
  return true;
}

/* During if-conversion aux field from basic block is used to hold predicate
   list. Clean each basic block's predicate list.  */

static void
clean_predicate_lists (struct loop *loop, basic_block *bbs)
{
  unsigned int i;

  for (i = 0; i < loop->num_nodes; i++)
    bbs[i]->aux = NULL;
}

/* Collapse all basic block into one huge basic block.  */

static void
collapse_blocks (struct loop *loop, basic_block *bbs)
{
  basic_block bb, exit_bb;
  unsigned int orig_loop_num_nodes = loop->num_nodes;
  unsigned int i;

  /* Replace phi nodes with cond. modify expr.  */
  for (i = 1; i < orig_loop_num_nodes; i++)
    {
      tree phi;

      bb = bbs[i];

      if (bb == loop->header || bb == loop->latch)
	continue;

      phi = phi_nodes (bb);
      while (phi)
	{
	  tree next = TREE_CHAIN (phi);
	  replace_phi_with_cond_modify_expr (phi);
	  remove_phi_node (phi, NULL_TREE, bb);
	  phi = next;
	}
    }


  /* Remove loop headers succ edges.  We already verified taht loop header 
     does not have exit edges. When we process exit block, that has exit edges,
     we will add new edge from loop header to exit block.  */
  while (loop->header->succ != NULL)
    ssa_remove_edge (loop->header->succ);

  exit_bb = NULL;

  /* Merge basic blocks */
  for (i = 1; i < orig_loop_num_nodes; i++)
    {
      edge e;
      block_stmt_iterator bsi;
      tree_stmt_iterator last;

      bb = bbs[i];

      if (bb == loop->latch)
	continue;

      for (e = bb->succ; e && !exit_bb; e = e->succ_next)
	  if (e->flags & EDGE_LOOP_EXIT)
	      exit_bb = bb;

      if (bb == exit_bb)
	{
	  edge new_e;

	  /* Connect this node with loop header.  */
	  new_e = make_edge (bbs[0], bb, EDGE_FALLTHRU);
	  set_immediate_dominator (CDI_DOMINATORS, bb, bbs[0]);

	  if (exit_bb != loop->latch)
	    {
	      /* Redirect non-exit edge to loop->latch.  */
	      for (e = bb->succ; e; e = e->succ_next)
		if (!(e->flags & EDGE_LOOP_EXIT))
		  {
		    redirect_edge_and_branch (e, loop->latch);
		    set_immediate_dominator (CDI_DOMINATORS, loop->latch, bb);
		  }
	    }
	  continue;
	}

      /* It is time to remove this basic block.	 First remove edges.  */
      while (bb->succ != NULL)
	ssa_remove_edge (bb->succ);
      while (bb->pred != NULL)
	ssa_remove_edge (bb->pred);

      /* Remove labels and make stmts member of loop->header.  */
      for (bsi = bsi_start (bb); !bsi_end_p (bsi); )
	{
	  if (TREE_CODE (bsi_stmt (bsi)) == LABEL_EXPR)
	    bsi_remove (&bsi);
	  else
	    {
	      set_bb_for_stmt (bsi_stmt (bsi), loop->header);
	      bsi_next (&bsi);
	    }
	}

      /* Update stmt list.  */
      last = tsi_last (loop->header->stmt_list);
      tsi_link_after (&last, bb->stmt_list, TSI_NEW_STMT);
      bb->stmt_list = NULL;

      /* Update dominator info.  */
      if (dom_computed[CDI_DOMINATORS])
	delete_from_dominance_info (CDI_DOMINATORS, bb);
      if (dom_computed[CDI_POST_DOMINATORS])
	delete_from_dominance_info (CDI_POST_DOMINATORS, bb);
      
      /* Remove basic block.  */
      remove_bb_from_loops (bb);
      expunge_block (bb);
    }
}

/* Main entry point.
   Apply if-conversion to the loop. Return true if successful otherwise return
   false. If false is returned, loop remains unchanged.  */

bool
tree_if_conversion (struct loop *loop, bool for_vectorizer)
{
  basic_block bb;
  basic_block *bbs;
  block_stmt_iterator itr;
  tree cond;
  unsigned int i;

  flow_loop_scan (loop, LOOP_ALL);

  /* if-conversion is not appropriate for all loops.  */
  if (!is_appropriate_for_if_conv (loop, for_vectorizer))
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file,"-------------------------\n");
	return false;
    }

  cond = boolean_true_node;
  
  /* Walk statements and add conditions.  */
  bbs = get_loop_body_in_bfs_order (loop);

  /* Do actual work now.  */
  for (i = 0; i < loop->num_nodes; i++)  
    {
      bb = bbs [i];

      /* Update condition using predicate list.  */
      cond = bb->aux;

      /* Process all statements in this basic block.
	 Remove conditional expresion, if any, and annotate
	 destination basic block(s) appropriately.  */
      for (itr = bsi_start (bb); !bsi_end_p (itr); /* empty */)
	{
	  tree t = bsi_stmt (itr);
	  cond = tree_if_convert_stmt (t, cond, &itr);
	  if (!bsi_end_p (itr))
	    bsi_next (&itr);
	}

      /* If current bb has only one successor, then consider it as an 
	 unconditional goto.  */
      if (bb->succ && !bb->succ->succ_next)
	{
	  basic_block bb_n = bb->succ->dest;
	  if (cond != NULL_TREE)
	    add_to_predicate_list (bb_n, cond);
	  cond = NULL_TREE;
	}
    }

  /* Now, all statements are if-converted and basic blocks are
     annotated appropriately. Collapse all basic block into one huge
     basic block.  */
  collapse_blocks (loop, bbs); 

  /* Re-write in SSA form */
  rewrite_into_ssa (false);
  rewrite_into_loop_closed_ssa ();
  bitmap_clear (vars_to_rename);

  /* Update ddg */
  /* TODO */

  /* clean up */
  clean_predicate_lists (loop, bbs);
  free (bbs);
  free_df ();
  return true;
}

