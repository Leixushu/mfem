// Copyright (c) 2010, Lawrence Livermore National Security, LLC. Produced at
// the Lawrence Livermore National Laboratory. LLNL-CODE-443211. All Rights
// reserved. See file COPYRIGHT for details.
//
// This file is part of the MFEM library. For more information and source code
// availability see http://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the GNU Lesser General Public License (as published by the Free
// Software Foundation) version 2.1 dated February 1999.

#include "sundials.hpp"

#ifdef MFEM_USE_SUNDIALS

#include "solvers.hpp"
#ifdef MFEM_USE_MPI
#include "hypre.hpp"
#endif

#include <sundials/sundials_config.h>
#include <nvector/nvector_serial.h>
#ifdef MFEM_USE_MPI
#include <nvector/nvector_parallel.h>
#endif

#include <cvode/cvode.h>
#include <sunlinsol/sunlinsol_spgmr.h>

using namespace std;

namespace mfem
{
  // ---------------------------------------------------------------------------
  // SUNMatrix interface functions
  // ---------------------------------------------------------------------------

  // Access the wrapped object in the SUNMatrix
  static inline SundialsODELinearSolver *GetObj(SUNMatrix A)
  {
    return (SundialsODELinearSolver *)(A->content);
  }

  // Return the matrix ID
  SUNMatrix_ID SUNMatGetID(SUNMatrix A)
  {
    return(SUNMATRIX_CUSTOM);
  }

  // ---------------------------------------------------------------------------
  // SUNLinearSolver interface functions
  // ---------------------------------------------------------------------------

  // Access wrapped object in the SUNLinearSolver
  static inline SundialsODELinearSolver *GetObj(SUNLinearSolver LS)
  {
    return (SundialsODELinearSolver *)(LS->content);
  }

  // Return the linear solver type
  static SUNLinearSolver_Type SUNLSGetType(SUNLinearSolver LS)
  {
    return(SUNLINEARSOLVER_MATRIX_ITERATIVE);
  }

  // Initialize the linear solver
  static int SUNLSInit(SUNLinearSolver LS)
  {
    return(GetObj(LS)->LSInit());
  }

  // Setup the linear solver
  static int SUNLSSetup(SUNLinearSolver LS, SUNMatrix A)
  {
    return(GetObj(LS)->LSSetup());
  }

  // Solve the linear system A x = b
  static int SUNLSSolve(SUNLinearSolver LS, SUNMatrix A, N_Vector x, N_Vector b,
                        realtype tol)
  {
    const Vector mfem_x(x);
    Vector mfem_b(b);
    return(GetObj(LS)->LSSolve(mfem_x, mfem_b));
  }

  // ---------------------------------------------------------------------------
  // Wrappers for evaluating the ODE linear system
  // ---------------------------------------------------------------------------

  static int cvLinSysSetup(realtype t, N_Vector y, N_Vector fy, SUNMatrix A,
                           booleantype jok, booleantype *jcur, realtype gamma,
                           void *user_data, N_Vector tmp1, N_Vector tmp2,
                           N_Vector tmp3)
  {
    // Get data from N_Vectors
    const Vector mfem_y(y);
    Vector mfem_fy(fy);

    // Compute the linear system
    return(GetObj(A)->ODELinSys(t, mfem_y, mfem_fy, jok, jcur, gamma));
  }

  static int arkLinSysSetup(realtype t, N_Vector y, N_Vector fy, SUNMatrix A,
                            SUNMatrix M, booleantype jok, booleantype *jcur,
                            realtype gamma, void *user_data, N_Vector tmp1,
                            N_Vector tmp2, N_Vector tmp3)
  {
    // Get data from N_Vectors
    const Vector mfem_y(y);
    Vector mfem_fy(fy);

    // Compute the linear system
    return(GetObj(A)->ODELinSys(t, mfem_y, mfem_fy, jok, jcur, gamma));
  }

  // ---------------------------------------------------------------------------
  // Wrapper for evaluating the ODE right-hand side
  // ---------------------------------------------------------------------------

  int SundialsSolver::ODERhs(realtype t, const N_Vector y, N_Vector ydot,
                             void *user_data)
  {
    // Get data from N_Vectors
    const Vector mfem_y(y);
    Vector mfem_ydot(ydot);

    // Get TimeDependentOperator
    TimeDependentOperator *f = (TimeDependentOperator *)user_data;

    // Compute y' = f1(t, y)
    f->SetTime(t);
    f->Mult(mfem_y, mfem_ydot);

    // Return success
    return(0);
  }

