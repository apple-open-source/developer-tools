/* Dependence Graph 
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

/* This pass build data dependence graph based on the information
   collected by scalar evolution analyzer.

   A short description of data dependence graph:

   Each node in the graph represents one GIMPLE statement.

   Nodes are connected using dependence edge that describes data
   dependence relation between two nodes.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "errors.h"
#include "ggc.h"
#include "tree.h"
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

/* local function prototypes */
static void dg_init_graph (void);
static void set_dg_node_for_stmt (tree, dependence_node);
static dependence_node dg_get_node_for_stmt (tree, bool);
static dependence_node alloc_dependence_node (void);
static dependence_edge alloc_dependence_edge (void);
static dependence_node dg_create_node (tree);
static dependence_edge dg_find_edge (dependence_node, dependence_node, bool);
static void dump_dg (FILE *, int);
static void dg_delete_edges (void);
static void dg_delete_node (dependence_node);
static struct data_dependence_relation * find_ddr_between_stmts (tree, tree);

/* Initial dependence graph capacity.  */
static int dependence_graph_size = 25;

/* The dependence graph.  */
static GTY (()) varray_type dependence_graph;
static GTY (()) varray_type datarefs;
static GTY (()) varray_type dependence_relations;
static GTY (()) varray_type classic_dist;
static GTY (()) varray_type classic_dir;

/* Total dependence node count.  */
static int n_dependence_node = 0;

#define DEPENDENCE_GRAPH(N) (VARRAY_DG (dependence_graph, (N)))

/* Initialize data dependence graph.  */
static
void dg_init_graph (void)
{
  VARRAY_DG_INIT (dependence_graph, dependence_graph_size, "dependence_graph");
}

/* Create dependency graph.  */
void dg_create_graph (struct loops *loops)
{
  unsigned int i;

  VARRAY_GENERIC_PTR_INIT (classic_dist, 10, "classic_dist");
  VARRAY_GENERIC_PTR_INIT (classic_dir, 10, "classic_dir");
  VARRAY_GENERIC_PTR_INIT (datarefs, 10, "datarefs");
  VARRAY_GENERIC_PTR_INIT (dependence_relations, 10 * 10,
			   "dependence_relations");

  /* Analyze data references and dependence relations using scev.  */
  
  compute_data_dependences_for_loop (loops->num, loop_from_num (loops, 0), 
				     &datarefs, &dependence_relations, 
				     &classic_dist, &classic_dir);
  
  /* Initialize.  */
  dg_init_graph ();

  /* Using data refernces, populate graph.  */
  for (i = 0; i < VARRAY_ACTIVE_SIZE (dependence_relations); i++)
    {
      dependence_edge connecting_edge;

      struct data_reference *first_dr, *second_dr;
      struct data_dependence_relation *ddr;
      tree first_stmt, second_stmt;

      ddr = VARRAY_GENERIC_PTR (dependence_relations, i);

      /* If there is no dependence than do not create an edge.  */
      if (DDR_ARE_DEPENDENT (ddr) == chrec_bot)
	continue;

      /* Get dependence references */
      first_dr = DDR_A (ddr);
      second_dr = DDR_B (ddr);

      /* Get statements */
      first_stmt = DR_STMT (first_dr);
      second_stmt = DR_STMT(second_dr);

      /* Find connecting edge.  */
      connecting_edge = dg_find_edge (dg_get_node_for_stmt (first_stmt, true),
				      dg_get_node_for_stmt (second_stmt, true),
				      true);

      /* Record data dependence relation.  */
      connecting_edge->ddr = ddr;
    }

  if (dump_file)
    {
      dump_dg (dump_file, dump_flags);
    }
}

/* Delete data dependence graph.  */
void
dg_delete_graph (void)
{
  if (dependence_graph)
    {

      /* Delete all edges.  */
      dg_delete_edges ();

      /* Reset node count.  */
      n_dependence_node = 0;

      /* Clear data reference and dependence relations.  */
      if (datarefs)
	VARRAY_CLEAR (datarefs);

      if (dependence_relations)
	VARRAY_CLEAR (dependence_relations);

      if (classic_dir)
	VARRAY_CLEAR (classic_dir);

      if (classic_dist)
	VARRAY_CLEAR (classic_dist);

      /* Clear dependence graph itself.  */
      VARRAY_CLEAR (dependence_graph);

      datarefs = NULL;
      dependence_relations = NULL;
      dependence_graph = NULL;
    } 

}


/*---------------------------------------------------------------------------
			Dependence node
---------------------------------------------------------------------------*/

/* Allocate memory for dependence_node.  */

static dependence_node
alloc_dependence_node (void)
{
  dependence_node dg_node;
  dg_node = ggc_alloc_cleared (sizeof (*dg_node));
  return dg_node;
}

/* Create new dependency_node.  */

