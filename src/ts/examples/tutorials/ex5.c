#ifdef PETSC_RCS_HEADER
static char vcid[] = "$Id: ex5.c,v 1.6 1999/05/04 20:36:56 balay Exp balay $";
#endif

/* Program usage:  ex3 [-help] [all PETSc options] */

static char help[] ="Solves a simple time-dependent linear PDE (the heat equation).\n\
Input parameters include:\n\
  -m <points>, where <points> = number of grid points\n\
  -time_dependent_rhs : Treat the problem as having a time-dependent right-hand side\n\
  -debug              : Activate debugging printouts\n\
  -nox                : Deactivate x-window graphics\n\n";

/*
   Concepts: TS^time-dependent linear problems
   Concepts: TS^heat equation
   Concepts: TS^diffusion equation
   Routines: TSCreate(); TSSetSolution(); TSSetRHSMatrix();
   Routines: TSSetInitialTimeStep(); TSSetDuration(); TSSetMonitor();
   Routines: TSSetFromOptions(); TSStep(); TSDestroy(); 
   Processors: 1
*/

/* ------------------------------------------------------------------------

   This program solves the one-dimensional heat equation (also called the
   diffusion equation),
       u_t = u_xx, 
   on the domain 0 <= x <= 1, with the boundary conditions
       u(t,0) = 1, u(t,1) = 1,
   and the initial condition
       u(0,x) = cos(6*pi*x) + 3*cos(2*pi*x).
   This is a linear, second-order, parabolic equation.

   We discretize the right-hand side using finite differences with
   uniform grid spacing h:
       u_xx = (u_{i+1} - 2u_{i} + u_{i-1})/(h^2)
   We then demonstrate time evolution using the various TS methods by
   running the program via
       ex3 -ts_type <timestepping solver>

   We compare the approximate solution with the exact solution, given by
       u_exact(x,t) = exp(-36*pi*pi*t) * cos(6*pi*x) +
                      3*exp(-4*pi*pi*t) * cos(2*pi*x)

   Notes:
   This code demonstrates the TS solver interface to two variants of 
   linear problems, u_t = f(u,t), namely
     - time-dependent f:   f(u,t) is a function of t
     - time-independent f: f(u,t) is simply just f(u)

    The parallel version of this code is ts/examples/tutorials/ex4.c

  ------------------------------------------------------------------------- */

/* 
   Include "ts.h" so that we can use TS solvers.  Note that this file
   automatically includes:
     petsc.h  - base PETSc routines   vec.h  - vectors
     sys.h    - system routines       mat.h  - matrices
     is.h     - index sets            ksp.h  - Krylov subspace methods
     viewer.h - viewers               pc.h   - preconditioners
     sles.h   - linear solvers        snes.h - nonlinear solvers
*/

#include "ts.h"

/* 
   User-defined application context - contains data needed by the 
   application-provided call-back routines.
*/
typedef struct {
  Vec      solution;          /* global exact solution vector */
  int      m;                 /* total number of grid points */
  double   h;                 /* mesh width h = 1/(m-1) */
  int      debug;             /* flag (1 indicates activation of debugging printouts) */
  Viewer   viewer1, viewer2;  /* viewers for the solution and error */
  double   norm_2, norm_max;  /* error norms */
} AppCtx;

/* 
   User-defined routines
*/
extern int InitialConditions(Vec,AppCtx*);
extern int RHSMatrixHeat(TS,double,Mat*,Mat*,MatStructure*,void*);
extern int Monitor(TS,int,double,Vec,void*);
extern int ExactSolution(double,Vec,AppCtx*);

