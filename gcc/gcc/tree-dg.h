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

#ifndef GCC_TREE_SSA_DG_H
#define GCC_TREE_SSA_DG_H

#include "tree-data-ref.h"

struct dependence_edge_def GTY (())
{
  /* Dependence relation */
  struct data_dependence_relation *ddr;

  /* Links through the predessor and successor lists.  */
  struct dependence_edge_def *pred_next;
  struct dependence_edge_def *succ_next;

  /* Source and destination.  */
  struct dependence_node_def *src;
  struct dependence_node_def *dst;

  /* Auxiliary info.  */
  /* PTR GTY ((skip (""))) aux; */
};

typedef struct dependence_edge_def *dependence_edge;

struct dependence_node_def GTY (())
{

  int node_id;

  /* Statement */
  tree stmt;

  /* Dependece ddges */
  dependence_edge pred;
  dependence_edge succ;

  /* Next and previous nodes in the chain.  */
  struct dependence_node_def *next;
  struct dependence_node_def *prev;

};

typedef struct dependence_node_def *dependence_node;

#define DN_STMT(node) (node->stmt)
#define DN_ID(node) (node->node_id)

/* Create dependency graph.  */
extern void dg_create_graph (struct loops *);

/* Delete dependency graph.  */
extern void dg_delete_graph (void);

/* Delete edge from the dependency graph.  */
void dg_delete_edge (dependence_edge);

/* Debug dependence graph.  */
extern void debug_dg (int);

/* Find data dependence direction between two statements.  */
enum data_dependence_direction ddg_direction_between_stmts (tree, tree, int);

/* Find data dependence distance between two statements.  */
tree ddg_distance_between_stmts (tree, tree, int);

#endif
