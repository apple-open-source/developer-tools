/* High-level loop manipulation functions.
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
#include "tree.h"
#include "rtl.h"
#include "tm_p.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "output.h"
#include "diagnostic.h"
#include "tree-flow.h"
#include "tree-dump.h"
#include "timevar.h"
#include "cfgloop.h"
#include "tree-pass.h"
#include "cfglayout.h"

static basic_block lv_adjust_loop_entry_edge (basic_block, basic_block, edge, 
					      tree); 
static void lv_update_pending_stmts (edge e);
static void lv_adjust_loop_header_phi (basic_block, basic_block, basic_block, 
				       edge);

/* Copies phi nodes in newly created copies of the LOOP.  The new blocks start
   since FIRST_NEW_BLOCK index.  PEELING is true if we were peeling
   the loop.  */

static void
copy_phi_nodes (struct loop *loop, unsigned first_new_block, bool peeling)
{
  unsigned i;
  basic_block bb, orig;
  tree phi, new_phi, def;
  edge e, new_e;
  edge latch = loop_latch_edge (loop), entry = loop_preheader_edge (loop);

  for (i = first_new_block; i < (unsigned) last_basic_block; i++)
    {
      tree nlist;
      bb = BASIC_BLOCK (i);
      orig = bb->rbi->original;

      for (phi = phi_nodes (orig); phi; phi = TREE_CHAIN (phi))
	{
	  new_phi = create_phi_node (PHI_RESULT (phi), bb);

	  if (orig == loop->header)
	    {
	      if (!bb->pred || bb->pred->pred_next)
		abort ();

	      new_e = bb->pred;
	      e = (peeling && bb->rbi->copy_number == 1 ? entry : latch);
	      def = phi_element_for_edge (phi, e)->def;
	      add_phi_arg (&new_phi, def, new_e);
	      continue;
	    }

	  for (new_e = bb->pred; new_e; new_e = new_e->pred_next)
	    {
	      e = find_edge (new_e->src->rbi->original, orig);
	      if (!e)
		abort ();

	      def = phi_element_for_edge (phi, e)->def;
	      add_phi_arg (&new_phi, def, new_e);
	    }
	}

      /* neverse phi nodes to keep them in original order.  */
      nlist = nreverse (phi_nodes (bb));
      set_phi_nodes (bb, nlist);
    }

  if (peeling)
    {
      /* Update the phi nodes in the header so that the latch value comes from
	 both edges.  */
      for (phi = phi_nodes (loop->header); phi; phi = TREE_CHAIN (phi))
	{
	  def = phi_element_for_edge (phi, latch)->def;
	  phi_element_for_edge (phi, entry)->def = def;
	}
    }
}

/* Constructs list of all ssa names defined inside LOOP.  */

static tree
collect_defs (struct loop *loop)
{
  basic_block *body = get_loop_body (loop);
  unsigned i, j;
  tree phi, stmt;
  block_stmt_iterator bsi;
  def_optype defs;
  vdef_optype vdefs;
  tree ret = NULL_TREE;

  for (i = 0; i < loop->num_nodes; i++)
    {
      for (bsi = bsi_start (body[i]); !bsi_end_p (bsi); bsi_next (&bsi))
	{
	  stmt = bsi_stmt (bsi);

	  get_stmt_operands (stmt);

	  defs = STMT_DEF_OPS (stmt);
	  for (j = 0; j < NUM_DEFS (defs); j++)
	    ret = tree_cons (NULL, DEF_OP (defs, j), ret);

	  vdefs = STMT_VDEF_OPS (stmt);
	  for (j = 0; j < NUM_VDEFS (vdefs); j++)
	    ret = tree_cons (NULL, VDEF_RESULT (vdefs, j), ret);
	}

      for (phi = phi_nodes (body[i]); phi; phi = TREE_CHAIN (phi))
	ret = tree_cons (NULL, PHI_RESULT (phi), ret);
    }

  return ret;
}

/* For each definition in DEFINITIONS allocates:

   NDUPL + 1 copies if ORIGIN is true
   NDUPL copies if ORIGIN is false

   (one for each duplicate of the loop body).  
   If ORIGIN is true, additional set of DEFINITIONS 
   is allocated for initial loop copy. */

static void
allocate_new_names (tree definitions, unsigned ndupl, bool origin)
{
  tree def;
  unsigned i;
  ssa_name_ann_t ann;
  tree *new_names;
  bool abnormal;

  for (; definitions; definitions = TREE_CHAIN (definitions))
    {
      def = TREE_VALUE (definitions);
      ann = get_ssa_name_ann (def);
      new_names = xmalloc (sizeof (tree) * (ndupl + (origin ? 1 : 0)));

      abnormal = SSA_NAME_OCCURS_IN_ABNORMAL_PHI (def);
      for (i = (origin ? 0 : 1); i <= ndupl; i++)
	{
	  new_names[i] = duplicate_ssa_name (def, SSA_NAME_DEF_STMT (def));
	  SSA_NAME_OCCURS_IN_ABNORMAL_PHI (new_names[i]) = abnormal;
	}
       /* Delay this until now so it doesn't get propagated to the copies.
	  That would cause problems in the next outer loop.  */
      ann->common.aux = new_names;
    }
}

