/* Loop invariant motion.
   Copyright (C) 2003 Free Software Foundation, Inc.
   
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
#include "domwalk.h"
#include "params.h"
#include "tree-pass.h"
#include "flags.h"

/* A list of dependencies.  */

struct depend
{
  tree stmt;
  struct depend *next;
};

/* APPLE LOCAL AV if-conversion -dpatel  */
/* Move enum move_pos from here to tree-flow.h  */

/* The auxiliary data kept for each statement.  */

struct lim_aux_data
{
  struct loop *max_loop;	/* The outermost loop in that the statement
				   is invariant.  */

  struct loop *tgt_loop;	/* The loop out of that we want to move the
				   invariant.  */

  struct loop *always_executed_in;
				/* The outermost loop for that we are sure
				   the statement is executed if the loop
				   is entered.  */

  bool sm_done;			/* The store motion for a memory reference in
				   the statement has already been decided.  */

  unsigned cost;		/* Cost of the computation of the value.  */

  struct depend *depends;	/* List of statements that must be moved as
				   well.  */
};

#define LIM_DATA(STMT) ((struct lim_aux_data *) (stmt_ann (STMT)->common.aux))

/* Description of a use.  */

struct use
{
  tree *addr;			/* The use itself.  */
  tree stmt;			/* The statement in that it occurs.  */
  struct use *next;		/* Next use in the chain.  */
};

/* Minimum cost of an expensive expression.  */
#define LIM_EXPENSIVE ((unsigned) PARAM_VALUE (PARAM_LIM_EXPENSIVE))

/* The outermost loop for that execution of the header guarantees that the
   block will be executed.  */
#define ALWAYS_EXECUTED_IN(BB) ((struct loop *) (BB)->aux)

/* Maximum uid in the statement in the function.  */

static unsigned max_uid;

/* Checks whether MEM is a memory access that might fail.  */

static bool
unsafe_memory_access_p (tree mem)
{
  tree base, idx;

  switch (TREE_CODE (mem))
    {
    case ADDR_EXPR:
      return false;

    case COMPONENT_REF:
    case REALPART_EXPR:
    case IMAGPART_EXPR:
      return unsafe_memory_access_p (TREE_OPERAND (mem, 0));

    case ARRAY_REF:
      base = TREE_OPERAND (mem, 0);
      idx = TREE_OPERAND (mem, 1);
      if (unsafe_memory_access_p (base))
	return true;

      if (TREE_CODE_CLASS (TREE_CODE (idx)) != 'c')
	return true;

      return !in_array_bounds_p (base, idx);

    case INDIRECT_REF:
      return true;

    default:
      return false;
    }
}

/* Determines whether it is possible to move the statement STMT.  */
/* APPLE LOCAL AV if-conversion -dpatel  */
/* Make this function externally visible.  */

enum move_pos
movement_possibility (tree stmt)
{
  tree lhs, rhs;

  if (flag_unswitch_loops
      && TREE_CODE (stmt) == COND_EXPR)
    {
      /* If we perform unswitching, force the operands of the invariant
	 condition to be moved out of the loop.  */
      get_stmt_operands (stmt);

      return MOVE_POSSIBLE;
    }

  if (TREE_CODE (stmt) != MODIFY_EXPR)
    return MOVE_IMPOSSIBLE;

  if (stmt_ends_bb_p (stmt))
    return MOVE_IMPOSSIBLE;

  get_stmt_operands (stmt);

  if (stmt_ann (stmt)->has_volatile_ops)
    return MOVE_IMPOSSIBLE;

  lhs = TREE_OPERAND (stmt, 0);
  if (TREE_CODE (lhs) == SSA_NAME
      && SSA_NAME_OCCURS_IN_ABNORMAL_PHI (lhs))
    return MOVE_IMPOSSIBLE;

  rhs = TREE_OPERAND (stmt, 1);

  if (TREE_SIDE_EFFECTS (rhs))
    return MOVE_IMPOSSIBLE;

  if (TREE_CODE (lhs) != SSA_NAME
      || tree_could_trap_p (rhs)
      || unsafe_memory_access_p (rhs))
    return MOVE_PRESERVE_EXECUTION;

  return MOVE_POSSIBLE;
}

/* Returns the outermost loop in that DEF behaves as an invariant with respect
   to LOOP.  */

