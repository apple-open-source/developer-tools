#ifndef LAMBDA_H
#define LAMBDA_H

typedef int *lambda_vector;
typedef lambda_vector *lambda_matrix;

/* A transformation matrix.  */
typedef struct
{
  lambda_matrix matrix;
  int rowsize;
  int colsize;
  int denominator;
} *lambda_trans_matrix;
#define LTM_MATRIX(T) ((T)->matrix)
#define LTM_ROWSIZE(T) ((T)->rowsize)
#define LTM_COLSIZE(T) ((T)->colsize)
#define LTM_DENOMINATOR(T) ((T)->denominator)

/* A vector representing a statement in the body of a loop.  */
typedef struct
{
  lambda_vector coefficients;
  int size;
  int denominator;
} *lambda_body_vector;
#define LBV_COEFFICIENTS(T) ((T)->coefficients)
#define LBV_SIZE(T) ((T)->size)
#define LBV_DENOMINATOR(T) ((T)->denominator)

/* Piecewise linear expression.  */
typedef struct lambda_linear_expression_s
{
  lambda_vector coefficients;
  int constant;
  lambda_vector invariant_coefficients;
  int denominator;
  struct lambda_linear_expression_s *next;
} *lambda_linear_expression;

#define LLE_COEFFICIENTS(T) ((T)->coefficients)
#define LLE_CONSTANT(T) ((T)->constant)
#define LLE_INVARIANT_COEFFICIENTS(T) ((T)->invariant_coefficients)
#define LLE_DENOMINATOR(T) ((T)->denominator)
#define LLE_NEXT(T) ((T)->next)

lambda_linear_expression lambda_linear_expression_new (int, int);
void print_lambda_linear_expression (FILE *, lambda_linear_expression, int,
				     int, char);

/* Loop structure.  */
typedef struct lambda_loop_s
{
  lambda_linear_expression lower_bound;
  lambda_linear_expression upper_bound;
  lambda_linear_expression linear_offset;
  int step;
} *lambda_loop;

#define LL_LOWER_BOUND(T) ((T)->lower_bound)
#define LL_UPPER_BOUND(T) ((T)->upper_bound)
#define LL_LINEAR_OFFSET(T) ((T)->linear_offset)
#define LL_STEP(T)   ((T)->step)

/* Loop nest structure.  */
typedef struct
{
  lambda_loop *loops;
  int depth;
  int invariants;
} *lambda_loopnest;

#define LN_LOOPS(T) ((T)->loops)
#define LN_DEPTH(T) ((T)->depth)
#define LN_INVARIANTS(T) ((T)->invariants)

lambda_loopnest lambda_loopnest_new (int, int);
lambda_loopnest lambda_loopnest_transform (lambda_loopnest, lambda_trans_matrix);
void print_lambda_loopnest (FILE *, lambda_loopnest, char);

#define lambda_loop_new() (lambda_loop) ggc_alloc_cleared (sizeof (struct lambda_loop_s))

void print_lambda_loop (FILE *, lambda_loop, int, int, char);

void print_lambda_vector (FILE *, lambda_vector, int);

lambda_matrix lambda_matrix_new (int, int);

void lambda_matrix_id (lambda_matrix, int);
void lambda_matrix_copy (lambda_matrix, lambda_matrix, int, int);
void lambda_matrix_negate (lambda_matrix, lambda_matrix, int, int);
void lambda_matrix_transpose (lambda_matrix, lambda_matrix, int, int);
void lambda_matrix_add (lambda_matrix, lambda_matrix, lambda_matrix, int,
			int);
void lambda_matrix_add_mc (lambda_matrix, int, lambda_matrix, int,
			   lambda_matrix, int, int);
void lambda_matrix_mult (lambda_matrix, lambda_matrix, lambda_matrix,
			 int, int, int);
void lambda_matrix_delete_rows (lambda_matrix, int, int, int);
void lambda_matrix_row_exchange (lambda_matrix, int, int);
void lambda_matrix_row_add (lambda_matrix, int, int, int, int);
void lambda_matrix_row_negate (lambda_matrix mat, int, int);
void lambda_matrix_row_mc (lambda_matrix, int, int, int);
void lambda_matrix_col_exchange (lambda_matrix, int, int, int);
void lambda_matrix_col_add (lambda_matrix, int, int, int, int);
void lambda_matrix_col_negate (lambda_matrix, int, int);
void lambda_matrix_col_mc (lambda_matrix, int, int, int);
int lambda_matrix_inverse (lambda_matrix, lambda_matrix, int);
void lambda_matrix_hermite (lambda_matrix, int, lambda_matrix, lambda_matrix);
int lambda_matrix_first_nz_vec (lambda_matrix, int, int, int);
void lambda_matrix_project_to_null (lambda_matrix, int, int, int, 
				    lambda_vector);
void print_lambda_matrix (FILE *, lambda_matrix, int, int);

lambda_trans_matrix lambda_trans_matrix_new (int, int);
bool lambda_trans_matrix_is_nonsingular (lambda_trans_matrix);
bool lambda_trans_matrix_is_fullrank (lambda_trans_matrix);
int lambda_trans_matrix_rank (lambda_trans_matrix);
lambda_trans_matrix lambda_trans_matrix_base (lambda_trans_matrix);
lambda_trans_matrix lambda_trans_matrix_padding (lambda_trans_matrix);
lambda_trans_matrix lambda_trans_matrix_inverse (lambda_trans_matrix);
void print_lambda_trans_matrix (FILE *, lambda_trans_matrix);
void lambda_matrix_vector_mult (lambda_matrix, int, int, lambda_vector, 
				lambda_vector);