/* Renames the variable *OP_P in statement STMT.  If DEF is true,
   *OP_P is defined by the statement.  N_COPY is the number of the
   copy of the loop body we are renaming.  */

static void
rename_op (tree *op_p, bool def, tree stmt, unsigned n_copy)
{
  ssa_name_ann_t ann;
  tree *new_names;

  if(!op_p)
    return;

  if (TREE_CODE (*op_p) != SSA_NAME)
    return;

  ann = ssa_name_ann (*op_p);
  new_names = ann ? ann->common.aux : NULL;

  /* Something defined outside of the loop.  */
  if (!new_names)
    return;

  /* An ordinary ssa name defined in the loop.  */

  *op_p = new_names[n_copy];
  if (def)
    SSA_NAME_DEF_STMT (*op_p) = stmt;
}

/* Renames the variables in basic block BB.  */

static void
rename_variables_in_bb (basic_block bb)
{
  tree phi;
  block_stmt_iterator bsi;
  tree stmt;
  stmt_ann_t ann;
  use_optype uses;
  vuse_optype vuses;
  def_optype defs;
  vdef_optype vdefs;
  unsigned i, nbb = bb->rbi->copy_number;
  edge e;

  for (phi = phi_nodes (bb); phi; phi = TREE_CHAIN (phi))
    rename_op (&PHI_RESULT (phi), true, phi, nbb);

  for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
    {
      stmt = bsi_stmt (bsi);
      get_stmt_operands (stmt);
      ann = stmt_ann (stmt);

      uses = USE_OPS (ann);
      for (i = 0; i < NUM_USES (uses); i++)
	rename_op (USE_OP_PTR (uses, i), false, stmt, nbb);

      defs = DEF_OPS (ann);
      for (i = 0; i < NUM_DEFS (defs); i++)
	rename_op (DEF_OP_PTR (defs, i), true, stmt, nbb);

      vuses = VUSE_OPS (ann);
      for (i = 0; i < NUM_VUSES (vuses); i++)
	rename_op (VUSE_OP_PTR (vuses, i), false, stmt, nbb);

      vdefs = VDEF_OPS (ann);
      for (i = 0; i < NUM_VDEFS (vdefs); i++)
	{
	  rename_op (VDEF_OP_PTR (vdefs, i), false, stmt, nbb);
	  rename_op (VDEF_RESULT_PTR (vdefs, i), true, stmt, nbb);
	}
    }

  for (e = bb->succ; e; e = e->succ_next)
    for (phi = phi_nodes (e->dest); phi; phi = TREE_CHAIN (phi))
      rename_op (&phi_element_for_edge (phi, e)->def, false, phi, nbb);
}

/* Renames variables in the area copied by tree_duplicate_loop_to_header_edge.
   FIRST_NEW_BLOCK is the first block in the copied area.   */

static void
rename_variables (unsigned first_new_block)
{
  unsigned i;
  basic_block bb;

  for (i = first_new_block; i < (unsigned) last_basic_block; i++)
    {
      bb = BASIC_BLOCK (i);

      rename_variables_in_bb (bb);

      if (bb->rbi->copy_number == 1)
	rename_variables_in_bb (bb->rbi->original);
    }
}

/* Releases the structures holding the new ssa names. 
   The original ssa names are released if ORIGIN is true.
   Otherwise they are saved for initial loop copy.  */

static void
free_new_names (tree definitions, bool origin)
{
  tree def;
  ssa_name_ann_t ann;

  for (; definitions; definitions = TREE_CHAIN (definitions))
    {
      def = TREE_VALUE (definitions);
      ann = ssa_name_ann (def);

      free (ann->common.aux);
      ann->common.aux = NULL;

      if (origin)
	 release_ssa_name (def);
    }
}

/* Sets SSA_NAME_DEF_STMT for results of all phi nodes in BB.  */

static void
set_phi_def_stmts (basic_block bb)
{
  tree phi;

  for (phi = phi_nodes (bb); phi; phi = TREE_CHAIN (phi))
    SSA_NAME_DEF_STMT (PHI_RESULT (phi)) = phi;
}

/* Extends phi nodes on EXIT to the newly created edges.  */

static void
extend_exit_phi_nodes (unsigned first_new_block, edge exit)
{
  basic_block exit_block = exit->dest;
  edge ae;
  tree phi, def;

  for (phi = phi_nodes (exit_block); phi; phi = TREE_CHAIN (phi))
    {
      def = phi_element_for_edge (phi, exit)->def;

      for (ae = exit_block->pred; ae; ae = ae->pred_next)
	{
	  if (ae->src->index < (int) first_new_block)
	    continue;

	  if (ae->src->rbi->original != exit->src)
	    continue;

	  add_phi_arg (&phi, def, ae);
	}
    }
}