static struct loop *
outermost_invariant_loop (tree def, struct loop *loop)
{
  tree def_stmt;
  basic_block def_bb;
  struct loop *max_loop;

  if (is_gimple_min_invariant (def))
    return superloop_at_depth (loop, 1);

  def_stmt = SSA_NAME_DEF_STMT (def);
  def_bb = bb_for_stmt (def_stmt);
  if (!def_bb)
    return superloop_at_depth (loop, 1);

  max_loop = find_common_loop (loop, def_bb->loop_father);

  if (LIM_DATA (def_stmt) && LIM_DATA (def_stmt)->max_loop)
    max_loop = find_common_loop (max_loop,
				 LIM_DATA (def_stmt)->max_loop->outer);
  if (max_loop == loop)
    return NULL;
  max_loop = superloop_at_depth (loop, max_loop->depth + 1);

  return max_loop;
}

/* Adds a dependency on DEF to DATA on statement inside LOOP.  If ADD_COST is
   true, add the cost of the computation to the cost in DATA.  */

static bool
add_dependency (tree def, struct lim_aux_data *data, struct loop *loop,
		bool add_cost)
{
  tree def_stmt = SSA_NAME_DEF_STMT (def);
  basic_block def_bb = bb_for_stmt (def_stmt);
  struct loop *max_loop;
  struct depend *dep;

  if (!def_bb)
    return true;

  max_loop = outermost_invariant_loop (def, loop);
  if (!max_loop)
    return false;

  if (flow_loop_nested_p (data->max_loop, max_loop))
    data->max_loop = max_loop;

  if (!LIM_DATA (def_stmt))
    return true;

  if (add_cost
      && def_bb->loop_father == loop)
    data->cost += LIM_DATA (def_stmt)->cost;

  dep = xmalloc (sizeof (struct depend));
  dep->stmt = def_stmt;
  dep->next = data->depends;
  data->depends = dep;

  return true;
}

/* Estimates a cost of statement STMT.  TODO -- the values here are just ad-hoc
   constants.  The estimates should be based on target-specific values.  */

static unsigned
stmt_cost (tree stmt)
{
  tree lhs, rhs;
  unsigned cost = 1;

  /* Always try to create possibilities for unswitching.  */
  if (TREE_CODE (stmt) == COND_EXPR)
    return 20;

  lhs = TREE_OPERAND (stmt, 0);
  rhs = TREE_OPERAND (stmt, 1);

  /* Hoisting memory references out should almost surely be a win.  */
  if (!is_gimple_variable (lhs))
    cost += 20;
  if (is_gimple_addr_expr_arg (rhs) && !is_gimple_variable (rhs))
    cost += 20;

  switch (TREE_CODE (rhs))
    {
    case CALL_EXPR:
      /* So should be hoisting calls.  */

      /* Unless the call is a builtin_constant_p; this always folds to a
	 constant, so moving it is useless.  */
      rhs = get_callee_fndecl (rhs);
      if (DECL_BUILT_IN (rhs)
	  && DECL_FUNCTION_CODE (rhs) == BUILT_IN_CONSTANT_P)
	return 0;

      cost += 20;
      break;

    case MULT_EXPR:
    case TRUNC_DIV_EXPR:
    case CEIL_DIV_EXPR:
    case FLOOR_DIV_EXPR:
    case ROUND_DIV_EXPR:
    case EXACT_DIV_EXPR:
    case CEIL_MOD_EXPR:
    case FLOOR_MOD_EXPR:
    case ROUND_MOD_EXPR:
    case TRUNC_MOD_EXPR:
      /* Division and multiplication are usually expensive.  */
      cost += 20;
      break;

    default:
      break;
    }

  return cost;
}

/* Determine maximal level to that it is possible to move a statement STMT.
   If MUST_PRESERVE_EXEC is true, we must preserve the fact whether the
   statement is executed.  */

