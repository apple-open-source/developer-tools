/* Optimization of return nodes by merging them into one basic block.
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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "errors.h"
#include "ggc.h"
#include "tree.h"
#include "flags.h"
#include "rtl.h"
#include "tm_p.h"
#include "basic-block.h"
#include "timevar.h"
#include "diagnostic.h"
#include "tree-flow.h"
#include "tree-pass.h"
#include "tree-dump.h"

static void tree_ssa_return (void);
                                  
                                  
/* Build a temporary.  Make sure and register it to be renamed.  */

static tree
make_temp (tree type)
{
  tree t = create_tmp_var (type, NULL);
  add_referenced_tmp_var (t);
  bitmap_set_bit (vars_to_rename, var_ann (t)->uid);
  return t;
}

   
static void
tree_ssa_return (void)
{
  basic_block bb;

  /* Search every basic block for return nodes we may be able to optimize.  */
  FOR_EACH_BB (bb)
    {
      tree returnstmt = last_stmt (bb);
      edge pred = bb->pred;
      basic_block bb_other = NULL;
      tree returnstmt_other;
      basic_block new_bb = NULL;
      tree new_var = NULL;
      block_stmt_iterator bsi, bsi_other, new_bsi;
      tree ret_decl;
      tree type;
      
      /* If we have no predecessor, we can not do this for
         the only basic block.   */
      if (!pred)
       continue;

      /* Cannot do this if there is no statements.   */
      if (!returnstmt)
        continue;
      
      if (TREE_CODE (returnstmt) == RETURN_EXPR)
        ;
      else
        continue;
        
      /* Only do this optimization for basic blocks which
         have only one predecessor.   */
      if (!pred || pred->pred_next)
        continue;
        
      /* Only do this for predecessor which are only conditional ones.  */
      if (pred->flags & (EDGE_TRUE_VALUE | EDGE_FALSE_VALUE))
        ;
      else
        continue;
      
      /* Cannot do this optimization if predecessor is abnormal.   */
      if (pred->flags & EDGE_ABNORMAL)
        continue;
      
      /* No reason to do this if we are not returning a value.   */
      if (TREE_OPERAND (returnstmt, 0) == NULL)
        continue;
      
      /* Make sure that the predecessor's successor is not an abnormal.   */
      if (pred->src->succ->flags & EDGE_ABNORMAL)
        continue;
      
      /* Find the other basic block which is connected to the predecessor.   */
      if (pred->src->succ->dest == bb)
        bb_other = pred->src->succ->succ_next->dest;
      else
        bb_other = pred->src->succ->dest;
        
      
      /* If we do not have a basic block for the other edge, just continue.   */
      if (!bb_other)
        continue;
        
        
      /*  Only do the optimization if are bb_other is only linked from one BB.   */
      if (bb_other->pred->src != pred->src)
        continue;
      else if (bb_other->pred->pred_next)
        continue;
      
      returnstmt_other = last_stmt (bb_other);
      
      /* Cannot do the optimization if the other basic block does not
         end with a return. */
      if (!returnstmt_other || TREE_CODE (returnstmt_other) != RETURN_EXPR)
        continue;

      /* No reason to do this if we are not returning a value.   */
      if (TREE_OPERAND (returnstmt_other, 0) == NULL)
        continue;

      /* If we do not have a modify expression on both returns, there is no point
         in doing this optimization. */
      if (TREE_CODE (TREE_OPERAND (returnstmt, 0)) != MODIFY_EXPR)
	continue;

      if (TREE_CODE (TREE_OPERAND (returnstmt_other, 0)) != MODIFY_EXPR)
	continue;
      
      /* Create the new basic block.   */
      new_bb = create_empty_bb (bb_other);
      
      /* Redirect the two basic blocks (the ones with the returns)
         to the new basic block.   */
      redirect_edge_and_branch (bb->succ, new_bb);
      redirect_edge_and_branch (bb_other->succ, new_bb);
      
      bsi = bsi_last (bb);
      bsi_other = bsi_last (bb_other);

      
      ret_decl = TREE_OPERAND (TREE_OPERAND (returnstmt, 0), 0);
      
      type = TREE_TYPE (ret_decl);
      
      /* Add the new variable to hold the return values.   */
      new_var = make_temp (type);
      
      if (TREE_CODE (ret_decl) == SSA_NAME)
        ret_decl = SSA_NAME_VAR (ret_decl);
      
      /* Assign the new temp variable to hold the return value of
         the first side of the branch.   */
      bsi_insert_after (&bsi, build (MODIFY_EXPR, type, new_var,
                                TREE_OPERAND (TREE_OPERAND (returnstmt, 0), 1)),
                         BSI_NEW_STMT);
                   
                   
      /* Do the other side. */
      
      bsi_insert_after (&bsi_other, build (MODIFY_EXPR, type, new_var,
                                           TREE_OPERAND (TREE_OPERAND (returnstmt_other,
                                                                       0), 1)),
                         BSI_NEW_STMT);
                   
      
      /* Build the new basic block which continues the return. */
      new_bsi = bsi_last (new_bb);
      bsi_insert_after (&new_bsi, build1 (RETURN_EXPR, void_type_node,
                                          build (MODIFY_EXPR, type, ret_decl,
                                                 new_var)), TSI_NEW_STMT);
      
      
      
      /* Make sure that the return value gets renamed if needed */
      bitmap_set_bit (vars_to_rename, var_ann (ret_decl)->uid);
      
      /* The new basic block exits.   */
      make_edge (new_bb, EXIT_BLOCK_PTR, 0);
      
      /* update the DOM info if we have to.   */
      if (dom_computed[CDI_DOMINATORS] >= DOM_CONS_OK)
        set_immediate_dominator (CDI_DOMINATORS, new_bb, pred->src);
    }

  
  cleanup_tree_cfg ();
}


/* Always do these optimizations if we have SSA
   trees to work on.  */						
static bool
gate_return (void)
{
  return 1;
}
												
struct tree_opt_pass pass_return =
{
  "return",				/* name */
  gate_return,				/* gate */
  tree_ssa_return,			/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_TREE_RETURN,			/* tv_id */
  PROP_cfg | PROP_ssa,			/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func | TODO_ggc_collect	/* todo_flags_finish */
    | TODO_verify_ssa | TODO_rename_vars
    | TODO_verify_flow
};
												