/* The same ad cfgloopmanip.c:duplicate_loop_to_header_edge, but also updates
   ssa.  In order to achieve this, only loops whose exits all lead to the same
   location are handled.
   
   FIXME: we create some degenerate phi nodes that could be avoided by copy
   propagating them instead.  Unfortunately this is not completely
   straightforward due to problems with constant folding.  */

bool
tree_duplicate_loop_to_header_edge (struct loop *loop, edge e,
				    struct loops *loops,
				    unsigned int ndupl, sbitmap wont_exit,
				    edge orig, edge *to_remove,
				    unsigned int *n_to_remove, int flags)
{
  unsigned first_new_block;
  basic_block bb;
  unsigned i;
  bool peeling = (e != loop_latch_edge (loop));
  edge latch, latch_copy;
  tree phi, arg, map, def;
  tree definitions;
  edge *exits;
  unsigned n_exits;

  if (!(loops->state & LOOPS_HAVE_SIMPLE_LATCHES))
    return false;
  if (!(loops->state & LOOPS_HAVE_PREHEADERS))
    return false;

#ifdef ENABLE_CHECKING
  verify_loop_closed_ssa ();
#endif

  exits = get_loop_exit_edges (loop, &n_exits);
  definitions = collect_defs (loop);

  first_new_block = last_basic_block;
  if (!duplicate_loop_to_header_edge (loop, e, loops, ndupl, wont_exit,
				      orig, to_remove, n_to_remove, flags))
    return false;

  allocate_new_names (definitions, ndupl, true);

  /* Readd the removed phi args for e.  */
  latch = loop_latch_edge (loop);
  latch_copy = peeling ? loop_preheader_edge (loop) : latch;
  map = PENDING_STMT (e);
  PENDING_STMT (e) = NULL;

  for (phi = phi_nodes (loop->header), arg = map;
       phi;
       phi = TREE_CHAIN (phi), arg = TREE_CHAIN (arg))
    {
      def = TREE_VALUE (arg);
      add_phi_arg (&phi, def, latch_copy);
    }
  if (arg)
    abort ();

  /* Extend exit phi nodes.  */
  for (i = 0; i < n_exits; i++)
    extend_exit_phi_nodes (first_new_block, exits[i]);
  free (exits);

  /* Copy the phi nodes.  */
  copy_phi_nodes (loop, first_new_block, peeling);

  /* Rename the variables.  */
  rename_variables (first_new_block);
  free_new_names (definitions, true);

  /* For some time we have the identical ssa names as results in multiple phi
     nodes.  When phi node is resized, it sets SSA_NAME_DEF_STMT of its result
     to the new copy.  This means that we cannot easily ensure that the ssa
     names defined in those phis are pointing to the right one -- so just
     recompute SSA_NAME_DEF_STMT for them.  */ 

  for (i = first_new_block; i < (unsigned) last_basic_block; i++)
    {
      bb = BASIC_BLOCK (i);
      set_phi_def_stmts (bb);
      if (bb->rbi->copy_number == 1)
  	set_phi_def_stmts (bb->rbi->original);
    }

#ifdef ENABLE_CHECKING
  verify_loop_closed_ssa ();
#endif

  return true;
}

/* Unrolls and peels each loop twice for testing.  */

void
test_unrolling_and_peeling (struct loops *loops)
{
  struct loop *loop;
  unsigned i;

  for (i = 1; i < loops->num; i++)
    {
      loop = loops->parray[i];

      if (!loop
	  || loop->inner)
	continue;

      tree_duplicate_loop_to_header_edge (loop, loop_preheader_edge (loop),
					  loops, 2, NULL, NULL, NULL, NULL, 0);
      verify_loop_structure (loops);
      verify_ssa ();

      tree_duplicate_loop_to_header_edge (loop, loop_latch_edge (loop),
					  loops, 2, NULL, NULL, NULL, NULL, 0);
      verify_loop_structure (loops);
      verify_ssa ();
    }
}

/*---------------------------------------------------------------------------
  Loop versioning
  ---------------------------------------------------------------------------*/
 
/* Adjust entry edge for lv.
   
  e is a incoming edge. 

  --- edge e ---- > [second_head]

  Split it and insert new conditional expression and adjust edges.
   
   --- edge e ---> [cond expr] ---> [first_head]
                        |
                        +---------> [second_head]

*/
   
