/* Chains of recurrences.
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

/* This file implements operations on chains of recurrences.  Chains
   of recurrences are used for modeling evolution functions of scalar
   variables.
*/

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "errors.h"
#include "ggc.h"
#include "tree.h"
#include "diagnostic.h"
#include "varray.h"
#include "tree-fold-const.h"
#include "tree-chrec.h"
#include "tree-pass.h"

/* Extended folder for chrecs.  */

/* Determines whether CST is not a constant evolution.  */

static inline bool
is_not_constant_evolution (tree cst)
{
  return (TREE_CODE (cst) == POLYNOMIAL_CHREC
	  || TREE_CODE (cst) == EXPONENTIAL_CHREC
	  || TREE_CODE (cst) == PEELED_CHREC);
}

/* Fold CODE for a polynomial function and a constant.  */

static inline tree 
chrec_fold_poly_cst (enum tree_code code, 
		     tree type, 
		     tree poly, 
		     tree cst)
{
#if defined ENABLE_CHECKING
  if (poly == NULL_TREE
      || cst == NULL_TREE
      || TREE_CODE (poly) != POLYNOMIAL_CHREC
      || is_not_constant_evolution (cst))
    abort ();
#endif
  
  switch (code)
    {
    case PLUS_EXPR:
      return build_polynomial_chrec 
	(CHREC_VARIABLE (poly), 
	 chrec_fold_plus (type, CHREC_LEFT (poly), cst),
	 CHREC_RIGHT (poly));
      
    case MINUS_EXPR:
      return build_polynomial_chrec 
	(CHREC_VARIABLE (poly), 
	 chrec_fold_minus (type, CHREC_LEFT (poly), cst),
	 CHREC_RIGHT (poly));
      
    case MULT_EXPR:
      return build_polynomial_chrec 
	(CHREC_VARIABLE (poly), 
	 chrec_fold_multiply (type, CHREC_LEFT (poly), cst),
	 chrec_fold_multiply (type, CHREC_RIGHT (poly), cst));
      
    default:
      return chrec_top;
    }
}

/* Fold the addition of a peeled chrec and a constant.  */

static inline tree 
chrec_fold_plus_peel_cst (tree type, 
			  tree peel, 
			  tree cst)
{
#if defined ENABLE_CHECKING
  if (peel == NULL_TREE
      || cst == NULL_TREE
      || TREE_CODE (peel) != PEELED_CHREC
      || is_not_constant_evolution (cst))
    abort ();
#endif
  
  return build_peeled_chrec 
    (CHREC_VARIABLE (peel),
     chrec_fold_plus (type, CHREC_LEFT (peel), cst),
     chrec_fold_plus (type, CHREC_RIGHT (peel), cst));
}

/* Fold the addition of an exponential function and a constant.  */

static inline tree 
chrec_fold_plus_expo_cst (enum tree_code code, 
			  tree type, 
			  tree expo, 
			  tree cst)
{
#if defined ENABLE_CHECKING
  if (expo == NULL_TREE
      || cst == NULL_TREE
      || TREE_CODE (expo) != EXPONENTIAL_CHREC
      || is_not_constant_evolution (cst))
    abort ();
#endif
  
  /* For the moment, we don't know how to fold this further.  */
  return build (code, type, expo, cst);
}

/* Fold the addition of an exponential function and a constant.  */

static inline tree 
chrec_fold_plus_cst_expo (enum tree_code code, 
			  tree type, 
			  tree cst, 
			  tree expo)
{
#if defined ENABLE_CHECKING
  if (expo == NULL_TREE
      || cst == NULL_TREE
      || TREE_CODE (expo) != EXPONENTIAL_CHREC
      || is_not_constant_evolution (cst))
    abort ();
#endif
  
  /* For the moment, we don't know how to fold this further.  */
  return build (code, type, cst, expo);
}

/* Fold the addition of two polynomial functions.  */

static inline tree 
chrec_fold_plus_poly_poly (enum tree_code code, 
			   tree type, 
			   tree poly0, 
			   tree poly1)
{
  tree left, right;
  
#if defined ENABLE_CHECKING
  if (poly0 == NULL_TREE
      || poly1 == NULL_TREE
      || TREE_CODE (poly0) != POLYNOMIAL_CHREC
      || TREE_CODE (poly1) != POLYNOMIAL_CHREC)
    abort ();
#endif
  
  /*
    {a, +, b}_1 + {c, +, d}_2  ->  {{a, +, b}_1 + c, +, d}_2,
    {a, +, b}_2 + {c, +, d}_1  ->  {{c, +, d}_1 + a, +, b}_2,
    {a, +, b}_x + {c, +, d}_x  ->  {a+c, +, b+d}_x.  */
  if (CHREC_VARIABLE (poly0) < CHREC_VARIABLE (poly1))
    {
      if (code == PLUS_EXPR)
	return build_polynomial_chrec 
	  (CHREC_VARIABLE (poly1), 
	   chrec_fold_plus (type, poly0, CHREC_LEFT (poly1)),
	   CHREC_RIGHT (poly1));
      else
	return build_polynomial_chrec 
	  (CHREC_VARIABLE (poly1), 
	   chrec_fold_minus (type, poly0, CHREC_LEFT (poly1)),
	   chrec_fold_multiply (type, CHREC_RIGHT (poly1), 
				convert (type, integer_minus_one_node)));
    }
  
  if (CHREC_VARIABLE (poly0) > CHREC_VARIABLE (poly1))
    {
      if (code == PLUS_EXPR)
	return build_polynomial_chrec 
	  (CHREC_VARIABLE (poly0), 
	   chrec_fold_plus (type, CHREC_LEFT (poly0), poly1),
	   CHREC_RIGHT (poly0));
      else
	return build_polynomial_chrec 
	  (CHREC_VARIABLE (poly0), 
	   chrec_fold_minus (type, CHREC_LEFT (poly0), poly1),
	   CHREC_RIGHT (poly0));
    }
  
  if (code == PLUS_EXPR)
    {
      left = chrec_fold_plus 
	(type, CHREC_LEFT (poly0), CHREC_LEFT (poly1));
      right = chrec_fold_plus 
	(type, CHREC_RIGHT (poly0), CHREC_RIGHT (poly1));
    }
  else
    {
      left = chrec_fold_minus 
	(type, CHREC_LEFT (poly0), CHREC_LEFT (poly1));
      right = chrec_fold_minus 
	(type, CHREC_RIGHT (poly0), CHREC_RIGHT (poly1));
    }

  if (chrec_zerop (right))
    return left;
  else
    return build_polynomial_chrec 
      (CHREC_VARIABLE (poly0), left, right); 
}

/* Fold the addition of a polynomial and a peeled chrec.  */

static inline tree 
chrec_fold_plus_poly_peel (enum tree_code code, 
			   tree type, 
			   tree poly, 
			   tree peel)
{
#if defined ENABLE_CHECKING
  if (peel == NULL_TREE
      || poly == NULL_TREE
      || TREE_CODE (peel) != PEELED_CHREC
      || TREE_CODE (poly) != POLYNOMIAL_CHREC)
    abort ();
#endif
  
  /*
    {a, +, b}_1 + (c, d)_2  ->  ({a, +, b}_1 + c, {a, +, b}_1 + d)_2,
    {a, +, b}_2 + (c, d)_1  ->  {a + (c, d)_1, +, b}_2,
    {a, +, b}_x + (c, d)_x  ->  ({a, +, b}_x + c, {a, +, b}_x + d)_x.  */
  if (CHREC_VARIABLE (poly) < CHREC_VARIABLE (peel))
    return build_peeled_chrec 
      (CHREC_VARIABLE (peel), 
       (code == PLUS_EXPR ? 
	chrec_fold_plus (type, poly, CHREC_LEFT (peel)) : 
	chrec_fold_minus (type, poly, CHREC_LEFT (peel))),
       (code == PLUS_EXPR ? 
	chrec_fold_plus (type, poly, CHREC_RIGHT (peel)) :
	chrec_fold_minus (type, poly, CHREC_RIGHT (peel))));
  
  if (CHREC_VARIABLE (poly) > CHREC_VARIABLE (peel))
    return build_polynomial_chrec 
      (CHREC_VARIABLE (poly), 
       (code == PLUS_EXPR ? 
	chrec_fold_plus (type, CHREC_LEFT (poly), peel) :
	chrec_fold_minus (type, CHREC_LEFT (poly), peel)),
       (code == PLUS_EXPR ? 
	chrec_fold_plus (type, CHREC_RIGHT (poly), peel) :
	chrec_fold_minus (type, CHREC_RIGHT (poly), peel)));
  
  return build_peeled_chrec 
    (CHREC_VARIABLE (peel), 
     (code == PLUS_EXPR ? 
      chrec_fold_plus (type, poly, CHREC_LEFT (peel)) :
      chrec_fold_minus (type, poly, CHREC_LEFT (peel))),
     (code == PLUS_EXPR ? 
      chrec_fold_plus (type, poly, CHREC_RIGHT (peel)) :
      chrec_fold_minus (type, poly, CHREC_RIGHT (peel))));
}

