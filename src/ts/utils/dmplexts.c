#include <petsc/private/dmpleximpl.h> /*I "petscdmplex.h" I*/
#include <petsc/private/tsimpl.h>     /*I "petscts.h" I*/
#include <petsc/private/snesimpl.h>
#include <petscds.h>
#include <petscfv.h>

#undef __FUNCT__
#define __FUNCT__ "DMTSConvertPlex"
static PetscErrorCode DMTSConvertPlex(DM dm, DM *plex, PetscBool copy)
{
  PetscBool      isPlex;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = PetscObjectTypeCompare((PetscObject)dm,DMPLEX,&isPlex);CHKERRQ(ierr);
  if (isPlex) {
    *plex = dm;
    ierr = PetscObjectReference((PetscObject)dm);CHKERRQ(ierr);
  }
  else {
    ierr = DMConvert(dm,DMPLEX,plex);CHKERRQ(ierr);
    if (copy) {
      PetscInt    i;
      PetscObject obj;
      const char *comps[3] = {"A","dmAux","dmCh"};

      ierr = DMCopyDMTS(dm,*plex);CHKERRQ(ierr);
      ierr = DMCopyDMSNES(dm,*plex);CHKERRQ(ierr);
      for (i = 0; i < 3; i++) {
        ierr = PetscObjectQuery((PetscObject)dm,comps[i],&obj);CHKERRQ(ierr);
        ierr = PetscObjectCompose((PetscObject)*plex,comps[i],obj);CHKERRQ(ierr);
      }
    }
  }
  PetscFunctionReturn(0);
}


#undef __FUNCT__
#define __FUNCT__ "DMPlexTSGetGeometryFVM"
/*@
  DMPlexTSGetGeometryFVM - Return precomputed geometric data

  Input Parameter:
. dm - The DM

  Output Parameters:
+ facegeom - The values precomputed from face geometry
. cellgeom - The values precomputed from cell geometry
- minRadius - The minimum radius over the mesh of an inscribed sphere in a cell

  Level: developer

.seealso: DMPlexTSSetRHSFunctionLocal()
@*/
PetscErrorCode DMPlexTSGetGeometryFVM(DM dm, Vec *facegeom, Vec *cellgeom, PetscReal *minRadius)
{
  DMTS           dmts;
  PetscObject    obj;
  DM             plex;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm,DM_CLASSID,1);
  ierr = DMTSConvertPlex(dm,&plex,PETSC_TRUE);CHKERRQ(ierr);
  ierr = DMGetDMTS(plex, &dmts);CHKERRQ(ierr);
  ierr = PetscObjectQuery((PetscObject) dmts, "DMPlexTS_facegeom_fvm", &obj);CHKERRQ(ierr);
  if (!obj) {
    Vec cellgeom, facegeom;

    ierr = DMPlexComputeGeometryFVM(plex, &cellgeom, &facegeom);CHKERRQ(ierr);
    ierr = PetscObjectCompose((PetscObject) dmts, "DMPlexTS_facegeom_fvm", (PetscObject) facegeom);CHKERRQ(ierr);
    ierr = PetscObjectCompose((PetscObject) dmts, "DMPlexTS_cellgeom_fvm", (PetscObject) cellgeom);CHKERRQ(ierr);
    ierr = VecDestroy(&facegeom);CHKERRQ(ierr);
    ierr = VecDestroy(&cellgeom);CHKERRQ(ierr);
  }
  if (facegeom) {PetscValidPointer(facegeom, 2); ierr = PetscObjectQuery((PetscObject) dmts, "DMPlexTS_facegeom_fvm", (PetscObject *) facegeom);CHKERRQ(ierr);}
  if (cellgeom) {PetscValidPointer(cellgeom, 3); ierr = PetscObjectQuery((PetscObject) dmts, "DMPlexTS_cellgeom_fvm", (PetscObject *) cellgeom);CHKERRQ(ierr);}
  if (minRadius) {ierr = DMPlexGetMinRadius(plex, minRadius);CHKERRQ(ierr);}
  ierr = DMDestroy(&plex);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "DMPlexTSGetGradientDM"