static basic_block
lv_adjust_loop_entry_edge (basic_block first_head,
			   basic_block second_head,
			   edge e,
			   tree cond_expr)
{ 
  block_stmt_iterator bsi;
  basic_block orig_head = e->src;
  basic_block new_head = NULL;
  tree goto1 = NULL_TREE;
  tree goto2 = NULL_TREE;
  tree new_cond_expr = NULL_TREE;
  edge e0, e1;

  /* Split edge 'e'. This will create a new basic block, where we can
     insert conditioanl expr.  */
  new_head = split_edge (e);
  set_immediate_dominator (CDI_DOMINATORS, new_head, orig_head);

  /* Build new conditional expr */
  goto1 = build1 (GOTO_EXPR, void_type_node, tree_block_label (first_head));
  goto2 = build1 (GOTO_EXPR, void_type_node, tree_block_label (second_head));
  new_cond_expr = build (COND_EXPR, void_type_node, cond_expr, goto1, goto2);

  /* Add new cond. in new head.  */ 
  bsi = bsi_start (new_head); 
  bsi_insert_after (&bsi, new_cond_expr, BSI_NEW_STMT);

  /* Adjust edges appropriately to connect new head with first head
     as well as second head.  */
  e1 = make_edge (new_head, first_head, EDGE_TRUE_VALUE);
  set_immediate_dominator (CDI_DOMINATORS, first_head, new_head);
  make_edge (new_head, second_head, EDGE_FALSE_VALUE);
  set_immediate_dominator (CDI_DOMINATORS, second_head, new_head);

  /* Adjust loop header phi nodes.  */
  lv_adjust_loop_header_phi (first_head, second_head, new_head, e1);

  /* When edge 'e' was split, it created a fall through edge
      from new head to second head. Above created FALSE edge
      from new head to second head and now we do not need the
      fall through edge.  */
  for (e0 = new_head->succ; e0; e0 = e0->succ_next)
    if (e0->dest == second_head)
      e0->flags &= ~EDGE_FALLTHRU;

  return new_head;
}

/* Add phi args using PENDINT_STMT list.  */

static void
lv_update_pending_stmts (edge e)
{
  basic_block dest;
  tree phi, arg, def;

  if (!PENDING_STMT (e))
    return;

  dest = e->dest;

  for (phi = phi_nodes (dest), arg = PENDING_STMT (e);
       phi;
       phi = TREE_CHAIN (phi), arg = TREE_CHAIN (arg))
    {
      def = TREE_VALUE (arg);
      add_phi_arg (&phi, def, e);
    }

  PENDING_STMT (e) = NULL;
}

/* Adjust phi nodes for 'first' basic block.  'second' basic block is a copy
   of 'first'. Both of them are dominated by 'new_head' basic block. When
   'new_head' was created by 'second's incoming edge it received phi arguments
   on the edge by split_edge(). Later, additional edge 'e' was created to
   connect 'new_head' and 'first'. Now this routnine adds phi args on this 
   additional edge 'e' that new_head to second edge received as part of edge 
   splitting.
*/

static void
lv_adjust_loop_header_phi (basic_block first, basic_block second,
			   basic_block new_head, edge e)
{
  tree phi1, phi2;

  /* Browse all 'second' basic block phi nodes and add phi args to
     edge 'e' for 'first' head. PHI args are always in correct order.  */

  for (phi2 = phi_nodes (second), phi1 = phi_nodes (first); 
       phi2 && phi1; 
       phi2 = TREE_CHAIN (phi2),  phi1 = TREE_CHAIN (phi1))
    {
      int i;
      for (i = 0; i < PHI_NUM_ARGS (phi2); i++)
	{
	  if (PHI_ARG_EDGE (phi2, i)->src == new_head)
	    {
	      tree def = PHI_ARG_DEF (phi2, i);
	      add_phi_arg (&phi1, def, e);
	    }
	}
    }
}


/* Main entry point for Loop Versioning transformation.
   
This transformation given a condition and a loop, creates
-if (condition) { loop_copy1 } else { loop_copy2 },
where loop_copy1 is the loop transformed in one way, and loop_copy2
is the loop transformed in another way (or unchanged). 'condition'
may be a run time test for things that were not resolved by static
analysis (overlapping ranges (anti-aliasing), alignment, etc.).  */

