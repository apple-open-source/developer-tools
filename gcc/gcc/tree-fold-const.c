/* Fold GENERIC expressions.
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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "errors.h"
#include "ggc.h"
#include "tree.h"
#include "tree-fold-const.h"



/* Least common multiple.  */

tree 
tree_fold_lcm (tree a, 
	       tree b)
{
  tree pgcd;
  
#if defined ENABLE_CHECKING
  if (TREE_CODE (a) != INTEGER_CST
      || TREE_CODE (b) != INTEGER_CST)
    abort ();
#endif
  
  if (integer_onep (a)) 
    return b;
  
  if (integer_onep (b)) 
    return a;
  
  if (integer_zerop (a)
      || integer_zerop (b)) 
    return integer_zero_node;
  
  pgcd = tree_fold_gcd (a, b);
  
  if (integer_onep (pgcd))
    return tree_fold_multiply (integer_type_node, a, b);
  else
    return tree_fold_multiply 
      (integer_type_node, pgcd, 
       tree_fold_lcm (tree_fold_exact_div (integer_type_node, a, pgcd), 
		      tree_fold_exact_div (integer_type_node, b, pgcd)));
}

/* Greatest common divisor.  */

tree 
tree_fold_gcd (tree a, 
	       tree b)
{
  tree a_mod_b;
  tree type = TREE_TYPE (a);
  
#if defined ENABLE_CHECKING
  if (TREE_CODE (a) != INTEGER_CST
      || TREE_CODE (b) != INTEGER_CST)
    abort ();
#endif
  
  if (integer_zerop (a)) 
    return b;
  
  if (integer_zerop (b)) 
    return a;
  
  if (tree_int_cst_sgn (a) == -1)
    a = tree_fold_multiply (type, a,
			    convert (type, integer_minus_one_node));
  
  if (tree_int_cst_sgn (b) == -1)
    b = tree_fold_multiply (type, b,
			    convert (type, integer_minus_one_node));
 
  while (1)
    {
      a_mod_b = fold (build (CEIL_MOD_EXPR, type, a, b));
 
      if (!TREE_INT_CST_LOW (a_mod_b)
	  && !TREE_INT_CST_HIGH (a_mod_b))
	return b;

      a = b;
      b = a_mod_b;
    }
}

/* Bezout: Let a1 and a2 be two integers; there exist two integers u11
   and u12 such that, 
   
   |  u11 * a1 + u12 * a2 = gcd (a1, a2).
   
   This function computes the greatest common divisor using the
   Blankinship algorithm.  The gcd is returned, and the coefficients
   of the unimodular matrix U are (u11, u12, u21, u22) such that, 

   |  U.A = S
   
   |  (u11 u12) (a1) = (gcd)
   |  (u21 u22) (a2)   (0)
   
   FIXME: Use lambda_..._hermite for implementing this function.
*/

tree 
tree_fold_bezout (tree a1, 
		  tree a2,
		  tree *u11, tree *u12,
		  tree *u21, tree *u22)
{
  tree s1, s2;
  
  /* Initialize S with the coefficients of A.  */
  s1 = a1;
  s2 = a2;
  
  /* Initialize the U matrix */
  *u11 = integer_one_node; 
  *u12 = integer_zero_node;
  *u21 = integer_zero_node;
  *u22 = integer_one_node;
  
  if (integer_zerop (a1)
      || integer_zerop (a2))
    return integer_zero_node;
  
  while (!integer_zerop (s2))
    {
      int sign;
      tree z, zu21, zu22, zs2;
      
      sign = tree_int_cst_sgn (s1) * tree_int_cst_sgn (s2);
      z = tree_fold_floor_div (integer_type_node, 
			       tree_fold_abs (integer_type_node, s1), 
			       tree_fold_abs (integer_type_node, s2));
      zu21 = tree_fold_multiply (integer_type_node, z, *u21);
      zu22 = tree_fold_multiply (integer_type_node, z, *u22);
      zs2 = tree_fold_multiply (integer_type_node, z, s2);
      
      /* row1 -= z * row2.  */
      if (sign < 0)
	{
	  *u11 = tree_fold_plus (integer_type_node, *u11, zu21);
	  *u12 = tree_fold_plus (integer_type_node, *u12, zu22);
	  s1 = tree_fold_plus (integer_type_node, s1, zs2);
	}
      else if (sign > 0)
	{
	  *u11 = tree_fold_minus (integer_type_node, *u11, zu21);
	  *u12 = tree_fold_minus (integer_type_node, *u12, zu22);
	  s1 = tree_fold_minus (integer_type_node, s1, zs2);
	}
      else
	/* Should not happen.  */
	abort ();
      
      /* Interchange row1 and row2.  */
      {
	tree flip;
	
	flip = *u11;
	*u11 = *u21;
	*u21 = flip;

	flip = *u12;
	*u12 = *u22;
	*u22 = flip;
	
	flip = s1;
	s1 = s2;
	s2 = flip;
      }
    }
  
  if (tree_int_cst_sgn (s1) < 0)
    {
      *u11 = tree_fold_multiply (integer_type_node, *u11, 
				 integer_minus_one_node);
      *u12 = tree_fold_multiply (integer_type_node, *u12, 
				 integer_minus_one_node);
      s1 = tree_fold_multiply (integer_type_node, s1, integer_minus_one_node);
    }
  
  return s1;
}

/* The factorial.  */

tree 
tree_fold_factorial (tree f)
{
  if (tree_int_cst_sgn (f) <= 0)
    return integer_one_node;
  else
    return tree_fold_multiply 
      (integer_type_node, f, 
       tree_fold_factorial (tree_fold_minus 
			    (integer_type_node, f, integer_one_node)));
}

