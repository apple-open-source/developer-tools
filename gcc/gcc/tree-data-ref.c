/* Data references and dependences detectors.
   Copyright (C) 2003 Free Software Foundation, Inc.
   Contributed by Sebastian Pop <s.pop@laposte.net>

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

/* This pass walks the whole program searching for array references.
   The array accesses are recorded in DATA_REFERENCE nodes.  Since the
   information in the DATA_REFERENCE nodes is too precise, the
   dependence testers abstract this information into classic
   representations: distance vectors, direction vectors, affine
   dependence functions, ...  Both the precise and more abstract
   informations are then exposed to the other passes.
   
   The basic test for determining the dependences is: 
   given two access functions chrec1 and chrec2 to a same array, and 
   x and y two vectors from the iteration domain, the same element of 
   the array is accessed twice at iterations x and y if and only if:
   |             chrec1 (x) == chrec2 (y).
   
   The goals of this analysis are:
   
   - to determine the independence: the relation between two
     independent accesses is qualified with the chrec_bot (this
     information allows a loop parallelization),
     
   - when two data references access the same data, to qualify the
     dependence relation with classic dependence representations:
     
       - distance vectors
       - direction vectors
       - loop carried level dependence
       - polyhedron dependence
     or with the chains of recurrences based representation,
     
     
   - to define a knowledge base for storing the data dependeces 
     information,
     
   - to define an interface to access this data.
   
   
   Definitions:
   
   - What is a subscript?  Given two array accesses a subscript is the
   tuple composed of the access functions for a given dimension.
   Example: Given A[f1][f2][f3] and B[g1][g2][g3], there are three
   subscripts: (f1, g1), (f2, g2), (f3, g3).
   
   - Vertical and horizontal couplings.  In some of the comments of
   this analysis, I refer to the overlapping elements of a subscript
   as the vertical coupling, in opposition to the horizontal coupling
   that refers to the coupling between subscripts.
   
   References:
   
   - "Advanced Compilation for High Performance Computing" by Randy Allen 
   and Ken Kennedy.
   
   - "Loop Transformations for Restructuring Compilers - The Foundations" 
   by Utpal Banerjee.
   
*/

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "errors.h"
#include "ggc.h"
#include "tree.h"

/* These RTL headers are needed for basic-block.h.  */
#include "rtl.h"
#include "basic-block.h"
#include "diagnostic.h"
#include "tree-flow.h"
#include "tree-dump.h"
#include "timevar.h"
#include "cfgloop.h"
#include "tree-fold-const.h"
#include "tree-chrec.h"
#include "tree-data-ref.h"
#include "tree-scalar-evolution.h"
#include "tree-pass.h"
#include "lambda.h"

static void subscript_dependence_tester (struct data_dependence_relation *);

static unsigned int data_ref_id = 0;



/* Dump into FILE all the data references from DATAREFS.  */ 

void 
dump_data_references (FILE *file, 
		      varray_type datarefs)
{
  unsigned int i;
  
  for (i = 0; i < VARRAY_ACTIVE_SIZE (datarefs); i++)
    dump_data_reference (file, VARRAY_GENERIC_PTR (datarefs, i));
}

/* Dump into FILE all the dependence relations from DDR.  */ 

void 
dump_data_dependence_relations (FILE *file, 
				varray_type ddr)
{
  unsigned int i;
  
  for (i = 0; i < VARRAY_ACTIVE_SIZE (ddr); i++)
    dump_data_dependence_relation (file, VARRAY_GENERIC_PTR (ddr, i));
}

/* Dump function for a DATA_REFERENCE structure.  */

void 
dump_data_reference (FILE *outf, 
		     struct data_reference *dr)
{
  unsigned int i;
  
  fprintf (outf, "(Data Ref %d: \n  stmt: ", DR_ID (dr));
  print_generic_stmt (outf, DR_STMT (dr), 0);
  fprintf (outf, "  ref: ");
  print_generic_stmt (outf, DR_REF (dr), 0);
  fprintf (outf, "  base_name: ");
  print_generic_stmt (outf, DR_BASE_NAME (dr), 0);
  
  for (i = 0; i < DR_NUM_DIMENSIONS (dr); i++)
    {
      fprintf (outf, "  Access function %d: ", i);
      print_generic_stmt (outf, DR_ACCESS_FN (dr, i), 0);
    }
  fprintf (outf, ")\n");
}

/* Dump function for a DATA_DEPENDENCE_RELATION structure.  */

void 
dump_data_dependence_relation (FILE *outf, 
			       struct data_dependence_relation *ddr)
{
  unsigned int i;
  struct data_reference *dra, *drb;
  
  dra = DDR_A (ddr);
  drb = DDR_B (ddr);
  
  fprintf (outf, "(Data Dep (A = %d, B = %d):", DR_ID (dra), DR_ID (drb));  

  if (DDR_ARE_DEPENDENT (ddr) == chrec_top)
    fprintf (outf, "    (don't know)\n");
  
  else if (DDR_ARE_DEPENDENT (ddr) == chrec_bot)
    fprintf (outf, "    (no dependence)\n");
  
  else
    {
      for (i = 0; i < DDR_NUM_SUBSCRIPTS (ddr); i++)
	{
	  tree chrec;
	  struct subscript *subscript = DDR_SUBSCRIPT (ddr, i);
	  
	  fprintf (outf, "\n (subscript %d:\n", i);
	  fprintf (outf, "  access_fn_A: ");
	  print_generic_stmt (outf, DR_ACCESS_FN (dra, i), 0);
	  fprintf (outf, "  access_fn_B: ");
	  print_generic_stmt (outf, DR_ACCESS_FN (drb, i), 0);
	  
	  chrec = SUB_CONFLICTS_IN_A (subscript);
	  fprintf (outf, "  iterations_that_access_an_element_twice_in_A: ");
	  print_generic_stmt (outf, chrec, 0);
	  if (chrec == chrec_bot)
	    fprintf (outf, "    (no dependence)\n");
	  else if (chrec == chrec_top)
	    fprintf (outf, "    (don't know)\n");
	  else
	    {
	      tree last_iteration = SUB_LAST_CONFLICT_IN_A (subscript);
	      fprintf (outf, "  last_iteration_that_access_an_element_twice_in_A: ");
	      print_generic_stmt (outf, last_iteration, 0);
	    }
	  
	  chrec = SUB_CONFLICTS_IN_B (subscript);
	  fprintf (outf, "  iterations_that_access_an_element_twice_in_B: ");
	  print_generic_stmt (outf, chrec, 0);
	  if (chrec == chrec_bot)
	    fprintf (outf, "    (no dependence)\n");
	  else if (chrec == chrec_top)
	    fprintf (outf, "    (don't know)\n");
	  else
	    {
	      tree last_iteration = SUB_LAST_CONFLICT_IN_B (subscript);
	      fprintf (outf, "  last_iteration_that_access_an_element_twice_in_B: ");
	      print_generic_stmt (outf, last_iteration, 0);
	    }
      
	  fprintf (outf, " )\n");
	}
  
      fprintf (outf, " (Distance Vector: \n");
      for (i = 0; i < DDR_NUM_SUBSCRIPTS (ddr); i++)
	{
	  struct subscript *subscript = DDR_SUBSCRIPT (ddr, i);
      
	  fprintf (outf, "(");
	  print_generic_stmt (outf, SUB_DISTANCE (subscript), 0);
	  fprintf (outf, ")\n");
	}
      fprintf (outf, " )\n");
    }