struct loop *
tree_ssa_loop_version (struct loops *loops, struct loop * loop, 
		       tree cond_expr, basic_block *condition_bb)
{
  edge entry, latch_edge;
  basic_block first_head, second_head;
  int irred_flag;
  struct loop *nloop;

  /* CHECKME: Loop versioning does not handle nested loop at this point.  */
  if (loop->inner)
    return NULL;

  /* Record entry and latch edges for the loop */
  entry = loop_preheader_edge (loop);

  /* Note down head of loop as first_head.  */
  first_head = entry->dest;

  /* Duplicate loop.  */
  irred_flag = entry->flags & EDGE_IRREDUCIBLE_LOOP;
  entry->flags &= ~EDGE_IRREDUCIBLE_LOOP;
  if (!tree_duplicate_loop_to_header_edge (loop, entry, loops, 1,
					   NULL, NULL, NULL, NULL, 0))
    {
      entry->flags |= irred_flag;
      return NULL;
    }

  /* After duplication entry edge now points to new loop head block.
     Note down new head as second_head.  */
  second_head = entry->dest;

  /* Split loop entry edge and insert new block with cond expr.  */
  *condition_bb = lv_adjust_loop_entry_edge (first_head, second_head, entry, 
					    cond_expr); 

  latch_edge = loop->latch->rbi->copy->succ;
  nloop = loopify (loops, 
		   latch_edge,
		   loop->header->rbi->copy->pred,
		   *condition_bb,
		   false /* Do not redirect all edges.  */);

  /* loopify redirected latch_edge. Update its PENDING_STMTS.  */ 
  lv_update_pending_stmts (latch_edge);

  /* loopify redirected condition_bb's succ edge. Update its PENDING_STMTS.  */ 
  lv_update_pending_stmts (FALLTHRU_EDGE (*condition_bb));

  /* Adjust irreducible flag.  */
  if (irred_flag)
    {
      (*condition_bb)->flags |= BB_IRREDUCIBLE_LOOP;
      loop_preheader_edge (loop)->flags |= EDGE_IRREDUCIBLE_LOOP;
      loop_preheader_edge (nloop)->flags |= EDGE_IRREDUCIBLE_LOOP;
      (*condition_bb)->pred->flags |= EDGE_IRREDUCIBLE_LOOP;
    }

  /* At this point condition_bb is loop predheader with two successors, 
     first_head and second_head.   Make sure that loop predheader has only 
     one successor. */
  loop_split_edge_with (loop_preheader_edge (loop), NULL);
  loop_split_edge_with (loop_preheader_edge (nloop), NULL);

  /* Ensure that the latch has just a single successor.  */
  loop_split_edge_with (loop_latch_edge (loop), NULL);
  loop_split_edge_with (loop_latch_edge (nloop), NULL);

  return nloop;
}

/* Update loop versioning condition.
   This is used by other optimizations/transformations to disable
   one loop version.  */
void
update_lv_condition (basic_block *bb, tree new_cond)
{
  tree stmt;
  block_stmt_iterator bsi = bsi_last (*bb);

  stmt = bsi_stmt (bsi);

  if (TREE_CODE (stmt) == COND_EXPR)
    {
      TREE_OPERAND (stmt, 0) = new_cond;
      modify_stmt (stmt);
    }
  else
    abort ();
}

void
test_loop_versioning (struct loops *loops)
{
  struct loop *loop;
  unsigned i;
  tree cond_expr;
  
  for (i = 1; i < loops->num; i = i+ 2)
    {
      struct loop *nloop;
      basic_block condition_bb;
      loop = loops->parray[i];
      
      if (!loop)
	continue;
      
      cond_expr = build (EQ_EXPR, boolean_type_node,
			 integer_one_node,
			 integer_zero_node);
      
      nloop = tree_ssa_loop_version (loops, loop, cond_expr, &condition_bb);
      
      if (nloop)
	{
	  verify_loop_structure (loops);
	  verify_dominators (CDI_DOMINATORS);
	  verify_ssa ();
	  
	  update_lv_condition (&condition_bb, boolean_true_node);
	}
    }

}

/* Add exit phis for the USE on EXIT.  */

static void
add_exit_phis_edge (basic_block exit, tree use)
{
  tree phi, def_stmt = SSA_NAME_DEF_STMT (use);
  basic_block def_bb = bb_for_stmt (def_stmt);
  struct loop *def_loop;
  edge e;

  /* Check that some of the edges entering the EXIT block exits a loop in
     that USE is defined.  */
  for (e = exit->pred; e; e = e->pred_next)
    {
      def_loop = find_common_loop (def_bb->loop_father, e->src->loop_father);
      if (!flow_bb_inside_loop_p (def_loop, e->dest))
	break;
    }

  if (!e)
    return;

  phi = create_phi_node (use, exit);

  for (e = exit->pred; e; e = e->pred_next)
    add_phi_arg (&phi, use, e);

  SSA_NAME_DEF_STMT (use) = def_stmt;
}

/* Add exit phis for VAR that is used in LIVEIN.
   Exits of the loops are stored in EXITS.  */

static void
add_exit_phis_var (tree var, bitmap livein, bitmap exits)
{
  bitmap def;
  int index;
  basic_block def_bb = bb_for_stmt (SSA_NAME_DEF_STMT (var));

  bitmap_clear_bit (livein, def_bb->index);

  def = BITMAP_XMALLOC ();
  bitmap_set_bit (def, def_bb->index);
  compute_global_livein (livein, def);
  BITMAP_XFREE (def);

  EXECUTE_IF_AND_IN_BITMAP (exits, livein, 0, index,
			    add_exit_phis_edge (BASIC_BLOCK (index), var));
}

/* Add exit phis for the names marked in NAMES_TO_RENAME.
   Exits of the loops are stored in EXITS.  Sets of blocks where the ssa
   names are used are stored in USE_BLOCKS.  SSA_NAMES is the array of ssa
   names indexed by their versions.  */