static bool
determine_max_movement (tree stmt, bool must_preserve_exec)
{
  basic_block bb = bb_for_stmt (stmt);
  struct loop *loop = bb->loop_father;
  struct loop *level;
  struct lim_aux_data *lim_data = LIM_DATA (stmt);
  use_optype uses;
  vuse_optype vuses;
  vdef_optype vdefs;
  stmt_ann_t ann = stmt_ann (stmt);
  unsigned i;
  
  if (must_preserve_exec)
    level = ALWAYS_EXECUTED_IN (bb);
  else
    level = superloop_at_depth (loop, 1);
  lim_data->max_loop = level;

  uses = USE_OPS (ann);
  for (i = 0; i < NUM_USES (uses); i++)
    if (!add_dependency (USE_OP (uses, i), lim_data, loop, true))
      return false;

  vuses = VUSE_OPS (ann);
  for (i = 0; i < NUM_VUSES (vuses); i++)
    if (!add_dependency (VUSE_OP (vuses, i), lim_data, loop, false))
      return false;

  vdefs = VDEF_OPS (ann);
  for (i = 0; i < NUM_VDEFS (vdefs); i++)
    if (!add_dependency (VDEF_OP (vdefs, i), lim_data, loop, false))
      return false;

  lim_data->cost += stmt_cost (stmt);

  return true;
}

/* Sets a level to that the statement STMT is moved to LEVEL due to moving of
   statement from ORIG_LOOP and update levels of all dependencies.  */

static void
set_level (tree stmt, struct loop *orig_loop, struct loop *level)
{
  struct loop *stmt_loop = bb_for_stmt (stmt)->loop_father;
  struct depend *dep;

  stmt_loop = find_common_loop (orig_loop, stmt_loop);
  if (LIM_DATA (stmt) && LIM_DATA (stmt)->tgt_loop)
    stmt_loop = find_common_loop (stmt_loop,
				  LIM_DATA (stmt)->tgt_loop->outer);
  if (flow_loop_nested_p (stmt_loop, level))
    return;

  if (!LIM_DATA (stmt))
    abort ();

  if (level != LIM_DATA (stmt)->max_loop
      && !flow_loop_nested_p (LIM_DATA (stmt)->max_loop, level))
    abort ();

  LIM_DATA (stmt)->tgt_loop = level;
  for (dep = LIM_DATA (stmt)->depends; dep; dep = dep->next)
    set_level (dep->stmt, orig_loop, level);
}

/* Determines a level to that really hoist the statement STMT.  TODO -- use
   profiling information to set it more sanely.  */

static void
set_profitable_level (tree stmt)
{
  set_level (stmt, bb_for_stmt (stmt)->loop_father, LIM_DATA (stmt)->max_loop);
}

/* Checks whether STMT is a nonpure call.  */

static bool
nonpure_call_p (tree stmt)
{
  if (TREE_CODE (stmt) == MODIFY_EXPR)
    stmt = TREE_OPERAND (stmt, 1);

  return (TREE_CODE (stmt) == CALL_EXPR
	  && TREE_SIDE_EFFECTS (stmt));
}

/* Releases the memory occupied by DATA.  */

static void
free_lim_aux_data (struct lim_aux_data *data)
{
  struct depend *dep, *next;

  for (dep = data->depends; dep; dep = next)
    {
      next = dep->next;
      free (dep);
    }
  free (data);
}

/* Determine invariantness of statements in basic block BB.  Callback
   for walk_dominator_tree.  */

static void
determine_invariantness_stmt (struct dom_walk_data *dw_data ATTRIBUTE_UNUSED,
			      basic_block bb)
{
  enum move_pos pos;
  block_stmt_iterator bsi;
  tree stmt;
  bool maybe_never = ALWAYS_EXECUTED_IN (bb) == NULL;
  struct loop *outermost = ALWAYS_EXECUTED_IN (bb);

  if (!bb->loop_father->outer)
    return;

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "Basic block %d (loop %d -- depth %d):\n\n",
	     bb->index, bb->loop_father->num, bb->loop_father->depth);

  for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
    {
      stmt = bsi_stmt (bsi);

      pos = movement_possibility (stmt);
      if (pos == MOVE_IMPOSSIBLE)
	{
	  if (nonpure_call_p (stmt))
	    {
	      maybe_never = true;
	      outermost = NULL;
	    }
	  continue;
	}

      stmt_ann (stmt)->common.aux = xcalloc (1, sizeof (struct lim_aux_data));
      LIM_DATA (stmt)->always_executed_in = outermost;

      if (maybe_never && pos == MOVE_PRESERVE_EXECUTION)
	continue;

      if (!determine_max_movement (stmt, pos == MOVE_PRESERVE_EXECUTION))
	{
	  LIM_DATA (stmt)->max_loop = NULL;
	  continue;
	}

      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  print_generic_stmt_indented (dump_file, stmt, 0, 2);
	  fprintf (dump_file, "  invariant up to level %d, cost %d.\n\n",
		   LIM_DATA (stmt)->max_loop->depth,
		   LIM_DATA (stmt)->cost);
	}

      if (LIM_DATA (stmt)->cost >= LIM_EXPENSIVE)
	set_profitable_level (stmt);
    }
}

