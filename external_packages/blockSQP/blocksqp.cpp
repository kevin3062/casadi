/*
 * blockSQP -- Sequential quadratic programming for problems with
 *             block-diagonal Hessian matrix.
 * Copyright (C) 2012-2015 by Dennis Janka <dennis.janka@iwr.uni-heidelberg.de>
 *
 * Licensed under the zlib license. See LICENSE for more details.
 */

/**
 * \file blocksqp.cpp
 * \author Dennis Janka, Joel Andersson
 * \date 2012-2015, 2016
 *
 */


#include "blocksqp.hpp"

// LAPACK routines
extern "C" {
  void dsyev_( char *jobz, char *uplo, int *n, double *a, int *lda,
               double *w, double *work, int *lwork, int *info,
               int strlen_jobz, int strlen_uplo );

  void dspev_( char *jobz, char *uplo, int *n, double *ap, double *w, double *z, int *ldz,
               double *work, int *info, int strlen_jobz, int strlen_uplo );

  void dgetrf_( int *m, int *n, double *a, int *lda, int *ipiv, int *info );

  void dgetri_( int *n, double *a, int *lda,
                int *ipiv, double *work, int *lwork, int *info );
}

namespace blocksqp {


  /**
   * Compute the inverse of a matrix
   * using LU decomposition (DGETRF and DGETRI)
   */
  int inverse( const Matrix &A, Matrix &Ainv ) {
    int i, j;
    int n, ldim, lwork, info = 0;
    int *ipiv;
    double *work;

    for (i=0; i<A.N(); i++ )
      for (j=0; j<A.M(); j++ )
        Ainv( j,i ) = A( j,i );

    n = Ainv.N();
    ldim = Ainv.LDIM();
    ipiv = new int[n];
    lwork = n*n;
    work = new double[lwork];

    // Compute LU factorization
    dgetrf_( &n, &n, Ainv.ARRAY(), &ldim, ipiv, &info );
    if ( info != 0 )
      printf( "WARNING: DGETRF returned info=%i\n", info );
    // Compute inverse
    dgetri_( &n, Ainv.ARRAY(), &ldim, ipiv, work, &lwork, &info );
    if ( info != 0 )
      printf( "WARNING: DGETRI returned info=%i\n", info );

    return info;
  }

  /**
   * Compute eigenvalues of a symmetric matrix by DSPEV
   */
  int calcEigenvalues( const SymMatrix &B, Matrix &ev ) {
    int n;
    SymMatrix temp;
    double *work, *dummy = 0;
    int info, iDummy = 1;

    n = B.M();
    ev.Dimension( n ).Initialize( 0.0 );
    work = new double[3*n];

    // copy Matrix, will be overwritten
    temp = SymMatrix( B );

    // DSPEV computes all the eigenvalues and, optionally, eigenvectors of a
    // real symmetric matrix A in packed storage.
    dspev_( const_cast<char*>("N"), const_cast<char*>("L"), &n, temp.ARRAY(), ev.ARRAY(), dummy, &iDummy,
            work, &info, strlen("N"), strlen("L") );

    delete[] work;

    return info;
  }

  /**
   * Estimate the smalles eigenvalue of a sqare matrix
   * with the help og Gershgorin's circle theorem
   */
  double estimateSmallestEigenvalue( const Matrix &B )
  {
    int i, j;
    double radius;
    int dim = B.M();
    double lambdaMin = 0.0;

    // For each row, sum up off-diagonal elements
    for (i=0; i<dim; i++ )
      {
        radius = 0.0;
        for (j=0; j<dim; j++ )
          if (j != i )
            radius += fabs( B( i,j ) );

        if (B( i,i ) - radius < lambdaMin )
          lambdaMin = B( i,i ) - radius;
      }

    return lambdaMin;
  }


  /**
   * Compute scalar product aTb
   */
  double adotb( const Matrix &a, const Matrix &b ) {
    double norm = 0.0;

    if (a.N() != 1 || b.N() != 1 )
      {
        printf("a or b is not a vector!\n");
      }
    else if (a.M() != b.M() )
      {
        printf("a and b must have the same dimension!\n");
      }
    else
      {
        for (int k=0; k<a.M(); k++ )
          norm += a(k) * b(k);
      }

    return norm;
  }

  /**
   * Compute the matrix vector product for a column-compressed sparse matrix A with a vector b and store it in result
   */
  void Atimesb( double *Anz, int *AIndRow, int *AIndCol, const Matrix &b, Matrix &result ) {
    int nCol = b.M();
    int nRow = result.M();
    int i, k;

    for (i=0; i<nRow; i++ )
      result( i ) = 0.0;

    for (i=0; i<nCol; i++ )
      {
        // k runs over all elements in one column
        for (k=AIndCol[i]; k<AIndCol[i+1]; k++ )
          result( AIndRow[k] ) += Anz[k] * b( i );
      }

  }

  /**
   * Compute the matrix vector product A*b and store it in result
   */
  void Atimesb( const Matrix &A, const Matrix &b, Matrix &result ) {
    result.Initialize( 0.0 );
    for (int i=0; i<A.M(); i++ )
      for (int k=0; k<A.N(); k++ )
        result( i ) += A( i, k ) * b( k );
  }

  double l1VectorNorm( const Matrix &v ) {
    double norm = 0.0;

    if (v.N() != 1 )
      {
        printf("v is not a vector!\n");
      }
    else
      {
        for (int k=0; k<v.M(); k++ )
          norm += fabs(v( k ));
      }

    return norm;
  }

  double l2VectorNorm( const Matrix &v ) {
    double norm = 0.0;

    if (v.N() != 1 )
      {
        printf("v is not a vector!\n");
      }
    else
      {
        for (int k=0; k<v.M(); k++ )
          norm += v( k )* v( k );
      }

    return sqrt(norm);
  }

  double lInfVectorNorm( const Matrix &v ) {
    double norm = 0.0;

    if (v.N() != 1 )
      {
        printf("v is not a vector!\n");
      }
    else
      {
        for (int k=0; k<v.M(); k++ )
          if (fabs(v( k )) > norm )
            norm = fabs(v( k ));
      }

    return norm;
  }


  /**
   * Calculate weighted l1 norm of constraint violations
   */
  double l1ConstraintNorm( const Matrix &xi, const Matrix &constr, const Matrix &bu, const Matrix &bl, const Matrix &weights ) {
    double norm = 0.0;
    int i;
    int nVar = xi.M();

    if (weights.M() < constr.M() + nVar )
      {
        printf("Weight vector too short!\n");
        return 0.0;
      }

    // Weighted violation of simple bounds
    for (i=0; i<nVar; i++ )
      {
        if (xi( i ) > bu( i ) )
          norm += fabs(weights( i )) * (xi( i ) - bu( i ));
        else if (xi( i ) < bl( i ) )
          norm += fabs(weights( i )) * (bl( i ) - xi( i ));
      }

    // Calculate weighted sum of constraint violations
    for (i=0; i<constr.M(); i++ )
      {
        if (constr( i ) > bu( nVar+i ) )
          norm += fabs(weights( nVar+i )) * (constr( i ) - bu( nVar+i ));
        else if (constr( i ) < bl( nVar+i ) )
          norm += fabs(weights( nVar+i )) * (bl( nVar+i ) - constr( i ));
      }

    return norm;
  }


  /**
   * Calculate l1 norm of constraint violations
   */
  double l1ConstraintNorm( const Matrix &xi, const Matrix &constr, const Matrix &bu, const Matrix &bl ) {
    double norm = 0.0;
    int i;
    int nVar = xi.M();

    // Violation of simple bounds
    for (i=0; i<nVar; i++ )
      {
        if (xi( i ) > bu( i ) )
          norm += xi( i ) - bu( i );
        else if (xi( i ) < bl( i ) )
          norm += bl( i ) - xi( i );
      }

    // Calculate sum of constraint violations
    for (i=0; i<constr.M(); i++ )
      {
        if (constr( i ) > bu( nVar+i ) )
          norm += constr( i ) - bu( nVar+i );
        else if (constr( i ) < bl( nVar+i ) )
          norm += bl( nVar+i ) - constr( i );
      }

    return norm;
  }


  /**
   * Calculate l2 norm of constraint violations
   */
  double l2ConstraintNorm( const Matrix &xi, const Matrix &constr, const Matrix &bu, const Matrix &bl ) {
    double norm = 0.0;
    int i;
    int nVar = xi.M();

    // Violation of simple bounds
    for (i=0; i<nVar; i++ )
      if (xi( i ) > bu( i ) )
        norm += xi( i ) - bu( i );
    if (xi( i ) < bl( i ) )
      norm += bl( i ) - xi( i );

    // Calculate sum of constraint violations
    for (i=0; i<constr.M(); i++ )
      if (constr( i ) > bu( nVar+i ) )
        norm += pow(constr( i ) - bu( nVar+i ), 2);
      else if (constr( i ) < bl( nVar+i ) )
        norm += pow(bl( nVar+i ) - constr( i ), 2);

    return sqrt(norm);
  }