static void
add_exit_phis (bitmap names_to_rename, bitmap *use_blocks, bitmap loop_exits,
	       tree *ssa_names)
{
  unsigned i;

  EXECUTE_IF_SET_IN_BITMAP (names_to_rename, 0, i,
    {
      add_exit_phis_var (ssa_names[i], use_blocks[i], loop_exits);
    });
}

/* Returns a bitmap of all loop exit edge targets.  */

static bitmap
get_loops_exits (void)
{
  bitmap exits = BITMAP_XMALLOC ();
  basic_block bb;
  edge e;

  FOR_EACH_BB (bb)
    {
      for (e = bb->pred; e; e = e->pred_next)
	if (e->src != ENTRY_BLOCK_PTR
	    && !flow_bb_inside_loop_p (e->src->loop_father, bb))
	  {
	    bitmap_set_bit (exits, bb->index);
	    break;
	  }
    }

  return exits;
}

/* For USE in BB, if it is used outside of the loop it is defined in,
   mark it in NAMES_TO_RENAME.  Record basic block BB where it is used
   to USE_BLOCKS, and the ssa name itself to NAMES.  */

static void
find_uses_to_rename_use (basic_block bb, tree use, bitmap names_to_rename,
			 bitmap *use_blocks, tree *names)
{
  unsigned ver;
  basic_block def_bb;
  struct loop *def_loop;

  if (TREE_CODE (use) != SSA_NAME)
    return;

  ver = SSA_NAME_VERSION (use);
  def_bb = bb_for_stmt (SSA_NAME_DEF_STMT (use));
  if (!def_bb)
    return;
  def_loop = def_bb->loop_father;

  /* If the definition is not inside loop, it is not interesting.  */
  if (!def_loop->outer)
    return;

  names[ver] = use;
  if (!use_blocks[ver])
    use_blocks[ver] = BITMAP_XMALLOC ();
  bitmap_set_bit (use_blocks[ver], bb->index);

  if (!flow_bb_inside_loop_p (def_loop, bb))
    bitmap_set_bit (names_to_rename, ver);
}

/* For uses in STMT, mark names that are used outside of the loop they are
   defined in in NAMES_TO_RENAME.  Record the set of blocks in that the ssa
   names are defined to USE_BLOCKS, and the names themselves to NAMES.  */

static void
find_uses_to_rename_stmt (tree stmt, bitmap names_to_rename,
			  bitmap *use_blocks, tree *names)
{
  use_optype uses;
  vuse_optype vuses;
  vdef_optype vdefs;
  stmt_ann_t ann;
  unsigned i;
  basic_block bb = bb_for_stmt (stmt);

  get_stmt_operands (stmt);
  ann = stmt_ann (stmt);

  uses = USE_OPS (ann);
  for (i = 0; i < NUM_USES (uses); i++)
    find_uses_to_rename_use (bb, USE_OP (uses, i),
			     names_to_rename, use_blocks, names);

  vuses = VUSE_OPS (ann);
  for (i = 0; i < NUM_VUSES (vuses); i++)
    find_uses_to_rename_use (bb, VUSE_OP (vuses, i),
			     names_to_rename, use_blocks, names);

  vdefs = VDEF_OPS (ann);
  for (i = 0; i < NUM_VDEFS (vdefs); i++)
    find_uses_to_rename_use (bb, VDEF_OP (vdefs, i),
			     names_to_rename, use_blocks, names);
}

/* Marks names that are used outside of the loop they are defined in
   in NAMES_TO_RENAME.  Records the set of blocks in that the ssa
   names are defined to USE_BLOCKS, and the names themselves to NAMES.  */

static void
find_uses_to_rename (bitmap names_to_rename, bitmap *use_blocks, tree *names)
{
  basic_block bb;
  block_stmt_iterator bsi;
  tree phi;
  unsigned i;

  FOR_EACH_BB (bb)
    {
      for (phi = phi_nodes (bb); phi; phi = TREE_CHAIN (phi))
	for (i = 0; i < (unsigned) PHI_NUM_ARGS (phi); i++)
	  find_uses_to_rename_use (PHI_ARG_EDGE (phi, i)->src,
				   PHI_ARG_DEF (phi, i),
				   names_to_rename, use_blocks, names);

      for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
	find_uses_to_rename_stmt (bsi_stmt (bsi),
				  names_to_rename, use_blocks, names);
    }
}

/* Rewrites the program into a loop closed ssa form -- i.e. inserts extra
   phi nodes to ensure that no variable is used outside the loop it is
   defined in.

   This strenghtening of the basic ssa form has several advantages:

   1) Updating it during unrolling/peeling/versioning is trivial, since
      we do not need to care about the uses outside of the loop.
   2) The behavior of all uses of an induction variable is the same.
      Without this, you need to distinguish the case when the variable
      is used outside of the loop it is defined in, for example

      for (i = 0; i < 100; i++)
	{
	  for (j = 0; j < 100; j++)
	    {
	      k = i + j;
	      use1 (k);
	    }
	  use2 (k);
	}

      Looking from the outer loop with the normal SSA form, the first use of k
      is not well-behaved, while the second one is an induction variable with
      base 99 and step 1.  */