lambda_body_vector lambda_body_vector_new (int);
lambda_body_vector lambda_body_vector_compute_new (lambda_trans_matrix, 
						   lambda_body_vector);
void print_lambda_body_vector (FILE *, lambda_body_vector);
struct loop;
lambda_loopnest gcc_loopnest_to_lambda_loopnest (struct loop *,
						 varray_type *,
						 varray_type *);
void lambda_loopnest_to_gcc_loopnest (struct loop *, varray_type,
				      varray_type,
				      lambda_loopnest, 
				      lambda_trans_matrix);


static inline void lambda_vector_negate (lambda_vector, lambda_vector, int);
static inline void lambda_vector_mult_const (lambda_vector, lambda_vector, int, int);
static inline void lambda_vector_add (lambda_vector, lambda_vector,
				      lambda_vector, int);
static inline void lambda_vector_add_mc (lambda_vector, int, lambda_vector, int,
					 lambda_vector, int);
static inline void lambda_vector_copy (lambda_vector, lambda_vector, int);
static inline bool lambda_vector_zerop (lambda_vector, int);
static inline void lambda_vector_clear (lambda_vector, int);
static inline bool lambda_vector_equal (lambda_vector, lambda_vector, int);
static inline int lambda_vector_min_nz (lambda_vector, int, int);
static inline int lambda_vector_first_nz (lambda_vector, int, int);

/* Allocate a new vector of given SIZE.  */

static inline lambda_vector
lambda_vector_new (int size)
{
  return ggc_alloc_cleared (size * sizeof(int));
}

/* Negate vector VEC1 with length SIZE and store it in VEC2.  */

static inline void 
lambda_vector_negate (lambda_vector vec1, lambda_vector vec2,
		      int size)
{
  int i;

  for (i = 0; i < size; i++)
    if (vec1[i] != 0)
      vec2[i] = (-1) * vec1[i];
}

/* Multiply vector VEC1 of length SIZE by a constant CONST1,
   and store the result in VEC2.  */

static inline void
lambda_vector_mult_const (lambda_vector vec1, lambda_vector vec2,
			  int size, int const1)
{
  int i;

  if (const1 == 0)
    lambda_vector_clear (vec2, size);
  else
    for (i = 0; i < size; i++)
      vec2[i] = const1 * vec1[i];
}

/* VEC3 = VEC1+VEC2, where all three the vectors are of length SIZE.  */

static inline void
lambda_vector_add (lambda_vector vec1, lambda_vector vec2,
		   lambda_vector vec3, int size)
{
  int i;
  for (i = 0; i < size; i++)
    vec3[i] = vec1[i] + vec2[i];
}

/* VEC3 = CONSTANT1*VEC1 + CONSTANT2*VEC2.  All vectors have length SIZE.  */

static inline void
lambda_vector_add_mc (lambda_vector vec1, int const1,
		      lambda_vector vec2, int const2,
		      lambda_vector vec3, int size)
{
  int i;
  for (i = 0; i < size; i++)
    vec3[i] = const1 * vec1[i] + const2 * vec2[i];
}

/* Copy the elements of vector VEC1 with length SIZE to VEC2.  */

static inline void
lambda_vector_copy (lambda_vector vec1, lambda_vector vec2,
		    int size)
{
  memcpy (vec2, vec1, size * sizeof (int));
}

/* Return true if vector VEC1 of length SIZE is the zero vector.  */

static inline bool 
lambda_vector_zerop (lambda_vector vec1, int size)
{
  int i;
  for (i = 0; i < size; i++)
    if (vec1[i] != 0)
      return false;
  return true;
}

/* Clear out vector VEC1 of length SIZE.  */

static inline void
lambda_vector_clear (lambda_vector vec1, int size)
{
  memset (vec1, 0, size * sizeof (int));
}

/* Return true if two vectors are equal.  */
 
static inline bool
lambda_vector_equal (lambda_vector vec1, lambda_vector vec2, int size)
{
  int i;
  for (i = 0; i < size; i++)
    if (vec1[i] != vec2[i])
      return false;
  return true;
}

/* Return the minimum non-zero element in vector VEC1 between START and N.
   We must have START <= N.  */

static inline int
lambda_vector_min_nz (lambda_vector vec1, int n, int start)
{
  int j;
  int min = -1;
  
  for (j = start; j < n; j++)
    {
      if (vec1[j])
	if (min < 0 || vec1[j] < vec1[min])
	  min= j;
    }

  if (min < 0)
    abort ();

  return min;
}

/* Return the first nonzero element of vector VEC1 between START and N.
   We must have START <= N.   Returns N if VEC1 is the zero vector.  */

static inline int
lambda_vector_first_nz (lambda_vector vec1, int n, int start)
{
  int j;
  for (j = start; j < n && vec1[j] == 0; j++) /* Nothing.  */ ;
  return j;
}


/* Multiply a vector by a matrix.  */

static inline void
lambda_vector_matrix_mult (lambda_vector vect, int m, lambda_matrix mat, 
			   int n, lambda_vector dest)
{
  int i, j;
  memset (dest, 0, n * sizeof (int));
  for (i = 0; i < n; i++)
    for (j = 0; j < m; j++)
      dest[i] += mat[j][i] * vect[j];
}


#endif /* LAMBDA_H  */