/* For each statement determines the outermost loop in that it is invariant,
   statements on whose motion it depends and the cost of the computation.  */

static void
determine_invariantness (void)
{
  struct dom_walk_data walk_data;

  memset (&walk_data, 0, sizeof (struct dom_walk_data));
  walk_data.before_dom_children_before_stmts = determine_invariantness_stmt;

  init_walk_dominator_tree (&walk_data);
  walk_dominator_tree (&walk_data, ENTRY_BLOCK_PTR);
  fini_walk_dominator_tree (&walk_data);
}

/* Commits edge inserts and updates loop info.  */

void
loop_commit_inserts (void)
{
  unsigned old_last_basic_block, i;
  basic_block bb;

  old_last_basic_block = last_basic_block;
  bsi_commit_edge_inserts (NULL);
  for (i = old_last_basic_block; i < (unsigned) last_basic_block; i++)
    {
      bb = BASIC_BLOCK (i);
      add_bb_to_loop (bb,
		      find_common_loop (bb->succ->dest->loop_father,
					bb->pred->src->loop_father));
    }
}

/* Moves the statements in basic block BB to the right place.  Callback
   for walk_dominator_tree.  */

static void
move_computations_stmt (struct dom_walk_data *dw_data ATTRIBUTE_UNUSED,
			basic_block bb)
{
  struct loop *level;
  block_stmt_iterator bsi;
  tree stmt;
  unsigned cost = 0;

  if (!bb->loop_father->outer)
    return;

  for (bsi = bsi_start (bb); !bsi_end_p (bsi); )
    {
      stmt = bsi_stmt (bsi);

      if (!LIM_DATA (stmt))
	{
	  bsi_next (&bsi);
	  continue;
	}

      cost = LIM_DATA (stmt)->cost;
      level = LIM_DATA (stmt)->tgt_loop;
      free_lim_aux_data (LIM_DATA (stmt));
      stmt_ann (stmt)->common.aux = NULL;

      if (!level)
	{
	  bsi_next (&bsi);
	  continue;
	}

      /* We do not really want to move conditionals out of the loop; we just
	 placed it here to force its operands to be moved if neccesary.  */
      if (TREE_CODE (stmt) == COND_EXPR)
	continue;

      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "Moving statement\n");
	  print_generic_stmt (dump_file, stmt, 0);
	  fprintf (dump_file, "(cost %u) out of loop %d.\n\n",
		   cost, level->num);
	}
      bsi_insert_on_edge (loop_preheader_edge (level), stmt);
      bsi_remove (&bsi);
    }
}

/* Moves the statements to the requiered level.  */

static void
move_computations (void)
{
  struct dom_walk_data walk_data;

  memset (&walk_data, 0, sizeof (struct dom_walk_data));
  walk_data.before_dom_children_before_stmts = move_computations_stmt;

  init_walk_dominator_tree (&walk_data);
  walk_dominator_tree (&walk_data, ENTRY_BLOCK_PTR);
  fini_walk_dominator_tree (&walk_data);

  loop_commit_inserts ();
  rewrite_into_ssa (false);
  if (bitmap_first_set_bit (vars_to_rename) >= 0)
    {
      /* The rewrite of ssa names may cause violation of loop closed ssa
	 form invariants.  TODO -- avoid these rewrites completely.
	 Information in virtual phi nodes is sufficient for it.  */
      rewrite_into_loop_closed_ssa ();
    }
  bitmap_clear (vars_to_rename);
}

/* Checks whether variable in *INDEX is movable out of the loop passed
   in DATA.  Callback for for_each_index.  */

static bool
may_move_till (tree base ATTRIBUTE_UNUSED, tree *index, void *data)
{
  struct loop *loop = data, *max_loop;

  if (TREE_CODE (*index) != SSA_NAME)
    return true;

  max_loop = outermost_invariant_loop (*index, loop);

  if (!max_loop)
    return false;

  if (loop == max_loop
      || flow_loop_nested_p (max_loop, loop))
    return true;

  return false;
}