void
rewrite_into_loop_closed_ssa (void)
{
  bitmap names_to_rename = BITMAP_XMALLOC ();
  bitmap loop_exits = get_loops_exits ();
  bitmap *use_blocks;
  tree *ssa_names;
  unsigned i;

  tree_ssa_dce_no_cfg_changes ();

  use_blocks = xcalloc (highest_ssa_version, sizeof (bitmap));
  ssa_names = xcalloc (highest_ssa_version, sizeof (tree));

  /* Find the uses outside loops.  */
  find_uses_to_rename (names_to_rename, use_blocks, ssa_names);

  /* Add the phi nodes on exits of the loops for the names we need to
     rewrite.  */
  add_exit_phis (names_to_rename, use_blocks, loop_exits, ssa_names);

  for (i = 0; i < highest_ssa_version; i++)
    BITMAP_XFREE (use_blocks[i]);
  free (use_blocks);
  free (ssa_names);
  BITMAP_XFREE (loop_exits);

  /* Do the rewriting.  */
  rewrite_ssa_into_ssa (names_to_rename);
  BITMAP_XFREE (names_to_rename);
  BITMAP_XFREE (loop_exits);
}

/* Check invariants of the loop closed ssa form for the USE in BB.  */

static void
check_loop_closed_ssa_use (basic_block bb, tree use)
{
  tree def;
  basic_block def_bb;
  
  if (TREE_CODE (use) != SSA_NAME)
    return;

  def = SSA_NAME_DEF_STMT (use);
  def_bb = bb_for_stmt (def);
  if (def_bb
      && !flow_bb_inside_loop_p (def_bb->loop_father, bb))
    abort ();
}

/* Checks invariants of loop closed ssa form in statement STMT in BB.  */

static void
check_loop_closed_ssa_stmt (basic_block bb, tree stmt)
{
  use_optype uses;
  vuse_optype vuses;
  vdef_optype vdefs;
  stmt_ann_t ann;
  unsigned i;

  get_stmt_operands (stmt);
  ann = stmt_ann (stmt);

  uses = USE_OPS (ann);
  for (i = 0; i < NUM_USES (uses); i++)
    check_loop_closed_ssa_use (bb, USE_OP (uses, i));

  vuses = VUSE_OPS (ann);
  for (i = 0; i < NUM_VUSES (vuses); i++)
    check_loop_closed_ssa_use (bb, VUSE_OP (vuses, i));

  vdefs = VDEF_OPS (ann);
  for (i = 0; i < NUM_VDEFS (vdefs); i++)
    check_loop_closed_ssa_use (bb, VDEF_OP (vdefs, i));
}

/* Checks that invariants of the loop closed ssa form are preserved.  */

void
verify_loop_closed_ssa (void)
{
  basic_block bb;
  block_stmt_iterator bsi;
  tree phi;
  unsigned i;

  verify_ssa ();

  FOR_EACH_BB (bb)
    {
      for (phi = phi_nodes (bb); phi; phi = TREE_CHAIN (phi))
	for (i = 0; i < (unsigned) PHI_NUM_ARGS (phi); i++)
	  check_loop_closed_ssa_use (PHI_ARG_EDGE (phi, i)->src,
				     PHI_ARG_DEF (phi, i));

      for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
	check_loop_closed_ssa_stmt (bb, bsi_stmt (bsi));
    }
}

/* Renames variables in new generated LOOP.  */

static void
tdlte_rename_variables_in_loop (struct loop *loop)
{
  unsigned i;
  basic_block *bbs;

  bbs = get_loop_body (loop);

  for (i = 0; i < loop->num_nodes; i++)
    {
      rename_variables_in_bb (bbs[i]);
    }

  free (bbs);
}

/* This function copies phis from loop to new_loop 
   as they were not generated by duplication of bbs.  */

static void
tdlte_copy_phi_nodes (struct loop *loop, struct loop *new_loop)
{
  tree phi, new_phi, def;
  edge new_e;
  edge latch = loop_latch_edge (loop);
  tree nlist;

     
  for (phi = phi_nodes (loop->header); phi; phi = TREE_CHAIN (phi))
    {
      new_phi = create_phi_node (PHI_RESULT (phi), new_loop->header);
      new_e = new_loop->header->pred;
      def = phi_element_for_edge (phi, latch)->def;
      add_phi_arg (&new_phi, def, new_e);
    }

  /* neverse phi nodes to keep them in original order.  */
  nlist = nreverse (phi_nodes (new_loop->header));
  set_phi_nodes (new_loop->header, nlist);

}