  fprintf (outf, ")\n");
}



/* Dump function for a DATA_DEPENDENCE_DIRECTION structure.  */

void
dump_data_dependence_direction (FILE *file, 
				enum data_dependence_direction dir)
{
  switch (dir)
    {
    case dir_positive: 
      fprintf (file, "+");
      break;
      
    case dir_negative:
      fprintf (file, "-");
      break;
      
    case dir_equal:
      fprintf (file, "=");
      break;
      
    case dir_positive_or_negative:
      fprintf (file, "+-");
      break;
      
    case dir_positive_or_equal: 
      fprintf (file, "+=");
      break;
      
    case dir_negative_or_equal: 
      fprintf (file, "-=");
      break;
      
    case dir_star: 
      fprintf (file, "*"); 
      break;
      
    default: 
      break;
    }
}



/* Given an ARRAY_REF node REF, records its access functions.
   Example: given A[i][3], record the opnd1 function, ie. the constant
   "3", then recursively call the function on opnd0, ie. the ARRAY_REF
   "A[i]".  The function returns the base name: "A".  */

static tree
analyze_array_indexes (struct loop *loop,
		       varray_type access_fns, 
		       tree ref)
{
  tree opnd0, opnd1;
  tree access_fn;
  
  opnd0 = TREE_OPERAND (ref, 0);
  opnd1 = TREE_OPERAND (ref, 1);
  
  /* The detection of the evolution function for this data access is
     postponed until the dependence test.  This lazy strategy avoids
     the computation of access functions that are of no interest for
     the optimizers.  */
  access_fn = instantiate_parameters 
    (loop, analyze_scalar_evolution (loop, opnd1));
  
  VARRAY_PUSH_TREE (access_fns, access_fn);
  
  /* Recursively record other array access functions.  */
  if (TREE_CODE (opnd0) == ARRAY_REF)
    return analyze_array_indexes (loop, access_fns, opnd0);
  
  /* Return the base name of the data access.  */
  else
    return opnd0;
}

/* For a data reference REF contained in the statemet STMT, initialize
   a DATA_REFERENCE structure, and return it.  Set the IS_READ flag to
   true when REF is in the right hand side of an assignment.  */

struct data_reference *
analyze_array (tree stmt, tree ref, bool is_read)
{
  struct data_reference *res;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "(analyze_array \n");
      fprintf (dump_file, "  (ref = ");
      print_generic_stmt (dump_file, ref, 0);
      fprintf (dump_file, ")\n");
    }
  
  res = ggc_alloc (sizeof (struct data_reference));
  
  DR_ID (res) = data_ref_id++;
  DR_STMT (res) = stmt;
  DR_REF (res) = ref;
  VARRAY_TREE_INIT (DR_ACCESS_FNS (res), 3, "access_fns");
  DR_BASE_NAME (res) = analyze_array_indexes 
    (loop_of_stmt (stmt), DR_ACCESS_FNS (res), ref);
  DR_IS_READ (res) = is_read;
  
  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, ")\n");
  
  return res;
}

/* For a data reference REF contained in the statemet STMT, initialize
   a DATA_REFERENCE structure, and return it.  Set the IS_READ flag to
   true when REF is in the right hand side of an assignment.  */

static struct data_reference *
analyze_array_top (tree stmt)
{
  struct data_reference *res;

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "(analyze_array_top \n");

  res = ggc_alloc (sizeof (struct data_reference));

  DR_ID (res) = data_ref_id++;
  DR_STMT (res) = stmt;
  DR_REF (res) = NULL_TREE;
  DR_BASE_NAME (res) = NULL_TREE;

  VARRAY_TREE_INIT (DR_ACCESS_FNS (res), 1, "access_fns");
  VARRAY_PUSH_TREE (DR_ACCESS_FNS (res), chrec_top);

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, ")\n");

  return res;
}



/* When there exists a dependence relation, determine its distance
   vector.  */

static void
compute_distance_vector (struct data_dependence_relation *ddr)
{
  if (DDR_ARE_DEPENDENT (ddr) == NULL_TREE)
    {
      unsigned int i;
      
      for (i = 0; i < DDR_NUM_SUBSCRIPTS (ddr); i++)
 	{
 	  tree conflicts_a, conflicts_b, difference;
 	  struct subscript *subscript;
 	  
 	  subscript = DDR_SUBSCRIPT (ddr, i);
 	  conflicts_a = SUB_CONFLICTS_IN_A (subscript);
 	  conflicts_b = SUB_CONFLICTS_IN_B (subscript);
 	  difference = chrec_fold_minus 
	    (integer_type_node, conflicts_b, conflicts_a);
 	  
 	  if (evolution_function_is_constant_p (difference))
 	    SUB_DISTANCE (subscript) = difference;
 	  
 	  else
 	    SUB_DISTANCE (subscript) = chrec_top;
 	}
    }
}

/* Initialize a ddr.  */

static struct data_dependence_relation *
initialize_data_dependence_relation (struct data_reference *a, 
				     struct data_reference *b)
{
  struct data_dependence_relation *res;
  
  res = ggc_alloc (sizeof (struct data_dependence_relation));
  DDR_A (res) = a;
  DDR_B (res) = b;

  if (DR_BASE_NAME (a) == NULL_TREE
      || DR_BASE_NAME (b) == NULL_TREE)
    DDR_ARE_DEPENDENT (res) = chrec_top;    

  /* When the dimensions of A and B differ, we directly initialize
     the relation to "there is no dependence": chrec_bot.  */
  else if (DR_NUM_DIMENSIONS (a) != DR_NUM_DIMENSIONS (b)
	   || array_base_name_differ_p (a, b))
    DDR_ARE_DEPENDENT (res) = chrec_bot;
  
  else
    {
      unsigned int i;
      DDR_ARE_DEPENDENT (res) = NULL_TREE;
      DDR_SUBSCRIPTS_VECTOR_INIT (res, DR_NUM_DIMENSIONS (a));
      
      for (i = 0; i < DR_NUM_DIMENSIONS (a); i++)
	{
	  struct subscript *subscript;
	  
	  subscript = ggc_alloc (sizeof (struct subscript));
	  SUB_CONFLICTS_IN_A (subscript) = chrec_top;
	  SUB_CONFLICTS_IN_B (subscript) = chrec_top;
	  SUB_LAST_CONFLICT_IN_A (subscript) = chrec_top;
	  SUB_LAST_CONFLICT_IN_B (subscript) = chrec_top;
	  SUB_DISTANCE (subscript) = chrec_top;
	  SUB_DIRECTION (subscript) = dir_star;
	  VARRAY_PUSH_GENERIC_PTR (DDR_SUBSCRIPTS (res), subscript);
	}
    }
  