  /**
   * Calculate l_Infinity norm of constraint violations
   */
  double lInfConstraintNorm( const Matrix &xi, const Matrix &constr, const Matrix &bu, const Matrix &bl ) {
    double norm = 0.0;
    int i;
    int nVar = xi.M();
    int nCon = constr.M();

    // Violation of simple bounds
    for (i=0; i<nVar; i++ ) {
      if (xi( i ) - bu( i ) > norm )
        norm = xi( i ) - bu( i );
      else if (bl( i ) - xi( i ) > norm )
        norm = bl( i ) - xi( i );
    }

    // Find out the largest constraint violation
    for (i=0; i<nCon; i++ ) {
      if (constr( i ) - bu( nVar+i ) > norm )
        norm = constr( i ) - bu( nVar+i );
      if (bl( nVar+i ) - constr( i ) > norm )
        norm = bl( nVar+i ) - constr( i );
    }

    return norm;
  }

  SQPiterate::SQPiterate( Problemspec* prob, SQPoptions* param, bool full ) {
    int maxblocksize = 1;

    // Set nBlocks structure according to if we use block updates or not
    if (param->blockHess == 0 || prob->nBlocks == 1 ) {
      nBlocks = 1;
      blockIdx = new int[2];
      blockIdx[0] = 0;
      blockIdx[1] = prob->nVar;
      maxblocksize = prob->nVar;
      param->whichSecondDerv = 0;
    } else if (param->blockHess == 2 && prob->nBlocks > 1 ) {
      // hybrid strategy: 1 block for constraints, 1 for objective
      nBlocks = 2;
      blockIdx = new int[3];
      blockIdx[0] = 0;
      blockIdx[1] = prob->blockIdx[prob->nBlocks-1];
      blockIdx[2] = prob->nVar;
    } else {
      nBlocks = prob->nBlocks;
      blockIdx = new int[nBlocks+1];
      for (int k=0; k<nBlocks+1; k++ ) {
        blockIdx[k] = prob->blockIdx[k];
        if (k > 0 )
          if (blockIdx[k] - blockIdx[k-1] > maxblocksize )
            maxblocksize = blockIdx[k] - blockIdx[k-1];
      }
    }

    if (param->hessLimMem && param->hessMemsize == 0 )
      param->hessMemsize = maxblocksize;

    allocMin( prob );

    if (!param->sparseQP ) {
      constrJac.Dimension( prob->nCon, prob->nVar ).Initialize( 0.0 );
      hessNz = new double[prob->nVar*prob->nVar];
    } else {
      hessNz = 0;
    }

    jacNz = 0;
    jacIndCol = 0;
    jacIndRow = 0;

    hessIndCol = 0;
    hessIndRow = 0;
    hessIndLo = 0;
    hess = 0;
    hess1 = 0;
    hess2 = 0;

    noUpdateCounter = 0;

    if (full ) {
      allocHess( param );
      allocAlg( prob, param );
    }
  }


  SQPiterate::SQPiterate( const SQPiterate &iter ) {
    int i;

    nBlocks = iter.nBlocks;
    blockIdx = new int[nBlocks+1];
    for (i=0; i<nBlocks+1; i++ )
      blockIdx[i] = iter.blockIdx[i];

    xi = iter.xi;
    lambda = iter.lambda;
    constr = iter.constr;
    gradObj = iter.gradObj;
    gradLagrange = iter.gradLagrange;

    constrJac = iter.constrJac;
    if (iter.jacNz != 0 ) {
      int nVar = xi.M();
      int nnz = iter.jacIndCol[nVar];

      jacNz = new double[nnz];
      for (i=0; i<nnz; i++ )
        jacNz[i] = iter.jacNz[i];

      jacIndRow = new int[nnz + (nVar+1) + nVar];
      for (i=0; i<nnz + (nVar+1) + nVar; i++ )
        jacIndRow[i] = iter.jacIndRow[i];
      jacIndCol = jacIndRow + nnz;
    } else {
      jacNz = 0;
      jacIndRow = 0;
      jacIndCol = 0;
    }

    noUpdateCounter = 0;
    hessNz = 0;
    hessIndCol = 0;
    hessIndRow = 0;
    hessIndLo = 0;
    hess = 0;
    hess1 = 0;
    hess2 = 0;
  }


  /**
   * Allocate memory for variables
   * required by all optimization
   * algorithms except for the Jacobian
   */
  void SQPiterate::allocMin( Problemspec *prob ) {
    // current iterate
    xi.Dimension( prob->nVar ).Initialize( 0.0 );

    // dual variables (for general constraints and variable bounds)
    lambda.Dimension( prob->nVar + prob->nCon ).Initialize( 0.0 );

    // constraint vector with lower and upper bounds
    // (Box constraints are not included in the constraint list)
    constr.Dimension( prob->nCon ).Initialize( 0.0 );

    // gradient of objective
    gradObj.Dimension( prob->nVar ).Initialize( 0.0 );

    // gradient of Lagrangian
    gradLagrange.Dimension( prob->nVar ).Initialize( 0.0 );
  }


  void SQPiterate::allocHess( SQPoptions *param ) {
    int iBlock, varDim;

    // Create one Matrix for one diagonal block in the Hessian
    hess1 = new SymMatrix[nBlocks];
    for (iBlock=0; iBlock<nBlocks; iBlock++ )
      {
        varDim = blockIdx[iBlock+1] - blockIdx[iBlock];
        hess1[iBlock].Dimension( varDim ).Initialize( 0.0 );
      }

    // For SR1 or finite differences, maintain two Hessians
    if (param->hessUpdate == 1 || param->hessUpdate == 4 ) {
      hess2 = new SymMatrix[nBlocks];
      for (iBlock=0; iBlock<nBlocks; iBlock++ ) {
        varDim = blockIdx[iBlock+1] - blockIdx[iBlock];
        hess2[iBlock].Dimension( varDim ).Initialize( 0.0 );
      }
    }

    // Set Hessian pointer
    hess = hess1;
  }

  /**
   * Convert diagonal block Hessian to double array.
   * Assumes that hessNz is already allocated.
   */
  void SQPiterate::convertHessian( Problemspec *prob, double eps, SymMatrix *&hess_ ) {
    if (hessNz == 0 ) return;
    int count = 0;
    int blockCnt = 0;
    for (int i=0; i<prob->nVar; i++ )
      for (int j=0; j<prob->nVar; j++ )
        {
          if (i == blockIdx[blockCnt+1] )
            blockCnt++;
          if (j >= blockIdx[blockCnt] && j < blockIdx[blockCnt+1] )
            hessNz[count++] = hess[blockCnt]( i - blockIdx[blockCnt], j - blockIdx[blockCnt] );
          else
            hessNz[count++] = 0.0;
        }
  }

  /**
   * Convert array *hess to a single symmetric sparse matrix in
   * Harwell-Boeing format (as used by qpOASES)
   */
  void SQPiterate::convertHessian( Problemspec *prob, double eps, SymMatrix *&hess_,
                                   double *&hessNz_, int *&hessIndRow_, int *&hessIndCol_, int *&hessIndLo_ ) {
    int iBlock, count, colCountTotal, rowOffset, i, j;
    int nnz, nCols, nRows;

    // 1) count nonzero elements
    nnz = 0;
    for (iBlock=0; iBlock<nBlocks; iBlock++ )
      for (i=0; i<hess_[iBlock].N(); i++ )
        for (j=i; j<hess_[iBlock].N(); j++ )
          if (fabs(hess_[iBlock]( i,j )) > eps ) {
            nnz++;
            if (i != j ) {
              // off-diagonal elements count twice
              nnz++;
            }
          }

    if (hessNz_ != 0 ) delete[] hessNz_;
    if (hessIndRow_ != 0 ) delete[] hessIndRow_;

    hessNz_ = new double[nnz];
    hessIndRow_ = new int[nnz + (prob->nVar+1) + prob->nVar];
    hessIndCol_ = hessIndRow_ + nnz;
    hessIndLo_ = hessIndCol_ + (prob->nVar+1);

    // 2) store matrix entries columnwise in hessNz
    count = 0; // runs over all nonzero elements
    colCountTotal = 0; // keep track of position in large matrix
    rowOffset = 0;
    for (iBlock=0; iBlock<nBlocks; iBlock++ ) {
      nCols = hess_[iBlock].N();
      nRows = hess_[iBlock].M();

      for (i=0; i<nCols; i++ ) {
        // column 'colCountTotal' starts at element 'count'
        hessIndCol_[colCountTotal] = count;

        for (j=0; j<nRows; j++ )
          if (fabs(hess_[iBlock]( i,j )) > eps )
            {
              hessNz_[count] = hess_[iBlock]( i, j );
              hessIndRow_[count] = j + rowOffset;
              count++;
            }
        colCountTotal++;
      }

      rowOffset += nRows;
    }
    hessIndCol_[colCountTotal] = count;

    // 3) Set reference to lower triangular matrix
    for (j=0; j<prob->nVar; j++ ) {
      for (i=hessIndCol_[j]; i<hessIndCol_[j+1] && hessIndRow_[i]<j; i++);
      hessIndLo_[j] = i;
    }

    if (count != nnz )
      printf( "Error in convertHessian: %i elements processed, should be %i elements!\n", count, nnz );
  }