/* Fold the addition of a polynomial and a peeled chrec.  */

static inline tree 
chrec_fold_plus_peel_poly (enum tree_code code, 
			   tree type, 
			   tree peel, 
			   tree poly)
{
#if defined ENABLE_CHECKING
  if (peel == NULL_TREE
      || poly == NULL_TREE
      || TREE_CODE (peel) != PEELED_CHREC
      || TREE_CODE (poly) != POLYNOMIAL_CHREC)
    abort ();
#endif
  
  /*
    {a, +, b}_1 + (c, d)_2  ->  ({a, +, b}_1 + c, {a, +, b}_1 + d)_2,
    {a, +, b}_2 + (c, d)_1  ->  {a + (c, d)_1, +, b}_2,
    {a, +, b}_x + (c, d)_x  ->  ({a, +, b}_x + c, {a, +, b}_x + d)_x.  */
  if (CHREC_VARIABLE (poly) < CHREC_VARIABLE (peel))
    return build_peeled_chrec 
      (CHREC_VARIABLE (peel), 
       (code == PLUS_EXPR ? 
	chrec_fold_plus (type, CHREC_LEFT (peel), poly) : 
	chrec_fold_minus (type, CHREC_LEFT (peel), poly)),
       (code == PLUS_EXPR ? 
	chrec_fold_plus (type, CHREC_RIGHT (peel), poly) :
	chrec_fold_minus (type, CHREC_RIGHT (peel), poly)));
  
  if (CHREC_VARIABLE (poly) > CHREC_VARIABLE (peel))
    return build_polynomial_chrec 
      (CHREC_VARIABLE (poly), 
       (code == PLUS_EXPR ? 
	chrec_fold_plus (type, peel, CHREC_LEFT (poly)) :
	chrec_fold_minus (type, peel, CHREC_LEFT (poly))),
       (code == PLUS_EXPR ? 
	chrec_fold_plus (type, peel, CHREC_RIGHT (poly)) :
	chrec_fold_minus (type, peel, CHREC_RIGHT (poly))));
  
  return build_peeled_chrec 
    (CHREC_VARIABLE (peel), 
     (code == PLUS_EXPR ? 
      chrec_fold_plus (type, CHREC_LEFT (peel), poly) :
      chrec_fold_minus (type, CHREC_LEFT (peel), poly)),
     (code == PLUS_EXPR ? 
      chrec_fold_plus (type, CHREC_RIGHT (peel), poly) :
      chrec_fold_minus (type, CHREC_RIGHT (peel), poly)));
}

/* Fold the addition of a polynomial and an exponential functions.  */

static inline tree 
chrec_fold_plus_poly_expo (enum tree_code code, 
			   tree type, 
			   tree poly, 
			   tree expo)
{
#if defined ENABLE_CHECKING
  if (expo == NULL_TREE
      || poly == NULL_TREE
      || TREE_CODE (expo) != EXPONENTIAL_CHREC
      || TREE_CODE (poly) != POLYNOMIAL_CHREC)
    abort ();
#endif
  
  /* For the moment, we don't know how to fold this further.  */
  return build (code, type, poly, expo);
}

/* Fold the addition of a polynomial and an exponential functions.  */

static inline tree 
chrec_fold_plus_expo_poly (enum tree_code code, 
			   tree type, 
			   tree expo, 
			   tree poly)
{
#if defined ENABLE_CHECKING
  if (expo == NULL_TREE
      || poly == NULL_TREE
      || TREE_CODE (expo) != EXPONENTIAL_CHREC
      || TREE_CODE (poly) != POLYNOMIAL_CHREC)
    abort ();
#endif
  
  /* For the moment, we don't know how to fold this further.  */
  return build (code, type, expo, poly);
}

/* Fold the addition of two peeled chrecs.  */

static inline tree 
chrec_fold_plus_peel_peel (tree type, 
			   tree peel0, 
			   tree peel1)
{
#if defined ENABLE_CHECKING
  if (peel0 == NULL_TREE
      || peel1 == NULL_TREE
      || TREE_CODE (peel0) != PEELED_CHREC
      || TREE_CODE (peel1) != PEELED_CHREC)
    abort ();
#endif
  
  /*
    (a, b)_1 + (c, d)_2  ->  ((a, b)_1 + c, (a, b)_1 + d)_2,
    (a, b)_2 + (c, d)_1  ->  (a + (c, d)_1, b + (c, d)_1)_2,
    (a, b)_x + (c, d)_x  ->  (a + c, b + d)_x.  */
  if (CHREC_VARIABLE (peel0) < CHREC_VARIABLE (peel1))
    return build_peeled_chrec 
      (CHREC_VARIABLE (peel1), 
       chrec_fold_plus (type, peel0, CHREC_LEFT (peel1)),
       chrec_fold_plus (type, peel0, CHREC_RIGHT (peel1)));
  
  if (CHREC_VARIABLE (peel0) > CHREC_VARIABLE (peel1))
    return build_peeled_chrec 
      (CHREC_VARIABLE (peel0), 
       chrec_fold_plus (type, CHREC_LEFT (peel0), peel1),
       chrec_fold_plus (type, CHREC_RIGHT (peel0), peel1));
  
  return build_peeled_chrec 
    (CHREC_VARIABLE (peel0), 
     chrec_fold_plus (type, CHREC_LEFT (peel0), CHREC_LEFT (peel1)),
     chrec_fold_plus (type, CHREC_RIGHT (peel0), CHREC_RIGHT (peel1)));
}

/* Fold the addition of two exponential functions.  */

static inline tree 
chrec_fold_plus_expo_expo (enum tree_code code, 
			   tree type, 
			   tree expo0, 
			   tree expo1)
{
#if defined ENABLE_CHECKING
  if (expo0 == NULL_TREE
      || expo1 == NULL_TREE
      || TREE_CODE (expo0) != EXPONENTIAL_CHREC
      || TREE_CODE (expo1) != EXPONENTIAL_CHREC)
    abort ();
#endif
  
  /* For the moment, we don't know how to fold this further.  */
  return build (code, type, expo0, expo1);
}



/* Fold the multiplication of a peeled chrec and a constant.  */

static inline tree 
chrec_fold_multiply_peel_cst (tree type, 
			      tree peel, 
			      tree cst)
{
#if defined ENABLE_CHECKING
  if (peel == NULL_TREE
      || cst == NULL_TREE
      || TREE_CODE (peel) != PEELED_CHREC
      || is_not_constant_evolution (cst))
    abort ();
#endif
  
  return build_peeled_chrec 
    (CHREC_VARIABLE (peel),
     chrec_fold_multiply (type, CHREC_LEFT (peel), cst),
     chrec_fold_multiply (type, CHREC_RIGHT (peel), cst));
}

/* Fold the multiplication of an interval and a constant.  */

static inline tree 
chrec_fold_multiply_ival_cst (tree type, 
			      tree ival, 
			      tree cst)
{
  tree lowm, upm;

#if defined ENABLE_CHECKING
  if (ival == NULL_TREE
      || cst == NULL_TREE
      || TREE_CODE (ival) != INTERVAL_CHREC
      || is_not_constant_evolution (cst))
    abort ();
#endif
 
  /* Don't modify abstract values.  */
  if (ival == chrec_top)
    return chrec_top;
  if (ival == chrec_bot)
    return chrec_bot;

  lowm = chrec_fold_multiply (type, CHREC_LOW (ival), cst);
  upm = chrec_fold_multiply (type, CHREC_UP (ival), cst);

  /* When the fold resulted in an overflow, conservatively answer
     chrec_top.  */
  if (!evolution_function_is_constant_p (lowm)
      || !evolution_function_is_constant_p (upm)
      || TREE_OVERFLOW (lowm)
      || TREE_OVERFLOW (upm))
    return build_chrec_top_type (type);

  return build_interval_chrec (tree_fold_min (type, lowm, upm),
			       tree_fold_max (type, lowm, upm));
}

/* Fold the multiplication of two polynomial functions.  */