  return res;
}

/* Set DDR_ARE_DEPENDENT to CHREC and finalize the subscript overlap
   description.  */

static inline void
finalize_ddr_dependent (struct data_dependence_relation *ddr, 
			tree chrec)
{
  DDR_ARE_DEPENDENT (ddr) = chrec;  
  varray_clear (DDR_SUBSCRIPTS (ddr));
}



/* This section contains the classic Banerjee.  The functions are
   represented by chains of recurrences.  */

/* This is the ZIV test.  ZIV = Zero Index Variable, ie. both
   functions do not depend on the iterations of a loop.  */

static inline bool
ziv_subscript_p (tree chrec_a, 
		 tree chrec_b)
{
  return (evolution_function_is_constant_p (chrec_a)
	  && evolution_function_is_constant_p (chrec_b));
}

/* Determines whether the subscript depends on the evolution of a
   single loop or not.  SIV = Single Index Variable.  */

static bool
siv_subscript_p (tree chrec_a,
		 tree chrec_b)
{
  if ((evolution_function_is_constant_p (chrec_a)
       && evolution_function_is_univariate_p (chrec_b))
      || (evolution_function_is_constant_p (chrec_b)
	  && evolution_function_is_univariate_p (chrec_a)))
    return true;
  
  if (evolution_function_is_univariate_p (chrec_a)
      && evolution_function_is_univariate_p (chrec_b))
    {
      switch (TREE_CODE (chrec_a))
	{
	case POLYNOMIAL_CHREC:
	case EXPONENTIAL_CHREC:
	  switch (TREE_CODE (chrec_b))
	    {
	    case POLYNOMIAL_CHREC:
	    case EXPONENTIAL_CHREC:
	      if (CHREC_VARIABLE (chrec_a) != CHREC_VARIABLE (chrec_b))
		return false;
	      
	    default:
	      return true;
	    }
	  
	default:
	  return true;
	}
    }
  
  return false;
}

/* Analyze a ZIV (Zero Index Variable) subscript.  */

static void 
analyze_ziv_subscript (tree chrec_a, 
		       tree chrec_b, 
		       tree *overlaps_a,
		       tree *overlaps_b)
{
  tree difference;
  
  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "(analyze_ziv_subscript \n");
  
  difference = chrec_fold_minus (integer_type_node, chrec_a, chrec_b);
  
  switch (TREE_CODE (difference))
    {
    case INTEGER_CST:
      if (integer_zerop (difference))
	{
	  /* The difference is equal to zero: the accessed index
	     overlaps for each iteration in the loop.  */
	  *overlaps_a = integer_zero_node;
	  *overlaps_b = integer_zero_node;
	}
      else
	{
	  /* The accesses do not overlap.  */
	  *overlaps_a = chrec_bot;
	  *overlaps_b = chrec_bot;	  
	}
      break;
      
    case INTERVAL_CHREC:
      if (integer_zerop (CHREC_LOW (difference)) 
	  && integer_zerop (CHREC_UP (difference)))
	{
	  /* The difference is equal to zero: the accessed index 
	     overlaps for each iteration in the loop.  */
	  *overlaps_a = integer_zero_node;
	  *overlaps_b = integer_zero_node;
	}
      else 
	{
	  /* There could be an overlap, conservative answer: 
	     "don't know".  */
	  *overlaps_a = chrec_top;
	  *overlaps_b = chrec_top;	  
	}
      break;
      
    default:
      /* We're not sure whether the indexes overlap.  For the moment, 
	 conservatively answer "don't know".  */
      *overlaps_a = chrec_top;
      *overlaps_b = chrec_top;	  
      break;
    }
  
  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, ")\n");
}

/* This is a part of the SIV subscript analyzer (Single Index
   Variable).  */

static void
analyze_siv_subscript_cst_affine (tree chrec_a, 
				  tree chrec_b,
				  tree *overlaps_a, 
				  tree *overlaps_b)
{
  bool value0, value1, value2;
  tree difference = chrec_fold_minus 
    (integer_type_node, CHREC_LEFT (chrec_b), chrec_a);
  
  if (!chrec_is_positive (initial_condition (difference), &value0))
    {
      *overlaps_a = chrec_top;
      *overlaps_b = chrec_top;
      return;
    }
  else
    {
      if (value0 == false)
	{
	  if (!chrec_is_positive (CHREC_RIGHT (chrec_b), &value1))
	    {
	      *overlaps_a = chrec_top;
	      *overlaps_b = chrec_top;      
	      return;
	    }
	  else
	    {
	      if (value1 == true)
		{
		  /* Example:  
		     chrec_a = 12
		     chrec_b = {10, +, 1}
		  */
		  
		  if (tree_fold_divides_p 
		      (integer_type_node, CHREC_RIGHT (chrec_b), difference))
		    {
		      *overlaps_a = integer_zero_node;
		      *overlaps_b = tree_fold_exact_div 
			(integer_type_node, 
			 tree_fold_abs (integer_type_node, difference), 
			 CHREC_RIGHT (chrec_b));
		      return;
		    }
		  
		  /* When the step does not divides the difference, there are
		     no overlaps.  */
		  else
		    {
		      *overlaps_a = chrec_bot;
		      *overlaps_b = chrec_bot;      
		      return;
		    }
		}
	      
	      else
		{
		  /* Example:  
		     chrec_a = 12
		     chrec_b = {10, +, -1}
		     
		     In this case, chrec_a will not overlap with chrec_b.  */
		  *overlaps_a = chrec_bot;
		  *overlaps_b = chrec_bot;
		  return;
		}
	    }
	}
      else 
	{
	  if (!chrec_is_positive (CHREC_RIGHT (chrec_b), &value2))
	    {
	      *overlaps_a = chrec_top;
	      *overlaps_b = chrec_top;      
	      return;
	    }
	  else
	    {
	      if (value2 == false)
		{
		  /* Example:  
		     chrec_a = 3
		     chrec_b = {10, +, -1}
		  */
		  if (tree_fold_divides_p 
		      (integer_type_node, CHREC_RIGHT (chrec_b), difference))
		    {
		      *overlaps_a = integer_zero_node;
		      *overlaps_b = tree_fold_exact_div 
			(integer_type_node, difference, CHREC_RIGHT (chrec_b));
		      return;
		    }
		  
		  /* When the step does not divides the difference, there
		     are no overlaps.  */
		  else
		    {
		      *overlaps_a = chrec_bot;
		      *overlaps_b = chrec_bot;      
		      return;
		    }
		}
	      else
		{
		  /* Example:  
		     chrec_a = 3  
		     chrec_b = {4, +, 1}
		 
		     In this case, chrec_a will not overlap with chrec_b.  */
		  *overlaps_a = chrec_bot;
		  *overlaps_b = chrec_bot;
		  return;
		}
	    }
	}
    }
}