/* Forces variable in *INDEX to be moved out of the loop passed
   in DATA.  Callback for for_each_index.  */

static bool
force_move_till (tree base ATTRIBUTE_UNUSED, tree *index, void *data)
{
  tree stmt;

  if (TREE_CODE (*index) != SSA_NAME)
    return true;

  stmt = SSA_NAME_DEF_STMT (*index);
  if (IS_EMPTY_STMT (stmt))
    return true;

  set_level (stmt, bb_for_stmt (stmt)->loop_father, data);

  return true;
}

/* Records use of *ADDR in STMT to USES.  */

static void
record_use (struct use **uses, tree stmt, tree *addr)
{
  struct use *use = xmalloc (sizeof (struct use));

  use->stmt = stmt;
  use->addr = addr;

  use->next = *uses;
  *uses = use;
}

/* Releases list of uses USES.  */

static void
free_uses (struct use *uses)
{
  struct use *act;

  while (uses)
    {
      act = uses;
      uses = uses->next;
      free (act);
    }
}

/* Finds the single address inside LOOP corresponding to the virtual
   ssa version defined in STMT.  Stores the list of its uses to USES.  */

static tree
single_reachable_address (struct loop *loop, tree stmt, struct use **uses)
{
  tree *queue = xmalloc (sizeof (tree) * max_uid);
  sbitmap seen = sbitmap_alloc (max_uid);
  tree addr = NULL, *aaddr;
  unsigned in_queue = 1;
  dataflow_t df;
  unsigned i, n;

  sbitmap_zero (seen);

  *uses = NULL;

  queue[0] = stmt;
  SET_BIT (seen, stmt_ann (stmt)->uid);

  while (in_queue)
    {
      stmt = queue[--in_queue];

      if (LIM_DATA (stmt)
	  && LIM_DATA (stmt)->sm_done)
	goto fail;

      switch (TREE_CODE (stmt))
	{
	case MODIFY_EXPR:
	  aaddr = &TREE_OPERAND (stmt, 0);
	  if (is_gimple_reg (*aaddr)
	      || !is_gimple_lvalue (*aaddr))
	    aaddr = &TREE_OPERAND (stmt, 1);
	  if (is_gimple_reg (*aaddr)
	      || !is_gimple_lvalue (*aaddr)
	      || (addr && !operand_equal_p (*aaddr, addr, 0)))
	    goto fail;
	  addr = *aaddr;

	  record_use (uses, stmt, aaddr);
	  /* Fallthru.  */

	case PHI_NODE:
	  df = get_immediate_uses (stmt);
	  n = num_immediate_uses (df);

	  for (i = 0; i < n; i++)
	    {
	      stmt = immediate_use (df, i);

	      if (!flow_bb_inside_loop_p (loop, bb_for_stmt (stmt)))
		continue;

	      if (TEST_BIT (seen, stmt_ann (stmt)->uid))
		continue;
	      SET_BIT (seen, stmt_ann (stmt)->uid);

	      queue[in_queue++] = stmt;
	    }

	  break;

	default:
	  goto fail;
	}
    }

  free (queue);
  sbitmap_free (seen);

  return addr;

fail:
  free_uses (*uses);
  *uses = NULL;
  free (queue);
  sbitmap_free (seen);

  return NULL;
}

/* Rewrites uses in list USES by TMP_VAR.  */

static void
rewrite_uses (tree tmp_var, struct use *uses)
{
  vdef_optype vdefs;
  vuse_optype vuses;
  unsigned i;
  tree var;

  for (; uses; uses = uses->next)
    {
      vdefs = STMT_VDEF_OPS (uses->stmt);
      for (i = 0; i < NUM_VDEFS (vdefs); i++)
	{
	  var = SSA_NAME_VAR (VDEF_RESULT (vdefs, i));
	  bitmap_set_bit (vars_to_rename, var_ann (var)->uid);
	}

      vuses = STMT_VUSE_OPS (uses->stmt);
      for (i = 0; i < NUM_VUSES (vuses); i++)
	{
	  var = SSA_NAME_VAR (VUSE_OP (vuses, i));
	  bitmap_set_bit (vars_to_rename, var_ann (var)->uid);
	}

      *uses->addr = tmp_var;
      modify_stmt (uses->stmt);
    }
}