static inline tree 
chrec_fold_multiply_poly_poly (tree type, 
			       tree poly0, 
			       tree poly1)
{
#if defined ENABLE_CHECKING
  if (poly0 == NULL_TREE
      || poly1 == NULL_TREE
      || TREE_CODE (poly0) != POLYNOMIAL_CHREC
      || TREE_CODE (poly1) != POLYNOMIAL_CHREC)
    abort ();
#endif
  
  /* {a, +, b}_1 * {c, +, d}_2  ->  {c*{a, +, b}_1, +, d}_2,
     {a, +, b}_2 * {c, +, d}_1  ->  {a*{c, +, d}_1, +, b}_2,
     {a, +, b}_x * {c, +, d}_x  ->  {a*c, +, a*d + b*c + b*d, +, 2*b*d}_x.  */
  if (CHREC_VARIABLE (poly0) < CHREC_VARIABLE (poly1))
    /* poly0 is a constant wrt. poly1.  */
    return build_polynomial_chrec 
      (CHREC_VARIABLE (poly1), 
       chrec_fold_multiply (type, CHREC_LEFT (poly1), poly0),
       CHREC_RIGHT (poly1));
  
  if (CHREC_VARIABLE (poly1) < CHREC_VARIABLE (poly0))
    /* poly1 is a constant wrt. poly0.  */
    return build_polynomial_chrec 
      (CHREC_VARIABLE (poly0), 
       chrec_fold_multiply (type, CHREC_LEFT (poly0), poly1),
       CHREC_RIGHT (poly0));
  
  /* poly0 and poly1 are two polynomials in the same variable,
     {a, +, b}_x * {c, +, d}_x  ->  {a*c, +, a*d + b*c + b*d, +, 2*b*d}_x.  */
  return 
    build_polynomial_chrec 
    (CHREC_VARIABLE (poly0), 
     build_polynomial_chrec 
     (CHREC_VARIABLE (poly0), 
      
      /* "a*c".  */
      chrec_fold_multiply (type, CHREC_LEFT (poly0), CHREC_LEFT (poly1)),
      
      /* "a*d + b*c + b*d".  */
      chrec_fold_plus 
      (type, chrec_fold_multiply (type, CHREC_LEFT (poly0), CHREC_RIGHT (poly1)),
       
       chrec_fold_plus 
       (type, 
	chrec_fold_multiply (type, CHREC_RIGHT (poly0), CHREC_LEFT (poly1)),
	chrec_fold_multiply (type, CHREC_RIGHT (poly0), CHREC_RIGHT (poly1))))),
     
     /* "2*b*d".  */
     chrec_fold_multiply
     (type, build_int_2 (2, 0),
      chrec_fold_multiply (type, CHREC_RIGHT (poly0), CHREC_RIGHT (poly1))));
}

/* Fold the addition of a polynomial and a peeled chrec.  */

static inline tree 
chrec_fold_multiply_poly_peel (tree type, 
			       tree poly, 
			       tree peel)
{
#if defined ENABLE_CHECKING
  if (peel == NULL_TREE
      || poly == NULL_TREE
      || TREE_CODE (peel) != PEELED_CHREC
      || TREE_CODE (poly) != POLYNOMIAL_CHREC)
    abort ();
#endif
  
  /*
    {a, +, b}_1 * (c, d)_2  ->  ({a, +, b}_1 * c, {a, +, b}_1 * d)_2,
    {a, +, b}_2 * (c, d)_1  ->  {a * (c, d)_1, +, b}_2,
    {a, +, b}_x * (c, d)_x  ->  ({a, +, b}_x * c, {a, +, b}_x * d)_x.  */
  if (CHREC_VARIABLE (poly) < CHREC_VARIABLE (peel))
    return build_peeled_chrec 
      (CHREC_VARIABLE (peel), 
       chrec_fold_multiply (type, poly, CHREC_LEFT (peel)),
       chrec_fold_multiply (type, poly, CHREC_RIGHT (peel)));
  
  if (CHREC_VARIABLE (poly) > CHREC_VARIABLE (peel))
    return build_polynomial_chrec 
      (CHREC_VARIABLE (poly), 
       chrec_fold_multiply (type, CHREC_LEFT (poly), peel),
       chrec_fold_multiply (type, CHREC_RIGHT (poly), peel));
  
  return build_peeled_chrec 
    (CHREC_VARIABLE (peel), 
     chrec_fold_multiply (type, poly, CHREC_LEFT (peel)),
     chrec_fold_multiply (type, poly, CHREC_RIGHT (peel)));
}

/* Fold the multiplication of a polynomial and an exponential
   functions.  */

static inline tree 
chrec_fold_multiply_poly_expo (tree type, 
			       tree poly, 
			       tree expo)
{
#if defined ENABLE_CHECKING
  if (expo == NULL_TREE
      || poly == NULL_TREE
      || TREE_CODE (expo) != EXPONENTIAL_CHREC
      || TREE_CODE (poly) != POLYNOMIAL_CHREC)
    abort ();
#endif
  
  /* For the moment, we don't know how to fold this further.  */
  return build (MULT_EXPR, type, expo, poly);
}

/* Fold the addition of two peeled chrecs.  */

static inline tree 
chrec_fold_multiply_peel_peel (tree type, 
			       tree peel0, 
			       tree peel1)
{
#if defined ENABLE_CHECKING
  if (peel0 == NULL_TREE
      || peel1 == NULL_TREE
      || TREE_CODE (peel0) != PEELED_CHREC
      || TREE_CODE (peel1) != PEELED_CHREC)
    abort ();
#endif
  
  /*
    (a, b)_1 * (c, d)_2  ->  ((a, b)_1 * c, (a, b)_1 * d)_2,
    (a, b)_2 * (c, d)_1  ->  (a * (c, d)_1, b * (c, d)_1)_2,
    (a, b)_x * (c, d)_x  ->  (a * c, b * d)_x.  */
  if (CHREC_VARIABLE (peel0) < CHREC_VARIABLE (peel1))
    return build_peeled_chrec 
      (CHREC_VARIABLE (peel1), 
       chrec_fold_multiply (type, peel0, CHREC_LEFT (peel1)),
       chrec_fold_multiply (type, peel0, CHREC_RIGHT (peel1)));
  
  if (CHREC_VARIABLE (peel0) > CHREC_VARIABLE (peel1))
    return build_peeled_chrec 
      (CHREC_VARIABLE (peel0), 
       chrec_fold_multiply (type, CHREC_LEFT (peel0), peel1),
       chrec_fold_multiply (type, CHREC_RIGHT (peel0), peel1));
  
  return build_peeled_chrec 
    (CHREC_VARIABLE (peel0), 
     chrec_fold_multiply (type, CHREC_LEFT (peel0), CHREC_LEFT (peel1)),
     chrec_fold_multiply (type, CHREC_RIGHT (peel0), CHREC_RIGHT (peel1)));
}

/* Fold the multiplication of two exponential functions.  */

static inline tree 
chrec_fold_multiply_expo_expo (tree type, 
			       tree expo0, 
			       tree expo1)
{
#if defined ENABLE_CHECKING
  if (expo0 == NULL_TREE
      || expo1 == NULL_TREE
      || TREE_CODE (expo0) != EXPONENTIAL_CHREC
      || TREE_CODE (expo1) != EXPONENTIAL_CHREC)
    abort ();
#endif
  
  if (CHREC_VARIABLE (expo0) < CHREC_VARIABLE (expo1))
    /* expo0 is a constant wrt. expo1.  */
    return build_exponential_chrec 
      (CHREC_VARIABLE (expo1), 
       chrec_fold_multiply (type, CHREC_LEFT (expo1), expo0),
       CHREC_RIGHT (expo1));
  
  if (CHREC_VARIABLE (expo1) < CHREC_VARIABLE (expo0))
    /* expo1 is a constant wrt. expo0.  */
    return build_exponential_chrec
      (CHREC_VARIABLE (expo0), 
       chrec_fold_multiply (type, CHREC_LEFT (expo0), expo1),
       CHREC_RIGHT (expo0));
  
  return build_exponential_chrec 
    (CHREC_VARIABLE (expo0), 
     chrec_fold_multiply (type, CHREC_LEFT (expo0), CHREC_LEFT (expo1)),
     chrec_fold_multiply (type, CHREC_RIGHT (expo0), CHREC_RIGHT (expo1)));
}

/* Fold the multiplication of two intervals.  */