  // ---------------------------------------------------------------------------
  // CVODE interface
  // ---------------------------------------------------------------------------

  CVODESolver::CVODESolver(int lmm)
  {
    // Create the solver memory
    sundials_mem = CVodeCreate(lmm);
    MFEM_ASSERT(sundials_mem, "error in CVodeCreate()");

    // Allocate an empty serial N_Vector
    y = N_VNewEmpty_Serial(0);
    MFEM_ASSERT(y, "error in N_VNewEmpty_Serial()");
  }

#ifdef MFEM_USE_MPI
  CVODESolver::CVODESolver(MPI_Comm comm, int lmm)
  {
    // Create the solver memory
    sundials_mem = CVodeCreate(lmm);
    MFEM_ASSERT(sundials_mem, "error in CVodeCreate()");

    if (comm == MPI_COMM_NULL) {

      // Allocate an empty serial N_Vector
      y = N_VNewEmpty_Serial(0);
      MFEM_ASSERT(y, "error in N_VNewEmpty_Serial()");

    } else {

      // Allocate an empty parallel N_Vector
      y = N_VNewEmpty_Parallel(comm, 0, 0);  // calls MPI_Allreduce()
      MFEM_ASSERT(y, "error in N_VNewEmpty_Parallel()");

    }
  }
#endif

  CVODESolver::Init(TimeDependentOperator &f_)
  {
    mfem_error("CVODE Initialization error: use the initialization method"
      "CVODESolver::Init(TimeDependentOperator &f_, double &t, Vector &x)");
  }

  CVODESolver::Init(TimeDependentOperator &f_, double &t, Vector &x)
  {
    // Check intputs for consistency
    int loc_size = f_.Height();
    MFEM_VERIFY(loc_size == x.Size(),
                "error inconsistent operator and vector size");

    MFEM_VERIFY(f_.GetTime() == t,
                "error inconsistent initial times");

    // Initialize the base class
    ODESolver::Init(f_);

    // Fill N_Vector wrapper with initial condition data
    if (!Parallel()) {
      NV_LENGTH_S(y) = x.Size();
      NV_DATA_S(y)   = x.GetData();
    } else {
#ifdef MFEM_USE_MPI
      long local_size = loc_size, global_size;
      MPI_Allreduce(&local_size, &global_size, 1, MPI_LONG, MPI_SUM,
                    NV_COMM_P(y));
      NV_LOCLENGTH_P(y)  = x.Size();
      NV_GLOBLENGTH_P(y) = x.GetData();
#endif
    }

    // Initialize CVODE
    flag = CVodeInit(sundials_mem, ODERhs, t, y);
    MFEM_ASSERT(flag != CV_SUCCESS, "error in CVodeInit()");

    // Attached the TimeDependentOperator pointer, f, as user-defined data
    flag = CVodeSetUserData(sundials_mem, f);
    MFEM_ASSERT(flag != CV_SUCCESS, "error in CVodeSetUserData()");

    // Set default tolerances
    flag = CVodeSetSStolerances(sundials_mem, default_rel_tol, default_abs_tol);
    MFEM_ASSERT(flag != CV_SUCCESS, "error in CVodeSetSStolerances()");

    // Set default linear solver (Newton is the default Nonlinear Solver)
    LSA = SUNLinSol_SPGMR(y, PREC_NONE, 0);
    MFEM_ASSERT(LSA, "error in SUNLinSol_SPGMR()");

    flag = CVodeSetLinearSolver(sundials_mem, LSA);
    MFEM_ASSERT(flag != CV_SUCCESS, "error in CVodeSetLinearSolver()");
  }