/* This is a part of the SIV subscript analyzer (Single Index
   Variable).  */

static void
analyze_siv_subscript_affine_cst (tree chrec_a, 
				  tree chrec_b,
				  tree *overlaps_a, 
				  tree *overlaps_b)
{
  analyze_siv_subscript_cst_affine (chrec_b, chrec_a, overlaps_b, overlaps_a);
}

/* Determines the overlapping elements due to accesses CHREC_A and
   CHREC_B, that are affine functions.  This is a part of the
   subscript analyzer.  */

static void
analyze_subscript_affine_affine (tree chrec_a, 
				 tree chrec_b,
				 tree *overlaps_a, 
				 tree *overlaps_b)
{
  tree left_a, left_b, right_a, right_b;
  
  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "(analyze_subscript_affine_affine \n");
  
  /* For determining the initial intersection, we have to solve a
     Diophantine equation.  This is the most time consuming part.
     
     For answering to the question: "Is there a dependence?" we have
     to prove that there exists a solution to the Diophantine
     equation, and that the solution is in the iteration domain,
     ie. the solution is positive or zero, and that the solution
     happens before the upper bound loop.nb_iterations.  Otherwise
     there is no dependence.  This function outputs a description of
     the iterations that hold the intersections.  */

  left_a = CHREC_LEFT (chrec_a);
  left_b = CHREC_LEFT (chrec_b);
  right_a = CHREC_RIGHT (chrec_a);
  right_b = CHREC_RIGHT (chrec_b);
  
  if (chrec_zerop (chrec_fold_minus (integer_type_node, left_a, left_b)))
    {
      /* The first element accessed twice is on the first
	 iteration.  */
      *overlaps_a = build_polynomial_chrec 
	(CHREC_VARIABLE (chrec_b), integer_zero_node, integer_one_node);
      *overlaps_b = build_polynomial_chrec 
	(CHREC_VARIABLE (chrec_a), integer_zero_node, integer_one_node);
    }
  
  else if (TREE_CODE (left_a) == INTEGER_CST
	   && TREE_CODE (left_b) == INTEGER_CST
	   && TREE_CODE (right_a) == INTEGER_CST 
	   && TREE_CODE (right_b) == INTEGER_CST
	   
	   /* Both functions should have the same evolution sign.  */
	   && ((tree_int_cst_sgn (right_a) > 0 
		&& tree_int_cst_sgn (right_b) > 0)
	       || (tree_int_cst_sgn (right_a) < 0
		   && tree_int_cst_sgn (right_b) < 0)))
    {
      /* Here we have to solve the Diophantine equation.  Reference
	 book: "Loop Transformations for Restructuring Compilers - The
	 Foundations" by Utpal Banerjee, pages 59-80.
	 
	 ALPHA * X0 = BETA * Y0 + GAMMA.  
	 
	 with:
	 ALPHA = RIGHT_A
	 BETA = RIGHT_B
	 GAMMA = LEFT_B - LEFT_A
	 CHREC_A = {LEFT_A, +, RIGHT_A}
	 CHREC_B = {LEFT_B, +, RIGHT_B}
	 
	 The Diophantine equation has a solution if and only if gcd
	 (ALPHA, BETA) divides GAMMA.  This is commonly known under
	 the name of the "gcd-test".
      */
      tree alpha, beta, gamma;
      tree la, lb;
      tree gcd_alpha_beta;
      tree u11, u12, u21, u22;

      /* Both alpha and beta have to be integer_type_node. The gcd
	 function does not work correctly otherwise.  */
      alpha = copy_node (right_a);
      beta = copy_node (right_b);
      la = copy_node (left_a);
      lb = copy_node (left_b);
      TREE_TYPE (alpha) = integer_type_node;
      TREE_TYPE (beta) = integer_type_node;
      TREE_TYPE (la) = integer_type_node;
      TREE_TYPE (lb) = integer_type_node;
      
      gamma = tree_fold_minus (integer_type_node, lb, la);
      
      /* FIXME: Use lambda_*_Hermite for implementing Bezout.  */
      gcd_alpha_beta = tree_fold_bezout 
	(alpha, 
	 tree_fold_multiply (integer_type_node, beta, integer_minus_one_node),
	 &u11, &u12, 
	 &u21, &u22);
      
      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "  (alpha = ");
	  print_generic_expr (dump_file, alpha, 0);
	  fprintf (dump_file, ")\n  (beta = ");
	  print_generic_expr (dump_file, beta, 0);
	  fprintf (dump_file, ")\n  (gamma = ");
	  print_generic_expr (dump_file, gamma, 0);
	  fprintf (dump_file, ")\n  (gcd_alpha_beta = ");
	  print_generic_expr (dump_file, gcd_alpha_beta, 0);
	  fprintf (dump_file, ")\n");
	}
      
      /* The classic "gcd-test".  */
      if (!tree_fold_divides_p (integer_type_node, gcd_alpha_beta, gamma))
	{
	  /* The "gcd-test" has determined that there is no integer
	     solution, ie. there is no dependence.  */
	  *overlaps_a = chrec_bot;
	  *overlaps_b = chrec_bot;
	}
      
      else
	{
	  /* The solutions are given by:
	     | 
	     | [GAMMA/GCD_ALPHA_BETA  t].[u11 u12]  = [X]
	     |                           [u21 u22]    [Y]
	     
	     For a given integer t.  Using the following variables,
	     
	     | i0 = u11 * gamma / gcd_alpha_beta
	     | j0 = u12 * gamma / gcd_alpha_beta
	     | i1 = u21
	     | j1 = u22
	     
	     the solutions are:
	     
	     | x = i0 + i1 * t, 
	     | y = j0 + j1 * t.  */
	  
	  tree i0, j0, i1, j1, t;
	  tree gamma_gcd;
	  
	  /* X0 and Y0 are the first iterations for which there is a
	     dependence.  X0, Y0 are two solutions of the Diophantine
	     equation: chrec_a (X0) = chrec_b (Y0).  */
	  tree x0, y0;
      
	  /* Exact div because in this case gcd_alpha_beta divides
	     gamma.  */
	  gamma_gcd = tree_fold_exact_div 
	    (integer_type_node, gamma, gcd_alpha_beta);
	  i0 = tree_fold_multiply (integer_type_node, u11, gamma_gcd);
	  j0 = tree_fold_multiply (integer_type_node, u12, gamma_gcd);
	  i1 = u21;
	  j1 = u22;
	  
	  if ((tree_int_cst_sgn (i1) == 0
	       && tree_int_cst_sgn (i0) < 0)
	      || (tree_int_cst_sgn (j1) == 0
		  && tree_int_cst_sgn (j0) < 0))
	    {
	      /* There is no solution.  
		 FIXME: The case "i0 > nb_iterations, j0 > nb_iterations" 
		 falls in here, but for the moment we don't look at the 
		 upper bound of the iteration domain.  */
	      *overlaps_a = chrec_bot;
	      *overlaps_b = chrec_bot;
  	    }
	  
	  else 
	    {
	      if (tree_int_cst_sgn (i1) > 0)
		{
		  t = tree_fold_ceil_div 
		    (integer_type_node, 
		     tree_fold_multiply (integer_type_node, i0, 
					 integer_minus_one_node), 
		     i1);
		  
		  if (tree_int_cst_sgn (j1) > 0)
		    {
		      t = tree_fold_max 
			(integer_type_node, t, 
			 tree_fold_ceil_div (integer_type_node, 
					     tree_fold_multiply 
					     (integer_type_node, j0, 
					      integer_minus_one_node), 
					     j1));
		      
		      x0 = tree_fold_plus 
			(integer_type_node, i0, 
			 tree_fold_multiply (integer_type_node, i1, t));
		      y0 = tree_fold_plus 
			(integer_type_node, j0, 
			 tree_fold_multiply (integer_type_node, j1, t));
		      
		      *overlaps_a = build_polynomial_chrec 
			(CHREC_VARIABLE (chrec_b), x0, u21);
		      *overlaps_b = build_polynomial_chrec 
			(CHREC_VARIABLE (chrec_a), y0, u22);
		    }
		  else
		    {
		      /* FIXME: For the moment, the upper bound of the
			 iteration domain for j is not checked. */
		      *overlaps_a = chrec_top;
		      *overlaps_b = chrec_top;
		    }
		}
	      
	      else
		{
		  /* FIXME: For the moment, the upper bound of the
		     iteration domain for i is not checked. */
		  *overlaps_a = chrec_top;
		  *overlaps_b = chrec_top;
		}
	    }
	}
    }
  
  else
    {
      /* For the moment, "don't know".  */
      *overlaps_a = chrec_top;
      *overlaps_b = chrec_top;
    }
  
  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "  (overlaps_a = ");
      print_generic_expr (dump_file, *overlaps_a, 0);
      fprintf (dump_file, ")\n  (overlaps_b = ");
      print_generic_expr (dump_file, *overlaps_b, 0);
      fprintf (dump_file, ")\n");
    }
  
  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, ")\n");
}