static inline tree
chrec_fold_multiply_ival_ival (tree type, 
			       tree ival0,
			       tree ival1)
{
  tree ac, ad, bc, bd;
  
#if defined ENABLE_CHECKING
  if (ival0 == NULL_TREE
      || ival1 == NULL_TREE
      || TREE_CODE (ival0) != INTERVAL_CHREC
      || TREE_CODE (ival1) != INTERVAL_CHREC)
    abort ();
#endif
  
  /* Don't modify abstract values.  */
  if (automatically_generated_chrec_p (ival0)
      || automatically_generated_chrec_p (ival1))
    chrec_fold_automatically_generated_operands (ival0, ival1);
  
  ac = tree_fold_multiply (type, CHREC_LOW (ival0), CHREC_LOW (ival1));
  ad = tree_fold_multiply (type, CHREC_LOW (ival0), CHREC_UP (ival1));
  bc = tree_fold_multiply (type, CHREC_UP (ival0), CHREC_LOW (ival1));
  bd = tree_fold_multiply (type, CHREC_UP (ival0), CHREC_UP (ival1));

  /* When the fold resulted in an overflow, conservatively answer
     chrec_top.  */
  if (!evolution_function_is_constant_p (ac)
      || !evolution_function_is_constant_p (ad)
      || !evolution_function_is_constant_p (bc)
      || !evolution_function_is_constant_p (bd)
      || TREE_OVERFLOW (ac)
      || TREE_OVERFLOW (ad)
      || TREE_OVERFLOW (bc)
      || TREE_OVERFLOW (bd))
    return build_chrec_top_type (type);

  /* [a, b] * [c, d]  ->  [min (ac, ad, bc, bd), max (ac, ad, bc, bd)],
     for reference, see Moore's "Interval Arithmetic".  */
  return build_interval_chrec 
    (tree_fold_min 
     (type, tree_fold_min
      (type, tree_fold_min (type, ac, ad), bc), bd),
     tree_fold_max
     (type, tree_fold_max
      (type, tree_fold_max (type, ac, ad), bc), bd));
}

/* When the operands are automatically_generated_chrec_p, the fold has
   to respect the semantics of the operands.  */

tree 
chrec_fold_automatically_generated_operands (tree op0, 
					     tree op1)
{
  /* TOP op x = TOP,
     x op TOP = TOP.  */
  if (op0 == chrec_top
      || op1 == chrec_top)
    return chrec_top;
  
  /* BOT op TOP = TOP, 
     TOP op BOT = TOP, 
     BOT op x = BOT,
     x op BOT = BOT.  */
  if (op0 == chrec_bot
      || op1 == chrec_bot)
    return chrec_bot;
  
  if (op0 == chrec_not_analyzed_yet
      || op1 == chrec_not_analyzed_yet)
    return chrec_not_analyzed_yet;
  
  /* The default case produces a safe result. */
  return chrec_top;
}

/* Fold the addition of two chrecs.  */

static tree
chrec_fold_plus_1 (enum tree_code code, 
		   tree type, 
		   tree op0,
		   tree op1)
{
  tree t1, t2;
  
  if (automatically_generated_chrec_p (op0)
      || automatically_generated_chrec_p (op1))
    return chrec_fold_automatically_generated_operands (op0, op1);
  
  switch (TREE_CODE (op0))
    {
    case POLYNOMIAL_CHREC:
      switch (TREE_CODE (op1))
	{
	case POLYNOMIAL_CHREC:
	  return chrec_fold_plus_poly_poly (code, type, op0, op1);
	  
	case EXPONENTIAL_CHREC:
	  return chrec_fold_plus_poly_expo (code, type, op0, op1);
	  
	case PEELED_CHREC:
	  return chrec_fold_plus_poly_peel (code, type, op0, op1);
	  
	default:
	  if (code == PLUS_EXPR)
	    return build_polynomial_chrec 
	      (CHREC_VARIABLE (op0), 
	       chrec_fold_plus (type, CHREC_LEFT (op0), op1),
	       CHREC_RIGHT (op0));
	  else
	    return build_polynomial_chrec 
	      (CHREC_VARIABLE (op0), 
	       chrec_fold_minus (type, CHREC_LEFT (op0), op1),
	       CHREC_RIGHT (op0));
	}
      
    case EXPONENTIAL_CHREC:
      switch (TREE_CODE (op1))
	{
	case POLYNOMIAL_CHREC:
	  return chrec_fold_plus_expo_poly (code, type, op0, op1);
	  
	case EXPONENTIAL_CHREC:
	  return chrec_fold_plus_expo_expo (code, type, op0, op1);
	  
	case PEELED_CHREC:
	  return build (code, type, op0, op1);

	default:
	  return chrec_fold_plus_expo_cst (code, type, op0, op1);
	}
      
    case PEELED_CHREC:
      switch (TREE_CODE (op1))
	{
	case POLYNOMIAL_CHREC:
	  return chrec_fold_plus_peel_poly (code, type, op0, op1);
	  
	case PEELED_CHREC:
	  return chrec_fold_plus_peel_peel (type, op0, op1);
	  
	case EXPONENTIAL_CHREC:
	  return build (code, type, op0, op1);
	  
	default:
	  return build_peeled_chrec 
	    (CHREC_VARIABLE (op0),
	     (code == PLUS_EXPR ? 
	      chrec_fold_plus (type, CHREC_LEFT (op0), op1) :
	      chrec_fold_minus (type, CHREC_LEFT (op0), op1)),
	     (code == PLUS_EXPR ? 
	      chrec_fold_plus (type, CHREC_RIGHT (op0), op1) :
	      chrec_fold_minus (type, CHREC_RIGHT (op0), op1)));
	}
      
    case INTERVAL_CHREC:
      switch (TREE_CODE (op1))
	{
	case POLYNOMIAL_CHREC:
	  return build_polynomial_chrec 
	    (CHREC_VARIABLE (op1), 
	     (code == PLUS_EXPR ? 
	      chrec_fold_plus (type, op0, CHREC_LEFT (op1)) :
	      chrec_fold_minus (type, op0, CHREC_LEFT (op1))),
	     CHREC_RIGHT (op1));
	  
	case EXPONENTIAL_CHREC:
	  return chrec_top;
	  
	case INTERVAL_CHREC:
	  t1 = (code == PLUS_EXPR ? 
		chrec_fold_plus (type, CHREC_LOW (op0), CHREC_LOW (op1)) :
		chrec_fold_minus (type, CHREC_LOW (op0), CHREC_LOW (op1)));
	  t2 = (code == PLUS_EXPR ? 
		chrec_fold_plus (type, CHREC_UP (op0), CHREC_UP (op1)) :
		chrec_fold_minus (type, CHREC_UP (op0), CHREC_UP (op1)));

	  /* When the fold resulted in an overflow, conservatively answer
	     chrec_top.  */
	  if (!evolution_function_is_constant_p (t1)
	      || !evolution_function_is_constant_p (t2)
	      || TREE_OVERFLOW (t1)
	      || TREE_OVERFLOW (t2))
	    return build_chrec_top_type (type);

	  return build_interval_chrec 
	    (tree_fold_min (type, t1, t2),
	     tree_fold_max (type, t1, t2));
	  
	default:
	  t1 = (code == PLUS_EXPR ? 
		chrec_fold_plus (type, CHREC_LOW (op0), op1) : 
		chrec_fold_minus (type, CHREC_LOW (op0), op1));
	  t2 = (code == PLUS_EXPR ? 
		chrec_fold_plus (type, CHREC_UP (op0), op1) : 
		chrec_fold_minus (type, CHREC_UP (op0), op1));
	  
	  /* When the fold resulted in an overflow, conservatively answer
	     chrec_top.  */
	  if (!evolution_function_is_constant_p (t1)
	      || !evolution_function_is_constant_p (t2)
	      || TREE_OVERFLOW (t1)
	      || TREE_OVERFLOW (t2))
	    return build_chrec_top_type (type);

	  return build_interval_chrec 
	    (tree_fold_min (type, t1, t2),
	     tree_fold_max (type, t1, t2));
	}
      
    default:
      switch (TREE_CODE (op1))
	{
	case POLYNOMIAL_CHREC:
	  if (code == PLUS_EXPR)
	    return build_polynomial_chrec 
	      (CHREC_VARIABLE (op1), 
	       chrec_fold_plus (type, op0, CHREC_LEFT (op1)),
	       CHREC_RIGHT (op1));
	  else
	    return build_polynomial_chrec 
	      (CHREC_VARIABLE (op1), 
	       chrec_fold_minus (type, op0, CHREC_LEFT (op1)),
	       chrec_fold_multiply (type, CHREC_RIGHT (op1), 
				    convert (type,
					     integer_minus_one_node)));
	     
	case EXPONENTIAL_CHREC:
	  return chrec_fold_plus_cst_expo (code, type, op0, op1);
	  
	case PEELED_CHREC:
	  if (code == PLUS_EXPR)
	    return build_peeled_chrec 
	      (CHREC_VARIABLE (op1),
	       chrec_fold_plus (type, op0, CHREC_LEFT (op1)),
	       chrec_fold_plus (type, op0, CHREC_RIGHT (op1)));
	  else
	    return build_peeled_chrec 
	      (CHREC_VARIABLE (op1),
	       chrec_fold_minus (type, op0, CHREC_LEFT (op1)),
	       chrec_fold_minus (type, op0, CHREC_RIGHT (op1)));
	  
	case INTERVAL_CHREC:
	  t1 = (code == PLUS_EXPR ? 
		chrec_fold_plus (type, op0, CHREC_LOW (op1)) : 
		chrec_fold_minus (type, op0, CHREC_LOW (op1)));
	  t2 = (code == PLUS_EXPR ? 
		chrec_fold_plus (type, op0, CHREC_UP (op1)) :
		chrec_fold_minus (type, op0, CHREC_UP (op1)));

	  /* When the fold resulted in an overflow, conservatively answer
	     chrec_top.  */
	  if (!evolution_function_is_constant_p (t1)
	      || !evolution_function_is_constant_p (t2)
	      || TREE_OVERFLOW (t1)
	      || TREE_OVERFLOW (t2))
	    return build_chrec_top_type (type);

	  return build_interval_chrec 
	      (tree_fold_min (type, t1, t2),
	       tree_fold_max (type, t1, t2));
	  
	default:
	  if (tree_contains_chrecs (op0)
	      || tree_contains_chrecs (op1))
	    return build (code, type, op0, op1);
	  else
	    return fold (build (code, type, op0, op1));
	}
    }
}