  /**
   * Allocate memory for additional variables
   * needed by the algorithm
   */
  void SQPiterate::allocAlg( Problemspec *prob, SQPoptions *param ) {
    int iBlock;
    int nVar = prob->nVar;
    int nCon = prob->nCon;

    // current step
    deltaMat.Dimension( nVar, param->hessMemsize, nVar ).Initialize( 0.0 );
    deltaXi.Submatrix( deltaMat, nVar, 1, 0, 0 );
    // trial step (temporary variable, for line search)
    trialXi.Dimension( nVar, 1, nVar ).Initialize( 0.0 );

    // bounds for step (QP subproblem)
    deltaBl.Dimension( nVar+nCon ).Initialize( 0.0 );
    deltaBu.Dimension( nVar+nCon ).Initialize( 0.0 );

    // product of constraint Jacobian with step (deltaXi)
    AdeltaXi.Dimension( nCon ).Initialize( 0.0 );

    // dual variables of QP (simple bounds and general constraints)
    lambdaQP.Dimension( nVar+nCon ).Initialize( 0.0 );

    // line search parameters
    deltaH.Dimension( nBlocks ).Initialize( 0.0 );

    // filter as a set of pairs
    filter = new std::set< std::pair<double,double> >;

    // difference of Lagrangian gradients
    gammaMat.Dimension( nVar, param->hessMemsize, nVar ).Initialize( 0.0 );
    gamma.Submatrix( gammaMat, nVar, 1, 0, 0 );

    // Scalars that are used in various Hessian update procedures
    noUpdateCounter = new int[nBlocks];
    for (iBlock=0; iBlock<nBlocks; iBlock++ )
      noUpdateCounter[iBlock] = -1;

    // For selective sizing: for each block save sTs, sTs_, sTy, sTy_
    deltaNorm.Dimension( nBlocks ).Initialize( 1.0 );
    deltaNormOld.Dimension( nBlocks ).Initialize( 1.0 );
    deltaGamma.Dimension( nBlocks ).Initialize( 0.0 );
    deltaGammaOld.Dimension( nBlocks ).Initialize( 0.0 );
  }


  void SQPiterate::initIterate( SQPoptions* param ) {
    alpha = 1.0;
    nSOCS = 0;
    reducedStepCount = 0;
    steptype = 0;

    obj = param->inf;
    tol = param->inf;
    cNorm = param->thetaMax;
    gradNorm = param->inf;
    lambdaStepNorm = 0.0;
  }

  SQPiterate::~SQPiterate( void ) {
    if (blockIdx != 0 )
      delete[] blockIdx;
    if (noUpdateCounter != 0 )
      delete[] noUpdateCounter;
    if (jacNz != 0 )
      delete[] jacNz;
    if (jacIndRow != 0 )
      delete[] jacIndRow;
    if (hessNz != 0 )
      delete[] hessNz;
    if (hessIndRow != 0 )
      delete[] hessIndRow;
  }

  void Error( const char *F ) {
    printf("Error: %s\n", F );
    //exit( 1 );
  }

  /* ----------------------------------------------------------------------- */

  int Matrix::malloc( void ) {
    int len;

    if ( tflag )
      Error("malloc cannot be called with Submatrix");

    if ( ldim < m )
      ldim = m;

    len = ldim*n;

    if ( len == 0 )
      array = 0;
    else
      if ( ( array = new double[len] ) == 0 )
        Error("'new' failed");

    return 0;
  }


  int Matrix::free( void ) {
    if ( tflag )
      Error("free cannot be called with Submatrix");

    if ( array != 0 )
      delete[] array;

    return 0;
  }


  double &Matrix::operator()( int i, int j ) {
    return array[i+j*ldim];
  }

  double &Matrix::operator()( int i, int j ) const {
    return array[i+j*ldim];
  }

  double &Matrix::operator()( int i ) {
    return array[i];
  }

  double &Matrix::operator()( int i ) const {
    return array[i];
  }

  Matrix::Matrix( int M, int N, int LDIM ) {
    m = M;
    n = N;
    ldim = LDIM;
    tflag = 0;

    malloc();
  }


  Matrix::Matrix( int M, int N, double *ARRAY, int LDIM ) {
    m = M;
    n = N;
    array = ARRAY;
    ldim = LDIM;
    tflag = 0;

    if ( ldim < m )
      ldim = m;
  }


  Matrix::Matrix( const Matrix &A ) {
    int i, j;

    m = A.m;
    n = A.n;
    ldim = A.ldim;
    tflag = 0;

    malloc();

    for ( i = 0; i < m; i++ )
      for ( j = 0; j < n ; j++ )
        (*this)(i,j) = A(i,j);
    //(*this)(i,j) = A.a(i,j);
  }

  Matrix &Matrix::operator=( const Matrix &A ) {
    int i, j;

    if ( this != &A )
      {
        if ( !tflag )
          {
            free();

            m = A.m;
            n = A.n;
            ldim = A.ldim;

            malloc();

            for ( i = 0; i < m; i++ )
              for ( j = 0; j < n ; j++ )
                (*this)(i,j) = A(i,j);
          }
        else
          {
            if ( m != A.m || n != A.n )
              Error("= operation not allowed");

            for ( i = 0; i < m; i++ )
              for ( j = 0; j < n ; j++ )
                (*this)(i,j) = A(i,j);
          }
      }

    return *this;
  }


  Matrix::~Matrix( void ) {
    if ( !tflag )
      free();
  }

  /* ----------------------------------------------------------------------- */

  int Matrix::M( void ) const {
    return m;
  }


  int Matrix::N( void ) const {
    return n;
  }


  int Matrix::LDIM( void ) const {
    return ldim;
  }


  double *Matrix::ARRAY( void ) const {
    return array;
  }


  int Matrix::TFLAG( void ) const {
    return tflag;
  }

  /* ----------------------------------------------------------------------- */

  Matrix &Matrix::Dimension( int M, int N, int LDIM ) {
    if ( M != m || N != n || ( LDIM != ldim && LDIM != -1 ) )
      {
        if ( tflag )
          Error("Cannot set new dimension for Submatrix");
        else
          {
            free();
            m = M;
            n = N;
            ldim = LDIM;

            malloc();
          }
      }

    return *this;
  }

  Matrix &Matrix::Initialize( double (*f)( int, int ) ) {
    int i, j;

    for ( i = 0; i < m; i++ )
      for ( j = 0; j < n; j++ )
        (*this)(i,j) = f(i,j);

    return *this;
  }


  Matrix &Matrix::Initialize( double val ) {
    int i, j;

    for ( i = 0; i < m; i++ )
      for ( j = 0; j < n; j++ )
        (*this)(i,j) = val;

    return *this;
  }


  /* ----------------------------------------------------------------------- */

  Matrix &Matrix::Submatrix( const Matrix &A, int M, int N, int i0, int j0 ) {
    if ( i0 + M > A.m || j0 + N > A.n )
      Error("Cannot create Submatrix");

    if ( !tflag )
      free();

    tflag = 1;

    m = M;
    n = N;
    array = &A.array[i0+j0*A.ldim];
    ldim = A.ldim;

    return *this;
  }


  Matrix &Matrix::Arraymatrix( int M, int N, double *ARRAY, int LDIM ) {
    if ( !tflag )
      free();

    tflag = 1;

    m = M;
    n = N;
    array = ARRAY;
    ldim = LDIM;

    if ( ldim < m )
      ldim = m;

    return *this;
  }


  const Matrix &Matrix::Print( FILE *f, int DIGITS, int flag ) const {
    int i, j;
    double x;

    // Flag == 1: Matlab output
    // else: plain output

    if ( flag == 1 )
      fprintf( f, "[" );

    for ( i = 0; i < m; i++ )
      {
        for ( j = 0; j < n; j++ )
          {
            x = (*this)(i,j);
            //x = a(i,j);

            if ( flag == 1 )
              {
                fprintf( f, j == 0 ? " " : ", " );
                fprintf( f, "%.*le", DIGITS, x );
              }
            else
              {
                fprintf( f, j == 0 ? "" : "  " );
                fprintf( f, "% .*le", DIGITS, x );
              }
          }
        if ( flag == 1 )
          {
            if ( i < m-1 )
              fprintf( f, ";\n" );
          }
        else
          {
            if ( i < m-1 )
              fprintf( f, "\n" );
          }
      }

    if ( flag == 1 )
      fprintf( f, " ];\n" );
    else
      fprintf( f, "\n" );

    return *this;
  }