static dependence_node 
dg_create_node (tree stmt)
{
  dependence_node dg_node;
  if (!stmt)
    return NULL;

  /* Allocate */
  dg_node = alloc_dependence_node ();

  /* Assign id.  */
  dg_node->node_id = n_dependence_node;

  VARRAY_PUSH_DG (dependence_graph, dg_node);

  /* Increment count.  */
  n_dependence_node++;

  /* Connect dg_node and stmt with each other.  */
  dg_node->stmt = stmt;
  set_dg_node_for_stmt (stmt, dg_node);

  return dg_node;
}

/* Delete dependence node.  */
static void
dg_delete_node (dependence_node node)
{
  stmt_ann_t ann = stmt_ann (node->stmt);

#ifdef ENABLE_CHECKING
  /* If node has live edges, then it is a problem.  */
  if (node->succ || node->pred)
    abort ();
#endif

  /* Clear dg_node entry in stmt_ann */
  if (ann)
    ann->dg_node = NULL;

  /* Delete node.  */
  node = NULL;
}

/*---------------------------------------------------------------------------
			Dependence edge
---------------------------------------------------------------------------*/

/* Allocate memory for dependence_edge.  */

static dependence_edge
alloc_dependence_edge (void)
{
  dependence_edge dg_edge;
  dg_edge = ggc_alloc_cleared (sizeof (*dg_edge));
  return dg_edge;
}

/* Find edge in the dependence graph that connects two nodes. 
 If required, create new edge.  */

static dependence_edge 
dg_find_edge (dependence_node n1, dependence_node n2, bool create)
{
  tree stmt1, stmt2;
  dependence_edge e;

  if (!n1 || !n2)
    abort ();

  stmt1 = DN_STMT (n1);
  stmt2 = DN_STMT (n2);

  if (!stmt1 || !stmt2)
    abort ();

  /* Browse succ edges and see if dst of any edge is stmt2.
     If there is one then return that edge.  */
  for (e = n1->succ; e; e = e->succ_next)
    {
      if (DN_STMT (e->dst) == stmt2)
	return e;
    }

  /* Browse pred edges and see if src of any edge is stmt2.
     If there is one then return that edge.  */
  for (e = n1->pred; e; e = e->pred_next)
    {
      if (DN_STMT (e->src) == stmt2)
	return e;
    }

  if (!create)
    return NULL;

  /* OK, time to create new edge to connect these two nodes.  */
  e = alloc_dependence_edge ();

  /* Set source and destination nodes.  */
  e->src = n1;
  e->dst = n2;

  /* Set succ and pred */
  if (n1->succ)
    e->succ_next = n1->succ;
  n1->succ = e;

  if (n2->pred)
    e->pred_next = n2->pred;
  n2->pred = e;

  /* Return newly created edge.  */
  return e;
}

/* Delete edge 'e' from the graph. After deleting edge 'e'
   if source or destination node does not have any more edges
   associated then delete nodes also.  */
void
dg_delete_edge (dependence_edge e)
{
  dependence_edge current_edge,prev_edge;
  dependence_node src, dst;

  src = e->src;
  dst = e->dst;

  /* Remove edge from the list of source successors.  */
  prev_edge = NULL;
  for (current_edge = src->succ; 
       current_edge; 
       current_edge = current_edge->succ_next)
    {
      if (current_edge == e)
	{
	  /* Found edge 'e' in the list. Remove it from the link list.  */
	  if (prev_edge)
	    prev_edge->succ_next = current_edge->succ_next;
	  else
	    src->succ = current_edge->succ_next;
	}
      else
	/* If this is not edge 'e' then make it prev_edge for next
	   iteration.  */
	prev_edge = current_edge;
    }

  /* If source is not associated with any edge then delete it.  */
  if (!src->succ && !src->pred)
    dg_delete_node (src);

  /* Remove edge from the list of destination predecessors.  */
  prev_edge = NULL;
  for (current_edge = dst->pred;
       current_edge; 
       current_edge = current_edge->pred_next)
    {
      if (current_edge == e)
	{
	  /* Found edge 'e' in the list. Remove it from the link list.  */
	  if (prev_edge)
	    prev_edge->pred_next = current_edge->pred_next;
	  else
	    dst->pred = current_edge->pred_next;
	}
      else
	/* If this is not edge 'e' then make it prev_edge for next
	   iteration.  */
	prev_edge = current_edge;
    }

  /* If source is not associated with any edge then delete it.  */
  if (!dst->succ && !dst->pred)
    dg_delete_node (dst);


  /* Now, actually delete this edge.  */
  e = NULL; 
}

/* Delete all edges in the dependence graph.  */
static void
dg_delete_edges (void)
{
  unsigned int i;
  for (i = 0; i < VARRAY_ACTIVE_SIZE (dependence_graph); i++)
    {
      dependence_edge e;
      dependence_node dg_node = DEPENDENCE_GRAPH (i);

      if (!dg_node)
	continue;

      /* One by one delete all edges.  */
      for (e = dg_node->succ; e; e = e->succ_next)
	dg_delete_edge (e);
    }

}

/*---------------------------------------------------------------------------
			stmt_ann manipulation for dg_node
---------------------------------------------------------------------------*/