/* Fold the addition of two chrecs.  */

tree
chrec_fold_plus (tree type, 
		 tree op0,
		 tree op1)
{
  if (integer_zerop (op0)
      || (TREE_CODE (op0) == INTERVAL_CHREC
	  && integer_zerop (CHREC_LOW (op0))
	  && integer_zerop (CHREC_UP (op0))))
    return op1;
  if (integer_zerop (op1)
      || (TREE_CODE (op1) == INTERVAL_CHREC
	  && integer_zerop (CHREC_LOW (op1))
	  && integer_zerop (CHREC_UP (op1))))
    return op0;
  
  return chrec_fold_plus_1 (PLUS_EXPR, type, op0, op1);
}

/* Fold the substraction of two chrecs.  */

tree 
chrec_fold_minus (tree type, 
		  tree op0, 
		  tree op1)
{
  if (integer_zerop (op1)
      || (TREE_CODE (op1) == INTERVAL_CHREC
	  && integer_zerop (CHREC_LOW (op1))
	  && integer_zerop (CHREC_UP (op1))))
    return op0;
  
  return chrec_fold_plus_1 (MINUS_EXPR, type, op0, op1);
}

/* Fold the multiplication of two chrecs.  */

tree
chrec_fold_multiply (tree type, 
		     tree op0,
		     tree op1)
{
  if (automatically_generated_chrec_p (op0)
      || automatically_generated_chrec_p (op1))
    return chrec_fold_automatically_generated_operands (op0, op1);
  
  switch (TREE_CODE (op0))
    {
    case POLYNOMIAL_CHREC:
      switch (TREE_CODE (op1))
	{
	case POLYNOMIAL_CHREC:
	  return chrec_fold_multiply_poly_poly (type, op0, op1);
	  
	case PEELED_CHREC:
	  return chrec_fold_multiply_poly_peel (type, op0, op1);
	  
	case EXPONENTIAL_CHREC:
	  return chrec_fold_multiply_poly_expo (type, op0, op1);
	  
	default:
	  if (integer_onep (op1))
	    return op0;
	  if (integer_zerop (op1))
	    return convert (type, integer_zero_node);
	  
	  return build_polynomial_chrec 
	    (CHREC_VARIABLE (op0), 
	     chrec_fold_multiply (type, CHREC_LEFT (op0), op1),
	     chrec_fold_multiply (type, CHREC_RIGHT (op0), op1));
	}
      
    case PEELED_CHREC:
      switch (TREE_CODE (op1))
	{
	case POLYNOMIAL_CHREC:
	  return chrec_fold_multiply_poly_peel (type, op1, op0);
	  
	case PEELED_CHREC:
	  return chrec_fold_multiply_peel_peel (type, op0, op1);
	  
	default:
	  if (integer_onep (op1))
	    return op0;
	  if (integer_zerop (op1))
	    return convert (type, integer_zero_node);
	  
	  return build_peeled_chrec 
	    (CHREC_VARIABLE (op0),
	     chrec_fold_multiply (type, CHREC_LEFT (op0), op1),
	     chrec_fold_multiply (type, CHREC_RIGHT (op0), op1));
	}
      
    case EXPONENTIAL_CHREC:
      switch (TREE_CODE (op1))
	{
	case POLYNOMIAL_CHREC:
	  return chrec_fold_multiply_poly_expo (type, op1, op0);
	  
	case EXPONENTIAL_CHREC:
	  return chrec_fold_multiply_expo_expo (type, op0, op1);
	  
	default:
	  if (integer_onep (op1))
	    return op0;
	  if (integer_zerop (op1))
	    return convert (type, integer_zero_node);
	  
	  return build_exponential_chrec 
	    (CHREC_VARIABLE (op0),
	     chrec_fold_multiply (type, CHREC_LEFT (op0), op1),
	     CHREC_RIGHT (op0));
	}
      
    case INTERVAL_CHREC:
      switch (TREE_CODE (op1))
	{
	case POLYNOMIAL_CHREC:
	  return build_polynomial_chrec 
	    (CHREC_VARIABLE (op1), 
	     chrec_fold_multiply (type, CHREC_LEFT (op1), op0),
	     chrec_fold_multiply (type, CHREC_RIGHT (op1), op0));
	  
	case PEELED_CHREC:
	  return build_peeled_chrec 
	    (CHREC_VARIABLE (op1),
	     chrec_fold_multiply (type, CHREC_LEFT (op1), op0),
	     chrec_fold_multiply (type, CHREC_RIGHT (op1), op0));
	  
	case EXPONENTIAL_CHREC:
	  return build_exponential_chrec 
	    (CHREC_VARIABLE (op1),
	     chrec_fold_multiply (type, CHREC_LEFT (op1), op0),
	     CHREC_RIGHT (op1));
	  
	case INTERVAL_CHREC:
	  return chrec_fold_multiply_ival_ival (type, op0, op1);
	  
	default:
	  if (integer_onep (op1))
	    return op0;
	  if (integer_zerop (op1))
	    return convert (type, integer_zero_node);
	  return chrec_fold_multiply_ival_cst (type, op0, op1);
	}
      
    default:
      if (integer_onep (op0))
	return op1;
      
      if (integer_zerop (op0))
	return convert (type, integer_zero_node);
      
      switch (TREE_CODE (op1))
	{
	case POLYNOMIAL_CHREC:
	  return build_polynomial_chrec 
	    (CHREC_VARIABLE (op1), 
	     chrec_fold_multiply (type, CHREC_LEFT (op1), op0),
	     chrec_fold_multiply (type, CHREC_RIGHT (op1), op0));
	  
	case PEELED_CHREC:
	  return build_peeled_chrec 
	    (CHREC_VARIABLE (op1),
	     chrec_fold_multiply (type, CHREC_LEFT (op1), op0),
	     chrec_fold_multiply (type, CHREC_RIGHT (op1), op0));
	  
	case EXPONENTIAL_CHREC:
	  return build_exponential_chrec 
	    (CHREC_VARIABLE (op1),
	     chrec_fold_multiply (type, CHREC_LEFT (op1), op0),
	     CHREC_RIGHT (op1));
	  
	case INTERVAL_CHREC:
	  return chrec_fold_multiply_ival_cst (type, op1, op0);
	  
	default:
	  if (integer_onep (op1))
	    return op0;
	  if (integer_zerop (op1))
	    return convert (type, integer_zero_node);
	  return tree_fold_multiply (type, op0, op1);
	}
    }
}



/* Operations.  */

/* Try to recognize the nature of the peeled chrec and to transform it
   into a more specific chrec: polynomial, periodic, ...  For example,
   this function transforms (6, {9, +, 3}_1)_1 into {6, +, 3}_1.  */

tree
simplify_peeled_chrec (tree peel)
{
  tree res = peel;
  
  return res;
}

/* Helper function.  Use the Newton's interpolating formula for
   evaluating the value of the evolution function.  */