/*@C
  DMPlexTSGetGradientDM - Return gradient data layout

  Input Parameters:
+ dm - The DM
- fv - The PetscFV

  Output Parameter:
. dmGrad - The layout for gradient values

  Level: developer

.seealso: DMPlexTSGetGeometryFVM(), DMPlexTSSetRHSFunctionLocal()
@*/
PetscErrorCode DMPlexTSGetGradientDM(DM dm, PetscFV fv, DM *dmGrad)
{
  DMTS           dmts;
  PetscObject    obj;
  PetscBool      computeGradients;
  DM             plex;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm,DM_CLASSID,1);
  PetscValidHeaderSpecific(fv,PETSCFV_CLASSID,2);
  PetscValidPointer(dmGrad,3);
  ierr = PetscFVGetComputeGradients(fv, &computeGradients);CHKERRQ(ierr);
  if (!computeGradients) {*dmGrad = NULL; PetscFunctionReturn(0);}
  ierr = DMTSConvertPlex(dm,&plex,PETSC_TRUE);CHKERRQ(ierr);
  ierr = DMGetDMTS(plex, &dmts);CHKERRQ(ierr);
  ierr = PetscObjectQuery((PetscObject) dmts, "DMPlexTS_dmgrad_fvm", &obj);CHKERRQ(ierr);
  if (!obj) {
    DM  dmGrad;
    Vec faceGeometry, cellGeometry;

    ierr = DMPlexTSGetGeometryFVM(plex, &faceGeometry, &cellGeometry, NULL);CHKERRQ(ierr);
    ierr = DMPlexComputeGradientFVM(plex, fv, faceGeometry, cellGeometry, &dmGrad);CHKERRQ(ierr);
    ierr = PetscObjectCompose((PetscObject) dmts, "DMPlexTS_dmgrad_fvm", (PetscObject) dmGrad);CHKERRQ(ierr);
    ierr = DMDestroy(&dmGrad);CHKERRQ(ierr);
  }
  ierr = PetscObjectQuery((PetscObject) dmts, "DMPlexTS_dmgrad_fvm", (PetscObject *) dmGrad);CHKERRQ(ierr);
  ierr = DMDestroy(&plex);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "DMPlexTSComputeRHSFunctionFVM"