  /* ----------------------------------------------------------------------- */
  /* ----------------------------------------------------------------------- */



  int SymMatrix::malloc( void ) {
    int len;

    len = m*(m+1)/2.0;

    if ( len == 0 )
      array = 0;
    else
      if ( ( array = new double[len] ) == 0 )
        Error("'new' failed");

    return 0;
  }


  int SymMatrix::free( void ) {
    if (array != 0 )
      delete[] array;

    return 0;
  }


  double &SymMatrix::operator()( int i, int j ) {
    int pos;

    if (i < j )//reference to upper triangular part
      pos = (int) (j + i*(m - (i+1.0)/2.0));
    else
      pos = (int) (i + j*(m - (j+1.0)/2.0));

    return array[pos];
  }


  double &SymMatrix::operator()( int i, int j ) const {
    int pos;

    if (i < j )//reference to upper triangular part
      pos = (int) (j + i*(m - (i+1.0)/2.0));
    else
      pos = (int) (i + j*(m - (j+1.0)/2.0));

    return array[pos];
  }


  double &SymMatrix::operator()( int i ) {
    return array[i];
  }


  double &SymMatrix::operator()( int i ) const {
    return array[i];
  }

  SymMatrix::SymMatrix( int M ) {
    m = M;
    n = M;
    ldim = M;
    tflag = 0;

    malloc();
  }

  SymMatrix::SymMatrix( int M, double *ARRAY ) {
    m = M;
    n = M;
    ldim = M;
    tflag = 0;

    malloc();
    array = ARRAY;
  }


  SymMatrix::SymMatrix( int M, int N, int LDIM ) {
    m = M;
    n = M;
    ldim = M;
    tflag = 0;

    malloc();
  }


  SymMatrix::SymMatrix( int M, int N, double *ARRAY, int LDIM ) {
    m = M;
    n = M;
    ldim = M;
    tflag = 0;

    malloc();
    array = ARRAY;
  }


  SymMatrix::SymMatrix( const Matrix &A ) {
    int i, j;

    m = A.M();
    n = A.M();
    ldim = A.M();
    tflag = 0;

    malloc();

    for ( j=0; j<m; j++ )//columns
      for ( i=j; i<m; i++ )//rows
        (*this)(i,j) = A(i,j);
    //(*this)(i,j) = A.a(i,j);
  }


  SymMatrix::SymMatrix( const SymMatrix &A ) {
    int i, j;

    m = A.m;
    n = A.n;
    ldim = A.ldim;
    tflag = 0;

    malloc();

    for ( j=0; j<m; j++ )//columns
      for ( i=j; i<m; i++ )//rows
        (*this)(i,j) = A(i,j);
    //(*this)(i,j) = A.a(i,j);
  }


  SymMatrix::~SymMatrix( void ) {
    if (!tflag )
      free();
  }



  SymMatrix &SymMatrix::Dimension( int M ) {
    free();
    m = M;
    n = M;
    ldim = M;

    malloc();

    return *this;
  }


  SymMatrix &SymMatrix::Dimension( int M, int N, int LDIM ) {
    free();
    m = M;
    n = M;
    ldim = M;

    malloc();

    return *this;
  }


  SymMatrix &SymMatrix::Initialize( double (*f)( int, int ) ) {
    int i, j;

    for ( j=0; j<m; j++ )
      for ( i=j; i<n ; i++ )
        (*this)(i,j) = f(i,j);

    return *this;
  }


  SymMatrix &SymMatrix::Initialize( double val ) {
    int i, j;

    for ( j=0; j<m; j++ )
      for ( i=j; i<n ; i++ )
        (*this)(i,j) = val;

    return *this;
  }


  SymMatrix &SymMatrix::Submatrix( const Matrix &A, int M, int N, int i0, int j0) {
    Error("SymMatrix doesn't support Submatrix");
    return *this;
  }


  SymMatrix &SymMatrix::Arraymatrix( int M, double *ARRAY ) {
    if (!tflag )
      free();

    tflag = 1;
    m = M;
    n = M;
    ldim = M;
    array = ARRAY;

    return *this;
  }


  SymMatrix &SymMatrix::Arraymatrix( int M, int N, double *ARRAY, int LDIM ) {
    if (!tflag )
      free();

    tflag = 1;
    m = M;
    n = M;
    ldim = M;
    array = ARRAY;

    return *this;
  }


  /* ----------------------------------------------------------------------- */
  /* ----------------------------------------------------------------------- */


  double delta( int i, int j ) {
    return (i == j) ? 1.0 : 0.0;
  }


  Matrix Transpose( const Matrix &A ) {
    int i, j;
    double *array;

    if ( ( array = new double[A.N()*A.M()] ) == 0 )
      Error("'new' failed");

    for ( i = 0; i < A.N(); i++ )
      for ( j = 0; j < A.M(); j++ )
        array[i+j*A.N()] = A(j,i);
    //array[i+j*A.N()] = A.a(j,i);

    return Matrix( A.N(), A.M(), array, A.N() );
  }


  Matrix &Transpose( const Matrix &A, Matrix &T ) {
    int i, j;

    T.Dimension( A.N(), A.M() );

    for ( i = 0; i < A.N(); i++ )
      for ( j = 0; j < A.M(); j++ )
        T(i,j) = A(j,i);
    //T(i,j) = A.a(j,i);

    return T;
  }

  /**
   * Standard Constructor:
   * Default settings
   */
  SQPoptions::SQPoptions() {
    /* qpOASES: dense (0), sparse (1), or Schur (2)
     * Choice of qpOASES method:
     * 0: dense Hessian and Jacobian, dense factorization of reduced Hessian
     * 1: sparse Hessian and Jacobian, dense factorization of reduced Hessian
     * 2: sparse Hessian and Jacobian, Schur complement approach (recommended) */
    sparseQP = 2;

    // 0: no output, 1: normal output, 2: verbose output
    printLevel = 2;
    // 1: (some) colorful output
    printColor = 1;

    /* 0: no debug output, 1: print one line per iteration to file,
       2: extensive debug output to files (impairs performance) */
    debugLevel = 0;

    //eps = 2.2204e-16;
    eps = 1.0e-16;
    inf = 1.0e20;
    opttol = 1.0e-6;
    nlinfeastol = 1.0e-6;

    // 0: no globalization, 1: filter line search
    globalization = 1;

    // 0: no feasibility restoration phase 1: if line search fails, start feasibility restoration phase
    restoreFeas = 1;

    // 0: globalization is always active, 1: take a full step at first SQP iteration, no matter what
    skipFirstGlobalization = false;

    // 0: one update for large Hessian, 1: apply updates blockwise, 2: 2 blocks: 1 block updates, 1 block Hessian of obj.
    blockHess = 1;

    // after too many consecutive skipped updates, Hessian block is reset to (scaled) identity
    maxConsecSkippedUpdates = 100;

    // for which blocks should second derivatives be provided by the user:
    // 0: none, 1: for the last block, 2: for all blocks
    whichSecondDerv = 0;

    // 0: initial Hessian is diagonal matrix, 1: scale initial Hessian according to Nocedal p.143,
    // 2: scale initial Hessian with Oren-Luenberger factor 3: geometric mean of 1 and 2
    // 4: centered Oren-Luenberger sizing according to Tapia paper
    hessScaling = 2;
    fallbackScaling = 4;
    iniHessDiag = 1.0;

    // Activate damping strategy for BFGS (if deactivated, BFGS might yield indefinite updates!)
    hessDamp = 1;

    // Damping factor for Powell modification of BFGS updates ( between 0.0 and 1.0 )
    hessDampFac = 0.2;

    // 0: constant, 1: SR1, 2: BFGS (damped), 3: [not used] , 4: finiteDiff, 5: Gauss-Newton
    hessUpdate = 1;
    fallbackUpdate = 2;

    //
    convStrategy = 0;

    // How many ADDITIONAL (convexified) QPs may be solved per iteration?
    maxConvQP = 1;

    // 0: full memory updates 1: limited memory
    hessLimMem = 1;

    // memory size for L-BFGS/L-SR1 updates
    hessMemsize = 20;

    // maximum number of line search iterations
    maxLineSearch = 20;

    // if step has to be reduced in too many consecutive iterations, feasibility restoration phase is invoked
    maxConsecReducedSteps = 100;

    // maximum number of second-order correction steps
    maxSOCiter = 3;

    // maximum number of QP iterations per QP solve
    maxItQP = 5000;
    // maximum time (in seconds) for one QP solve
    maxTimeQP = 10000.0;

    // Oren-Luenberger scaling parameters
    colEps = 0.1;
    colTau1 = 0.5;
    colTau2 = 1.0e4;

    // Filter line search parameters
    gammaTheta = 1.0e-5;
    gammaF = 1.0e-5;
    kappaSOC = 0.99;
    kappaF = 0.999;
    thetaMax = 1.0e7;       // reject steps if constr viol. is larger than thetaMax
    thetaMin = 1.0e-5;      // if constr viol. is smaller than thetaMin require Armijo cond. for obj.
    delta = 1.0;
    sTheta = 1.1;
    sF = 2.3;
    eta = 1.0e-4;

    // Inertia correction for filter line search and indefinite Hessians
    kappaMinus = 0.333;
    kappaPlus = 8.0;
    kappaPlusMax = 100.0;
    deltaH0 = 1.0e-4;
  }


