/* Fold GENERIC expressions.
   Copyright (C) 2003, 2004 Free Software Foundation, Inc.
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


/* This file defines an interface of the tree folder.  
   For the moment, the functions in this file are just wrappers around 
   the "big-ugly" fold function.  The final aim is to completely split
   up the fold function into small pieces in such a way that client 
   passes don't see the changes to the underlying implementation.  */

#ifndef GCC_TREE_FOLD_H
#define GCC_TREE_FOLD_H



/* Interface for integer operations folding.  */
extern tree tree_fold_lcm (tree, tree);
extern tree tree_fold_gcd (tree, tree);
extern tree tree_fold_bezout (tree, tree, tree *, tree *, tree *, tree *);
extern tree tree_fold_factorial (tree);



/* Fold the addition.  */

static inline tree 
tree_fold_plus (tree type, 
		tree a,
		tree b)
{
  tree res = fold (build (PLUS_EXPR, type, a, b));
  
  /* Strip away the NON_LVALUE_EXPR: fixes bootstrap on Darwin.  */
  if (TREE_CODE (res) == NON_LVALUE_EXPR)
    return TREE_OPERAND (res, 0);
  
  else
    return res;
}

/* Fold the substraction.  */

static inline tree 
tree_fold_minus (tree type, 
		 tree a,
		 tree b)
{
  tree res = fold (build (MINUS_EXPR, type, a, b));
  
  /* Strip away the NON_LVALUE_EXPR: fixes bootstrap on Darwin.  */
  if (TREE_CODE (res) == NON_LVALUE_EXPR)
    return TREE_OPERAND (res, 0);
  
  else 
    return res;
}

/* Fold the multiplication.  */

static inline tree 
tree_fold_multiply (tree type, 
		    tree a,
		    tree b)
{
  tree res = fold (build (MULT_EXPR, type, a, b));
  
  /* Strip away the NON_LVALUE_EXPR: fixes bootstrap on Darwin.  */
  if (TREE_CODE (res) == NON_LVALUE_EXPR)
    return TREE_OPERAND (res, 0);
  
  else
    return res;
}

/* Division for integer result that rounds the quotient toward zero.  */

static inline tree 
tree_fold_trunc_div (tree type, 
		     tree a,
		     tree b)
{
  return fold (build (TRUNC_DIV_EXPR, type, a, b));
}

/* Division for integer result that rounds the quotient toward infinity.  */

static inline tree 
tree_fold_ceil_div (tree type, 
		    tree a,
		    tree b)
{
  return fold (build (CEIL_DIV_EXPR, type, a, b));
}

/* Division for integer result that rounds toward minus infinity.  */

static inline tree 
tree_fold_floor_div (tree type, 
		     tree a,
		     tree b)
{
  return fold (build (FLOOR_DIV_EXPR, type, a, b));
}

/* Division which is not supposed to need rounding.  */

static inline tree 
tree_fold_exact_div (tree type, 
		     tree a,
		     tree b)
{
  return fold (build (EXACT_DIV_EXPR, type, a, b));
}

/* Computes the minimum.  */

static inline tree 
tree_fold_min (tree type, 
	       tree a,
	       tree b)
{
  return fold (build (MIN_EXPR, type, a, b));
}

/* Computes the maximum.  */

static inline tree 
tree_fold_max (tree type, 
	       tree a,
	       tree b)
{
  return fold (build (MAX_EXPR, type, a, b));
}

/* Computes the absolute value.  */

static inline tree 
tree_fold_abs (tree type, 
	       tree a)
{
  return fold (build1 (ABS_EXPR, type, a));
}

/* The binomial coefficient.  */

static inline tree 
tree_fold_binomial (tree n,
		    tree k)
{
  return tree_fold_exact_div 
    (integer_type_node, tree_fold_factorial (n), 
     tree_fold_multiply 
     (integer_type_node, tree_fold_factorial (k),
      tree_fold_factorial 
      (tree_fold_minus (integer_type_node, n, k))));
}

/* Determines whether (a divides b), or (a == gcd (a, b)).  */

static inline bool 
tree_fold_divides_p (tree type, 
		     tree a, 
		     tree b)
{
  if (integer_onep (a))
    return true;
  
  return integer_zerop 
    (tree_fold_minus 
     (type, a, tree_fold_gcd (a, b)));
}

/* Given two integer constants A and B, determine whether "A >= B".  */

static inline bool
tree_is_ge (tree a, tree b, bool *res)
{
  tree cmp = fold (build (GE_EXPR, boolean_type_node, a, b));
  if (TREE_CODE (cmp) != INTEGER_CST)
    return false;

  *res = (tree_int_cst_sgn (cmp) != 0);
  return true;
}

/* Given two integer constants A and B, determine whether "A > B".  */

static inline bool
tree_is_gt (tree a, tree b, bool *res)
{
  tree cmp = fold (build (GT_EXPR, boolean_type_node, a, b));
  if (TREE_CODE (cmp) != INTEGER_CST)
    return false;

  *res = (tree_int_cst_sgn (cmp) != 0);
  return true;
}

/* Given two integer constants A and B, determine whether "A <= B".  */

static inline bool
tree_is_le (tree a, tree b, bool *res)
{
  tree cmp = fold (build (LE_EXPR, boolean_type_node, a, b));
  if (TREE_CODE (cmp) != INTEGER_CST)
    return false;

  *res = (tree_int_cst_sgn (cmp) != 0);
  return true;
}

/* Given two integer constants A and B, determine whether "A < B".  */

static inline bool
tree_is_lt (tree a, tree b, bool *res)
{
  tree cmp = fold (build (LT_EXPR, boolean_type_node, a, b));
  if (TREE_CODE (cmp) != INTEGER_CST)
    return false;

  *res = (tree_int_cst_sgn (cmp) != 0);
  return true;
}

/* Given two integer constants A and B, determine whether "A == B".  */

static inline bool
tree_is_eq (tree a, tree b, bool *res)
{
  tree cmp = fold (build (EQ_EXPR, boolean_type_node, a, b));
  if (TREE_CODE (cmp) != INTEGER_CST)
    return false;

  *res = (tree_int_cst_sgn (cmp) != 0);
  return true;
}

/* Given two integer constants A and B, determine whether "A != B".  */

static inline bool
tree_is_ne (tree a, tree b, bool *res)
{
  tree cmp = fold (build (NE_EXPR, boolean_type_node, a, b));
  if (TREE_CODE (cmp) != INTEGER_CST)
    return false;

  *res = (tree_int_cst_sgn (cmp) != 0);
  return true;
}

#endif  /* GCC_TREE_FOLD_H  */