/* Analyze single index variable subscript.  Note that the dependence
   testing is not commutative, and that's why both versions of
   analyze_siv_subscript_x_y and analyze_siv_subscript_y_x are
   implemented.  */

static void
analyze_siv_subscript (tree chrec_a, 
		       tree chrec_b,
		       tree *overlaps_a, 
		       tree *overlaps_b)
{
  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "(analyze_siv_subscript \n");
  
  if (evolution_function_is_constant_p (chrec_a)
      && evolution_function_is_affine_p (chrec_b))
    analyze_siv_subscript_cst_affine (chrec_a, chrec_b, 
				      overlaps_a, overlaps_b);
  
  else if (evolution_function_is_affine_p (chrec_a)
	   && evolution_function_is_constant_p (chrec_b))
    analyze_siv_subscript_affine_cst (chrec_a, chrec_b, 
				      overlaps_a, overlaps_b);
  
  else if (evolution_function_is_affine_p (chrec_a)
	   && evolution_function_is_affine_p (chrec_b)
	   && (CHREC_VARIABLE (chrec_a) == CHREC_VARIABLE (chrec_b)))
    analyze_subscript_affine_affine (chrec_a, chrec_b, 
				     overlaps_a, overlaps_b);
  else
    {
      *overlaps_a = chrec_top;
      *overlaps_b = chrec_top;
    }
  
  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, ")\n");
}

/* Helper for determining whether the evolution steps of an affine
   CHREC divide the constant CST.  */

static bool
chrec_steps_divide_constant_p (tree chrec, 
			       tree cst)
{
  switch (TREE_CODE (chrec))
    {
    case POLYNOMIAL_CHREC:
      return (tree_fold_divides_p (integer_type_node, CHREC_RIGHT (chrec), cst)
	      && chrec_steps_divide_constant_p (CHREC_LEFT (chrec), cst));
      
    default:
      /* On the initial condition, return true.  */
      return true;
    }
}

/* This is the MIV subscript analyzer (Multiple Index Variable).  */

static void
analyze_miv_subscript (tree chrec_a, 
		       tree chrec_b, 
		       tree *overlaps_a, 
		       tree *overlaps_b)
{
  /* FIXME:  This is a MIV subscript, not yet handled.
     Example: (A[{1, +, 1}_1] vs. A[{1, +, 1}_2]) that comes from 
     (A[i] vs. A[j]).  
     
     In the SIV test we had to solve a Diophantine equation with two
     variables.  In the MIV case we have to solve a Diophantine
     equation with 2*n variables (if the subscript uses n IVs).
  */
  tree difference;
  
  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "(analyze_miv_subscript \n");
  
  difference = chrec_fold_minus (integer_type_node, chrec_a, chrec_b);
  
  if (chrec_zerop (difference))
    {
      /* Access functions are the same: all the elements are accessed
	 in the same order.  */
      *overlaps_a = integer_zero_node;
      *overlaps_b = integer_zero_node;
    }
  
  else if (evolution_function_is_constant_p (difference)
	   /* For the moment, the following is verified:
	      evolution_function_is_affine_multivariate_p (chrec_a) */
	   && !chrec_steps_divide_constant_p (chrec_a, difference))
    {
      /* testsuite/.../ssa-chrec-33.c
	 {{21, +, 2}_1, +, -2}_2  vs.  {{20, +, 2}_1, +, -2}_2 
        
	 The difference is 1, and the evolution steps are equal to 2,
	 consequently there are no overlapping elements.  */
      *overlaps_a = chrec_bot;
      *overlaps_b = chrec_bot;
    }
  
  else if (evolution_function_is_univariate_p (chrec_a)
	   && evolution_function_is_univariate_p (chrec_b))
    {
      /* testsuite/.../ssa-chrec-35.c
	 {0, +, 1}_2  vs.  {0, +, 1}_3
	 the overlapping elements are respectively located at iterations:
	 {0, +, 1}_3 and {0, +, 1}_2.
      */
      if (evolution_function_is_affine_p (chrec_a)
	  && evolution_function_is_affine_p (chrec_b))
	analyze_subscript_affine_affine (chrec_a, chrec_b, 
					 overlaps_a, overlaps_b);
      else
	{
	  *overlaps_a = chrec_top;
	  *overlaps_b = chrec_top;
	}
    }
  
  else
    {
      /* When the analysis is too difficult, answer "don't know".  */
      *overlaps_a = chrec_top;
      *overlaps_b = chrec_top;
    }
  
  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, ")\n");
}

