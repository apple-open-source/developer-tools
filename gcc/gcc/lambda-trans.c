
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "errors.h"
#include "ggc.h"
#include "tree.h"
#include "target.h"
#include "varray.h"
#include "lambda.h"

/* Allocate a new transformation matrix.  */

lambda_trans_matrix
lambda_trans_matrix_new (int colsize, int rowsize)
{
  lambda_trans_matrix ret;
  
  ret = xcalloc (1, sizeof (*ret));
  LTM_MATRIX (ret) = lambda_matrix_new (rowsize, colsize);
  LTM_ROWSIZE (ret) = rowsize;
  LTM_COLSIZE (ret) = colsize;
  LTM_DENOMINATOR (ret) = 1;
  return ret;
}

/* Return true if the transformation matrix is nonsingular.  */

bool
lambda_trans_matrix_is_nonsingular (lambda_trans_matrix t)
{
  return lambda_trans_matrix_is_fullrank (t);
}


/* Return true if the transformation matrix is full row rank.  */

bool
lambda_trans_matrix_is_fullrank (lambda_trans_matrix t)
{
  return (lambda_trans_matrix_rank (t) == LTM_ROWSIZE (t));
}

/* Compute the rank of the matrix.  */

int
lambda_trans_matrix_rank (lambda_trans_matrix t)
{
  lambda_matrix partial;
  int rowsize, colsize;
  int i, j, nextrow;
  
  lambda_matrix tempmatrix;
  lambda_vector row;
  int minimum_column, factor;
  
  partial = LTM_MATRIX (t);
  rowsize = LTM_ROWSIZE (t);
  colsize = LTM_COLSIZE (t);
  
  tempmatrix = lambda_matrix_new (rowsize, colsize);
  lambda_matrix_copy (partial, tempmatrix, rowsize, colsize);
  
  j = 0;
  while ((j < colsize) && (j < rowsize))
    {
      /* Considering the submatrix A[j:m, j:n], search for the first
	 row k >= k such that A[k, j:n] != 0 */
      
      nextrow = lambda_matrix_first_nz_vec (tempmatrix, rowsize, colsize, j);
      
      if (nextrow != j)
	return j;
      
      /* Delete rows j .. nextrow - 1 */

      lambda_matrix_delete_rows (tempmatrix, rowsize, j, nextrow);
      lambda_matrix_delete_rows (partial, rowsize, j, nextrow);
      
      rowsize = rowsize - nextrow + j;

      /* Nextrow becomes row j+1 in the matrix, but not necessary row
	 j + 1 in the array.  */
      
      /* Apply elementary column operations to make the diagonal
	 element non-zero and the others zero.  */
      
      row = tempmatrix[j];

      /* Make every element of tempmatrix[j, j:colsize] positive.  */

      for (i = j; i < colsize; i++)
	if (row[i] < 0)
	  lambda_matrix_col_negate (tempmatrix, rowsize, i);

      /* Stop only when the diagonal element is non-zero.  */
      while (lambda_vector_first_nz (row, colsize, j+1) < colsize)
	{
	  minimum_column = lambda_vector_min_nz (row, colsize, j);
	  lambda_matrix_col_exchange (tempmatrix, rowsize, j, minimum_column);
	  
	  for (i = j + 1; i < colsize; i++)
	    {
	      if (row[i])
		{
		  factor = row[i] / row[j];
		  /* Add (-1) * factor of column j to column i.  */
		  lambda_matrix_col_add (tempmatrix, rowsize, 
					 j, i, (-1) * factor);
		}
	    }
	}
      j++;
    }

  return rowsize;
}


/* Compute the base matrix.  */