  /**
   * Some options cannot be set together, resolve here
   */
  void SQPoptions::optionsConsistency() {
    // If we compute second constraints derivatives switch to finite differences Hessian (convenience)
    if (whichSecondDerv == 2 )
      {
        hessUpdate = 4;
        blockHess = 1;
      }

    // If we don't use limited memory BFGS we need to store only one vector.
    if (!hessLimMem )
      hessMemsize = 1;

    if (sparseQP != 2 && hessUpdate == 1 )
      {
        printf( "SR1 update only works with qpOASES Schur complement version. Using BFGS updates instead.\n" );
        hessUpdate = 2;
        hessScaling = fallbackScaling;
      }
  }

  void Problemspec::evaluate( const Matrix &xi, double *objval, Matrix &constr, int *info ) {
    Matrix lambdaDummy, gradObjDummy;
    SymMatrix *hessDummy;
    int dmode = 0;

    Matrix constrJacDummy;
    double *jacNzDummy;
    int *jacIndRowDummy, *jacIndColDummy;
    *info = 0;

    // Try sparse version first
    evaluate( xi, lambdaDummy, objval, constr, gradObjDummy, jacNzDummy, jacIndRowDummy, jacIndColDummy, hessDummy, dmode, info );

    // If sparse version is not implemented, try dense version
    if (info )
      evaluate( xi, lambdaDummy, objval, constr, gradObjDummy, constrJacDummy, hessDummy, dmode, info );
  }

  SQPstats::SQPstats( PATHSTR myOutpath ) {
    strcpy( outpath, myOutpath );

    itCount = 0;
    qpItTotal = 0;
    qpIterations = 0;
    qpIterations2 = 0;
    qpResolve = 0;
    rejectedSR1 = 0;
    hessSkipped = 0;
    hessDamped = 0;
    averageSizingFactor = 0.0;
    nFunCalls = 0;
    nDerCalls = 0;
    nRestHeurCalls = 0;
    nRestPhaseCalls = 0;

    nTotalUpdates = 0;
    nTotalSkippedUpdates = 0;
  }


  void SQPstats::printProgress( Problemspec *prob, SQPiterate *vars, SQPoptions *param, bool hasConverged ) {
    /*
     * vars->steptype:
     *-1: full step was accepted because it reduces the KKT error although line search failed
     * 0: standard line search step
     * 1: Hessian has been reset to identity
     * 2: feasibility restoration heuristic has been called
     * 3: feasibility restoration phase has been called
     */

    if (itCount == 0 ) {
      if (param->printLevel > 0 ) {
        prob->printInfo();

        // Headline
        printf("%-8s", "   it" );
        printf("%-21s", " qpIt" );
        printf("%-9s","obj" );
        printf("%-11s","feas" );
        printf("%-7s","opt" );
        if (param->printLevel > 1 ) {
          printf("%-11s","|lgrd|" );
          printf("%-9s","|stp|" );
          printf("%-10s","|lstp|" );
        }
        printf("%-8s","alpha" );
        if (param->printLevel > 1 ) {
          printf("%-6s","nSOCS" );
          printf("%-18s","sk, da, sca" );
          printf("%-6s","QPr,mu" );
        }
        printf("\n");

        // Values for first iteration
        printf("%5i  ", itCount );
        printf("%11i ", 0 );
        printf("% 10e  ", vars->obj );
        printf("%-10.2e", vars->cNormS );
        printf("%-10.2e", vars->tol );
        printf("\n");
      }

      if (param->debugLevel > 0 ) {
        // Print everything in a CSV file as well
        fprintf( progressFile, "%23.16e, %23.16e, %23.16e, %23.16e, %23.16e, %23.16e, %23.16e, %23.16e, %i, %i, %23.16e, %i, %23.16e\n",
                 vars->obj, vars->cNormS, vars->tol, 0.0, 0.0, 0.0, 0.0, 0.0, 0, 0, 0.0, 0, 0.0 );
      }
    } else {
      // Every twenty iterations print headline
      if (itCount % 20 == 0 && param->printLevel > 0 ) {
        printf("%-8s", "   it" );
        printf("%-21s", " qpIt" );
        printf("%-9s","obj" );
        printf("%-11s","feas" );
        printf("%-7s","opt" );
        if (param->printLevel > 1 )
          {
            printf("%-11s","|lgrd|" );
            printf("%-9s","|stp|" );
            printf("%-10s","|lstp|" );
          }
        printf("%-8s","alpha" );
        if (param->printLevel > 1 )
          {
            printf("%-6s","nSOCS" );
            printf("%-18s","sk, da, sca" );
            printf("%-6s","QPr,mu" );
          }
        printf("\n");
      }

      // All values
      if (param->printLevel > 0 ) {
        printf("%5i  ", itCount );
        printf("%5i+%5i ", qpIterations, qpIterations2 );
        printf("% 10e  ", vars->obj );
        printf("%-10.2e", vars->cNormS );
        printf("%-10.2e", vars->tol );
        if (param->printLevel > 1 )
          {
            printf("%-10.2e", vars->gradNorm );
            printf("%-10.2e", lInfVectorNorm( vars->deltaXi ) );
            printf("%-10.2e", vars->lambdaStepNorm );
          }

        if ((vars->alpha == 1.0 && vars->steptype != -1) || !param->printColor ) {
          printf("%-9.1e", vars->alpha );
        } else {
          printf("\033[0;36m%-9.1e\033[0m", vars->alpha );
        }

        if (param->printLevel > 1 ) {
          if (vars->nSOCS == 0 || !param->printColor ) {
            printf("%5i", vars->nSOCS );
          } else {
            printf("\033[0;36m%5i\033[0m", vars->nSOCS );
          }
          printf("%3i, %3i, %-9.1e", hessSkipped, hessDamped, averageSizingFactor );
          printf("%i, %-9.1e", qpResolve, l1VectorNorm( vars->deltaH )/vars->nBlocks );
        }
        printf("\n");
      }

      if (param->debugLevel > 0 ) {
        // Print everything in a CSV file as well
        fprintf( progressFile, "%23.16e, %23.16e, %23.16e, %23.16e, %23.16e, %23.16e, %23.16e, %i, %i, %i, %23.16e, %i, %23.16e\n",
                 vars->obj, vars->cNormS, vars->tol, vars->gradNorm, lInfVectorNorm( vars->deltaXi ),
                 vars->lambdaStepNorm, vars->alpha, vars->nSOCS, hessSkipped, hessDamped, averageSizingFactor,
                 qpResolve, l1VectorNorm( vars->deltaH )/vars->nBlocks );

        // Print update sequence
        fprintf( updateFile, "%i\t", qpResolve );
      }
    }

    // Print Debug information
    printDebug( vars, param );

    // Do not accidentally print hessSkipped in the next iteration
    hessSkipped = 0;
    hessDamped = 0;

    // qpIterations = number of iterations for the QP that determines the step, can be a resolve (+SOC)
    // qpIterations2 = number of iterations for a QP which solution was discarded
    qpItTotal += qpIterations;
    qpItTotal += qpIterations2;
    qpIterations = 0;
    qpIterations2 = 0;
    qpResolve = 0;

    if (param->printLevel > 0 ) {
      if (hasConverged && vars->steptype < 2 ) {
        if (param->printColor ) {
          printf("\n\033[1;32m***CONVERGENCE ACHIEVED!***\n\033[0m");
        } else {
          printf("\n***CONVERGENCE ACHIEVED!***\n");
        }
      }
    }
  }


  void SQPstats::initStats( SQPoptions *param ) {
    PATHSTR filename;

    // Open files

    if (param->debugLevel > 0 ) {
      // SQP progress
      strcpy( filename, outpath );
      strcat( filename, "sqpits.csv" );
      progressFile = fopen( filename, "w");

      // Update sequence
      strcpy( filename, outpath );
      strcat( filename, "updatesequence.txt" );
      updateFile = fopen( filename, "w" );
    }

    if (param->debugLevel > 1 ) {
      // Primal variables
      strcpy( filename, outpath );
      strcat( filename, "pv.csv" );
      primalVarsFile = fopen( filename, "w");

      // Dual variables
      strcpy( filename, outpath );
      strcat( filename, "dv.csv" );
      dualVarsFile = fopen( filename, "w");
    }

    itCount = 0;
    qpItTotal = 0;
    qpIterations = 0;
    hessSkipped = 0;
    hessDamped = 0;
    averageSizingFactor = 0.0;
  }