/* Records request for store motion of address ADDR from LOOP.  USES is the
   list of uses to replace.  Exits from the LOOP are stored in EXITS, there
   are N_EXITS of them.  */

static void
schedule_sm (struct loop *loop, edge *exits, unsigned n_exits, tree addr,
	     struct use *uses)
{
  struct use *use;
  tree tmp_var;
  unsigned i;
  tree load, store;

  tmp_var = create_tmp_var (TREE_TYPE (addr), "lsm_tmp");
  add_referenced_tmp_var (tmp_var);
  bitmap_set_bit (vars_to_rename,  var_ann (tmp_var)->uid);

  for_each_index (&addr, force_move_till, loop);

  rewrite_uses (tmp_var, uses);
  for (use = uses; use; use = use->next)
    if (LIM_DATA (use->stmt))
      LIM_DATA (use->stmt)->sm_done = true;

  /* Emit the load & stores.  */
  load = build (MODIFY_EXPR, void_type_node, tmp_var, addr);
  modify_stmt (load);
  stmt_ann (load)->common.aux = xcalloc (1, sizeof (struct lim_aux_data));
  LIM_DATA (load)->max_loop = loop;
  LIM_DATA (load)->tgt_loop = loop;

  /* Put this into the latch, so that we are sure it will be processed after
     all dependencies.  */
  bsi_insert_on_edge (loop_latch_edge (loop), load);

  for (i = 0; i < n_exits; i++)
    {
      store = build (MODIFY_EXPR, void_type_node,
		     unshare_expr (addr), tmp_var);
      bsi_insert_on_edge (exits[i], store);
    }
}

/* For a virtual ssa version REG, determine whether all its uses inside
   the LOOP correspond to a single address and whether it is hoistable.  LOOP
   has N_EXITS stored in EXITS.  */

static void
determine_lsm_reg (struct loop *loop, edge *exits, unsigned n_exits, tree reg)
{
  tree addr;
  struct use *uses, *use;
  struct loop *must_exec;
  
  if (is_gimple_reg (reg))
    return;
  
  addr = single_reachable_address (loop, SSA_NAME_DEF_STMT (reg), &uses);
  if (!addr)
    return;

  if (!for_each_index (&addr, may_move_till, loop))
    {
      free_uses (uses);
      return;
    }

  if (unsafe_memory_access_p (addr))
    {
      for (use = uses; use; use = use->next)
	{
	  if (!LIM_DATA (use->stmt))
	    continue;

	  must_exec = LIM_DATA (use->stmt)->always_executed_in;
	  if (!must_exec)
	    continue;

	  if (must_exec == loop
	      || flow_loop_nested_p (must_exec, loop))
	    break;
	}

      if (!use)
	{
	  free_uses (uses);
	  return;
	}
    }

  schedule_sm (loop, exits, n_exits, addr, uses);
  free_uses (uses);
}

/* Checks whether LOOP with N_EXITS exits stored in EXITS is suitable for
   a store motion.  */

static bool
loop_suitable_for_sm (struct loop *loop ATTRIBUTE_UNUSED, edge *exits, unsigned n_exits)
{
  unsigned i;

  for (i = 0; i < n_exits; i++)
    if (exits[i]->flags & EDGE_ABNORMAL)
      return false;

  return true;
}

/* Determine for all memory references whether we can hoist them out of
   the LOOP.  */

static void
determine_lsm_loop (struct loop *loop)
{
  tree phi;
  unsigned n_exits;
  edge *exits = get_loop_exit_edges (loop, &n_exits);

  if (!loop_suitable_for_sm (loop, exits, n_exits))
    {
      free (exits);
      return;
    }

  for (phi = phi_nodes (loop->header); phi; phi = TREE_CHAIN (phi))
    determine_lsm_reg (loop, exits, n_exits, PHI_RESULT (phi));

  free (exits);
}

/* Determine for all memory references inside LOOPS whether we can hoist them
   out.  */