static tree 
chrec_evaluate (unsigned var,
		tree chrec,
		tree n,
		tree k)
{
  tree type = chrec_type (chrec);
  tree binomial_n_k = tree_fold_binomial (n, k);
  
  if (TREE_CODE (chrec) == EXPONENTIAL_CHREC
      && CHREC_VARIABLE (chrec) == var)
    return chrec_top;
  
  if (TREE_CODE (chrec) == POLYNOMIAL_CHREC)
    {
      if (CHREC_VARIABLE (chrec) > var)
	return chrec_evaluate (var, CHREC_LEFT (chrec), n, k);
      
      if (CHREC_VARIABLE (chrec) == var)
	return chrec_fold_plus 
	  (type, 
	   chrec_fold_multiply (type, binomial_n_k, CHREC_LEFT (chrec)),
	   chrec_evaluate (var, CHREC_RIGHT (chrec), n, 
			   tree_fold_plus (type, k, integer_one_node)));
      
      return chrec_fold_multiply (type, binomial_n_k, chrec);
    }
  else
    return chrec_fold_multiply (type, binomial_n_k, chrec);
}

/* Evaluates "CHREC (X)" when the varying variable is VAR.  
   Example:  Given the following parameters, 
   
   var = 1
   chrec = {5, +, {3, +, 4}_1}_1
   x = 10
   
   The result is given by the Newton's interpolating formula: 
   5 * \binom{10}{0} + 3 * \binom{10}{1} + 4 * \binom{10}{2}.
*/

tree 
chrec_apply (unsigned var,
	     tree chrec, 
	     tree x)
{
  tree type = chrec_type (chrec);
  tree res = chrec_top;

  if (automatically_generated_chrec_p (chrec)
      || automatically_generated_chrec_p (x)

      /* When the symbols are defined in an outer loop, it is possible
	 to symbolically compute the apply, since the symbols are
	 constants with respect to the varying loop.  */
      || chrec_contains_symbols_defined_in_loop (chrec, var)
      || chrec_contains_symbols (x))
    return chrec_top;
  
  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "(chrec_apply \n");
  
  if (evolution_function_is_affine_p (chrec))
    {
      /* "{a, +, b} (x)"  ->  "a + b*x".  */
      if (TREE_CODE (CHREC_LEFT (chrec)) == INTEGER_CST
	  && integer_zerop (CHREC_LEFT (chrec)))
	res = chrec_fold_multiply (type, CHREC_RIGHT (chrec), x);
      
      else
	res = chrec_fold_plus (type, CHREC_LEFT (chrec), 
			       chrec_fold_multiply (type, 
						    CHREC_RIGHT (chrec), x));
    }
  
  else if (TREE_CODE (chrec) != POLYNOMIAL_CHREC
	   && TREE_CODE (chrec) != EXPONENTIAL_CHREC)
    res = chrec;
  
  else if (TREE_CODE (x) == INTEGER_CST
	   && tree_int_cst_sgn (x) == 1)
    /* testsuite/.../ssa-chrec-38.c.  */
    res = chrec_evaluate (var, chrec, x, integer_zero_node);
  
  else
    res = chrec_top;
  
  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "  (varying_loop = %d\n", var);
      fprintf (dump_file, ")\n  (chrec = ");
      print_generic_expr (dump_file, chrec, 0);
      fprintf (dump_file, ")\n  (x = ");
      print_generic_expr (dump_file, x, 0);
      fprintf (dump_file, ")\n  (res = ");
      print_generic_expr (dump_file, res, 0);
      fprintf (dump_file, "))\n");
    }
  
  return res;
}

/* Replaces the initial condition in CHREC with INIT_COND.  */

tree 
chrec_replace_initial_condition (tree chrec, 
				 tree init_cond)
{
  if (automatically_generated_chrec_p (chrec))
    return chrec;
  
  switch (TREE_CODE (chrec))
    {
    case POLYNOMIAL_CHREC:
      return build_polynomial_chrec 
	(CHREC_VARIABLE (chrec),
	 chrec_replace_initial_condition (CHREC_LEFT (chrec), init_cond),
	 CHREC_RIGHT (chrec));
      
    case EXPONENTIAL_CHREC:
      return build_exponential_chrec
	(CHREC_VARIABLE (chrec),
	 chrec_replace_initial_condition (CHREC_LEFT (chrec), init_cond),
	 CHREC_RIGHT (chrec));
      
    case PEELED_CHREC:
      return chrec_top;
      
    default:
      return init_cond;
    }
}

/* Returns the initial condition of a given CHREC.  */

tree 
initial_condition (tree chrec)
{
  if (automatically_generated_chrec_p (chrec))
    return chrec;
  
  if (TREE_CODE (chrec) == POLYNOMIAL_CHREC
      || TREE_CODE (chrec) == EXPONENTIAL_CHREC)
    return initial_condition (CHREC_LEFT (chrec));
  else
    return chrec;
}

/* Returns a multivariate function that has no evolution in LOOP_NUM.
   Mask the evolution LOOP_NUM.  */

tree 
hide_evolution_in_loop (tree chrec, unsigned loop_num)
{
  if (automatically_generated_chrec_p (chrec))
    return chrec;
  
  switch (TREE_CODE (chrec))
    {
    case POLYNOMIAL_CHREC:
      if (CHREC_VARIABLE (chrec) >= loop_num)
	return hide_evolution_in_loop (CHREC_LEFT (chrec), loop_num);

      else
	return build_polynomial_chrec 
	  (CHREC_VARIABLE (chrec), 
	   hide_evolution_in_loop (CHREC_LEFT (chrec), loop_num), 
	   CHREC_RIGHT (chrec));

    case EXPONENTIAL_CHREC:
      if (CHREC_VARIABLE (chrec) >= loop_num)
	return hide_evolution_in_loop (CHREC_LEFT (chrec), loop_num);

      else
	return build_exponential_chrec 
	  (CHREC_VARIABLE (chrec),
	   hide_evolution_in_loop (CHREC_LEFT (chrec), loop_num),
	   CHREC_RIGHT (chrec));

    default:
      return chrec;
    }
}

/* Returns a univariate function that represents the evolution in
   LOOP_NUM.  Mask the evolution of any other loop.  */

tree 
hide_evolution_in_other_loops_than_loop (tree chrec, 
					 unsigned loop_num)
{
  if (automatically_generated_chrec_p (chrec))
    return chrec;
  
  switch (TREE_CODE (chrec))
    {
    case POLYNOMIAL_CHREC:
      if (CHREC_VARIABLE (chrec) == loop_num)
	return build_polynomial_chrec 
	  (loop_num, 
	   hide_evolution_in_other_loops_than_loop (CHREC_LEFT (chrec), 
						    loop_num), 
	   CHREC_RIGHT (chrec));
      
      else if (CHREC_VARIABLE (chrec) < loop_num)
	/* There is no evolution in this loop.  */
	return initial_condition (chrec);
      
      else
	return hide_evolution_in_other_loops_than_loop (CHREC_LEFT (chrec), 
							loop_num);
      
    case EXPONENTIAL_CHREC:
      if (CHREC_VARIABLE (chrec) == loop_num)
	return build_exponential_chrec 
	  (loop_num,
	   hide_evolution_in_other_loops_than_loop (CHREC_LEFT (chrec), 
						    loop_num),
	   CHREC_RIGHT (chrec));
      
      else if (CHREC_VARIABLE (chrec) < loop_num)
	/* There is no evolution in this loop.  */
	return initial_condition (chrec);
      
      else
	return hide_evolution_in_other_loops_than_loop (CHREC_LEFT (chrec), 
							loop_num);
      
    default:
      return chrec;
    }
}

/* Returns the evolution part in LOOP_NUM.  Example: the call
   get_evolution_in_loop (1, {{0, +, 1}_1, +, 2}_1) returns 
   {1, +, 2}_1  */

tree 
evolution_part_in_loop_num (tree chrec, 
			    unsigned loop_num)
{
  if (automatically_generated_chrec_p (chrec))
    return chrec;
  
  switch (TREE_CODE (chrec))
    {
    case POLYNOMIAL_CHREC:
      if (CHREC_VARIABLE (chrec) == loop_num)
	{
	  if (TREE_CODE (CHREC_LEFT (chrec)) != POLYNOMIAL_CHREC
	      || CHREC_VARIABLE (CHREC_LEFT (chrec)) != CHREC_VARIABLE (chrec))
	    return CHREC_RIGHT (chrec);
	  
	  else
	    return build_polynomial_chrec
	      (loop_num, 
	       evolution_part_in_loop_num (CHREC_LEFT (chrec), loop_num), 
	       CHREC_RIGHT (chrec));
	}
      
      else if (CHREC_VARIABLE (chrec) < loop_num)
	/* There is no evolution part in this loop.  */
	return NULL_TREE;
      
      else
	return evolution_part_in_loop_num (CHREC_LEFT (chrec), loop_num);
      
    case EXPONENTIAL_CHREC:
      if (CHREC_VARIABLE (chrec) == loop_num)
	{
	  if (TREE_CODE (CHREC_LEFT (chrec)) != EXPONENTIAL_CHREC
	      || CHREC_VARIABLE (CHREC_LEFT (chrec)) != CHREC_VARIABLE (chrec))
	    return CHREC_RIGHT (chrec);
	  
	  else
	    return build_exponential_chrec 
	      (loop_num,
	       evolution_part_in_loop_num (CHREC_LEFT (chrec), loop_num),
	       CHREC_RIGHT (chrec));
	}
      
      else if (CHREC_VARIABLE (chrec) < loop_num)
	/* There is no evolution part in this loop.  */
	return NULL_TREE;
      
      else
	return evolution_part_in_loop_num (CHREC_LEFT (chrec), loop_num);
      
    default:
      return NULL_TREE;
    }
}

