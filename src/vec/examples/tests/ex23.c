/*$Id: ex23.c,v 1.19 2001/08/07 03:02:26 balay Exp bsmith $*/

static char help[] = "Scatters from a parallel vector to a sequential vector.\n\
  Using a blocked send and a strided receive.\n\n";

/*
        0 1 2 3 | 4 5 6 7 ||  8 9 10 11 

     Scatter first and third block to first processor and 
     second and third block to second processor
*/
#include "petscvec.h"
#include "petscsys.h"

#undef __FUNCT__
#define __FUNCT__ "main"
int main(int argc,char **argv)
{
  int           ierr,i;
  int           size,rank,blocks[2],nlocal;
  PetscScalar   value;
  Vec           x,y;
  IS            is1,is2;
  VecScatter    ctx = 0;

  ierr = PetscInitialize(&argc,&argv,(char*)0,help);CHKERRQ(ierr); 
  ierr = MPI_Comm_size(PETSC_COMM_WORLD,&size);CHKERRQ(ierr);
  ierr = MPI_Comm_rank(PETSC_COMM_WORLD,&rank);CHKERRQ(ierr);

  if (size != 2) SETERRQ(1,"Must run with 2 processors");

  /* create two vectors */
  if (!rank) nlocal = 8;
  else nlocal = 4;
  ierr = VecCreate(PETSC_COMM_WORLD,nlocal,12,&x);CHKERRQ(ierr);
  ierr = VecSetFromOptions(x);CHKERRQ(ierr);
  ierr = VecCreateSeq(PETSC_COMM_SELF,8,&y);CHKERRQ(ierr);

  /* create two index sets */
  if (!rank) {
    blocks[0] = 0; blocks[1] = 8;
  } else {
    blocks[0] = 4; blocks[1] = 8;
  }
  ierr = ISCreateBlock(PETSC_COMM_SELF,4,2,blocks,&is1);CHKERRQ(ierr);
  ierr = ISCreateStride(PETSC_COMM_SELF,8,0,1,&is2);CHKERRQ(ierr);

  for (i=0; i<12; i++) {
    value = i;
    ierr = VecSetValues(x,1,&i,&value,INSERT_VALUES);CHKERRQ(ierr);
  }
  ierr = VecAssemblyBegin(x);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(x);CHKERRQ(ierr);

  ierr = VecScatterCreate(x,is1,y,is2,&ctx);CHKERRQ(ierr);
  ierr = VecScatterBegin(x,y,INSERT_VALUES,SCATTER_FORWARD,ctx);CHKERRQ(ierr);
  ierr = VecScatterEnd(x,y,INSERT_VALUES,SCATTER_FORWARD,ctx);CHKERRQ(ierr);
  ierr = VecScatterDestroy(ctx);CHKERRQ(ierr); 
 
  ierr = PetscSleep(2*rank);CHKERRQ(ierr);
  ierr = VecView(y,PETSC_VIEWER_STDOUT_SELF);CHKERRQ(ierr);

  ierr = VecDestroy(x);CHKERRQ(ierr);
  ierr = VecDestroy(y);CHKERRQ(ierr);
  ierr = ISDestroy(is1);CHKERRQ(ierr);
  ierr = ISDestroy(is2);CHKERRQ(ierr);

  ierr = PetscFinalize();CHKERRQ(ierr);
  return 0;
}
 