  void SQPstats::printPrimalVars( const Matrix &xi ) {
    for (int i=0; i<xi.M()-1; i++ )
      fprintf( primalVarsFile, "%23.16e ", xi( i ) );
    fprintf( primalVarsFile, "%23.16e\n", xi( xi.M()-1 ) );
  }


  void SQPstats::printDualVars( const Matrix &lambda ) {
    for (int i=0; i<lambda.M()-1; i++ )
      fprintf( dualVarsFile, "%23.16e ", lambda( i ) );
    fprintf( dualVarsFile, "%23.16e\n", lambda( lambda.M()-1 ) );
  }


  void SQPstats::printHessian( int nBlocks, SymMatrix *&hess ) {
    PATHSTR filename;
    int offset, i, j, iBlock, nVar;

    nVar = 0;
    for (iBlock=0; iBlock<nBlocks; iBlock++ )
      nVar += hess[iBlock].M();

    SymMatrix fullHessian;
    fullHessian.Dimension( nVar ).Initialize( 0.0 );

    strcpy( filename, outpath );
    strcat( filename, "hes.m" );
    hessFile = fopen( filename, "w");

    offset = 0;
    for (iBlock=0; iBlock<nBlocks; iBlock++ )
      {
        for (i=0; i<hess[iBlock].N(); i++ )
          for (j=i; j<hess[iBlock].N(); j++ )
            fullHessian( offset + i, offset + j ) = hess[iBlock]( i,j );

        offset += hess[iBlock].N();
      }

    fprintf( hessFile, "H=" );
    fullHessian.Print( hessFile, 23, 1 );
    fprintf( hessFile, "\n" );
    fclose( hessFile );
  }


  void SQPstats::printHessian( int nVar, double *hesNz, int *hesIndRow, int *hesIndCol ) {
    PATHSTR filename;

    strcpy( filename, outpath );
    strcat( filename, "hes.dat" );
    hessFile = fopen( filename, "w");

    printSparseMatlab( hessFile, nVar, nVar, hesNz, hesIndRow, hesIndCol );

    fprintf( hessFile, "\n" );
    fclose( hessFile );
  }


  void SQPstats::printJacobian( const Matrix &constrJac ) {
    PATHSTR filename;

    strcpy( filename, outpath );
    strcat( filename, "jac.m" );
    jacFile = fopen( filename, "w");

    fprintf( jacFile, "A=" );
    constrJac.Print( jacFile, 23, 1 );
    fprintf( jacFile, "\n" );

    fclose( jacFile );
  }


  void SQPstats::printJacobian( int nCon, int nVar, double *jacNz, int *jacIndRow, int *jacIndCol ) {
    PATHSTR filename;

    strcpy( filename, outpath );
    strcat( filename, "jac.dat" );
    jacFile = fopen( filename, "w");

    printSparseMatlab( jacFile, nCon, nVar, jacNz, jacIndRow, jacIndCol );

    fprintf( jacFile, "\n" );
    fclose( jacFile );
  }


  void SQPstats::printSparseMatlab( FILE *file, int nRow, int nCol, double *nz, int *indRow, int *indCol ) {
    int i, j, count;

    count = 0;
    fprintf( file, "%i %i 0\n", nRow, nCol );
    for (i=0; i<nCol; i++ )
      for (j=indCol[i]; j<indCol[i+1]; j++ )
        {
          // +1 for MATLAB indices!
          fprintf( file, "%i %i %23.16e\n", indRow[count]+1, i+1, nz[count] );
          count++;
        }
  }


  void SQPstats::printDebug( SQPiterate *vars, SQPoptions *param ) {
    if (param->debugLevel > 1 )
      {
        printPrimalVars( vars->xi );
        printDualVars( vars->lambda );
      }
  }


  void SQPstats::finish( SQPoptions *param ) {
    if (param->debugLevel > 0 ) {
      fprintf( progressFile, "\n" );
      fclose( progressFile );
      fprintf( updateFile, "\n" );
      fclose( updateFile );
    }

    if (param->debugLevel > 1 ) {
      fclose( primalVarsFile );
      fclose( dualVarsFile );
    }
  }


  void SQPstats::printCppNull( FILE *outfile, char* varname ) {
    fprintf( outfile, "    double *%s = 0;\n", varname );
  }


  void SQPstats::printVectorCpp( FILE *outfile, double *vec, int len, char* varname ) {
    int i;

    fprintf( outfile, "    double %s[%i] = { ", varname, len );
    for (i=0; i<len; i++ ) {
      fprintf( outfile, "%23.16e", vec[i] );
      if (i != len-1 )
        fprintf( outfile, ", " );
      if ((i+1) % 10 == 0 )
        fprintf( outfile, "\n          " );
    }
    fprintf( outfile, " };\n\n" );
  }


  void SQPstats::printVectorCpp( FILE *outfile, int *vec, int len, char* varname ) {
    int i;

    fprintf( outfile, "    int %s[%i] = { ", varname, len );
    for (i=0; i<len; i++ ) {
      fprintf( outfile, "%i", vec[i] );
      if (i != len-1 )
        fprintf( outfile, ", " );
      if ((i+1) % 15 == 0 )
        fprintf( outfile, "\n          " );
    }
    fprintf( outfile, " };\n\n" );
  }


  void SQPstats::dumpQPCpp( Problemspec *prob, SQPiterate *vars, qpOASES::SQProblem *qp, int sparseQP ) {
    int i, j;
    PATHSTR filename;
    FILE *outfile;
    int n = prob->nVar;
    int m = prob->nCon;

    // Print dimensions
    strcpy( filename, outpath );
    strcat( filename, "qpoases_dim.dat" );
    outfile = fopen( filename, "w" );
    fprintf( outfile, "%i %i\n", n, m );
    fclose( outfile );

    // Print Hessian
    if (sparseQP ) {
      strcpy( filename, outpath );
      strcat( filename, "qpoases_H_sparse.dat" );
      outfile = fopen( filename, "w" );
      for (i=0; i<prob->nVar+1; i++ )
        fprintf( outfile, "%i ", vars->hessIndCol[i] );
      fprintf( outfile, "\n" );

      for (i=0; i<vars->hessIndCol[prob->nVar]; i++ )
        fprintf( outfile, "%i ", vars->hessIndRow[i] );
      fprintf( outfile, "\n" );

      for (i=0; i<vars->hessIndCol[prob->nVar]; i++ )
        fprintf( outfile, "%23.16e ", vars->hessNz[i] );
      fprintf( outfile, "\n" );
      fclose( outfile );
    }
    strcpy( filename, outpath );
    strcat( filename, "qpoases_H.dat" );
    outfile = fopen( filename, "w" );
    int blockCnt = 0;
    for (i=0; i<n; i++ ) {
      for (j=0; j<n; j++ ) {
        if (i == vars->blockIdx[blockCnt+1] ) blockCnt++;
        if (j >= vars->blockIdx[blockCnt] && j < vars->blockIdx[blockCnt+1] ) {
          fprintf( outfile, "%23.16e ", vars->hess[blockCnt]( i - vars->blockIdx[blockCnt], j - vars->blockIdx[blockCnt] ) );
        } else {
          fprintf( outfile, "0.0 " );
        }
      }
      fprintf( outfile, "\n" );
    }
    fclose( outfile );

    // Print gradient
    strcpy( filename, outpath );
    strcat( filename, "qpoases_g.dat" );
    outfile = fopen( filename, "w" );
    for (i=0; i<n; i++ )
      fprintf( outfile, "%23.16e ", vars->gradObj( i ) );
    fprintf( outfile, "\n" );
    fclose( outfile );

    // Print Jacobian
    strcpy( filename, outpath );
    strcat( filename, "qpoases_A.dat" );
    outfile = fopen( filename, "w" );
    if (sparseQP ) {
      // Always print dense Jacobian
      Matrix constrJacTemp;
      constrJacTemp.Dimension( prob->nCon, prob->nVar ).Initialize( 0.0 );
      for (i=0; i<prob->nVar; i++ )
        for (j=vars->jacIndCol[i]; j<vars->jacIndCol[i+1]; j++ )
          constrJacTemp( vars->jacIndRow[j], i ) = vars->jacNz[j];
      for (i=0; i<m; i++ ) {
        for (j=0; j<n; j++ )
          fprintf( outfile, "%23.16e ", constrJacTemp( i, j ) );
        fprintf( outfile, "\n" );
      }
      fclose( outfile );
    } else {
      for (i=0; i<m; i++ ) {
        for (j=0; j<n; j++ )
          fprintf( outfile, "%23.16e ", vars->constrJac( i, j ) );
        fprintf( outfile, "\n" );
      }
      fclose( outfile );
    }

    if (sparseQP ) {
      strcpy( filename, outpath );
      strcat( filename, "qpoases_A_sparse.dat" );
      outfile = fopen( filename, "w" );
      for (i=0; i<prob->nVar+1; i++ )
        fprintf( outfile, "%i ", vars->jacIndCol[i] );
      fprintf( outfile, "\n" );

      for (i=0; i<vars->jacIndCol[prob->nVar]; i++ )
        fprintf( outfile, "%i ", vars->jacIndRow[i] );
      fprintf( outfile, "\n" );

      for (i=0; i<vars->jacIndCol[prob->nVar]; i++ )
        fprintf( outfile, "%23.16e ", vars->jacNz[i] );
      fprintf( outfile, "\n" );
      fclose( outfile );
    }

    // Print variable lower bounds
    strcpy( filename, outpath );
    strcat( filename, "qpoases_lb.dat" );
    outfile = fopen( filename, "w" );
    for (i=0; i<n; i++ )
      fprintf( outfile, "%23.16e ", vars->deltaBl( i ) );
    fprintf( outfile, "\n" );
    fclose( outfile );

    // Print variable upper bounds
    strcpy( filename, outpath );
    strcat( filename, "qpoases_ub.dat" );
    outfile = fopen( filename, "w" );
    for (i=0; i<n; i++ )
      fprintf( outfile, "%23.16e ", vars->deltaBu( i ) );
    fprintf( outfile, "\n" );
    fclose( outfile );

    // Print constraint lower bounds
    strcpy( filename, outpath );
    strcat( filename, "qpoases_lbA.dat" );
    outfile = fopen( filename, "w" );
    for (i=0; i<m; i++ )
      fprintf( outfile, "%23.16e ", vars->deltaBl( i+n ) );
    fprintf( outfile, "\n" );
    fclose( outfile );

    // Print constraint upper bounds
    strcpy( filename, outpath );
    strcat( filename, "qpoases_ubA.dat" );
    outfile = fopen( filename, "w" );
    for (i=0; i<m; i++ )
      fprintf( outfile, "%23.16e ", vars->deltaBu( i+n ) );
    fprintf( outfile, "\n" );
    fclose( outfile );

    // Print active set
    qpOASES::Bounds b;
    qpOASES::Constraints c;
    qp->getBounds( b );
    qp->getConstraints( c );

    strcpy( filename, outpath );
    strcat( filename, "qpoases_as.dat" );
    outfile = fopen( filename, "w" );
    for (i=0; i<n; i++ )
      fprintf( outfile, "%i ", b.getStatus( i ) );
    fprintf( outfile, "\n" );
    for (i=0; i<m; i++ )
      fprintf( outfile, "%i ", c.getStatus( i ) );
    fprintf( outfile, "\n" );
    fclose( outfile );
  }