/*@
  DMPlexTSComputeRHSFunctionFVM - Form the local forcing F from the local input X using pointwise functions specified by the user

  Input Parameters:
+ dm - The mesh
. t - The time
. locX  - Local solution
- user - The user context

  Output Parameter:
. F  - Global output vector

  Level: developer

.seealso: DMPlexComputeJacobianActionFEM()
@*/
PetscErrorCode DMPlexTSComputeRHSFunctionFVM(DM dm, PetscReal time, Vec locX, Vec F, void *user)
{
  Vec            locF;
  PetscInt       cStart, cEnd, cEndInterior;
  DM             plex;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = DMTSConvertPlex(dm,&plex,PETSC_TRUE);CHKERRQ(ierr);
  ierr = DMPlexGetHeightStratum(plex, 0, &cStart, &cEnd);CHKERRQ(ierr);
  ierr = DMPlexGetHybridBounds(plex, &cEndInterior, NULL, NULL, NULL);CHKERRQ(ierr);
  cEnd = cEndInterior < 0 ? cEnd : cEndInterior;
  ierr = DMGetLocalVector(plex, &locF);CHKERRQ(ierr);
  ierr = VecZeroEntries(locF);CHKERRQ(ierr);
  ierr = DMPlexComputeResidual_Internal(plex, cStart, cEnd, time, locX, NULL, locF, user);CHKERRQ(ierr);
  ierr = DMLocalToGlobalBegin(plex, locF, INSERT_VALUES, F);CHKERRQ(ierr);
  ierr = DMLocalToGlobalEnd(plex, locF, INSERT_VALUES, F);CHKERRQ(ierr);
  ierr = DMRestoreLocalVector(plex, &locF);CHKERRQ(ierr);
  ierr = DMDestroy(&plex);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "DMPlexTSComputeBoundary"
/*@
  DMPlexTSComputeBoundary - Insert the essential boundary values for the local input X and/or its time derivative X_t using pointwise functions specified by the user

  Input Parameters:
+ dm - The mesh
. t - The time
. locX  - Local solution
. locX_t - Local solution time derivative, or NULL
- user - The user context

  Level: developer

.seealso: DMPlexComputeJacobianActionFEM()
@*/
PetscErrorCode DMPlexTSComputeBoundary(DM dm, PetscReal time, Vec locX, Vec locX_t, void *user)
{
  DM             plex;
  Vec            faceGeometryFVM = NULL;
  PetscInt       Nf, f;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = DMTSConvertPlex(dm, &plex, PETSC_TRUE);CHKERRQ(ierr);
  ierr = DMGetNumFields(plex, &Nf);CHKERRQ(ierr);
  if (!locX_t) {
    /* This is the RHS part */
    for (f = 0; f < Nf; f++) {
      PetscObject  obj;
      PetscClassId id;

      ierr = DMGetField(plex, f, &obj);CHKERRQ(ierr);
      ierr = PetscObjectGetClassId(obj, &id);CHKERRQ(ierr);
      if (id == PETSCFV_CLASSID) {
        ierr = DMPlexSNESGetGeometryFVM(plex, &faceGeometryFVM, NULL, NULL);CHKERRQ(ierr);
        break;
      }
    }
  }
  ierr = DMPlexInsertBoundaryValues(plex, PETSC_TRUE, locX, time, faceGeometryFVM, NULL, NULL);CHKERRQ(ierr);
  /* TODO: locX_t */
  ierr = DMDestroy(&plex);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "DMPlexTSComputeIFunctionFEM"
/*@
  DMPlexTSComputeIFunctionFEM - Form the local residual F from the local input X using pointwise functions specified by the user

  Input Parameters:
+ dm - The mesh
. t - The time
. locX  - Local solution
. locX_t - Local solution time derivative, or NULL
- user - The user context

  Output Parameter:
. locF  - Local output vector

  Level: developer

.seealso: DMPlexComputeJacobianActionFEM()
@*/
PetscErrorCode DMPlexTSComputeIFunctionFEM(DM dm, PetscReal time, Vec locX, Vec locX_t, Vec locF, void *user)
{
  PetscInt       cStart, cEnd, cEndInterior;
  DM             plex;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = DMTSConvertPlex(dm,&plex,PETSC_TRUE);CHKERRQ(ierr);
  ierr = DMPlexGetHeightStratum(plex, 0, &cStart, &cEnd);CHKERRQ(ierr);
  ierr = DMPlexGetHybridBounds(plex, &cEndInterior, NULL, NULL, NULL);CHKERRQ(ierr);
  cEnd = cEndInterior < 0 ? cEnd : cEndInterior;
  ierr = DMPlexComputeResidual_Internal(plex, cStart, cEnd, time, locX, locX_t, locF, user);CHKERRQ(ierr);
  ierr = DMDestroy(&plex);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "DMPlexTSComputeIJacobianFEM"
/*@
  DMPlexTSComputeIJacobianFEM - Form the local Jacobian J from the local input X using pointwise functions specified by the user

  Input Parameters:
+ dm - The mesh
. t - The time
. locX  - Local solution
. locX_t - Local solution time derivative, or NULL
. X_tshift - The multiplicative parameter for dF/du_t
- user - The user context

  Output Parameter:
. locF  - Local output vector

  Level: developer

.seealso: DMPlexComputeJacobianActionFEM()
@*/
PetscErrorCode DMPlexTSComputeIJacobianFEM(DM dm, PetscReal time, Vec locX, Vec locX_t, PetscReal X_tShift, Mat Jac, Mat JacP, void *user)
{
  PetscInt       cStart, cEnd, cEndInterior;
  DM             plex;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = DMTSConvertPlex(dm,&plex,PETSC_TRUE);CHKERRQ(ierr);
  ierr = DMPlexGetHeightStratum(plex, 0, &cStart, &cEnd);CHKERRQ(ierr);
  ierr = DMPlexGetHybridBounds(plex, &cEndInterior, NULL, NULL, NULL);CHKERRQ(ierr);
  cEnd = cEndInterior < 0 ? cEnd : cEndInterior;
  ierr = DMPlexComputeJacobian_Internal(plex, cStart, cEnd, time, X_tShift, locX, locX_t, Jac, JacP, user);CHKERRQ(ierr);
  ierr = DMDestroy(&plex);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "DMTSCheckFromOptions"
PetscErrorCode DMTSCheckFromOptions(TS ts, Vec u, PetscErrorCode (**exactFuncs)(PetscInt dim, PetscReal time, const PetscReal x[], PetscInt Nf, PetscScalar *u, void *ctx), void **ctxs)
{
  DM             dm;
  SNES           snes;
  Vec            sol;
  PetscBool      check;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = PetscOptionsHasName(((PetscObject)ts)->options,((PetscObject)ts)->prefix, "-dmts_check", &check);CHKERRQ(ierr);
  if (!check) PetscFunctionReturn(0);
  ierr = VecDuplicate(u, &sol);CHKERRQ(ierr);
  ierr = TSSetSolution(ts, sol);CHKERRQ(ierr);
  ierr = TSGetDM(ts, &dm);CHKERRQ(ierr);
  ierr = TSSetUp(ts);CHKERRQ(ierr);
  ierr = TSGetSNES(ts, &snes);CHKERRQ(ierr);
  ierr = SNESSetSolution(snes, sol);CHKERRQ(ierr);
  ierr = DMSNESCheckFromOptions_Internal(snes, dm, u, sol, exactFuncs, ctxs);CHKERRQ(ierr);
  ierr = VecDestroy(&sol);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}