lambda_trans_matrix
lambda_trans_matrix_base (lambda_trans_matrix mat)
{
  int rowsize, colsize;
  int i, j, nextrow;
  lambda_matrix partial, tempmatrix;
  lambda_vector row;
  int minimum_column, factor;
  
  lambda_trans_matrix base;
  
  rowsize = LTM_ROWSIZE (mat);
  colsize = LTM_COLSIZE (mat);
  base = lambda_trans_matrix_new (rowsize, colsize);
  partial = LTM_MATRIX (base);
  lambda_matrix_copy (LTM_MATRIX (mat), partial, rowsize, colsize);
  tempmatrix = lambda_matrix_new (rowsize, colsize);
  lambda_matrix_copy (partial, tempmatrix, rowsize, colsize);

  j = 0;
  
  while ((j < colsize) 
	 && (nextrow = lambda_matrix_first_nz_vec (tempmatrix, 
						   rowsize, 
						   colsize, j)) < rowsize)
    {
      /* Consider the submatrix A[j:m, j:n].
	 Search for the first row k >= j such that A[k, j:n] != 0.  */
      
      /* Delete rows j .. nextrow - 1.  */
      lambda_matrix_delete_rows (tempmatrix, rowsize, j, nextrow);
      lambda_matrix_delete_rows (partial, rowsize, j, nextrow);

      /* nextrow becomes row j+1 in the matrix, though not necessarily
	 row j+1 in the array.  */
      /* Apply elementary column oeprations to make the diagonal
	 element nonzero and the others zero.  */
      row = tempmatrix[j];

      /* Make every element of tempmatrix[j, j:colsize] positive.  */
      
      for (i = j; i < colsize; i++)
	if (row[i] < 0)
	  lambda_matrix_col_negate (tempmatrix, rowsize, i);
      
      /* Stop when only the diagonal element is nonzero.  */
      
      while (lambda_vector_first_nz (row, colsize, j+1) < colsize)
	{
	  minimum_column = lambda_vector_min_nz (row, colsize, j);
	  lambda_matrix_col_exchange (tempmatrix, rowsize, j, minimum_column);
	  
	  for (i = j + 1; i < colsize; i++)
	    {
	      if (row[i])
		{
		  factor = row[i] / row[j];
		  /* Add (-1) * factor of column j to column i.  */
		  lambda_matrix_col_add (tempmatrix, rowsize, 
					 j, i, (-1) * factor);
		}
	    }
	}
      j++;
    }
  /* Store the rank.  */
  LTM_ROWSIZE (base) = j;
  return base;
}

/* Pad the legal base matrix to an invertable matrix.  */

lambda_trans_matrix
lambda_trans_matrix_padding (lambda_trans_matrix matrix)
{
  int i, k;
  int currrow, minimum_column, factor;

  lambda_matrix tempmatrix, padmatrix;
  lambda_vector row;

  lambda_trans_matrix padded;
  lambda_matrix partial;
  int rowsize, colsize;
  
  rowsize = LTM_ROWSIZE (matrix);
  colsize = LTM_COLSIZE (matrix);
  
  padded = lambda_trans_matrix_new (rowsize, colsize);
  partial = LTM_MATRIX (padded);
  lambda_matrix_copy(LTM_MATRIX (matrix), partial, rowsize, colsize);

  /* full rank, no need for padding */
  if (rowsize==colsize)
    return(padded);

  tempmatrix = lambda_matrix_new (rowsize, colsize);
  lambda_matrix_copy (partial, tempmatrix, rowsize, colsize);

  padmatrix = lambda_matrix_new (colsize, colsize);
  lambda_matrix_id (padmatrix, colsize);

  for(currrow = 0; currrow < rowsize; currrow++)
    {
      /* consider the submatrix A[i:m, i:n].  */

      /* apply elementary column operations to make the diag element nonzero 
	 and others zero.  */

      /* only consider columns from currrow to colsize.  */
      
      row = tempmatrix[currrow];

      /* make every element of tempmatrix[currrow, currrow:colsize]
	 positive. */

      for(i = currrow; i < colsize; i++)
	if(row[i] < 0)
	  lambda_matrix_col_negate (tempmatrix, rowsize, i);

      /* stop when only the diagonal element is nonzero  */
      while (lambda_vector_first_nz (row, colsize, currrow + 1) < colsize)
	{
	  minimum_column = lambda_vector_min_nz (row, colsize, currrow);
	  
	  lambda_matrix_col_exchange (tempmatrix, rowsize, currrow, 
				      minimum_column);
	  lambda_matrix_row_exchange (padmatrix, currrow, minimum_column);

	  for (i = currrow + 1; i < colsize; i++)
	    {
	      if(row[i])
		{
		  factor = row[i] / row[currrow];
		  lambda_matrix_col_add (tempmatrix, rowsize, 
					 currrow, i, (-1) * factor);
		}
	    }
	}
    }


  for(k = rowsize; k < colsize; k++)
    partial[k] = padmatrix[k];

  return(padded);
}

/* Compute the inverse of the transformation.  */

lambda_trans_matrix 
lambda_trans_matrix_inverse (lambda_trans_matrix mat)
{
  lambda_trans_matrix inverse;
  int determinant;
  
  inverse = lambda_trans_matrix_new (LTM_ROWSIZE (mat), LTM_COLSIZE (mat));
  determinant = lambda_matrix_inverse (LTM_MATRIX (mat), LTM_MATRIX (inverse), 
				       LTM_ROWSIZE (mat));
  LTM_DENOMINATOR (inverse) = determinant;
  return inverse;
}


/* Print out a transformation matrix.  */

void
print_lambda_trans_matrix (FILE *outfile, lambda_trans_matrix mat)
{
  print_lambda_matrix (outfile, LTM_MATRIX (mat), LTM_ROWSIZE (mat), 
		       LTM_COLSIZE (mat));
}
