!
!  $Id: ts.h,v 1.15 1999/03/24 22:25:56 balay Exp balay $;
!
!  Include file for Fortran use of the TS (timestepping) package in PETSc
!
#if !defined (__TS_H)
#define __TS_H

#define TS                     PetscFortranAddr
#define TSProblemType          integer 
#define TSPVodeType            integer
#define TSPVodeGramSchmitdType integer
#define TSType                 character*(80)

#define TS_EULER            'euler'
#define TS_BEULER           'beuler'
#define TS_PSEUDO           'pseudo'
#define TS_PVODE            'pvode'
#define TS_CRANK_NICHOLSON  'crank-nicholson'

#endif
!
!  TSProblemType
!
      integer TS_LINEAR, TS_NONLINEAR
      parameter (TS_LINEAR = 0, TS_NONLINEAR = 1)
!
!  TSPvodeType
!
      integer PVODE_ADAMS, PVODE_BDF
      parameter (PVODE_ADAMS=0, PVODE_BDF=1)
!
!  TSPvodeGramSchmidtType
!
      integer PVODE_MODIFIED_GS,PVODE_CLASSICAL_GS,PVODE_UNMODIFIED_GS

      parameter (PVODE_MODIFIED_GS=0,PVODE_CLASSICAL_GS=1)
      parameter (PVODE_UNMODIFIED_GS=1)
!
!  Some PETSc fortran functions that the user might pass as arguments
!
      external TSDEFAULTCOMPUTEJACOBIAN
      external TSDEFAULTCOMPUTEJACOBIANCOLOR
!
!  End of Fortran include file for the TS package in PETSc