/* Set or reset the evolution of CHREC to NEW_EVOL in loop LOOP_NUM.
   This function is essentially used for setting the evolution to
   chrec_top, for example after having determined that it is
   impossible to say how many times a loop will execute.  */

tree 
reset_evolution_in_loop (unsigned loop_num,
			 tree chrec, 
			 tree new_evol)
{
  if ((TREE_CODE (chrec) == POLYNOMIAL_CHREC
       || TREE_CODE (chrec) == EXPONENTIAL_CHREC)
      && CHREC_VARIABLE (chrec) > loop_num)
    return build 
      (TREE_CODE (chrec), 
       build_int_2 (CHREC_VARIABLE (chrec), 0), 
       reset_evolution_in_loop (loop_num, CHREC_LEFT (chrec), new_evol), 
       reset_evolution_in_loop (loop_num, CHREC_RIGHT (chrec), new_evol));
  
  while ((TREE_CODE (chrec) == POLYNOMIAL_CHREC
	  || TREE_CODE (chrec) == EXPONENTIAL_CHREC)
	 && CHREC_VARIABLE (chrec) == loop_num)
    chrec = CHREC_LEFT (chrec);
  
  return build_polynomial_chrec (loop_num, chrec, new_evol);
}

/* Determine the type of the result after the merge of types TYPE0 and
   TYPE1.  */

static inline tree 
chrec_merge_types (tree type0, 
		   tree type1)
{
  if (type0 == type1)
    return type0;

  else 
    /* FIXME.  */
    return NULL_TREE;
}

/* Merge the information contained in intervals or scalar constants. 
   merge ([a, b], [c, d])  ->  [min (a, c), max (b, d)],
   merge ([a, b], c)       ->  [min (a, c), max (b, c)],
   merge (a, [b, c])       ->  [min (a, b), max (a, c)],
   merge (a, b)            ->  [min (a, b), max (a, b)].  */

static inline tree 
chrec_merge_intervals (tree chrec1, 
		       tree chrec2)
{
  tree type1 = chrec_type (chrec1);
  tree type2 = chrec_type (chrec2);
  tree type = chrec_merge_types (type1, type2);
  
  if (type == NULL_TREE)
    return chrec_top;
  
  if (TREE_CODE (chrec1) == INTERVAL_CHREC)
    {
      if (TREE_CODE (chrec2) == INTERVAL_CHREC)
	return build_interval_chrec 
	  (tree_fold_min (type, CHREC_LOW (chrec1), CHREC_LOW (chrec2)),
	   tree_fold_max (type, CHREC_UP (chrec1), CHREC_UP (chrec2)));
      else
	return build_interval_chrec 
	  (tree_fold_min (type, CHREC_LOW (chrec1), chrec2),
	   tree_fold_max (type, CHREC_UP (chrec1), chrec2));
    }
  else if (TREE_CODE (chrec2) == INTERVAL_CHREC)
    return build_interval_chrec 
      (tree_fold_min (type, CHREC_LOW (chrec2), chrec1),
       tree_fold_max (type, CHREC_UP (chrec2), chrec1));
  else
    return build_interval_chrec 
      (tree_fold_min (type, chrec1, chrec2),
       tree_fold_max (type, chrec1, chrec2));
}

/* Merges two evolution functions that were found by following two
   alternate paths of a conditional expression.  */

tree
chrec_merge (tree chrec1, 
	     tree chrec2)
{
  tree type = chrec_type (chrec1);

  if (chrec1 == chrec_top
      || chrec2 == chrec_top)
    return chrec_top;

  if (chrec1 == chrec_bot 
      || chrec2 == chrec_bot)
    return chrec_bot;

  if (chrec1 == chrec_not_analyzed_yet)
    return chrec2;
  if (chrec2 == chrec_not_analyzed_yet)
    return chrec1;

  if (operand_equal_p (chrec1, chrec2, 0))
    return chrec1;

  switch (TREE_CODE (chrec1))
    {
    case INTEGER_CST:
    case INTERVAL_CHREC:
      switch (TREE_CODE (chrec2))
	{
	case INTEGER_CST:
	case INTERVAL_CHREC:
	  return chrec_merge_intervals (chrec1, chrec2);
	  
	case POLYNOMIAL_CHREC:
	  return build_polynomial_chrec 
	    (CHREC_VARIABLE (chrec2),
	     chrec_merge (chrec1, CHREC_LEFT (chrec2)),
	     chrec_merge (convert (type, integer_zero_node),
			  CHREC_RIGHT (chrec2)));
	  
	case EXPONENTIAL_CHREC:
	  return build_exponential_chrec 
	    (CHREC_VARIABLE (chrec2),
	     chrec_merge (chrec1, CHREC_LEFT (chrec2)),
	     chrec_merge (convert (type, integer_one_node),
			  CHREC_RIGHT (chrec2)));

	default:
	  return chrec_top;
	}
      
    case POLYNOMIAL_CHREC:
      switch (TREE_CODE (chrec2))
	{
	case INTEGER_CST:
	case INTERVAL_CHREC:
	  return build_polynomial_chrec 
	    (CHREC_VARIABLE (chrec1),
	     chrec_merge (CHREC_LEFT (chrec1), chrec2),
	     chrec_merge (CHREC_RIGHT (chrec1),
			  convert (type, integer_zero_node)));
	  
	case POLYNOMIAL_CHREC:
	  if (CHREC_VARIABLE (chrec1) == CHREC_VARIABLE (chrec2))
	    return build_polynomial_chrec 
	      (CHREC_VARIABLE (chrec2),
	       chrec_merge (CHREC_LEFT (chrec1), CHREC_LEFT (chrec2)),
	       chrec_merge (CHREC_RIGHT (chrec1), CHREC_RIGHT (chrec2)));
	  else if (CHREC_VARIABLE (chrec1) < CHREC_VARIABLE (chrec2))
	    return build_polynomial_chrec 
	      (CHREC_VARIABLE (chrec2),
	       chrec_merge (chrec1, CHREC_LEFT (chrec2)),
	       chrec_merge (convert (type, integer_zero_node),
			    CHREC_RIGHT (chrec2)));
	  else
	    return build_polynomial_chrec 
	      (CHREC_VARIABLE (chrec1),
	       chrec_merge (CHREC_LEFT (chrec1), chrec2),
	       chrec_merge (CHREC_RIGHT (chrec1),
			    convert (type, integer_zero_node)));
	  
	case EXPONENTIAL_CHREC:
	  return chrec_top;
	  
	default:
	  return chrec_top;
	}
      
    case EXPONENTIAL_CHREC:
      return chrec_top;

    default:
      return chrec_top;
    }
}



/* Observers.  */

/* Helper function for is_multivariate_chrec.  */

static bool 
is_multivariate_chrec_rec (tree chrec, unsigned int rec_var)
{
  if (chrec == NULL_TREE)
    return false;
  
  if (TREE_CODE (chrec) == POLYNOMIAL_CHREC
      || TREE_CODE (chrec) == EXPONENTIAL_CHREC)
    {
      if (CHREC_VARIABLE (chrec) != rec_var)
	return true;
      else
	return (is_multivariate_chrec_rec (CHREC_LEFT (chrec), rec_var) 
		|| is_multivariate_chrec_rec (CHREC_RIGHT (chrec), rec_var));
    }
  else
    return false;
}

/* Determine whether the given chrec is multivariate or not.  */

bool 
is_multivariate_chrec (tree chrec)
{
  if (chrec == NULL_TREE)
    return false;
  
  if (TREE_CODE (chrec) == POLYNOMIAL_CHREC
      || TREE_CODE (chrec) == EXPONENTIAL_CHREC)
    return (is_multivariate_chrec_rec (CHREC_LEFT (chrec), 
				       CHREC_VARIABLE (chrec))
	    || is_multivariate_chrec_rec (CHREC_RIGHT (chrec), 
					  CHREC_VARIABLE (chrec)));
  else
    return false;
}