  void SQPstats::dumpQPMatlab( Problemspec *prob, SQPiterate *vars, int sparseQP ) {
    Matrix temp;
    PATHSTR filename;
    FILE *qpFile;
    FILE *vecFile;

    // Print vectors g, lb, lu, lbA, luA
    strcpy( filename, outpath );
    strcat( filename, "vec.m" );
    vecFile = fopen( filename, "w");

    fprintf( vecFile, "g=" );
    vars->gradObj.Print( vecFile, 23, 1 );
    fprintf( vecFile, "\n\n" );

    temp.Submatrix( vars->deltaBl, prob->nVar, 1, 0, 0 );
    fprintf( vecFile, "lb=" );
    temp.Print( vecFile, 23, 1 );
    fprintf( vecFile, "\n\n" );

    temp.Submatrix( vars->deltaBu, prob->nVar, 1, 0, 0 );
    fprintf( vecFile, "lu=" );
    temp.Print( vecFile, 23, 1 );
    fprintf( vecFile, "\n\n" );

    temp.Submatrix( vars->deltaBl, prob->nCon, 1, prob->nVar, 0 );
    fprintf( vecFile, "lbA=" );
    temp.Print( vecFile, 23, 1 );
    fprintf( vecFile, "\n\n" );

    temp.Submatrix( vars->deltaBu, prob->nCon, 1, prob->nVar, 0 );
    fprintf( vecFile, "luA=" );
    temp.Print( vecFile, 23, 1 );
    fprintf( vecFile, "\n" );

    fclose( vecFile );

    // Print sparse Jacobian and Hessian
    if (sparseQP )
      {
        printJacobian( prob->nCon, prob->nVar, vars->jacNz, vars->jacIndRow, vars->jacIndCol );
        printHessian( prob->nVar, vars->hessNz, vars->hessIndRow, vars->hessIndCol );
      }

    // Print a script that correctly reads everything
    strcpy( filename, outpath );
    strcat( filename, "getqp.m" );
    qpFile = fopen( filename, "w");

    fprintf( qpFile, "%% Read vectors g, lb, lu, lbA, luA\n" );
    fprintf( qpFile, "vec;\n" );
    fprintf( qpFile, "%% Read sparse Jacobian\n" );
    fprintf( qpFile, "load jac.dat\n" );
    fprintf( qpFile, "if jac(1) == 0\n" );
    fprintf( qpFile, "    A = [];\n" );
    fprintf( qpFile, "else\n" );
    fprintf( qpFile, "    A = spconvert( jac );\n" );
    fprintf( qpFile, "end\n" );
    fprintf( qpFile, "%% Read sparse Hessian\n" );
    fprintf( qpFile, "load hes.dat\n" );
    fprintf( qpFile, "H = spconvert( hes );\n" );

    fclose( qpFile );
  }

  RestorationProblem::RestorationProblem( Problemspec *parentProblem, const Matrix &xiReference ) {
    int i, iVar, iCon;

    parent = parentProblem;
    xiRef.Dimension( parent->nVar );
    for (i=0; i<parent->nVar; i++)
      xiRef( i ) = xiReference( i );

    /* nCon slack variables */
    nVar = parent->nVar + parent->nCon;
    nCon = parent->nCon;

    /* Block structure: One additional block for every slack variable */
    nBlocks = parent->nBlocks+nCon;
    blockIdx = new int[nBlocks+1];
    for (i=0; i<parent->nBlocks+1; i++ )
      blockIdx[i] = parent->blockIdx[i];
    for (i=parent->nBlocks+1; i<nBlocks+1; i++ )
      blockIdx[i] = blockIdx[i-1]+1;

    /* Set bounds */
    objLo = 0.0;
    objUp = 1.0e20;

    bl.Dimension( nVar + nCon ).Initialize( -1.0e20 );
    bu.Dimension( nVar + nCon ).Initialize( 1.0e20 );
    for (iVar=0; iVar<parent->nVar; iVar++ )
      {
        bl( iVar ) = parent->bl( iVar );
        bu( iVar ) = parent->bu( iVar );
      }

    for (iCon=0; iCon<parent->nCon; iCon++ )
      {
        bl( nVar+iCon ) = parent->bl( parent->nVar+iCon );
        bu( nVar+iCon ) = parent->bu( parent->nVar+iCon );
      }
  }


  void RestorationProblem::evaluate( const Matrix &xi, const Matrix &lambda,
                                     double *objval, Matrix &constr,
                                     Matrix &gradObj, double *&jacNz, int *&jacIndRow, int *&jacIndCol,
                                     SymMatrix *&hess, int dmode, int *info ) {
    int iCon, i;
    double diff, regTerm;
    Matrix xiOrig, slack;

    // The first nVar elements of the variable vector correspond to the variables of the original problem
    xiOrig.Submatrix( xi, parent->nVar, 1, 0, 0 );
    slack.Submatrix( xi, parent->nCon, 1, parent->nVar, 0 );

    // Evaluate constraints of the original problem
    parent->evaluate( xiOrig, lambda, objval, constr,
                      gradObj, jacNz, jacIndRow, jacIndCol, hess, dmode, info );

    // Subtract slacks
    for (iCon=0; iCon<nCon; iCon++ )
      constr( iCon ) -= slack( iCon );


    /* Evaluate objective: minimize slacks plus deviation from reference point */
    if (dmode < 0 )
      return;

    *objval = 0.0;

    // First part: sum of slack variables
    for (i=0; i<nCon; i++ )
      *objval += slack( i ) * slack( i );
    *objval = 0.5 * rho * (*objval);

    // Second part: regularization term
    regTerm = 0.0;
    for (i=0; i<parent->nVar; i++ )
      {
        diff = xiOrig( i ) - xiRef( i );
        regTerm += diagScale( i ) * diff * diff;
      }
    regTerm = 0.5 * zeta * regTerm;
    *objval += regTerm;

    if (dmode > 0 )
      {// compute objective gradient

        // gradient w.r.t. xi (regularization term)
        for (i=0; i<parent->nVar; i++ )
          gradObj( i ) = zeta * diagScale( i ) * diagScale( i ) * (xiOrig( i ) - xiRef( i ));

        // gradient w.r.t. slack variables
        for (i=parent->nVar; i<nVar; i++ )
          gradObj( i ) = rho * xi( i );
      }

    *info = 0;
  }