#undef __FUNC__
#define __FUNC__ "main"
int main(int argc,char **argv)
{
  AppCtx        appctx;                 /* user-defined application context */
  TS            ts;                     /* timestepping context */
  Mat           A;                      /* matrix data structure */
  Vec           u;                      /* approximate solution vector */
  double        time_total_max = 100.0; /* default max total time */
  int           time_steps_max = 100;   /* default max timesteps */
  Draw          draw;                   /* drawing context */
  int           ierr,  steps, flg, size, m;
  double        dt, ftime;

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Initialize program and set problem parameters
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
 
  PetscInitialize(&argc,&argv,(char*)0,help);
  MPI_Comm_size(PETSC_COMM_WORLD,&size);
  if (size != 1) SETERRA(1,0,"This is a uniprocessor example only!");

  m    = 60;
  ierr = OptionsGetInt(PETSC_NULL,"-m",&m,&flg);CHKERRA(ierr);
  ierr = OptionsHasName(PETSC_NULL,"-debug",&appctx.debug);CHKERRA(ierr);    
  appctx.m        = m;
  appctx.h        = 1.0/(m-1.0);
  appctx.norm_2   = 0.0;
  appctx.norm_max = 0.0;
  PetscPrintf(PETSC_COMM_SELF,"Solving a linear TS problem on 1 processor\n");

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Create vector data structures
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

  /* 
     Create vector data structures for approximate and exact solutions
  */
  ierr = VecCreateSeq(PETSC_COMM_SELF,m,&u);CHKERRA(ierr);
  ierr = VecDuplicate(u,&appctx.solution);CHKERRA(ierr);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Set up displays to show graphs of the solution and error 
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

  ierr = ViewerDrawOpen(PETSC_COMM_SELF,0,"",80,380,400,160,&appctx.viewer1);CHKERRA(ierr);
  ierr = ViewerDrawGetDraw(appctx.viewer1,0,&draw);CHKERRA(ierr);
  ierr = DrawSetDoubleBuffer(draw);CHKERRA(ierr);   
  ierr = ViewerDrawOpen(PETSC_COMM_SELF,0,"",80,0,400,160,&appctx.viewer2);CHKERRA(ierr);
  ierr = ViewerDrawGetDraw(appctx.viewer2,0,&draw);CHKERRA(ierr);
  ierr = DrawSetDoubleBuffer(draw);CHKERRA(ierr);   

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Create timestepping solver context
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

  ierr = TSCreate(PETSC_COMM_SELF,TS_LINEAR,&ts);CHKERRA(ierr);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Set optional user-defined monitoring routine
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

  ierr = TSSetMonitor(ts,Monitor,&appctx);CHKERRA(ierr);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

     Create matrix data structure; set matrix evaluation routine.
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

  ierr = MatCreate(PETSC_COMM_SELF,PETSC_DECIDE,PETSC_DECIDE,m,m,&A);CHKERRA(ierr);

  ierr = OptionsHasName(PETSC_NULL,"-time_dependent_rhs",&flg);CHKERRA(ierr);
  if (flg) {
    /*
       For linear problems with a time-dependent f(u,t) in the equation 
       u_t = f(u,t), the user provides the discretized right-hand-side
       as a time-dependent matrix.
    */
    ierr = TSSetRHSMatrix(ts,A,A,RHSMatrixHeat,&appctx);CHKERRA(ierr);
  } else {
    /*
       For linear problems with a time-independent f(u) in the equation 
       u_t = f(u), the user provides the discretized right-hand-side
       as a matrix only once, and then sets a null matrix evaluation
       routine.
    */
    MatStructure A_structure;
    ierr = RHSMatrixHeat(ts,0.0,&A,&A,&A_structure,&appctx); CHKERRA(ierr);
    ierr = TSSetRHSMatrix(ts,A,A,PETSC_NULL,&appctx);CHKERRA(ierr);
  }

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Set solution vector and initial timestep
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

  dt = appctx.h*appctx.h/2.0;
  ierr = TSSetInitialTimeStep(ts,0.0,dt);CHKERRA(ierr);
  ierr = TSSetSolution(ts,u);CHKERRA(ierr);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Customize timestepping solver:  
       - Set the solution method to be the Backward Euler method.
       - Set timestepping duration info 
     Then set runtime options, which can override these defaults.
     For example,
          -ts_max_steps <maxsteps> -ts_max_time <maxtime>
     to override the defaults set by TSSetDuration().
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

  ierr = TSSetDuration(ts,time_steps_max,time_total_max);CHKERRA(ierr);
  ierr = TSSetFromOptions(ts);CHKERRA(ierr);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Solve the problem
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

  /*
     Evaluate initial conditions
  */
  ierr = InitialConditions(u,&appctx);CHKERRA(ierr);

  /*
     Run the timestepping solver
  */
  ierr = TSStep(ts,&steps,&ftime);CHKERRA(ierr);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     View timestepping solver info
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

  PetscPrintf(PETSC_COMM_SELF,"avg. error (2 norm) = %g, avg. error (max norm) = %g\n",
              appctx.norm_2/steps,appctx.norm_max/steps);
  ierr = TSView(ts,VIEWER_STDOUT_SELF);CHKERRA(ierr);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Free work space.  All PETSc objects should be destroyed when they
     are no longer needed.
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

  ierr = TSDestroy(ts);CHKERRA(ierr);
  ierr = MatDestroy(A);CHKERRA(ierr);
  ierr = VecDestroy(u);CHKERRA(ierr);
  ierr = ViewerDestroy(appctx.viewer1);CHKERRA(ierr);
  ierr = ViewerDestroy(appctx.viewer2);CHKERRA(ierr);
  ierr = VecDestroy(appctx.solution);CHKERRA(ierr);

  /*
     Always call PetscFinalize() before exiting a program.  This routine
       - finalizes the PETSc libraries as well as MPI
       - provides summary and diagnostic information if certain runtime
         options are chosen (e.g., -log_summary). 
  */
  PetscFinalize();
  return 0;
}
/* --------------------------------------------------------------------- */
#undef __FUNC__
#define __FUNC__ "InitialConditions"
/*
   InitialConditions - Computes the solution at the initial time. 

   Input Parameter:
   u - uninitialized solution vector (global)
   appctx - user-defined application context

   Output Parameter:
   u - vector with solution at initial time (global)
*/ 
int InitialConditions(Vec u,AppCtx *appctx)
{
  Scalar *u_localptr, h = appctx->h;
  int    i, ierr;

  /* 
    Get a pointer to vector data.
    - For default PETSc vectors, VecGetArray() returns a pointer to
      the data array.  Otherwise, the routine is implementation dependent.
    - You MUST call VecRestoreArray() when you no longer need access to
      the array.
    - Note that the Fortran interface to VecGetArray() differs from the
      C version.  See the users manual for details.
  */
  ierr = VecGetArray(u,&u_localptr);CHKERRQ(ierr);

  /* 
     We initialize the solution array by simply writing the solution
     directly into the array locations.  Alternatively, we could use
     VecSetValues() or VecSetValuesLocal().
  */
  for (i=0; i<appctx->m; i++) {
    u_localptr[i] = cos(PETSC_PI*i*6.*h) + 3.*cos(PETSC_PI*i*2.*h);
  }

  /* 
     Restore vector
  */
  ierr = VecRestoreArray(u,&u_localptr);CHKERRQ(ierr);

  /* 
     Print debugging information if desired
  */
  if (appctx->debug) {
     printf("initial guess vector\n");
     ierr = VecView(u,VIEWER_STDOUT_SELF);CHKERRQ(ierr);
  }

  return 0;
}
/* --------------------------------------------------------------------- */
#undef __FUNC__
#define __FUNC__ "ExactSolution"
/*
   ExactSolution - Computes the exact solution at a given time.

   Input Parameters:
   t - current time
   solution - vector in which exact solution will be computed
   appctx - user-defined application context

   Output Parameter:
   solution - vector with the newly computed exact solution
*/
int ExactSolution(double t,Vec solution,AppCtx *appctx)
{
  Scalar *s_localptr, h = appctx->h, ex1, ex2, sc1, sc2;
  int    i, ierr;

  /*
     Get a pointer to vector data.
  */
  ierr = VecGetArray(solution,&s_localptr);CHKERRQ(ierr);

  /* 
     Simply write the solution directly into the array locations.
     Alternatively, we culd use VecSetValues() or VecSetValuesLocal().
  */
  ex1 = exp(-36.*PETSC_PI*PETSC_PI*t); ex2 = exp(-4.*PETSC_PI*PETSC_PI*t);
  sc1 = PETSC_PI*6.*h;                 sc2 = PETSC_PI*2.*h;
  for (i=0; i<appctx->m; i++) {
    s_localptr[i] = cos(sc1*(double)i)*ex1 + 3.*cos(sc2*(double)i)*ex2;
  }

  /* 
     Restore vector
  */
  ierr = VecRestoreArray(solution,&s_localptr);CHKERRQ(ierr);
  return 0;
}
/* --------------------------------------------------------------------- */
#undef __FUNC__
#define __FUNC__ "Monitor"
/*
   Monitor - User-provided routine to monitor the solution computed at 
   each timestep.  This example plots the solution and computes the
   error in two different norms.

   Input Parameters:
   ts     - the timestep context
   step   - the count of the current step (with 0 meaning the
             initial condition)
   time   - the current time
   u      - the solution at this timestep
   ctx    - the user-provided context for this monitoring routine.
            In this case we use the application context which contains 
            information about the problem size, workspace and the exact 
            solution.
*/
int Monitor(TS ts,int step,double time,Vec u,void *ctx)
{
  AppCtx   *appctx = (AppCtx*) ctx;   /* user-defined application context */
  int      ierr;
  double   norm_2,norm_max;
  Scalar   mone = -1.0;

  /* 
     View a graph of the current iterate
  */
  ierr = VecView(u,appctx->viewer2);CHKERRQ(ierr);

  /* 
     Compute the exact solution
  */
  ierr = ExactSolution(time,appctx->solution,appctx);CHKERRQ(ierr);

  /*
     Print debugging information if desired
  */
  if (appctx->debug) {
     printf("Computed solution vector\n");
     ierr = VecView(u,VIEWER_STDOUT_SELF);CHKERRQ(ierr);
     printf("Exact solution vector\n");
     ierr = VecView(appctx->solution,VIEWER_STDOUT_SELF);CHKERRQ(ierr);
  }

  /*
     Compute the 2-norm and max-norm of the error
  */
  ierr = VecAXPY(&mone,u,appctx->solution);CHKERRQ(ierr);
  ierr = VecNorm(appctx->solution,NORM_2,&norm_2);CHKERRQ(ierr);
  norm_2 = sqrt(appctx->h)*norm_2;
  ierr = VecNorm(appctx->solution,NORM_MAX,&norm_max);CHKERRQ(ierr);

  printf("Timestep %d: time = %g, 2-norm error = %g, max norm error = %g\n",
         step,time,norm_2,norm_max);
  appctx->norm_2   += norm_2;
  appctx->norm_max += norm_max;

  /* 
     View a graph of the error
  */
  ierr = VecView(appctx->solution,appctx->viewer1);CHKERRQ(ierr);

  /*
     Print debugging information if desired
  */
  if (appctx->debug) {
     printf("Error vector\n");
     ierr = VecView(appctx->solution,VIEWER_STDOUT_SELF);CHKERRQ(ierr);
  }

  return 0;
}
/* --------------------------------------------------------------------- */
#undef __FUNC__
#define __FUNC__ "RHSMatrixHeat"
/*
   RHSMatrixHeat - User-provided routine to compute the right-hand-side
   matrix for the heat equation.

   Input Parameters:
   ts - the TS context
   t - current time
   global_in - global input vector
   dummy - optional user-defined context, as set by TSetRHSJacobian()

   Output Parameters:
   AA - Jacobian matrix
   BB - optionally different preconditioning matrix
   str - flag indicating matrix structure

  Notes:
  Recall that MatSetValues() uses 0-based row and column numbers
  in Fortran as well as in C.
*/
int RHSMatrixHeat(TS ts,double t,Mat *AA,Mat *BB,MatStructure *str,void *ctx)
{
  Mat    A = *AA;                      /* Jacobian matrix */
  AppCtx *appctx = (AppCtx *) ctx;     /* user-defined application context */
  int    mstart = 0;
  int    mend = appctx->m;
  int    ierr, i, idx[3];
  Scalar v[3], stwo = -2./(appctx->h*appctx->h), sone = -.5*stwo;

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Compute entries for the locally owned part of the matrix
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  /* 
     Set matrix rows corresponding to boundary data
  */

  mstart = 0;
  v[0] = 1.0;
  ierr = MatSetValues(A,1,&mstart,1,&mstart,v,INSERT_VALUES);CHKERRQ(ierr);
  mstart++;

  mend--;
  v[0] = 1.0;
  ierr = MatSetValues(A,1,&mend,1,&mend,v,INSERT_VALUES);CHKERRQ(ierr);

  /*
     Set matrix rows corresponding to interior data.  We construct the 
     matrix one row at a time.
  */
  v[0] = sone; v[1] = stwo; v[2] = sone;  
  for ( i=mstart; i<mend; i++ ) {
    idx[0] = i-1; idx[1] = i; idx[2] = i+1;
    ierr = MatSetValues(A,1,&i,3,idx,v,INSERT_VALUES);CHKERRQ(ierr);
  }

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Complete the matrix assembly process and set some options
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  /*
     Assemble matrix, using the 2-step process:
       MatAssemblyBegin(), MatAssemblyEnd()
     Computations can be done while messages are in transition
     by placing code between these two statements.
  */
  ierr = MatAssemblyBegin(A,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = MatAssemblyEnd(A,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);

  /*
     Set flag to indicate that the Jacobian matrix retains an identical
     nonzero structure throughout all timestepping iterations (although the
     values of the entries change). Thus, we can save some work in setting
     up the preconditioner (e.g., no need to redo symbolic factorization for
     ILU/ICC preconditioners).
      - If the nonzero structure of the matrix is different during
        successive linear solves, then the flag DIFFERENT_NONZERO_PATTERN
        must be used instead.  If you are unsure whether the matrix
        structure has changed or not, use the flag DIFFERENT_NONZERO_PATTERN.
      - Caution:  If you specify SAME_NONZERO_PATTERN, PETSc
        believes your assertion and does not check the structure
        of the matrix.  If you erroneously claim that the structure
        is the same when it actually is not, the new preconditioner
        will not function correctly.  Thus, use this optimization
        feature with caution!
  */
  *str = SAME_NONZERO_PATTERN;

  /*
     Set and option to indicate that we will never add a new nonzero location 
     to the matrix. If we do, it will generate an error.
  */
  ierr = MatSetOption(A,MAT_NEW_NONZERO_LOCATION_ERR);CHKERRQ(ierr);

  return 0;
}