/* Determine whether the given chrec is a polynomial or not.  */

bool
is_pure_sum_chrec (tree chrec)
{
  if (chrec == NULL_TREE)
    return true;
  
  if (TREE_CODE (chrec) == EXPONENTIAL_CHREC)
    return false;
  
  if (TREE_CODE (chrec) == POLYNOMIAL_CHREC)
    return (is_pure_sum_chrec (CHREC_LEFT (chrec))
	    && is_pure_sum_chrec (CHREC_RIGHT (chrec)));
  
  return true;
}

/* Determines whether the chrec contains symbolic names or not.  */

bool 
chrec_contains_symbols (tree chrec)
{
  if (chrec == NULL_TREE)
    return false;
  
  if (TREE_CODE (chrec) == SSA_NAME
      || TREE_CODE (chrec) == VAR_DECL
      || TREE_CODE (chrec) == PARM_DECL
      || TREE_CODE (chrec) == FUNCTION_DECL
      || TREE_CODE (chrec) == LABEL_DECL
      || TREE_CODE (chrec) == RESULT_DECL
      || TREE_CODE (chrec) == FIELD_DECL)
    return true;
  
  switch (TREE_CODE_LENGTH (TREE_CODE (chrec)))
    {
    case 3:
      if (chrec_contains_symbols (TREE_OPERAND (chrec, 2)))
	return true;
      
    case 2:
      if (chrec_contains_symbols (TREE_OPERAND (chrec, 1)))
	return true;
      
    case 1:
      if (chrec_contains_symbols (TREE_OPERAND (chrec, 0)))
	return true;
      
    default:
      return false;
    }
}

/* Determines whether the chrec contains undetermined coefficients.  */

bool 
chrec_contains_undetermined (tree chrec)
{
  if (chrec == chrec_top
      || chrec == chrec_not_analyzed_yet
      || chrec == NULL_TREE)
    return true;
  
  switch (TREE_CODE_LENGTH (TREE_CODE (chrec)))
    {
    case 3:
      if (chrec_contains_undetermined (TREE_OPERAND (chrec, 2)))
	return true;
      
    case 2:
      if (chrec_contains_undetermined (TREE_OPERAND (chrec, 1)))
	return true;
      
    case 1:
      if (chrec_contains_undetermined (TREE_OPERAND (chrec, 0)))
	return true;
      
    default:
      return false;
    }
}

/* Determines whether the chrec contains interval coefficients.  */

bool 
chrec_contains_intervals (tree chrec)
{
  if (chrec == NULL_TREE
      || automatically_generated_chrec_p (chrec))
    return false;
  
  if (TREE_CODE (chrec) == INTERVAL_CHREC)
    return true;
  
  switch (TREE_CODE_LENGTH (TREE_CODE (chrec)))
    {
    case 3:
      if (chrec_contains_intervals (TREE_OPERAND (chrec, 2)))
	return true;
      
    case 2:
      if (chrec_contains_intervals (TREE_OPERAND (chrec, 1)))
	return true;
      
    case 1:
      if (chrec_contains_intervals (TREE_OPERAND (chrec, 0)))
	return true;
      
    default:
      return false;
    }
}

/* Determines whether the tree EXPR contains chrecs.  */

bool
tree_contains_chrecs (tree expr)
{
  if (expr == NULL_TREE)
    return false;
  
  if (tree_is_chrec (expr))
    return true;
  
  switch (TREE_CODE_LENGTH (TREE_CODE (expr)))
    {
    case 3:
      if (tree_contains_chrecs (TREE_OPERAND (expr, 2)))
	return true;
      
    case 2:
      if (tree_contains_chrecs (TREE_OPERAND (expr, 1)))
	return true;
      
    case 1:
      if (tree_contains_chrecs (TREE_OPERAND (expr, 0)))
	return true;
      
    default:
      return false;
    }
}

/* Determine whether the given tree is an affine multivariate
   evolution.  */

bool 
evolution_function_is_affine_multivariate_p (tree chrec)
{
  if (chrec == NULL_TREE)
    return false;
  
  switch (TREE_CODE (chrec))
    {
    case POLYNOMIAL_CHREC:
      if (evolution_function_is_constant_p (CHREC_LEFT (chrec)))
	{
	  if (evolution_function_is_constant_p (CHREC_RIGHT (chrec)))
	    return true;
	  else
	    {
	      if (CHREC_VARIABLE (CHREC_RIGHT (chrec)) 
		  != CHREC_VARIABLE (chrec)
		  && evolution_function_is_affine_multivariate_p 
		  (CHREC_RIGHT (chrec)))
		return true;
	      else
		return false;
	    }
	}
      else
	{
	  if (evolution_function_is_constant_p (CHREC_RIGHT (chrec))
	      && TREE_CODE (CHREC_LEFT (chrec)) == POLYNOMIAL_CHREC
	      && CHREC_VARIABLE (CHREC_LEFT (chrec)) != CHREC_VARIABLE (chrec)
	      && evolution_function_is_affine_multivariate_p 
	      (CHREC_LEFT (chrec)))
	    return true;
	  else
	    return false;
	}
      
      
    case EXPONENTIAL_CHREC:
    case INTERVAL_CHREC:
    default:
      return false;
    }
}

/* Determine whether the given tree is a function in zero or one 
   variables.  */

bool
evolution_function_is_univariate_p (tree chrec)
{
  if (chrec == NULL_TREE)
    return true;
  
  switch (TREE_CODE (chrec))
    {
    case POLYNOMIAL_CHREC:
    case EXPONENTIAL_CHREC:
      switch (TREE_CODE (CHREC_LEFT (chrec)))
	{
	case POLYNOMIAL_CHREC:
	case EXPONENTIAL_CHREC:
	  if (CHREC_VARIABLE (chrec) != CHREC_VARIABLE (CHREC_LEFT (chrec)))
	    return false;
	  if (!evolution_function_is_univariate_p (CHREC_LEFT (chrec)))
	    return false;
	  break;
	  
	default:
	  break;
	}
      
      switch (TREE_CODE (CHREC_RIGHT (chrec)))
	{
	case POLYNOMIAL_CHREC:
	case EXPONENTIAL_CHREC:
	  if (CHREC_VARIABLE (chrec) != CHREC_VARIABLE (CHREC_RIGHT (chrec)))
	    return false;
	  if (!evolution_function_is_univariate_p (CHREC_RIGHT (chrec)))
	    return false;
	  break;
	  
	default:
	  break;	  
	}
      
    default:
      return true;
    }
}



/* Convert the initial condition of chrec to type.  */

tree 
chrec_convert (tree type, 
	       tree chrec)
{
  tree ct;
  
  if (automatically_generated_chrec_p (chrec))
    return chrec;
  
  ct = chrec_type (chrec);
  if (ct == type)
    return chrec;

  if (TYPE_PRECISION (ct) < TYPE_PRECISION (type))
    return count_ev_in_wider_type (type, chrec);

  switch (TREE_CODE (chrec))
    {
    case POLYNOMIAL_CHREC:
      return build_polynomial_chrec (CHREC_VARIABLE (chrec),
				     chrec_convert (type,
						    CHREC_LEFT (chrec)),
				     chrec_convert (type,
						    CHREC_RIGHT (chrec)));

    case EXPONENTIAL_CHREC:
      return build_exponential_chrec (CHREC_VARIABLE (chrec),
				      chrec_convert (type,
						     CHREC_LEFT (chrec)),
				      chrec_convert (type,
						     CHREC_RIGHT (chrec)));
      
    case PEELED_CHREC:
      return build_peeled_chrec 
	(CHREC_VARIABLE (chrec), 
	 chrec_convert (type, CHREC_LEFT (chrec)), 
	 chrec_convert (type, CHREC_RIGHT (chrec)));
      
    case INTERVAL_CHREC:
      return build_interval_chrec
	(chrec_convert (type, CHREC_LOW (chrec)), 
	 chrec_convert (type, CHREC_UP (chrec)));
      
    default:
      {
	tree res = convert (type, chrec);

	/* Don't propagate overflows.  */
	TREE_OVERFLOW (res) = 0;
	if (TREE_CODE_CLASS (TREE_CODE (res)) == 'c')
	  TREE_CONSTANT_OVERFLOW (res) = 0;
	return res;
      }
    }
}

/* Returns the type of the chrec.  */

tree 
chrec_type (tree chrec)
{
  if (automatically_generated_chrec_p (chrec))
    return NULL_TREE;
  
  return TREE_TYPE (chrec);
}