/* Find dependence_node for the given input tree. If there is not one,
   create new one.  */

static 
dependence_node  dg_get_node_for_stmt (tree t, bool create)
{
  dependence_node dg_node = dg_node_for_stmt (t);

  /* If there is none, create one.  */
  if (!dg_node && create)
      dg_node = dg_create_node (t);

  return dg_node;
}

/* Set the dg_node for the input tree.  */
static void 
set_dg_node_for_stmt (tree t, dependence_node dg_node)
{
  stmt_ann_t ann;

  if (!t)
    abort (); 

  ann = get_stmt_ann (t);
  if (!ann)
    abort ();

  ann->dg_node = dg_node;
}

/*---------------------------------------------------------------------------
                         Dependence Info Access 
---------------------------------------------------------------------------*/

/* Find data dependence relation between two statements.  If there is no
   relation between two statements then return NULL. */

static struct data_dependence_relation * 
find_ddr_between_stmts (tree stmt1, tree stmt2)
{
  dependence_edge e = NULL;
  dependence_node n1 = NULL;
  dependence_node n2 = NULL;


#ifdef ENABLE_CHECKING
  if (!stmt1 || !stmt2)
    abort ();
#endif
  
  /* First find nodes for the statements.  */
  n1 = dg_node_for_stmt (stmt1);
  n2 = dg_node_for_stmt (stmt2);

  /* If associated dependence node does not exist then this
     two statements are independent.  */
  if (!n1 || !n2)
    return NULL;

  /* Find edge between these two statements.  */
  e = dg_find_edge (n1, n2, false /* Do not create new edge */);

  /* Absence of edge indicates that this two statements are independent.  */
  if (!e)
    return NULL;

  return e->ddr;

}

/* Find data dependence direction between two statements.  */

enum data_dependence_direction
ddg_direction_between_stmts (tree stmt1, tree stmt2, int loop_num)
{
  struct subscript *sub = NULL;
  struct data_dependence_relation *ddr = find_ddr_between_stmts (stmt1, stmt2);

  /* If there is no relation then statements are independent.  */
  if (!ddr)
    return dir_independent;

  /* Get subscript info.  */
  sub = DDR_SUBSCRIPT (ddr, loop_num);  
  if (!sub)
    abort ();

  return SUB_DIRECTION (sub);
}

/* Find data dependence distance between two statements.  */

tree
ddg_distance_between_stmts (tree stmt1, tree stmt2, int loop_num)
{
  struct subscript *sub = NULL;
  struct data_dependence_relation *ddr = find_ddr_between_stmts (stmt1, stmt2);

  /* If there is no relation then statements are independent.  */
  if (!ddr)
    return NULL_TREE;

  /* Get subscript info.  */
  sub = DDR_SUBSCRIPT (ddr, loop_num);  
  if (!sub)
    abort ();

  return SUB_DISTANCE (sub);
}

/*---------------------------------------------------------------------------
			 Printing and debugging
---------------------------------------------------------------------------*/

/* Print dependency graph in the dump file.  */
static void 
dump_dg (FILE *file, int flags ATTRIBUTE_UNUSED)
{
  unsigned int i, j;

  for (i = 0; i < VARRAY_ACTIVE_SIZE (dependence_graph); i++)
    {
      dependence_edge e;
      dependence_node dg_node = DEPENDENCE_GRAPH (i);

      if (!dg_node)
	abort ();

      fprintf (file, "# Dependence Node %d\n", dg_node->node_id);

      /* Print Predecssors */
      fprintf (file, "# Pred :");
      for (e = dg_node->pred; e; e = e->pred_next)
	if (e->dst == dg_node)
	  fprintf (file, "%d ", DN_ID(e->src));
      fprintf (file, "\n");

      /* Print Successors */
      fprintf (file, "# Succ :");
      for (e = dg_node->succ; e; e = e->succ_next)
	if (e->src == dg_node)
	  fprintf (file, "%d ", DN_ID(e->dst));
      fprintf (file, "\n");

      fprintf (file, "# Statement :");
      print_generic_stmt (file, DN_STMT (dg_node), 0);
      
      fprintf (file, "# From\tTo\tDirection Vector\n");
      for (e = dg_node->succ; e; e = e->succ_next)
	{

	  fprintf (file,"  %d\t", DN_ID(e->src));
	  fprintf (file,"%d\t", DN_ID(e->dst));

	  if (DDR_ARE_DEPENDENT (e->ddr) == chrec_top)
	    fprintf (file, "don't know\n");

	  for (j = 0; j < DDR_NUM_SUBSCRIPTS (e->ddr); j++)
	    {
	      struct subscript *sub = DDR_SUBSCRIPT (e->ddr, j);
	      
	      dump_data_dependence_direction (file, SUB_DIRECTION (sub));
	      fprintf (file, " ");
	    }
	  fprintf (file,"\n");
	}

      /* Add one blank line at the end of this node.  */
      fprintf (file, "\n");
    }
}

void 
debug_dg (int flags)
{
  dump_dg (stderr, flags);
}