/* Determines the iterations for which CHREC_A is equal to CHREC_B.
   OVERLAP_ITERATIONS_A and OVERLAP_ITERATIONS_B are two functions
   that describe the iterations that contain conflicting elements.
   
   Remark: For an integer k >= 0, the following equality is true:
   
   CHREC_A (OVERLAP_ITERATIONS_A (k)) == CHREC_B (OVERLAP_ITERATIONS_B (k)).
*/

static void 
analyze_overlapping_iterations (tree chrec_a, 
				tree chrec_b, 
				tree *overlap_iterations_a, 
				tree *overlap_iterations_b)
{
  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "(analyze_overlapping_iterations \n");
      fprintf (dump_file, "  (chrec_a = ");
      print_generic_expr (dump_file, chrec_a, 0);
      fprintf (dump_file, ")\n  chrec_b = ");
      print_generic_expr (dump_file, chrec_b, 0);
      fprintf (dump_file, ")\n");
    }
  
  if (chrec_a == NULL_TREE
      || chrec_b == NULL_TREE
      || chrec_contains_undetermined (chrec_a)
      || chrec_contains_undetermined (chrec_b)
      || chrec_contains_symbols (chrec_a)
      || chrec_contains_symbols (chrec_b)
      || chrec_contains_intervals (chrec_a)
      || chrec_contains_intervals (chrec_b))
    {
      *overlap_iterations_a = chrec_top;
      *overlap_iterations_b = chrec_top;
    }
  
  else if (ziv_subscript_p (chrec_a, chrec_b))
    analyze_ziv_subscript (chrec_a, chrec_b, 
			   overlap_iterations_a, overlap_iterations_b);
  
  else if (siv_subscript_p (chrec_a, chrec_b))
    analyze_siv_subscript (chrec_a, chrec_b, 
			   overlap_iterations_a, overlap_iterations_b);
  
  else
    analyze_miv_subscript (chrec_a, chrec_b, 
			   overlap_iterations_a, overlap_iterations_b);
  
  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "  (overlap_iterations_a = ");
      print_generic_expr (dump_file, *overlap_iterations_a, 0);
      fprintf (dump_file, ")\n  (overlap_iterations_b = ");
      print_generic_expr (dump_file, *overlap_iterations_b, 0);
      fprintf (dump_file, ")\n");
    }
}



/* This section contains the affine functions dependences detector.  */

/* This is the subscript dependence tester (SubDT).  It computes the
   conflicting iterations.  */

static void
subscript_dependence_tester (struct data_dependence_relation *ddr)
{
  unsigned int i;
  struct data_reference *dra = DDR_A (ddr);
  struct data_reference *drb = DDR_B (ddr);
  
  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "(subscript_dependence_tester \n");
  
  for (i = 0; i < DDR_NUM_SUBSCRIPTS (ddr); i++)
    {
      tree overlaps_a, overlaps_b;
      struct subscript *subscript = DDR_SUBSCRIPT (ddr, i);
      
      analyze_overlapping_iterations (DR_ACCESS_FN (dra, i), 
				      DR_ACCESS_FN (drb, i),
				      &overlaps_a, &overlaps_b);
      
      if (overlaps_a == chrec_top
 	  || overlaps_b == chrec_top)
 	{
 	  finalize_ddr_dependent (ddr, chrec_top);
	  break;
 	}
      
      else if (overlaps_a == chrec_bot
 	       || overlaps_b == chrec_bot)
 	{
 	  finalize_ddr_dependent (ddr, chrec_bot);
 	  break;
 	}
      
      else
 	{
 	  SUB_CONFLICTS_IN_A (subscript) = overlaps_a;
 	  SUB_CONFLICTS_IN_B (subscript) = overlaps_b;
 	}
    }
  
  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, ")\n");
}

/* Compute the classic per loop distance vector.  */

static void
build_classic_dist_vector (struct data_dependence_relation *res, 
			   varray_type *classic_dist, 
			   unsigned nb_loops)
{
  unsigned i;
  lambda_vector dist_v, init_v;
  
  dist_v = lambda_vector_new (nb_loops);
  init_v = lambda_vector_new (nb_loops);
  lambda_vector_clear (dist_v, nb_loops);
  lambda_vector_clear (init_v, nb_loops);
  
  if (DDR_ARE_DEPENDENT (res) != NULL_TREE)
    return;
  
  for (i = 0; i < DDR_NUM_SUBSCRIPTS (res); i++)
    {
      struct subscript *subscript = DDR_SUBSCRIPT (res, i);

      if (SUB_DISTANCE (subscript) == chrec_top)
	return;

      if (TREE_CODE (SUB_CONFLICTS_IN_A (subscript)) == POLYNOMIAL_CHREC)
	{
	  int dist;
	  unsigned loop_nb;
	  loop_nb = CHREC_VARIABLE (SUB_CONFLICTS_IN_A (subscript));
	  dist = int_cst_value (SUB_DISTANCE (subscript));

	  /* This is the subscript coupling test.  
	     | loop i = 0, N, 1
	     |   T[i+1][i] = ...
	     |   ... = T[i][i]
	     | endloop
	     There is no dependence.  */
	  if (init_v[loop_nb] != 0
	      && dist_v[loop_nb] != dist)
	    {
	      finalize_ddr_dependent (res, chrec_bot);
	      return;
	    }

	  dist_v[loop_nb] = dist;
	  init_v[loop_nb] = 1;
	}
    }
  
  /* There is a distance of 1 on all the outer loops: 
     
     Example: there is a dependence of distance 1 on loop_1 for the array A.
     | loop_1
     |   A[5] = ...
     | endloop
  */
  {
    struct loop *lca, *loop_a, *loop_b;
    struct data_reference *a = DDR_A (res);
    struct data_reference *b = DDR_B (res);
    
    loop_a = loop_of_stmt (DR_STMT (a));
    loop_b = loop_of_stmt (DR_STMT (b));
    
    /* Get the common ancestor loop.  */
    lca = find_common_loop (loop_a, loop_b); 
    
    /* For each outer_loop where init_v is not set, the accesses are
       in dependence of distance 1 in the loop.  */
    if (lca != loop_a
	&& lca != loop_b
	&& init_v[loop_num (lca)] == 0)
      dist_v[loop_num (lca)] = 1;
    
    lca = outer_loop (lca);
    if (lca)
      while (loop_depth (lca) != 0)
	{
	  if (init_v[loop_num (lca)] == 0)
	    dist_v[loop_num (lca)] = 1;
	  lca = outer_loop (lca);
	}
  }
  
  VARRAY_PUSH_GENERIC_PTR (*classic_dist, dist_v);
}

/* Compute the classic per loop direction vector.  */