  void RestorationProblem::evaluate( const Matrix &xi, const Matrix &lambda,
                                     double *objval, Matrix &constr,
                                     Matrix &gradObj, Matrix &constrJac,
                                     SymMatrix *&hess, int dmode, int *info ) {
    int iCon, i;
    double diff, regTerm;
    Matrix xiOrig, constrJacOrig;
    Matrix slack;

    // The first nVar elements of the variable vector correspond to the variables of the original problem
    xiOrig.Submatrix( xi, parent->nVar, 1, 0, 0 );
    slack.Submatrix( xi, parent->nCon, 1, parent->nVar, 0 );
    if (dmode != 0 )
      constrJacOrig.Submatrix( constrJac, parent->nCon, parent->nVar, 0, 0 );

    // Evaluate constraints of the original problem
    parent->evaluate( xiOrig, lambda, objval, constr,
                      gradObj, constrJacOrig, hess, dmode, info );

    // Subtract slacks
    for (iCon=0; iCon<nCon; iCon++ )
      constr( iCon ) -= slack( iCon );


    /* Evaluate objective: minimize slacks plus deviation from reference point */
    if (dmode < 0 )
      return;

    *objval = 0.0;

    // First part: sum of slack variables
    for (i=0; i<nCon; i++ )
      *objval += slack( i ) * slack( i );
    *objval = 0.5 * rho * (*objval);

    // Second part: regularization term
    regTerm = 0.0;
    for (i=0; i<parent->nVar; i++ ) {
      diff = xiOrig( i ) - xiRef( i );
      regTerm += diagScale( i ) * diff * diff;
    }
    regTerm = 0.5 * zeta * regTerm;
    *objval += regTerm;

    if (dmode > 0 ) {
      // compute objective gradient

      // gradient w.r.t. xi (regularization term)
      for (i=0; i<parent->nVar; i++ )
        gradObj( i ) = zeta * diagScale( i ) * diagScale( i ) * (xiOrig( i ) - xiRef( i ));

      // gradient w.r.t. slack variables
      for (i=parent->nVar; i<nVar; i++ )
        gradObj( i ) = rho * slack( i );
    }

    *info = 0;
  }


  void RestorationProblem::initialize( Matrix &xi, Matrix &lambda, double *&jacNz, int *&jacIndRow, int *&jacIndCol ) {
    int i, info;
    double objval;
    Matrix xiOrig, slack, constrRef;

    xiOrig.Submatrix( xi, parent->nVar, 1, 0, 0 );
    slack.Submatrix( xi, parent->nCon, 1, parent->nVar, 0 );

    // Call initialize of the parent problem. There, the sparse Jacobian is allocated
    double *jacNzOrig = 0;
    int *jacIndRowOrig = 0, *jacIndColOrig = 0, nnz, nnzOrig;
    parent->initialize( xiOrig, lambda, jacNzOrig, jacIndRowOrig, jacIndColOrig );
    nnzOrig = jacIndColOrig[parent->nVar];

    // Copy sparse Jacobian from original problem
    nnz = nnzOrig + nCon;
    jacNz = new double[nnz];
    jacIndRow = new int[nnz + (nVar+1)];
    jacIndCol = jacIndRow + nnz;
    for (i=0; i<nnzOrig; i++ )
      {
        jacNz[i] = jacNzOrig[i];
        jacIndRow[i] = jacIndRowOrig[i];
      }
    for (i=0; i<parent->nVar; i++ ) jacIndCol[i] = jacIndColOrig[i];

    // Jacobian entries for slacks (one nonzero entry per column)
    for (i=nnzOrig; i<nnz; i++ ) {
      jacNz[i] = -1.0;
      jacIndRow[i] = i-nnzOrig;
    }
    for (i=parent->nVar; i<nVar+1; i++ )
      jacIndCol[i] = nnzOrig + i - parent->nVar;

    // The reference point is the starting value for the restoration phase
    for (i=0; i<parent->nVar; i++ )
      xiOrig( i ) = xiRef( i );

    // Initialize slack variables such that the constraints are feasible
    constrRef.Dimension( nCon );
    parent->evaluate( xiOrig, &objval, constrRef, &info );

    for (i=0; i<nCon; i++ ) {
      if (constrRef( i ) <= parent->bl( parent->nVar + i ) )// if lower bound is violated
        slack( i ) = constrRef( i ) - parent->bl( parent->nVar + i );
      else if (constrRef( i ) > parent->bu( parent->nVar + i ) )// if upper bound is violated
        slack( i ) = constrRef( i ) - parent->bu( parent->nVar + i );
    }

    // Set diagonal scaling matrix
    diagScale.Dimension( parent->nVar ).Initialize( 1.0 );
    for (i=0; i<parent->nVar; i++ )
      if (fabs( xiRef( i ) ) > 1.0 )
        diagScale( i ) = 1.0 / fabs( xiRef( i ) );

    // Regularization factor zeta and rho \todo wie setzen?
    zeta = 1.0e-3;
    rho = 1.0e3;

    lambda.Initialize( 0.0 );
  }


  void RestorationProblem::initialize( Matrix &xi, Matrix &lambda, Matrix &constrJac ) {
    int i, info;
    double objval;
    Matrix xiOrig, slack, constrJacOrig, constrRef;

    xiOrig.Submatrix( xi, parent->nVar, 1, 0, 0 );
    slack.Submatrix( xi, parent->nCon, 1, parent->nVar, 0 );
    constrJacOrig.Submatrix( constrJac, parent->nCon, parent->nVar, 0, 0 );

    // Call initialize of the parent problem to set up linear constraint matrix correctly
    parent->initialize( xiOrig, lambda, constrJacOrig );

    // Jacobian entries for slacks
    for (i=0; i<parent->nCon; i++ )
      constrJac( i, parent->nVar+i ) = -1.0;

    // The reference point is the starting value for the restoration phase
    for (i=0; i<parent->nVar; i++ )
      xiOrig( i ) = xiRef( i );

    // Initialize slack variables such that the constraints are feasible
    constrRef.Dimension( nCon );
    parent->evaluate( xiOrig, &objval, constrRef, &info );

    for (i=0; i<nCon; i++ ) {
      if (constrRef( i ) <= parent->bl( parent->nVar + i ) )// if lower bound is violated
        slack( i ) = constrRef( i ) - parent->bl( parent->nVar + i );
      else if (constrRef( i ) > parent->bu( parent->nVar + i ) )// if upper bound is violated
        slack( i ) = constrRef( i ) - parent->bu( parent->nVar + i );
    }

    // Set diagonal scaling matrix
    diagScale.Dimension( parent->nVar ).Initialize( 1.0 );
    for (i=0; i<parent->nVar; i++ )
      if (fabs( xiRef( i ) ) > 1.0 )
        diagScale( i ) = 1.0 / fabs( xiRef( i ) );

    // Regularization factor zeta and rho \todo wie setzen?
    zeta = 1.0e-3;
    rho = 1.0e3;

    lambda.Initialize( 0.0 );
  }


  void RestorationProblem::printVariables( const Matrix &xi, const Matrix &lambda, int verbose ) {
    int k;

    printf("\n<|----- Original Variables -----|>\n");
    for (k=0; k<parent->nVar; k++ )
      //printf("%7i: %-30s   %7g <= %10.3g <= %7g   |   mul=%10.3g\n", k+1, parent->varNames[k], bl(k), xi(k), bu(k), lambda(k));
      printf("%7i: x%-5i   %7g <= %10.3g <= %7g   |   mul=%10.3g\n", k+1, k, bl(k), xi(k), bu(k), lambda(k));
    printf("\n<|----- Slack Variables -----|>\n");
    for (k=parent->nVar; k<nVar; k++ )
      printf("%7i: slack   %7g <= %10.3g <= %7g   |   mul=%10.3g\n", k+1, bl(k), xi(k), bu(k), lambda(k));
  }


  void RestorationProblem::printConstraints( const Matrix &constr, const Matrix &lambda ) {
    printf("\n<|----- Constraints -----|>\n");
    for (int k=0; k<nCon; k++ )
      //printf("%5i: %-30s   %7g <= %10.4g <= %7g   |   mul=%10.3g\n", k+1, parent->conNames[parent->nVar+k], bl(nVar+k), constr(k), bu(nVar+k), lambda(nVar+k));
      printf("%5i: c%-5i   %7g <= %10.4g <= %7g   |   mul=%10.3g\n", k+1, k, bl(nVar+k), constr(k), bu(nVar+k), lambda(nVar+k));
  }


  void RestorationProblem::printInfo() {
    printf("Minimum 2-norm NLP to find a point acceptable to the filter\n");
  }

} // namespace blocksqp
