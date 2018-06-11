static char help[] = "Basic problem for multi-rate method.\n";

/*F

\begin{eqnarray}
                 ys' = \frac{ys}{a}\\
                 yf' = ys*cos(bt)\\
\end{eqnarray}

F*/

/*
   Include "petscts.h" so that we can use TS solvers.  Note that this
   file automatically includes:
     petscsys.h       - base PETSc routines   petscvec.h - vectors
     petscmat.h - matrices
     petscis.h     - index sets            petscksp.h - Krylov subspace methods
     petscviewer.h - viewers               petscpc.h  - preconditioners
     petscksp.h   - linear solvers
*/

#include <petscts.h>

typedef struct {
  PetscReal a,b,Tf,dt;
} AppCtx;

/*
     Defines the ODE passed to the ODE solver
*/
static PetscErrorCode RHSFunction(TS ts,PetscReal t,Vec U,Vec F,AppCtx *ctx)
{
  PetscErrorCode    ierr;
  const PetscScalar *u;
  PetscScalar       *f;

  PetscFunctionBegin;
  /*  The next three lines allow us to access the entries of the vectors directly */
  ierr = VecGetArrayRead(U,&u);CHKERRQ(ierr);
  ierr = VecGetArray(F,&f);CHKERRQ(ierr);

  f[0] = u[0]/ctx->a;
  f[1] = u[0]*PetscCosScalar(t*ctx->b);

  ierr = VecRestoreArrayRead(U,&u);CHKERRQ(ierr);
  ierr = VecRestoreArray(F,&f);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/*
   Define the analytic solution for check method easily
 */
static PetscErrorCode sol_true(PetscReal t, Vec U,AppCtx *ctx)
{
  PetscErrorCode    ierr;
  PetscScalar       *u;

  PetscFunctionBegin;
  /* The next allow us to access the entries of the vector directly */
  ierr = VecGetArray(U,&u);CHKERRQ(ierr);

  u[0] = PetscExpScalar(t/ctx->a);
  u[1] = (ctx->a*PetscCosScalar(ctx->b*t)+ctx->a*ctx->a*ctx->b*PetscSinScalar(ctx->b*t))*PetscExpScalar(t/ctx->a)/(1+ctx->a*ctx->a*ctx->b*ctx->b);

  ierr = VecRestoreArray(U,&u);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

int main(int argc,char **argv)
{
  TS             ts;            /* ODE integrator */
  Vec            U;             /* solution will be stored here */
  Vec            Utrue;
  PetscErrorCode ierr;
  PetscMPIInt    size;
  AppCtx         ctx;
  PetscScalar    *u;
  PetscInt       n=2;
  PetscReal      error;

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Initialize program
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  ierr = PetscInitialize(&argc,&argv,(char*)0,help);if (ierr) return ierr;
  ierr = MPI_Comm_size(PETSC_COMM_WORLD,&size);CHKERRQ(ierr);
  if (size > 1) SETERRQ(PETSC_COMM_WORLD,PETSC_ERR_SUP,"Only for sequential runs");

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    Create necesary vector
    - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  ierr = VecCreate(PETSC_COMM_WORLD,&U);CHKERRQ(ierr);
  ierr = VecSetSizes(U,n,PETSC_DETERMINE);CHKERRQ(ierr);
  ierr = VecSetFromOptions(U);CHKERRQ(ierr);
  ierr = VecDuplicate(U,&Utrue);CHKERRQ(ierr);
  ierr = VecCopy(U,Utrue);CHKERRQ(ierr);
  
  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    Set runtime options
    - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  ierr = PetscOptionsBegin(PETSC_COMM_WORLD,NULL,"Swing equation options","");CHKERRQ(ierr);
  {
    ctx.a  = 2.0;
    ctx.b  = 25.0;
    ierr   = PetscOptionsScalar("-a","","",ctx.a,&ctx.a,NULL);CHKERRQ(ierr);
    ierr   = PetscOptionsScalar("-b","","",ctx.b,&ctx.b,NULL);CHKERRQ(ierr);
    ctx.Tf = 5.0;
    ctx.dt = 0.01;
    ierr   = PetscOptionsScalar("-Tf","","",ctx.Tf,&ctx.Tf,NULL);CHKERRQ(ierr);
    ierr   = PetscOptionsScalar("-dt","","",ctx.dt,&ctx.dt,NULL);CHKERRQ(ierr);
  }
 
 ierr = PetscOptionsEnd();CHKERRQ(ierr);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Initialize the solution
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  ierr   = VecGetArray(U,&u);CHKERRQ(ierr);
  u[0]   = 1.0;
  u[1]   = ctx.a/(1+ctx.a*ctx.a*ctx.b*ctx.b);
  ierr   = VecRestoreArray(U,&u);CHKERRQ(ierr);


  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Create timestepping solver context
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  ierr = TSCreate(PETSC_COMM_WORLD,&ts);CHKERRQ(ierr);
  ierr = TSSetProblemType(ts,TS_LINEAR);CHKERRQ(ierr);
  /*ierr = TSSetType(ts,TSRK);CHKERRQ(ierr);*/
  ierr = TSSetType(ts,TSMULTIRATEEULER);CHKERRQ(ierr);
  ierr = TSSetRHSFunction(ts,NULL,(TSRHSFunction)RHSFunction,&ctx);CHKERRQ(ierr);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Set initial conditions
   - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  ierr = TSSetSolution(ts,U);CHKERRQ(ierr);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Set solver options
   - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  ierr = TSSetMaxTime(ts,ctx.Tf);CHKERRQ(ierr);
  ierr = TSSetTimeStep(ts,ctx.dt);CHKERRQ(ierr);
  ierr = TSSetExactFinalTime(ts,TS_EXACTFINALTIME_MATCHSTEP);CHKERRQ(ierr);
  ierr = TSSetFromOptions(ts);CHKERRQ(ierr);       

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Solve linear system
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  ierr = TSSolve(ts,U);CHKERRQ(ierr);
  ierr = VecView(U,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Check the error of the Petsc solution
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  ierr = sol_true(ctx.Tf,Utrue,&ctx);CHKERRQ(ierr);
  ierr = VecAXPY(Utrue,-1.0,U);CHKERRQ(ierr);
  ierr = VecNorm(Utrue,NORM_2,&error);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Print norm2 error
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  ierr = PetscPrintf(PETSC_COMM_WORLD,"error is = %g\n", error);CHKERRQ(ierr);
  
  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Free work space.  All PETSc objects should be destroyed when they are no longer needed.
   - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  ierr = VecDestroy(&U);CHKERRQ(ierr);
  ierr = VecDestroy(&Utrue);CHKERRQ(ierr);
  ierr = TSDestroy(&ts);CHKERRQ(ierr);
  ierr = PetscFinalize();
  return ierr;
}