  void CVODESolver::SetLinearSolver(SundialsODELinearSolver &ls_spec)
  {
    // Free any existing linear solver
    if (LSA != NULL) { SUNLinSolFree(LSA); LSA = NULL; }

    // Wrap linear solver as SUNLinearSolver and SUNMatrix
    LSA = SUNLinSolEmpty();
    MFEM_ASSERT(sundials_mem, "error in SUNLinSolEmpty()");

    LSA->content         = &ls_spec;
    LSA->ops->gettype    = SUNLSGetType;
    LSA->ops->initialize = SUNLSInit;
    LSA->ops->setup      = SUNLSSetup;
    LSA->ops->solve      = SUNLSSolve;

    A = SUNMatEmpty();
    MFEM_ASSERT(sundials_mem, "error in SUNMatEmpty()");

    A->content    = &ls_spec;
    A->ops->getid = SUNMatGetID;

    // Attach the linear solver and matrix
    flag = CVodeSetLinearSolver(sundials_mem, LS, A);
    MFEM_ASSERT(flag != CV_SUCCESS, "error in CVodeSetLinearSolver()");

    // Set the linear system function
    flag = CVodeSetLinSysFn(sundials_mem, cvLinSysSetup);
    MFEM_ASSERT(flag != CV_SUCCESS, "error in CVodeSetLinSysFn()");
  }

  void CVODESolver::Step(Vector &x, double &t, double &dt)
  {
    if (!Parallel()) {
      NV_DATA_S(y) = x.GetData();
      MFEM_VERIFY(NV_LENGTH_S(y) == x.Size(), "");
    } else {
#ifdef MFEM_USE_MPI
      NV_DATA_P(y) = x.GetData();
      MFEM_VERIFY(NV_LOCLENGTH_P(y) == x.Size(), "");
#endif
    }

    // Integrate the system
    double tout = t + dt;
    flag = CVode(sundials_mem, tout, y, &t, step_mode);
    MFEM_ASSERT(flag < 0, "error in CVode()");

    // Return the last incremental step size
    flag = CVodeGetLastStep(sundials_mem, &dt);
    MFEM_ASSERT(flag != CV_SUCCESS, "error in CVodeGetLastStep()");
  }

  void CVODESolver::SetStepMode(int itask)
  {
    step_mode = itask;
  }

  CVODESolver::~CVODESolver()
  {
    N_VDestroy(y);
    SUNMatDestroy(A);
    SUNLinSolFree(LSA);
    SUNNonlinSolFree(NLS);
    CVodeFree(&sundials_mem);
  }

  // ---------------------------------------------------------------------------
  // ARKStep interface
  // ---------------------------------------------------------------------------

  ARKStepSolver::ARKStepSolver(Type type)
    : use_implicit(type == IMPLICIT), irk_table(-1), erk_table(-1)
  {
    // Allocate an empty serial N_Vector
    y = N_VNewEmpty_Serial(0);
    MFEM_ASSERT(y, "error in N_VNewEmpty_Serial()");

    flag = ARK_SUCCESS;
  }

#ifdef MFEM_USE_MPI
  ARKStepSolver::ARKStepSolver(MPI_Comm comm, Type type)
    : use_implicit(type == IMPLICIT), irk_table(-1), erk_table(-1)
  {
    if (comm == MPI_COMM_NULL) {

      // Allocate an empty serial N_Vector
      y = N_VNewEmpty_Serial(0);
      MFEM_ASSERT(y, "error in N_VNewEmpty_Serial()");

    } else {

      // Allocate an empty parallel N_Vector
      y = N_VNewEmpty_Parallel(comm, 0, 0);  // calls MPI_Allreduce()
      MFEM_ASSERT(y, "error in N_VNewEmpty_Parallel()");

    }
  }
#endif

  ARKStepSolver::Init(TimeDependentOperator &f_)
  {
    mfem_error("ARKStep Initialization error: use the initialization method"
      "ARKStepSolver::Init(TimeDependentOperator &f_, double &t, Vector &x)");
  }

