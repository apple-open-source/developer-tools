/* Loop Vectorization
   Copyright (C) 2003, 2004 Free Software Foundation, Inc.
   Contributed by Dorit Naishlos <dorit@il.ibm.com>

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

#ifndef GCC_TREE_VECTORIZER_H
#define GCC_TREE_VECTORIZER_H

/* Used for naming of new temporaries.  */
enum vect_var_kind {
  vect_simple_var,
  vect_pointer_var
};

/* Defines type of operation: unary or binary. */
enum operation_type {
  unary_op = 1,
  binary_op
};

/*-----------------------------------------------------------------*/
/* Info on vectorized defs.                                        */
/*-----------------------------------------------------------------*/
enum stmt_vec_info_type {
  undef_vec_info_type = 0,
  load_vec_info_type,
  store_vec_info_type,
  op_vec_info_type,
  /* APPLE LOCAL begin AV if-conversion -dpatel  */
  assignment_vec_info_type,
  select_vec_info_type,
  compare_vec_info_type
  /* APPLE LOCAL end AV if-conversion -dpatel  */
};

typedef struct _stmt_vec_info {

  enum stmt_vec_info_type type;

  /* The stmt to which this info struct refers to.  */
  tree stmt;

  /* The loop with resprct to which STMT is vectorized.  */
  struct loop *loop;

  /* Not all stmts in the loop need to be vectorized. e.g, the incrementation
     of the loop induction variable and computation of array indexes. relevant
     indicates whether the stmt needs to be vectorized.  */
  bool relevant;

  /* The vector type to be used.  */
  tree vectype;

  /* The vectorized version of the stmt.  */
  tree vectorized_stmt;

  /* Relevant only for array accesses;
     A GIMPLE stmt is expected to have at most 1 array access.  */

  struct data_reference *data_ref_info;
} *stmt_vec_info;

/* Access Functions.  */
#define STMT_VINFO_TYPE(S)       (S)->type
#define STMT_VINFO_STMT(S)       (S)->stmt
#define STMT_VINFO_LOOP(S)       (S)->loop
#define STMT_VINFO_RELEVANT_P(S) (S)->relevant
#define STMT_VINFO_VECTYPE(S)    (S)->vectype
#define STMT_VINFO_VEC_STMT(S)   (S)->vectorized_stmt
#define STMT_VINFO_DATA_REF(S)   (S)->data_ref_info

static inline void set_stmt_info (stmt_ann_t ann, stmt_vec_info stmt_info);
static inline stmt_vec_info vinfo_for_stmt (tree stmt);

static inline void
set_stmt_info (stmt_ann_t ann, stmt_vec_info stmt_info)
{
  if (ann)
    ann->common.aux = (char *) stmt_info;
}

static inline stmt_vec_info
vinfo_for_stmt (tree stmt)
{
  stmt_ann_t ann = stmt_ann (stmt);
  return ann ? (stmt_vec_info) ann->common.aux : NULL;
}

/*-----------------------------------------------------------------*/
/* Info on data references alignment.                              */
/*-----------------------------------------------------------------*/

#define DR_MISALIGNMENT(DR)   (DR)->aux

static inline bool
aligned_access_p (struct data_reference *data_ref_info)
{
  return (DR_MISALIGNMENT (data_ref_info) == 0);
}

static inline bool
unknown_alignment_for_access_p (struct data_reference *data_ref_info)
{
  return (DR_MISALIGNMENT (data_ref_info) == -1);
}


/*-----------------------------------------------------------------*/
/* Info on vectorized loops.                                       */
/*-----------------------------------------------------------------*/
typedef struct _loop_vec_info {

  /* The loop to which this info struct refers to.  */
  struct loop *loop;

  /* The loop basic blocks.  */
  basic_block *bbs;

  /* The loop exit_condition.  */
  tree exit_cond;

  /* Number of iterations. -1 if unknown.  */
  int num_iters;

  /* If number of iterations is unknown at compile time, this tree represents it.  */
  tree symb_numb_of_iters;

  /* Is the loop vectorizable? */
  bool vectorizable;

  /* Unrolling factor  */
  int vectorization_factor;

  /* All data references in the loop that are being written to.  */
  varray_type data_ref_writes;

  /* All data references in the loop that are being read from.  */
  varray_type data_ref_reads;
} *loop_vec_info;

/* Access Functions.  */
#define LOOP_VINFO_LOOP(L)           (L)->loop
#define LOOP_VINFO_BBS(L)            (L)->bbs
#define LOOP_VINFO_EXIT_COND(L)      (L)->exit_cond
#define LOOP_VINFO_NITERS(L)         (L)->num_iters
#define LOOP_VINFO_VECTORIZABLE_P(L) (L)->vectorizable
#define LOOP_VINFO_VECT_FACTOR(L)    (L)->vectorization_factor
#define LOOP_VINFO_DATAREF_WRITES(L) (L)->data_ref_writes
#define LOOP_VINFO_DATAREF_READS(L)  (L)->data_ref_reads
#define LOOP_VINFO_SYMB_NUM_OF_ITERS(L) (L)->symb_numb_of_iters

#define LOOP_VINFO_NITERS_KNOWN_P(L) ((L)->num_iters > 0)

/*-----------------------------------------------------------------*/
/* Function prototypes.                                            */
/*-----------------------------------------------------------------*/

/* Main driver.  */
extern void vectorize_loops (struct loops *);

/* creation and deletion of loop and stmt info structs.  */
extern loop_vec_info new_loop_vec_info (struct loop *loop);
extern void destroy_loop_vec_info (loop_vec_info);
extern stmt_vec_info new_stmt_vec_info (tree stmt, struct loop *loop);

/* FORNOW: analyze and then vectorize each loop, rather than first analyzing all
   loops and then vetorizing all loops, which we may want to do in the future
   (for example, to exploit data reuse across loops?).  */
#undef ANALYZE_ALL_THEN_VECTORIZE_ALL

/* APPLE LOCAL begin AV if-conversion -dpatel  */
extern bool default_vector_compare_p (void);
extern bool default_vector_compare_for_p (tree, enum tree_code);
extern tree default_vector_compare_stmt (tree, tree, tree, tree, enum tree_code);
extern bool default_vector_select_p (void);
extern bool default_vector_select_for_p (tree);
extern tree default_vector_select_stmt (tree, tree, tree, tree, tree);
/* APPLE LOCAL end AV if-conversion -dpatel  */
#endif  /* GCC_TREE_VECTORIZER_H  */