static void
determine_lsm (struct loops *loops)
{
  struct loop *loop;
  basic_block bb;

  /* Create a UID for each statement in the function.  Ordering of the
     UIDs is not important for this pass.  */
  max_uid = 0;
  FOR_EACH_BB (bb)
    {
      block_stmt_iterator bsi;
      tree phi;

      for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
	stmt_ann (bsi_stmt (bsi))->uid = max_uid++;

      for (phi = phi_nodes (bb); phi; phi = TREE_CHAIN (phi))
	stmt_ann (phi)->uid = max_uid++;
    }

  compute_immediate_uses (TDFA_USE_VOPS, NULL);

  /* Pass the loops from the outermost.  For each virtual operand loop phi node
     check whether all the references inside the loop correspond to a single
     address, and if so, move them.  */

  loop = loops->tree_root->inner;
  while (1)
    {
      determine_lsm_loop (loop);

      if (loop->inner)
	{
	  loop = loop->inner;
	  continue;
	}
      while (!loop->next)
	{
	  loop = loop->outer;
	  if (loop == loops->tree_root)
	    {
	      free_df ();
	      loop_commit_inserts ();
	      return;
	    }
	}
      loop = loop->next;
    }
}

/* Fills ALWAYS_EXECUTED_IN for basic blocks in LOOP.  CONTAINS_CALL is
   the bitmap of blocks that contain a call.  */

static void
fill_always_executed_in (struct loop *loop, sbitmap contains_call)
{
  basic_block bb = NULL, *bbs, last = NULL;
  unsigned i;
  edge e;
  struct loop *inn_loop = loop;

  if (!loop->header->aux)
    {
      bbs = get_loop_body_in_dom_order (loop);

      for (i = 0; i < loop->num_nodes; i++)
	{
	  bb = bbs[i];

	  if (dominated_by_p (CDI_DOMINATORS, loop->latch, bb))
	    last = bb;

	  if (TEST_BIT (contains_call, bb->index))
	    break;

	  for (e = bb->succ; e; e = e->succ_next)
	    if (!flow_bb_inside_loop_p (loop, e->dest))
	      break;
	  if (e)
	    break;

	  /* A loop might be infinite (TODO use simple loop analysis
	     to disprove this if possible).  */
	  if (bb->flags & BB_IRREDUCIBLE_LOOP)
	    break;

	  if (!flow_bb_inside_loop_p (inn_loop, bb))
	    break;

	  if (bb->loop_father->header == bb)
	    {
	      if (!dominated_by_p (CDI_DOMINATORS, loop->latch, bb))
		break;

	      /* In a loop that is always entered we may proceed anyway.
		 But record that we entered it and stop once we leave it.  */
	      inn_loop = bb->loop_father;
	    }
	}

      while (1)
	{
	  last->aux = loop;
	  if (last == loop->header)
	    break;
	  last = get_immediate_dominator (CDI_DOMINATORS, last);
	}

      free (bbs);
    }

  for (loop = loop->inner; loop; loop = loop->next)
    fill_always_executed_in (loop, contains_call);
}

/* Compute information needed by the pass.  LOOPS is the loop tree.  */

static void
tree_ssa_lim_initialize (struct loops *loops)
{
  sbitmap contains_call = sbitmap_alloc (last_basic_block);
  block_stmt_iterator bsi;
  struct loop *loop;
  basic_block bb;

  /* Set ALWAYS_EXECUTED_IN.  Quadratic, can be improved.  */
  
  sbitmap_zero (contains_call);
  FOR_EACH_BB (bb)
    {
      for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
	{
	  if (nonpure_call_p (bsi_stmt (bsi)))
	    break;
	}

      if (!bsi_end_p (bsi))
	SET_BIT (contains_call, bb->index);
    }

  for (loop = loops->tree_root->inner; loop; loop = loop->next)
    fill_always_executed_in (loop, contains_call);

  sbitmap_free (contains_call);
}

/* Cleans up after the invariant motion pass.  */

static void
tree_ssa_lim_finalize (void)
{
  basic_block bb;

  FOR_EACH_BB (bb)
    {
      bb->aux = NULL;
    }
}

/* Moves invariants from LOOPS.  Only "expensive" invariants are moved out --
   i.e. those that are likely to be win regardless of the register presure.  */

void
tree_ssa_lim (struct loops *loops)
{
  tree_ssa_lim_initialize (loops);

  /* For each statement determine the outermost loop in that it is
     invariant and cost for computing the invariant.  */
  determine_invariantness ();

  /* For each memory reference determine whether it is possible to hoist it
     out of the loop.  Force the necessary invariants to be moved out of the
     loops as well.  */
  determine_lsm (loops);

  /* Move the expressions that are expensive enough.  */
  move_computations ();

  tree_ssa_lim_finalize ();
}