  ARKStepSolver::Init(TimeDependentOperator &f_, double &t, Vector &x)
  {
    // Check intputs for consistency
    int loc_size = f_.Height();
    MFEM_VERIFY(loc_size == x.Size(),
                "error inconsistent operator and vector size");

    MFEM_VERIFY(f_.GetTime() == t,
                "error inconsistent initial times");

    // Initialize the base class
    ODESolver::Init(f_);

    // Fill N_Vector wrapper with initial condition data
    if (!Parallel()) {
      NV_LENGTH_S(y) = x.Size();
      NV_DATA_S(y)   = x.GetData();
    } else {
#ifdef MFEM_USE_MPI
      long local_size = loc_size, global_size;
      MPI_Allreduce(&local_size, &global_size, 1, MPI_LONG, MPI_SUM,
                    NV_COMM_P(y));
      NV_LOCLENGTH_P(y)  = x.Size();
      NV_GLOBLENGTH_P(y) = x.GetData();
#endif
    }

    // Initialize ARKStep
    if (use_implicit)
      sundials_mem = ARKStepCrate(NULL, ODERhs, t, y);
    else
      sundials_mem = ARKStepCrate(ODERhs, NULL, t, y);
    MFEM_ASSERT(sundials_mem, "error in ARKStepCreate()");

    // Attached the TimeDependentOperator pointer, f, as user-defined data
    flag = ARKStepSetUserData(sundials_mem, f);
    MFEM_ASSERT(flag != CV_SUCCESS, "error in ARKStepSetUserData()");

    // Set default tolerances
    flag = ARKStepSetSStolerances(sundials_mem, default_rel_tol, default_abs_tol);
    MFEM_ASSERT(flag != CV_SUCCESS, "error in ARKStepSetSStolerances()");

    // If implicit, set default linear solver
    if (use_implicit) {
      LSA = SUNLinSol_SPGMR(y, PREC_NONE, 0);
      MFEM_ASSERT(LSA, "error in SUNLinSol_SPGMR()");

      flag = ARKStepSetLinearSolver(sundials_mem, LSA);
      MFEM_ASSERT(flag != CV_SUCCESS, "error in ARKStepSetLinearSolver()");
    }
  }

  void ARKStepSolver::SetLinearSolver(SundialsODELinearSolver &ls_spec)
  {
    // Free any existing linear solver
    if (LSA != NULL) { SUNLinSolFree(LSA); LSA = NULL; }

    // Check for implicit method before attaching
    MFEM_VERIFY(use_implicit,
                "The function is applicable only to implicit time integration.");

    // Wrap linear solver as SUNLinearSolver and SUNMatrix
    LSA = SUNLinSolEmpty();
    MFEM_ASSERT(sundials_mem, "error in SUNLinSolEmpty()");

    LSA->content         = &ls_spec;
    LSA->ops->gettype    = SUNLSGetType;
    LSA->ops->initialize = SUNLSInit;
    LSA->ops->setup      = SUNLSSetup;
    LSA->ops->solve      = SUNLSSolve;

    A = SUNMatEmpty();
    MFEM_ASSERT(sundials_mem, "error in SUNMatEmpty()");

    A->content    = &ls_spec;
    A->ops->getid = SUNMatGetID;

    // Attach the linear solver and matrix
    flag = ARKStepSetLinearSolver(sundials_mem, LS, A);
    MFEM_ASSERT(flag != CV_SUCCESS, "error in ARKStepSetLinearSolver()");

    // Set the linear system function
    flag = ARKStepSetLinSysFn(sundials_mem, arkLinSysSetup);
    MFEM_ASSERT(flag != CV_SUCCESS, "error in ARKStepSetLinSysFn()");
  }

  void ARKStepSolver::Step(Vector &x, double &t, double &dt)
  {
    if (!Parallel()) {
      NV_DATA_S(y) = x.GetData();
      MFEM_VERIFY(NV_LENGTH_S(y) == x.Size(), "");
    } else {
#ifdef MFEM_USE_MPI
      NV_DATA_P(y) = x.GetData();
      MFEM_VERIFY(NV_LOCLENGTH_P(y) == x.Size(), "");
#endif
    }

    // Integrate the system
    double tout = t + dt;
    flag = ARKStepEvolve(sundials_mem, tout, y, &t, step_mode);
    MFEM_ASSERT(flag < 0, "error in ARKStepEvolve()");

    // Return the last incremental step size
    flag = ARKStepGetLastStep(sundials_mem, &dt);
    MFEM_ASSERT(flag != CV_SUCCESS, "error in ARKStepGetLastStep()");
  }

  void ARKStepSolver::SetStepMode(int itask)
  {
    step_mode = itask;
  }

  ARKStepSolver::~ARKStepSolver()
  {
    N_VDestroy(y);
    SUNMatDestroy(A);
    SUNLinSolFree(LSA);
    SUNNonlinSolFree(NLS);
    ARKStepFree(&sundials_mem);
  }

} // namespace mfem

#endif