static void
build_classic_dir_vector (struct data_dependence_relation *res, 
			  varray_type *classic_dir, 
			  unsigned nb_loops)
{
  unsigned i;
  lambda_vector dir_v, init_v;
  
  dir_v = lambda_vector_new (nb_loops);
  init_v = lambda_vector_new (nb_loops);
  lambda_vector_clear (dir_v, nb_loops);
  lambda_vector_clear (init_v, nb_loops);
  
  if (DDR_ARE_DEPENDENT (res) != NULL_TREE)
    return;
  
  for (i = 0; i < DDR_NUM_SUBSCRIPTS (res); i++)
    {
      struct subscript *subscript = DDR_SUBSCRIPT (res, i);

      if (TREE_CODE (SUB_CONFLICTS_IN_A (subscript)) == POLYNOMIAL_CHREC
	  && TREE_CODE (SUB_CONFLICTS_IN_B (subscript)) == POLYNOMIAL_CHREC)
	{
	  unsigned loop_nb = CHREC_VARIABLE (SUB_CONFLICTS_IN_A (subscript));
	  enum data_dependence_direction dir = dir_star;
	  
	  if (SUB_DISTANCE (subscript) == chrec_top)
	    {
	      
	    }
	  else
	    {
	      int dist = int_cst_value (SUB_DISTANCE (subscript));
	      
	      if (dist == 0)
		dir = dir_equal;
	      else if (dist > 0)
		dir = dir_positive;
	      else if (dist < 0)
		dir = dir_negative;
	    }
	  
	  /* This is the subscript coupling test.  
	     | loop i = 0, N, 1
	     |   T[i+1][i] = ...
	     |   ... = T[i][i]
	     | endloop
	     There is no dependence.  */
	  if (init_v[loop_nb] != 0
	      && dir != dir_star
	      && (enum data_dependence_direction) dir_v[loop_nb] != dir
	      && (enum data_dependence_direction) dir_v[loop_nb] != dir_star)
	    {
	      finalize_ddr_dependent (res, chrec_bot);
	      return;
	    }
	  
	  dir_v[loop_nb] = dir;
	  init_v[loop_nb] = 1;
	}
    }
  
  /* There is a distance of 1 on all the outer loops: 
     
     Example: there is a dependence of distance 1 on loop_1 for the array A.
     | loop_1
     |   A[5] = ...
     | endloop
  */
  {
    struct loop *lca, *loop_a, *loop_b;
    struct data_reference *a = DDR_A (res);
    struct data_reference *b = DDR_B (res);
    
    loop_a = loop_of_stmt (DR_STMT (a));
    loop_b = loop_of_stmt (DR_STMT (b));
    
    /* Get the common ancestor loop.  */
    lca = find_common_loop (loop_a, loop_b); 
    
    /* For each outer_loop where init_v is not set, the accesses are
       in dependence of distance 1 in the loop.  */
    if (lca != loop_a
	&& lca != loop_b
	&& init_v[loop_num (lca)] == 0)
      dir_v[loop_num (lca)] = dir_positive;
    
    lca = outer_loop (lca);
    if (lca)
      while (loop_depth (lca) != 0)
	{
	  if (init_v[loop_num (lca)] == 0)
	    dir_v[loop_num (lca)] = dir_positive;
	  lca = outer_loop (lca);
	}
  }
  
  VARRAY_PUSH_GENERIC_PTR (*classic_dir, dir_v);
}

/* Determine whether the access functions are affine functions, in
   which case the dependence tester can be runned.  */

static bool 
access_functions_are_affine_or_constant_p (struct data_reference *a)
{
  unsigned int i;
  varray_type fns = DR_ACCESS_FNS (a);
  
  for (i = 0; i < VARRAY_ACTIVE_SIZE (fns); i++)
    if (!evolution_function_is_constant_p (VARRAY_TREE (fns, i))
	&& !evolution_function_is_affine_multivariate_p (VARRAY_TREE (fns, i)))
      return false;
  
  return true;
}

/* This computes the affine dependence relation between A and B.
   CHREC_BOT is used for representing the independence between two
   accesses, while CHREC_TOP is used for representing the unknown
   relation.
   
   Note that it is possible to stop the computation of the dependence
   relation the first time we detect a CHREC_BOT element for a given
   subscript.  */

static void
compute_affine_dependence (struct data_dependence_relation *ddr)
{
  struct data_reference *dra = DDR_A (ddr);
  struct data_reference *drb = DDR_B (ddr);
  
  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "(compute_affine_dependence (%d, %d)\n", 
	       DR_ID (dra), DR_ID (drb));
      fprintf (dump_file, "  (stmt_a = \n");
      print_generic_expr (dump_file, DR_STMT (dra), 0);
      fprintf (dump_file, ")\n  (stmt_b = \n");
      print_generic_expr (dump_file, DR_STMT (drb), 0);
      fprintf (dump_file, ")\n");
    }
  
  /* Analyze only when the dependence relation is not yet known.  */
  if (DDR_ARE_DEPENDENT (ddr) == NULL_TREE)
    {
      if (access_functions_are_affine_or_constant_p (dra)
	  && access_functions_are_affine_or_constant_p (drb))
	subscript_dependence_tester (ddr);
      
      /* As a last case, if the dependence cannot be determined, or if
	 the dependence is considered too difficult to determine, answer
	 "don't know".  */
      else
	finalize_ddr_dependent (ddr, chrec_top);
    }
  
  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, ")\n");
}

/* Compute a subset of the data dependence relation graph.  Don't
   compute read-read relations, and avoid the computation of the
   opposite relation, ie. when AB has been computed, don't compute
   BA.  */

static void 
compute_rw_wr_ww_dependences (varray_type datarefs, 
			      varray_type *dependence_relations)
{
  unsigned int i, j, N;

  N = VARRAY_ACTIVE_SIZE (datarefs);

  for (i = 0; i < N; i++)
    for (j = i; j < N; j++)
      {
	struct data_reference *a, *b;
	struct data_dependence_relation *ddr;

	a = VARRAY_GENERIC_PTR (datarefs, i);
	b = VARRAY_GENERIC_PTR (datarefs, j);

	/* Don't compute the "read-read" relations.  */
	if (DR_IS_READ (a) && DR_IS_READ (b))
	  continue;

	ddr = initialize_data_dependence_relation (a, b);

	VARRAY_PUSH_GENERIC_PTR (*dependence_relations, ddr);
	compute_affine_dependence (ddr);
	compute_distance_vector (ddr);
      }
}

/* Search the data references in LOOP, and record the information into
   DATAREFS.
   
   FIXME: This is a "dumb" walker over all the trees in the loop body.
   Find another technique that avoids this costly walk.  This is
   acceptable for the moment, since this function is used only for
   debugging purposes.  */