/* This function: 
   - copies basic blocks of the loop LOOP;
   - locate them at the only exit of LOOP; 
   - redirect edges so that two loops are produced: 
             initial LOOP and newly generated;
   - update dominators;
   - returns pointer to new loop in NEW_LOOP_P.

   FORNOW: only innermost loops with 
           1 exit are handled. 
*/

static bool
tree_duplicate_loop_to_exit_cfg (struct loop *loop, struct loops *loops,
				 struct loop **new_loop_p)
{
  struct loop *target;
  /* APPLE LOCAL AV while -haifa.  */
  /* Remove local variable latch.  */
  basic_block *new_bbs, *bbs;
  edge latch_edge;

  unsigned i; 
  unsigned  n = loop->num_nodes;
  struct loop *new_loop;
  basic_block exit_dest; 

  bbs = get_loop_body (loop);

  /* Check whether duplication is possible.  */
  if (!can_copy_bbs_p (bbs, loop->num_nodes))
    {
      free (bbs);
      return false;
    }
  new_bbs = xmalloc (sizeof (basic_block) * loop->num_nodes);

  /* Find edge from latch.  */
  latch_edge = loop_latch_edge (loop);

  /* We duplicate only innermost loops */
  if(loop->inner)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	  fprintf (dump_file,
		   "Loop duplication failed. Loop is not innermost.\n");
      free (bbs);
      return false;
    }

  /* FORNOW: only loops with 1 exit. */
  if(loop->num_exits != 1)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	  fprintf (dump_file,
		   "More than one exit from loop.\n");
      return false;
    }    

  /* Loop the new bbs will belong to.  */
  target = loop->outer;
  if(!target)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	  fprintf (dump_file,
		   "Loop is outer-most loop.\n");
      return false;
    }    
    
  /* Generate new loop structure. */
  new_loop = duplicate_loop (loops, loop, target); 
  if(!new_loop)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	  fprintf (dump_file,
		   "duplicate_loop returns NULL.\n");
      return false;

    }
  /* FIXME: Should we copy contents of the loop structure
     to the new loop?  */

  copy_bbs (bbs, n, new_bbs, NULL, 0, NULL, NULL);
  for (i = 0; i < n; i++)
    new_bbs[i]->rbi->copy_number = 1;

  /* Redirect the special edges.  */
  exit_dest = loop->exit_edges[0]->dest;
  if(!exit_dest)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	  fprintf (dump_file,
		   "First exit basic block of loop is NULL.\n");
      return false;
    }
    
  
  redirect_edge_and_branch_force (loop->exit_edges[0],
				  new_bbs[0]);
  /* APPLE LOCAL begin AV while -haifa  */
  set_immediate_dominator (CDI_DOMINATORS, new_bbs[0], loop->exit_edges[0]->src);
  set_immediate_dominator (CDI_DOMINATORS, exit_dest, new_loop->header);
  /* APPLE LOCAL end AV while -haifa  */
  free (new_bbs);
  free (bbs);


  *new_loop_p = new_loop;
  return true;
}

/* This funtion generate pure copy of LOOP 
   and locate it immediately after given LOOP.
   It fixes phis of copy loop so that they inherite 
   one of thier values from exit edge of initil LOOP.  */

bool
tree_duplicate_loop_to_exit (struct loop *loop, struct loops *loops)
{
  struct loop *new_loop = NULL;
  tree definitions;
  edge pred;
  tree *new_names, new_var;
  tree phi, def;
  unsigned first_new_block;
  ssa_name_ann_t ann;

  definitions = collect_defs (loop);

  first_new_block = last_basic_block;
  if(!tree_duplicate_loop_to_exit_cfg (loop, loops, &new_loop))
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file,
		 "tree_duplicate_loop_to_exit failed.\n");
      return false;
    }

  if(!new_loop)
    abort();


  allocate_new_names (definitions, 1, false);

  /* Copy phis from loop->header to new_loop->header.  */
  tdlte_copy_phi_nodes (loop, new_loop);

  /* Rename the variables.  */
  tdlte_rename_variables_in_loop (new_loop);

  /* Fix phis to inherit values from loop exit edge.  */
  for (phi = phi_nodes (new_loop->header); phi; phi = TREE_CHAIN (phi))
    {
      pred = new_loop->header->pred;
      def = phi_element_for_edge (phi, pred)->def;

      if (TREE_CODE (def) != SSA_NAME)
	continue;

      ann = ssa_name_ann (def);
      new_names = ann ? ann->common.aux : NULL;

      /* Something defined outside of the loop.  */
      if (!new_names)
	continue;

      /* An ordinary ssa name defined in the loop.  */
      new_var = new_names[new_loop->header->rbi->copy_number];
      
      add_phi_arg (&phi, new_var, loop_latch_edge(new_loop));
    }


  free_new_names (definitions, false);

  return true;
}