static void 
find_data_references_in_loop (struct loop *loop, varray_type *datarefs)
{
  basic_block bb;
  block_stmt_iterator bsi;
  
  FOR_EACH_BB (bb)
    {
      if (!flow_bb_inside_loop_p (loop, bb))
	continue;
      
      for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
        {
	  tree stmt = bsi_stmt (bsi);
	  vdef_optype vdefs = VDEF_OPS (stmt_ann (stmt));
	  vuse_optype vuses = VUSE_OPS (stmt_ann (stmt));
	  
	  if (vuses || vdefs)
	    switch (TREE_CODE (stmt))
	      {
	      case MODIFY_EXPR:
		/* In the GIMPLE representation, a modify expression
		   contains a single load or store to memory.  */
		if (TREE_CODE (TREE_OPERAND (stmt, 0)) == ARRAY_REF)
		  VARRAY_PUSH_GENERIC_PTR 
		    (*datarefs, analyze_array (stmt, TREE_OPERAND (stmt, 0), 
					       false));
		
		else if (TREE_CODE (TREE_OPERAND (stmt, 1)) == ARRAY_REF)
		  VARRAY_PUSH_GENERIC_PTR 
		    (*datarefs, analyze_array (stmt, TREE_OPERAND (stmt, 1), 
					       true));

		else
		  VARRAY_PUSH_GENERIC_PTR (*datarefs, 
					   analyze_array_top (stmt));
		  
		break;
		
	      case COND_EXPR:
	      case CALL_EXPR:
	      case VA_ARG_EXPR:
	      case ASM_EXPR:
	      case RETURN_EXPR:
		/* In the GIMPLE representation, these nodes do not
		   contain ARRAY_REFs in their operands.  */
		break;
		
	      default:
		break;
	      }
	}
    }
}



/* This section contains all the entry points.  */

/* Entry point.  "Give me a loop nest, I will return you a set of
   distance/direction vectors."  */

void
compute_data_dependences_for_loop (unsigned nb_loops, 
				   struct loop *loop,
				   varray_type *datarefs,
				   varray_type *dependence_relations,
				   varray_type *classic_dist, 
				   varray_type *classic_dir)
{
  unsigned int i;

  find_data_references_in_loop (loop, datarefs);
  compute_rw_wr_ww_dependences (*datarefs, dependence_relations);

  for (i = 0; i < VARRAY_ACTIVE_SIZE (*dependence_relations); i++)
    {
      struct data_dependence_relation *ddr;
      ddr = VARRAY_GENERIC_PTR (*dependence_relations, i);
      build_classic_dist_vector (ddr, classic_dist, nb_loops);
      build_classic_dir_vector (ddr, classic_dir, nb_loops);
    }
}

/* Entry point (for testing only).  Analyze all the data references
   and the dependence relations.

   The data references are computed first.  
   
   A relation on these nodes is represented by a complete graph.  Some
   of the relations could be of no interest, thus the relations can be
   computed on demand.
   
   In the following function we compute all the relations.  This is
   just a first implementation that is here for:
   - for showing how to ask for the dependence relations, 
   - for the debugging the whole dependence graph,
   - for the dejagnu testcases and maintenance.
   
   It is possible to ask only for a part of the graph, avoiding to
   compute the whole dependence graph.  The computed dependences are
   stored in a knowledge base (KB) such that later queries don't
   recompute the same information.  The implementation of this KB is
   transparent to the optimizer, and thus the KB can be changed with a
   more efficient implementation, or the KB could be disabled.  */

void 
analyze_all_data_dependences (struct loops *loops)
{
  unsigned int i;
  varray_type datarefs;
  varray_type dependence_relations;
  varray_type classic_dist, classic_dir;
  int nb_data_refs = 10;

#if 0
  dump_file = stderr;
  dump_flags = 31;
#endif

  VARRAY_GENERIC_PTR_INIT (classic_dist, 10, "classic_dist");
  VARRAY_GENERIC_PTR_INIT (classic_dir, 10, "classic_dir");
  VARRAY_GENERIC_PTR_INIT (datarefs, nb_data_refs, "datarefs");
  VARRAY_GENERIC_PTR_INIT (dependence_relations, 
			   nb_data_refs * nb_data_refs,
			   "dependence_relations");

  /* Compute DDs on the whole function.  */
  compute_data_dependences_for_loop (loops->num, loop_from_num (loops, 0), 
				     &datarefs, &dependence_relations, 
				     &classic_dist, &classic_dir);

  if (dump_file)
    {
      dump_data_dependence_relations (dump_file, dependence_relations);
      fprintf (dump_file, "\n\n");
    }

  /* Don't dump distances in order to avoid to update the
     testsuite.  */
  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      for (i = 0; i < VARRAY_ACTIVE_SIZE (classic_dist); i++)
	{
	  fprintf (dump_file, "DISTANCE_V (");
	  print_lambda_vector (dump_file, 
			       VARRAY_GENERIC_PTR (classic_dist, i),
			       loops->num);
	  fprintf (dump_file, ")\n");
	}
      for (i = 0; i < VARRAY_ACTIVE_SIZE (classic_dir); i++)
	{
	  fprintf (dump_file, "DIRECTION_V (");
	  print_lambda_vector (dump_file, 
			       VARRAY_GENERIC_PTR (classic_dir, i),
			       loops->num);
	  fprintf (dump_file, ")\n");
	}
      fprintf (dump_file, "\n\n");
    }

  if (dump_file && (dump_flags & TDF_STATS))
    {
      unsigned nb_top_relations = 0;
      unsigned nb_bot_relations = 0;
      unsigned nb_basename_differ = 0;
      unsigned nb_chrec_relations = 0;

      for (i = 0; i < VARRAY_ACTIVE_SIZE (dependence_relations); i++)
	{
	  struct data_dependence_relation *ddr;
	  ddr = VARRAY_GENERIC_PTR (dependence_relations, i);
	  
	  if (DDR_ARE_DEPENDENT (ddr) == chrec_top)
	    nb_top_relations++;
	  
	  else if (DDR_ARE_DEPENDENT (ddr) == chrec_bot)
	    {
	      struct data_reference *a = DDR_A (ddr);
	      struct data_reference *b = DDR_B (ddr);
	      
	      if (DR_NUM_DIMENSIONS (a) != DR_NUM_DIMENSIONS (b)
		  || array_base_name_differ_p (a, b))
		nb_basename_differ++;
	      else
		nb_bot_relations++;
	    }
	  
	  else 
	    nb_chrec_relations++;
	}
      
      fprintf (dump_file, "\n(\n");
      fprintf (dump_file, "%d\tnb_top_relations\n", nb_top_relations);
      fprintf (dump_file, "%d\tnb_bot_relations\n", nb_bot_relations);
      fprintf (dump_file, "%d\tnb_basename_differ\n", nb_basename_differ);
      fprintf (dump_file, "%d\tnb_distance_relations\n", (int) VARRAY_ACTIVE_SIZE (classic_dist));
      fprintf (dump_file, "%d\tnb_chrec_relations\n", nb_chrec_relations);
      fprintf (dump_file, "\n)\n");
      
      gather_stats_on_scev_database ();
    }
  
  varray_clear (dependence_relations);
  varray_clear (datarefs);
  varray_clear (classic_dist);
  varray_clear (classic_dir);
}


