/* Using Modified Sparse Row (MSR) storage.
See page 85, "Iterative Methods ..." by Saad. */

/*$Id: sbaijfact.c,v 1.18 2000/09/20 15:03:18 hzhang Exp hzhang $*/
/*
    Factorization code for SBAIJ format. 
*/
#include "sbaij.h"
#include "src/mat/impls/baij/seq/baij.h" 
#include "src/vec/vecimpl.h"
#include "src/inline/ilu.h"
#include "include/petscis.h"

#undef __FUNC__  
#define __FUNC__ "MatCholeskyFactorSymbolic_SeqSBAIJ"
int MatCholeskyFactorSymbolic_SeqSBAIJ(Mat A,IS perm,PetscReal f,Mat *B)
{
  Mat_SeqSBAIJ *a = (Mat_SeqSBAIJ*)A->data,*b;
  int         *rip,ierr,i,mbs = a->mbs,*ai,*aj;
  int         *jutmp,bs = a->bs,bs2=a->bs2;
  int         m,nzi,realloc = 0;
  int         *jl,*q,jumin,jmin,jmax,juptr,nzk,qm,*iu,*ju,k,j,vj,umax,maxadd;
  /* PetscTruth  ident; */

  PetscFunctionBegin;
  PetscValidHeaderSpecific(perm,IS_COOKIE);
  if (A->M != A->N) SETERRQ(PETSC_ERR_ARG_WRONG,0,"matrix must be square");

  /* check whether perm is the identity mapping */
  /*
  ierr = ISView(perm, VIEWER_STDOUT_SELF);CHKERRA(ierr);
  ierr = ISIdentity(perm,&ident);CHKERRQ(ierr);
  printf("ident = %d\n", ident);
  */   
  ierr = ISGetIndices(perm,&rip);CHKERRQ(ierr);   
  for (i=0; i<mbs; i++){  
    if (rip[i] != i){
      a->permute = PETSC_TRUE;
      /* printf("non-trivial perm\n"); */
      break;
    }
  }

  if (!a->permute){ /* without permutation */
    ai = a->i; aj = a->j;
  } else {       /* non-trivial permutation */    
    ierr = MatReorderingSeqSBAIJ(A, perm);CHKERRA(ierr);   
    ai = a->inew; aj = a->jnew;
  }
  
  /* initialization */
  /* Don't know how many column pointers are needed so estimate. 
     Use Modified Sparse Row storage for u and ju, see Sasd pp.85 */
  iu   = (int*)PetscMalloc((mbs+1)*sizeof(int));CHKPTRQ(iu);
  umax = (int)(f*ai[mbs] + 1); umax += mbs + 1; 
  ju   = (int*)PetscMalloc(umax*sizeof(int));CHKPTRQ(ju);
  iu[0] = mbs+1; 
  juptr = mbs;
  jl =  (int*)PetscMalloc(mbs*sizeof(int));CHKPTRQ(jl);
  q  =  (int*)PetscMalloc(mbs*sizeof(int));CHKPTRQ(q);
  for (i=0; i<mbs; i++){
    jl[i] = mbs; q[i] = 0;
  }

  /* for each row k */
  for (k=0; k<mbs; k++){   
    nzk = 0; /* num. of nz blocks in k-th block row with diagonal block excluded */
    q[k] = mbs;
    /* initialize nonzero structure of k-th row to row rip[k] of A */
    jmin = ai[rip[k]];
    jmax = ai[rip[k]+1];
    for (j=jmin; j<jmax; j++){
      vj = rip[aj[j]]; /* col. value */
      if(vj > k){
        qm = k; 
        do {
          m  = qm; qm = q[m];
        } while(qm < vj);
        if (qm == vj) {
          printf(" error: duplicate entry in A\n"); break;
        }     
        nzk++;
        q[m] = vj;
        q[vj] = qm;  
      } /* if(vj > k) */
    } /* for (j=jmin; j<jmax; j++) */

    /* modify nonzero structure of k-th row by computing fill-in
       for each row i to be merged in */
    i = k; 
    i = jl[i]; /* next pivot row (== mbs for symbolic factorization) */
    /* printf(" next pivot row i=%d\n",i); */
    while (i < mbs){
      /* merge row i into k-th row */
      nzi = iu[i+1] - (iu[i]+1);
      jmin = iu[i] + 1; jmax = iu[i] + nzi;
      qm = k;
      for (j=jmin; j<jmax+1; j++){
        vj = ju[j];
        do {
          m = qm; qm = q[m];
        } while (qm < vj);
        if (qm != vj){
         nzk++; q[m] = vj; q[vj] = qm; qm = vj;
        }
      } 
      i = jl[i]; /* next pivot row */     
    }  
   
    /* add k to row list for first nonzero element in k-th row */
    if (nzk > 0){
      i = q[k]; /* col value of first nonzero element in U(k, k+1:mbs-1) */    
      jl[k] = jl[i]; jl[i] = k;
    } 
    iu[k+1] = iu[k] + nzk;   /* printf(" iu[%d]=%d, umax=%d\n", k+1, iu[k+1],umax);*/

    /* allocate more space to ju if needed */
    if (iu[k+1] > umax) { printf("allocate more space, iu[%d]=%d > umax=%d\n",k+1, iu[k+1],umax);
      /* estimate how much additional space we will need */
      /* use the strategy suggested by David Hysom <hysom@perch-t.icase.edu> */
      /* just double the memory each time */
      maxadd = umax;      
      if (maxadd < nzk) maxadd = (mbs-k)*(nzk+1)/2;
      umax += maxadd;

      /* allocate a longer ju */
      jutmp = (int*)PetscMalloc(umax*sizeof(int));CHKPTRQ(jutmp);
      ierr  = PetscMemcpy(jutmp,ju,iu[k]*sizeof(int));CHKERRQ(ierr);
      ierr  = PetscFree(ju);CHKERRQ(ierr);       
      ju    = jutmp; 
      realloc++; /* count how many times we realloc */
    }

    /* save nonzero structure of k-th row in ju */
    i=k;
    jumin = juptr + 1; juptr += nzk; 
    for (j=jumin; j<juptr+1; j++){
      i=q[i];
      ju[j]=i;
    }     
  } 

  if (ai[mbs] != 0) {
    PetscReal af = ((PetscReal)iu[mbs])/((PetscReal)ai[mbs]);
    PLogInfo(A,"MatCholeskyFactorSymbolic_SeqSBAIJ:Reallocs %d Fill ratio:given %g needed %g\n",realloc,f,af);
    PLogInfo(A,"MatCholeskyFactorSymbolic_SeqSBAIJ:Run with -pc_lu_fill %g or use \n",af);
    PLogInfo(A,"MatCholeskyFactorSymbolic_SeqSBAIJ:PCLUSetFill(pc,%g);\n",af);
    PLogInfo(A,"MatCholeskyFactorSymbolic_SeqSBAIJ:for best performance.\n");
  } else {
     PLogInfo(A,"MatCholeskyFactorSymbolic_SeqSBAIJ:Empty matrix.\n");
  }

  ierr = ISRestoreIndices(perm,&rip);CHKERRQ(ierr);
  ierr = PetscFree(q);CHKERRQ(ierr);
  ierr = PetscFree(jl);CHKERRQ(ierr);

  /* put together the new matrix */
  ierr = MatCreateSeqSBAIJ(A->comm,bs,bs*mbs,bs*mbs,0,PETSC_NULL,B);CHKERRQ(ierr);
  /* PLogObjectParent(*B,iperm); */
  b = (Mat_SeqSBAIJ*)(*B)->data;
  ierr = PetscFree(b->imax);CHKERRQ(ierr);
  b->singlemalloc = PETSC_FALSE;
  /* the next line frees the default space generated by the Create() */
  ierr = PetscFree(b->a);CHKERRQ(ierr);
  ierr = PetscFree(b->ilen);CHKERRQ(ierr);
  b->a          = (MatScalar*)PetscMalloc((iu[mbs]+1)*sizeof(MatScalar)*bs2);CHKPTRQ(b->a);
  b->j          = ju;
  b->i          = iu;
  b->diag       = 0;
  b->ilen       = 0;
  b->imax       = 0;
  b->row        = perm;
  ierr          = PetscObjectReference((PetscObject)perm);CHKERRQ(ierr); 
  b->icol       = perm;
  ierr          = PetscObjectReference((PetscObject)perm);CHKERRQ(ierr);
  b->solve_work = (Scalar*)PetscMalloc((bs*mbs+bs)*sizeof(Scalar));CHKPTRQ(b->solve_work);
  /* In b structure:  Free imax, ilen, old a, old j.  
     Allocate idnew, solve_work, new a, new j */
  PLogObjectMemory(*B,(iu[mbs]-mbs)*(sizeof(int)+sizeof(MatScalar)));
  b->s_maxnz = b->s_nz = iu[mbs];
  
  (*B)->factor                 = FACTOR_LU;
  (*B)->info.factor_mallocs    = realloc;
  (*B)->info.fill_ratio_given  = f;
  if (ai[mbs] != 0) {
    (*B)->info.fill_ratio_needed = ((PetscReal)iu[mbs])/((PetscReal)ai[mbs]);
  } else {
    (*B)->info.fill_ratio_needed = 0.0;
  }
  PetscFunctionReturn(0); 
}

#undef __FUNC__  
#define __FUNC__ "MatCholeskyFactorNumeric_SeqSBAIJ_N"
int MatCholeskyFactorNumeric_SeqSBAIJ_N(Mat A,Mat *B)
{
  Mat                C = *B;
  Mat_SeqBAIJ        *a = (Mat_SeqBAIJ*)A->data,*b = (Mat_SeqBAIJ *)C->data;
  IS                 isrow = b->row,isicol = b->icol;
  int                *r,*ic,ierr,i,j,n = a->mbs,*bi = b->i,*bj = b->j;
  int                *ajtmpold,*ajtmp,nz,row,bslog,*ai=a->i,*aj=a->j,k,flg;
  int                *diag_offset=b->diag,diag,bs=a->bs,bs2 = a->bs2,*v_pivots,*pj;
  MatScalar          *ba = b->a,*aa = a->a,*pv,*v,*rtmp,*multiplier,*v_work,*pc,*w;

  PetscFunctionBegin;
  ierr = ISGetIndices(isrow,&r);CHKERRQ(ierr);
  ierr = ISGetIndices(isicol,&ic);CHKERRQ(ierr);
  rtmp = (MatScalar*)PetscMalloc(bs2*(n+1)*sizeof(MatScalar));CHKPTRQ(rtmp);
  ierr = PetscMemzero(rtmp,bs2*(n+1)*sizeof(MatScalar));CHKERRQ(ierr);
  /* generate work space needed by dense LU factorization */
  v_work     = (MatScalar*)PetscMalloc(bs*sizeof(int) + (bs+bs2)*sizeof(MatScalar));CHKPTRQ(v_work);
  multiplier = v_work + bs;
  v_pivots   = (int*)(multiplier + bs2);

  /* flops in while loop */
  bslog = 2*bs*bs2;

  for (i=0; i<n; i++) {
    nz    = bi[i+1] - bi[i];
    ajtmp = bj + bi[i];
    for  (j=0; j<nz; j++) {
      ierr = PetscMemzero(rtmp+bs2*ajtmp[j],bs2*sizeof(MatScalar));CHKERRQ(ierr);
    }
    /* load in initial (unfactored row) */
    nz       = ai[r[i]+1] - ai[r[i]];
    ajtmpold = aj + ai[r[i]];
    v        = aa + bs2*ai[r[i]];
    for (j=0; j<nz; j++) {
      ierr = PetscMemcpy(rtmp+bs2*ic[ajtmpold[j]],v+bs2*j,bs2*sizeof(MatScalar));CHKERRQ(ierr);
    }
    row = *ajtmp++;
    while (row < i) {
      pc = rtmp + bs2*row;
/*      if (*pc) { */
      for (flg=0,k=0; k<bs2; k++) { if (pc[k]!=0.0) { flg =1; break; }}
      if (flg) {
        pv = ba + bs2*diag_offset[row];
        pj = bj + diag_offset[row] + 1;
        Kernel_A_gets_A_times_B(bs,pc,pv,multiplier); 
        nz = bi[row+1] - diag_offset[row] - 1;
        pv += bs2;
        for (j=0; j<nz; j++) {
          Kernel_A_gets_A_minus_B_times_C(bs,rtmp+bs2*pj[j],pc,pv+bs2*j);
        }
        PLogFlops(bslog*(nz+1)-bs);
      } 
        row = *ajtmp++;
    }
    /* finished row so stick it into b->a */
    pv = ba + bs2*bi[i];
    pj = bj + bi[i];
    nz = bi[i+1] - bi[i];
    for (j=0; j<nz; j++) {
      ierr = PetscMemcpy(pv+bs2*j,rtmp+bs2*pj[j],bs2*sizeof(MatScalar));CHKERRQ(ierr);
    }
    diag = diag_offset[i] - bi[i];
    /* invert diagonal block */
    w = pv + bs2*diag; 
    Kernel_A_gets_inverse_A(bs,w,v_pivots,v_work);
  }

  ierr = PetscFree(rtmp);CHKERRQ(ierr);
  ierr = PetscFree(v_work);CHKERRQ(ierr);
  ierr = ISRestoreIndices(isicol,&ic);CHKERRQ(ierr);
  ierr = ISRestoreIndices(isrow,&r);CHKERRQ(ierr);
  C->factor = FACTOR_LU;
  C->assembled = PETSC_TRUE;
  PLogFlops(1.3333*bs*bs2*b->mbs); /* from inverting diagonal blocks */
  PetscFunctionReturn(0);
}

/*
      Version for when blocks are 7 by 7
*/
#undef __FUNC__  
#define __FUNC__ "MatCholeskyFactorNumeric_SeqSBAIJ_7"
int MatCholeskyFactorNumeric_SeqSBAIJ_7(Mat A,Mat *B)
{
  Mat         C = *B;
  Mat_SeqBAIJ *a = (Mat_SeqBAIJ*)A->data,*b = (Mat_SeqBAIJ *)C->data;
  IS          isrow = b->row,isicol = b->icol;
  int         *r,*ic,ierr,i,j,n = a->mbs,*bi = b->i,*bj = b->j;
  int         *ajtmpold,*ajtmp,nz,row;
  int         *diag_offset = b->diag,idx,*ai=a->i,*aj=a->j,*pj;
  MatScalar   *pv,*v,*rtmp,*pc,*w,*x;
  MatScalar   p1,p2,p3,p4,m1,m2,m3,m4,m5,m6,m7,m8,m9,x1,x2,x3,x4;
  MatScalar   p5,p6,p7,p8,p9,x5,x6,x7,x8,x9,x10,x11,x12,x13,x14,x15,x16;
  MatScalar   x17,x18,x19,x20,x21,x22,x23,x24,x25,p10,p11,p12,p13,p14;
  MatScalar   p15,p16,p17,p18,p19,p20,p21,p22,p23,p24,p25,m10,m11,m12;
  MatScalar   m13,m14,m15,m16,m17,m18,m19,m20,m21,m22,m23,m24,m25;
  MatScalar   p26,p27,p28,p29,p30,p31,p32,p33,p34,p35,p36;
  MatScalar   p37,p38,p39,p40,p41,p42,p43,p44,p45,p46,p47,p48,p49;
  MatScalar   x26,x27,x28,x29,x30,x31,x32,x33,x34,x35,x36;
  MatScalar   x37,x38,x39,x40,x41,x42,x43,x44,x45,x46,x47,x48,x49;
  MatScalar   m26,m27,m28,m29,m30,m31,m32,m33,m34,m35,m36;
  MatScalar   m37,m38,m39,m40,m41,m42,m43,m44,m45,m46,m47,m48,m49;
  MatScalar   *ba = b->a,*aa = a->a;

  PetscFunctionBegin;
  ierr  = ISGetIndices(isrow,&r);CHKERRQ(ierr);
  ierr  = ISGetIndices(isicol,&ic);CHKERRQ(ierr);
  rtmp  = (MatScalar*)PetscMalloc(49*(n+1)*sizeof(MatScalar));CHKPTRQ(rtmp);

  for (i=0; i<n; i++) {
    nz    = bi[i+1] - bi[i];
    ajtmp = bj + bi[i];
    for  (j=0; j<nz; j++) {
      x = rtmp+49*ajtmp[j]; 
      x[0] = x[1] = x[2] = x[3] = x[4] = x[5] = x[6] = x[7] = x[8] = x[9] = 0.0;
      x[10] = x[11] = x[12] = x[13] = x[14] = x[15] = x[16] = x[17] = 0.0;
      x[18] = x[19] = x[20] = x[21] = x[22] = x[23] = x[24] = x[25] = 0.0 ;
      x[26] = x[27] = x[28] = x[29] = x[30] = x[31] = x[32] = x[33] = 0.0 ;
      x[34] = x[35] = x[36] = x[37] = x[38] = x[39] = x[40] = x[41] = 0.0 ;
      x[42] = x[43] = x[44] = x[45] = x[46] = x[47] = x[48] = 0.0 ;
    }
    /* load in initial (unfactored row) */
    idx      = r[i];
    nz       = ai[idx+1] - ai[idx];
    ajtmpold = aj + ai[idx];
    v        = aa + 49*ai[idx];
    for (j=0; j<nz; j++) {
      x    = rtmp+49*ic[ajtmpold[j]];
      x[0] =  v[0];  x[1] =  v[1];  x[2] =  v[2];  x[3] =  v[3];
      x[4] =  v[4];  x[5] =  v[5];  x[6] =  v[6];  x[7] =  v[7]; 
      x[8] =  v[8];  x[9] =  v[9];  x[10] = v[10]; x[11] = v[11]; 
      x[12] = v[12]; x[13] = v[13]; x[14] = v[14]; x[15] = v[15]; 
      x[16] = v[16]; x[17] = v[17]; x[18] = v[18]; x[19] = v[19]; 
      x[20] = v[20]; x[21] = v[21]; x[22] = v[22]; x[23] = v[23]; 
      x[24] = v[24]; x[25] = v[25]; x[26] = v[26]; x[27] = v[27]; 
      x[28] = v[28]; x[29] = v[29]; x[30] = v[30]; x[31] = v[31]; 
      x[32] = v[32]; x[33] = v[33]; x[34] = v[34]; x[35] = v[35]; 
      x[36] = v[36]; x[37] = v[37]; x[38] = v[38]; x[39] = v[39]; 
      x[40] = v[40]; x[41] = v[41]; x[42] = v[42]; x[43] = v[43]; 
      x[44] = v[44]; x[45] = v[45]; x[46] = v[46]; x[47] = v[47]; 
      x[48] = v[48];  
      v    += 49;
    }
    row = *ajtmp++;
    while (row < i) {
      pc  =  rtmp + 49*row;
      p1  = pc[0];  p2  = pc[1];  p3  = pc[2];  p4  = pc[3];
      p5  = pc[4];  p6  = pc[5];  p7  = pc[6];  p8  = pc[7];
      p9  = pc[8];  p10 = pc[9];  p11 = pc[10]; p12 = pc[11]; 
      p13 = pc[12]; p14 = pc[13]; p15 = pc[14]; p16 = pc[15]; 
      p17 = pc[16]; p18 = pc[17]; p19 = pc[18]; p20 = pc[19];
      p21 = pc[20]; p22 = pc[21]; p23 = pc[22]; p24 = pc[23];
      p25 = pc[24]; p26 = pc[25]; p27 = pc[26]; p28 = pc[27];
      p29 = pc[28]; p30 = pc[29]; p31 = pc[30]; p32 = pc[31];
      p33 = pc[32]; p34 = pc[33]; p35 = pc[34]; p36 = pc[35];
      p37 = pc[36]; p38 = pc[37]; p39 = pc[38]; p40 = pc[39];
      p41 = pc[40]; p42 = pc[41]; p43 = pc[42]; p44 = pc[43];
      p45 = pc[44]; p46 = pc[45]; p47 = pc[46]; p48 = pc[47];
      p49 = pc[48];
      if (p1  != 0.0 || p2  != 0.0 || p3  != 0.0 || p4  != 0.0 || 
          p5  != 0.0 || p6  != 0.0 || p7  != 0.0 || p8  != 0.0 ||
          p9  != 0.0 || p10 != 0.0 || p11 != 0.0 || p12 != 0.0 || 
          p13 != 0.0 || p14 != 0.0 || p15 != 0.0 || p16 != 0.0 ||
          p17 != 0.0 || p18 != 0.0 || p19 != 0.0 || p20 != 0.0 ||
          p21 != 0.0 || p22 != 0.0 || p23 != 0.0 || p24 != 0.0 ||
          p25 != 0.0 || p26 != 0.0 || p27 != 0.0 || p28 != 0.0 ||
          p29 != 0.0 || p30 != 0.0 || p31 != 0.0 || p32 != 0.0 ||
          p33 != 0.0 || p34 != 0.0 || p35 != 0.0 || p36 != 0.0 ||
          p37 != 0.0 || p38 != 0.0 || p39 != 0.0 || p40 != 0.0 ||
          p41 != 0.0 || p42 != 0.0 || p43 != 0.0 || p44 != 0.0 ||
          p45 != 0.0 || p46 != 0.0 || p47 != 0.0 || p48 != 0.0 ||
          p49 != 0.0) { 
        pv = ba + 49*diag_offset[row];
        pj = bj + diag_offset[row] + 1;
	x1  = pv[0];  x2  = pv[1];  x3  = pv[2];  x4  = pv[3];
	x5  = pv[4];  x6  = pv[5];  x7  = pv[6];  x8  = pv[7];
	x9  = pv[8];  x10 = pv[9];  x11 = pv[10]; x12 = pv[11]; 
	x13 = pv[12]; x14 = pv[13]; x15 = pv[14]; x16 = pv[15]; 
	x17 = pv[16]; x18 = pv[17]; x19 = pv[18]; x20 = pv[19];
	x21 = pv[20]; x22 = pv[21]; x23 = pv[22]; x24 = pv[23];
	x25 = pv[24]; x26 = pv[25]; x27 = pv[26]; x28 = pv[27];
	x29 = pv[28]; x30 = pv[29]; x31 = pv[30]; x32 = pv[31];
	x33 = pv[32]; x34 = pv[33]; x35 = pv[34]; x36 = pv[35];
	x37 = pv[36]; x38 = pv[37]; x39 = pv[38]; x40 = pv[39];
	x41 = pv[40]; x42 = pv[41]; x43 = pv[42]; x44 = pv[43];
	x45 = pv[44]; x46 = pv[45]; x47 = pv[46]; x48 = pv[47];
	x49 = pv[48];
        pc[0]  = m1  = p1*x1  + p8*x2   + p15*x3  + p22*x4  + p29*x5  + p36*x6 + p43*x7;
        pc[1]  = m2  = p2*x1  + p9*x2   + p16*x3  + p23*x4  + p30*x5  + p37*x6 + p44*x7;
        pc[2]  = m3  = p3*x1  + p10*x2  + p17*x3  + p24*x4  + p31*x5  + p38*x6 + p45*x7;
        pc[3]  = m4  = p4*x1  + p11*x2  + p18*x3  + p25*x4  + p32*x5  + p39*x6 + p46*x7;
        pc[4]  = m5  = p5*x1  + p12*x2  + p19*x3  + p26*x4  + p33*x5  + p40*x6 + p47*x7;
        pc[5]  = m6  = p6*x1  + p13*x2  + p20*x3  + p27*x4  + p34*x5  + p41*x6 + p48*x7;
        pc[6]  = m7  = p7*x1  + p14*x2  + p21*x3  + p28*x4  + p35*x5  + p42*x6 + p49*x7;

        pc[7]  = m8  = p1*x8  + p8*x9   + p15*x10 + p22*x11 + p29*x12 + p36*x13 + p43*x14;
        pc[8]  = m9  = p2*x8  + p9*x9   + p16*x10 + p23*x11 + p30*x12 + p37*x13 + p44*x14;
        pc[9]  = m10 = p3*x8  + p10*x9  + p17*x10 + p24*x11 + p31*x12 + p38*x13 + p45*x14;
        pc[10] = m11 = p4*x8  + p11*x9  + p18*x10 + p25*x11 + p32*x12 + p39*x13 + p46*x14;
        pc[11] = m12 = p5*x8  + p12*x9  + p19*x10 + p26*x11 + p33*x12 + p40*x13 + p47*x14;
        pc[12] = m13 = p6*x8  + p13*x9  + p20*x10 + p27*x11 + p34*x12 + p41*x13 + p48*x14;
        pc[13] = m14 = p7*x8  + p14*x9  + p21*x10 + p28*x11 + p35*x12 + p42*x13 + p49*x14;

        pc[14] = m15 = p1*x15 + p8*x16  + p15*x17 + p22*x18 + p29*x19 + p36*x20 + p43*x21;
        pc[15] = m16 = p2*x15 + p9*x16  + p16*x17 + p23*x18 + p30*x19 + p37*x20 + p44*x21;
        pc[16] = m17 = p3*x15 + p10*x16 + p17*x17 + p24*x18 + p31*x19 + p38*x20 + p45*x21;
        pc[17] = m18 = p4*x15 + p11*x16 + p18*x17 + p25*x18 + p32*x19 + p39*x20 + p46*x21;
        pc[18] = m19 = p5*x15 + p12*x16 + p19*x17 + p26*x18 + p33*x19 + p40*x20 + p47*x21;
        pc[19] = m20 = p6*x15 + p13*x16 + p20*x17 + p27*x18 + p34*x19 + p41*x20 + p48*x21;
        pc[20] = m21 = p7*x15 + p14*x16 + p21*x17 + p28*x18 + p35*x19 + p42*x20 + p49*x21;

        pc[21] = m22 = p1*x22 + p8*x23  + p15*x24 + p22*x25 + p29*x26 + p36*x27 + p43*x28;
        pc[22] = m23 = p2*x22 + p9*x23  + p16*x24 + p23*x25 + p30*x26 + p37*x27 + p44*x28;
        pc[23] = m24 = p3*x22 + p10*x23 + p17*x24 + p24*x25 + p31*x26 + p38*x27 + p45*x28;
        pc[24] = m25 = p4*x22 + p11*x23 + p18*x24 + p25*x25 + p32*x26 + p39*x27 + p46*x28;
        pc[25] = m26 = p5*x22 + p12*x23 + p19*x24 + p26*x25 + p33*x26 + p40*x27 + p47*x28;
        pc[26] = m27 = p6*x22 + p13*x23 + p20*x24 + p27*x25 + p34*x26 + p41*x27 + p48*x28;
        pc[27] = m28 = p7*x22 + p14*x23 + p21*x24 + p28*x25 + p35*x26 + p42*x27 + p49*x28;

        pc[28] = m29 = p1*x29 + p8*x30  + p15*x31 + p22*x32 + p29*x33 + p36*x34 + p43*x35;
        pc[29] = m30 = p2*x29 + p9*x30  + p16*x31 + p23*x32 + p30*x33 + p37*x34 + p44*x35;
        pc[30] = m31 = p3*x29 + p10*x30 + p17*x31 + p24*x32 + p31*x33 + p38*x34 + p45*x35;
        pc[31] = m32 = p4*x29 + p11*x30 + p18*x31 + p25*x32 + p32*x33 + p39*x34 + p46*x35;
        pc[32] = m33 = p5*x29 + p12*x30 + p19*x31 + p26*x32 + p33*x33 + p40*x34 + p47*x35;
        pc[33] = m34 = p6*x29 + p13*x30 + p20*x31 + p27*x32 + p34*x33 + p41*x34 + p48*x35;
        pc[34] = m35 = p7*x29 + p14*x30 + p21*x31 + p28*x32 + p35*x33 + p42*x34 + p49*x35;

        pc[35] = m36 = p1*x36 + p8*x37  + p15*x38 + p22*x39 + p29*x40 + p36*x41 + p43*x42;
        pc[36] = m37 = p2*x36 + p9*x37  + p16*x38 + p23*x39 + p30*x40 + p37*x41 + p44*x42;
        pc[37] = m38 = p3*x36 + p10*x37 + p17*x38 + p24*x39 + p31*x40 + p38*x41 + p45*x42;
        pc[38] = m39 = p4*x36 + p11*x37 + p18*x38 + p25*x39 + p32*x40 + p39*x41 + p46*x42;
        pc[39] = m40 = p5*x36 + p12*x37 + p19*x38 + p26*x39 + p33*x40 + p40*x41 + p47*x42;
        pc[40] = m41 = p6*x36 + p13*x37 + p20*x38 + p27*x39 + p34*x40 + p41*x41 + p48*x42;
        pc[41] = m42 = p7*x36 + p14*x37 + p21*x38 + p28*x39 + p35*x40 + p42*x41 + p49*x42;

        pc[42] = m43 = p1*x43 + p8*x44  + p15*x45 + p22*x46 + p29*x47 + p36*x48 + p43*x49;
        pc[43] = m44 = p2*x43 + p9*x44  + p16*x45 + p23*x46 + p30*x47 + p37*x48 + p44*x49;
        pc[44] = m45 = p3*x43 + p10*x44 + p17*x45 + p24*x46 + p31*x47 + p38*x48 + p45*x49;
        pc[45] = m46 = p4*x43 + p11*x44 + p18*x45 + p25*x46 + p32*x47 + p39*x48 + p46*x49;
        pc[46] = m47 = p5*x43 + p12*x44 + p19*x45 + p26*x46 + p33*x47 + p40*x48 + p47*x49;
        pc[47] = m48 = p6*x43 + p13*x44 + p20*x45 + p27*x46 + p34*x47 + p41*x48 + p48*x49;
        pc[48] = m49 = p7*x43 + p14*x44 + p21*x45 + p28*x46 + p35*x47 + p42*x48 + p49*x49;

        nz = bi[row+1] - diag_offset[row] - 1;
        pv += 49;
        for (j=0; j<nz; j++) {
	  x1  = pv[0];  x2  = pv[1];  x3  = pv[2];  x4  = pv[3];
	  x5  = pv[4];  x6  = pv[5];  x7  = pv[6];  x8  = pv[7];
	  x9  = pv[8];  x10 = pv[9];  x11 = pv[10]; x12 = pv[11]; 
	  x13 = pv[12]; x14 = pv[13]; x15 = pv[14]; x16 = pv[15]; 
	  x17 = pv[16]; x18 = pv[17]; x19 = pv[18]; x20 = pv[19];
	  x21 = pv[20]; x22 = pv[21]; x23 = pv[22]; x24 = pv[23];
	  x25 = pv[24]; x26 = pv[25]; x27 = pv[26]; x28 = pv[27];
	  x29 = pv[28]; x30 = pv[29]; x31 = pv[30]; x32 = pv[31];
	  x33 = pv[32]; x34 = pv[33]; x35 = pv[34]; x36 = pv[35];
	  x37 = pv[36]; x38 = pv[37]; x39 = pv[38]; x40 = pv[39];
	  x41 = pv[40]; x42 = pv[41]; x43 = pv[42]; x44 = pv[43];
	  x45 = pv[44]; x46 = pv[45]; x47 = pv[46]; x48 = pv[47];
	  x49 = pv[48];
	  x    = rtmp + 49*pj[j];
	  x[0]  -= m1*x1  + m8*x2   + m15*x3  + m22*x4  + m29*x5  + m36*x6 + m43*x7;
	  x[1]  -= m2*x1  + m9*x2   + m16*x3  + m23*x4  + m30*x5  + m37*x6 + m44*x7;
	  x[2]  -= m3*x1  + m10*x2  + m17*x3  + m24*x4  + m31*x5  + m38*x6 + m45*x7;
	  x[3]  -= m4*x1  + m11*x2  + m18*x3  + m25*x4  + m32*x5  + m39*x6 + m46*x7;
	  x[4]  -= m5*x1  + m12*x2  + m19*x3  + m26*x4  + m33*x5  + m40*x6 + m47*x7;
	  x[5]  -= m6*x1  + m13*x2  + m20*x3  + m27*x4  + m34*x5  + m41*x6 + m48*x7;
	  x[6]  -= m7*x1  + m14*x2  + m21*x3  + m28*x4  + m35*x5  + m42*x6 + m49*x7;

	  x[7]  -= m1*x8  + m8*x9   + m15*x10 + m22*x11 + m29*x12 + m36*x13 + m43*x14;
	  x[8]  -= m2*x8  + m9*x9   + m16*x10 + m23*x11 + m30*x12 + m37*x13 + m44*x14;
	  x[9]  -= m3*x8  + m10*x9  + m17*x10 + m24*x11 + m31*x12 + m38*x13 + m45*x14;
	  x[10] -= m4*x8  + m11*x9  + m18*x10 + m25*x11 + m32*x12 + m39*x13 + m46*x14;
	  x[11] -= m5*x8  + m12*x9  + m19*x10 + m26*x11 + m33*x12 + m40*x13 + m47*x14;
	  x[12] -= m6*x8  + m13*x9  + m20*x10 + m27*x11 + m34*x12 + m41*x13 + m48*x14;
	  x[13] -= m7*x8  + m14*x9  + m21*x10 + m28*x11 + m35*x12 + m42*x13 + m49*x14;

	  x[14] -= m1*x15 + m8*x16  + m15*x17 + m22*x18 + m29*x19 + m36*x20 + m43*x21;
	  x[15] -= m2*x15 + m9*x16  + m16*x17 + m23*x18 + m30*x19 + m37*x20 + m44*x21;
	  x[16] -= m3*x15 + m10*x16 + m17*x17 + m24*x18 + m31*x19 + m38*x20 + m45*x21;
	  x[17] -= m4*x15 + m11*x16 + m18*x17 + m25*x18 + m32*x19 + m39*x20 + m46*x21;
	  x[18] -= m5*x15 + m12*x16 + m19*x17 + m26*x18 + m33*x19 + m40*x20 + m47*x21;
	  x[19] -= m6*x15 + m13*x16 + m20*x17 + m27*x18 + m34*x19 + m41*x20 + m48*x21;
	  x[20] -= m7*x15 + m14*x16 + m21*x17 + m28*x18 + m35*x19 + m42*x20 + m49*x21;

	  x[21] -= m1*x22 + m8*x23  + m15*x24 + m22*x25 + m29*x26 + m36*x27 + m43*x28;
	  x[22] -= m2*x22 + m9*x23  + m16*x24 + m23*x25 + m30*x26 + m37*x27 + m44*x28;
	  x[23] -= m3*x22 + m10*x23 + m17*x24 + m24*x25 + m31*x26 + m38*x27 + m45*x28;
	  x[24] -= m4*x22 + m11*x23 + m18*x24 + m25*x25 + m32*x26 + m39*x27 + m46*x28;
	  x[25] -= m5*x22 + m12*x23 + m19*x24 + m26*x25 + m33*x26 + m40*x27 + m47*x28;
	  x[26] -= m6*x22 + m13*x23 + m20*x24 + m27*x25 + m34*x26 + m41*x27 + m48*x28;
	  x[27] -= m7*x22 + m14*x23 + m21*x24 + m28*x25 + m35*x26 + m42*x27 + m49*x28;

	  x[28] -= m1*x29 + m8*x30  + m15*x31 + m22*x32 + m29*x33 + m36*x34 + m43*x35;
	  x[29] -= m2*x29 + m9*x30  + m16*x31 + m23*x32 + m30*x33 + m37*x34 + m44*x35;
	  x[30] -= m3*x29 + m10*x30 + m17*x31 + m24*x32 + m31*x33 + m38*x34 + m45*x35;
	  x[31] -= m4*x29 + m11*x30 + m18*x31 + m25*x32 + m32*x33 + m39*x34 + m46*x35;
	  x[32] -= m5*x29 + m12*x30 + m19*x31 + m26*x32 + m33*x33 + m40*x34 + m47*x35;
	  x[33] -= m6*x29 + m13*x30 + m20*x31 + m27*x32 + m34*x33 + m41*x34 + m48*x35;
	  x[34] -= m7*x29 + m14*x30 + m21*x31 + m28*x32 + m35*x33 + m42*x34 + m49*x35;

	  x[35] -= m1*x36 + m8*x37  + m15*x38 + m22*x39 + m29*x40 + m36*x41 + m43*x42;
	  x[36] -= m2*x36 + m9*x37  + m16*x38 + m23*x39 + m30*x40 + m37*x41 + m44*x42;
	  x[37] -= m3*x36 + m10*x37 + m17*x38 + m24*x39 + m31*x40 + m38*x41 + m45*x42;
	  x[38] -= m4*x36 + m11*x37 + m18*x38 + m25*x39 + m32*x40 + m39*x41 + m46*x42;
	  x[39] -= m5*x36 + m12*x37 + m19*x38 + m26*x39 + m33*x40 + m40*x41 + m47*x42;
	  x[40] -= m6*x36 + m13*x37 + m20*x38 + m27*x39 + m34*x40 + m41*x41 + m48*x42;
	  x[41] -= m7*x36 + m14*x37 + m21*x38 + m28*x39 + m35*x40 + m42*x41 + m49*x42;

	  x[42] -= m1*x43 + m8*x44  + m15*x45 + m22*x46 + m29*x47 + m36*x48 + m43*x49;
	  x[43] -= m2*x43 + m9*x44  + m16*x45 + m23*x46 + m30*x47 + m37*x48 + m44*x49;
	  x[44] -= m3*x43 + m10*x44 + m17*x45 + m24*x46 + m31*x47 + m38*x48 + m45*x49;
	  x[45] -= m4*x43 + m11*x44 + m18*x45 + m25*x46 + m32*x47 + m39*x48 + m46*x49;
	  x[46] -= m5*x43 + m12*x44 + m19*x45 + m26*x46 + m33*x47 + m40*x48 + m47*x49;
	  x[47] -= m6*x43 + m13*x44 + m20*x45 + m27*x46 + m34*x47 + m41*x48 + m48*x49;
	  x[48] -= m7*x43 + m14*x44 + m21*x45 + m28*x46 + m35*x47 + m42*x48 + m49*x49;
          pv   += 49;
        }
        PLogFlops(686*nz+637);
      } 
      row = *ajtmp++;
    }
    /* finished row so stick it into b->a */
    pv = ba + 49*bi[i];
    pj = bj + bi[i];
    nz = bi[i+1] - bi[i];
    for (j=0; j<nz; j++) {
      x      = rtmp+49*pj[j];
      pv[0]  = x[0];  pv[1]  = x[1];  pv[2]  = x[2];  pv[3]  = x[3];
      pv[4]  = x[4];  pv[5]  = x[5];  pv[6]  = x[6];  pv[7]  = x[7]; 
      pv[8]  = x[8];  pv[9]  = x[9];  pv[10] = x[10]; pv[11] = x[11]; 
      pv[12] = x[12]; pv[13] = x[13]; pv[14] = x[14]; pv[15] = x[15]; 
      pv[16] = x[16]; pv[17] = x[17]; pv[18] = x[18]; pv[19] = x[19]; 
      pv[20] = x[20]; pv[21] = x[21]; pv[22] = x[22]; pv[23] = x[23]; 
      pv[24] = x[24]; pv[25] = x[25]; pv[26] = x[26]; pv[27] = x[27]; 
      pv[28] = x[28]; pv[29] = x[29]; pv[30] = x[30]; pv[31] = x[31]; 
      pv[32] = x[32]; pv[33] = x[33]; pv[34] = x[34]; pv[35] = x[35]; 
      pv[36] = x[36]; pv[37] = x[37]; pv[38] = x[38]; pv[39] = x[39]; 
      pv[40] = x[40]; pv[41] = x[41]; pv[42] = x[42]; pv[43] = x[43]; 
      pv[44] = x[44]; pv[45] = x[45]; pv[46] = x[46]; pv[47] = x[47]; 
      pv[48] = x[48];  
      pv   += 49;
    }
    /* invert diagonal block */
    w = ba + 49*diag_offset[i];
    ierr = Kernel_A_gets_inverse_A_7(w);CHKERRQ(ierr);
  }

  ierr = PetscFree(rtmp);CHKERRQ(ierr);
  ierr = ISRestoreIndices(isicol,&ic);CHKERRQ(ierr);
  ierr = ISRestoreIndices(isrow,&r);CHKERRQ(ierr);
  C->factor = FACTOR_LU;
  C->assembled = PETSC_TRUE;
  PLogFlops(1.3333*343*b->mbs); /* from inverting diagonal blocks */
  PetscFunctionReturn(0);
}

/*
      Version for when blocks are 7 by 7 Using natural ordering
*/
#undef __FUNC__  
#define __FUNC__ "MatCholeskyFactorNumeric_SeqSBAIJ_7_NaturalOrdering"
int MatCholeskyFactorNumeric_SeqSBAIJ_7_NaturalOrdering(Mat A,Mat *B)
{
  Mat          C = *B;
  Mat_SeqBAIJ  *a = (Mat_SeqBAIJ*)A->data,*b = (Mat_SeqBAIJ *)C->data;
  int          ierr,i,j,n = a->mbs,*bi = b->i,*bj = b->j;
  int          *ajtmpold,*ajtmp,nz,row;
  int          *diag_offset = b->diag,*ai=a->i,*aj=a->j,*pj;
  MatScalar    *pv,*v,*rtmp,*pc,*w,*x;
  MatScalar    x1,x2,x3,x4,x5,x6,x7,x8,x9,x10,x11,x12,x13,x14,x15;
  MatScalar    x16,x17,x18,x19,x20,x21,x22,x23,x24,x25;
  MatScalar    p1,p2,p3,p4,p5,p6,p7,p8,p9,p10,p11,p12,p13,p14,p15;
  MatScalar    p16,p17,p18,p19,p20,p21,p22,p23,p24,p25;
  MatScalar    m1,m2,m3,m4,m5,m6,m7,m8,m9,m10,m11,m12,m13,m14,m15;
  MatScalar    m16,m17,m18,m19,m20,m21,m22,m23,m24,m25;
  MatScalar    p26,p27,p28,p29,p30,p31,p32,p33,p34,p35,p36;
  MatScalar    p37,p38,p39,p40,p41,p42,p43,p44,p45,p46,p47,p48,p49;
  MatScalar    x26,x27,x28,x29,x30,x31,x32,x33,x34,x35,x36;
  MatScalar    x37,x38,x39,x40,x41,x42,x43,x44,x45,x46,x47,x48,x49;
  MatScalar    m26,m27,m28,m29,m30,m31,m32,m33,m34,m35,m36;
  MatScalar    m37,m38,m39,m40,m41,m42,m43,m44,m45,m46,m47,m48,m49;
  MatScalar    *ba = b->a,*aa = a->a;

  PetscFunctionBegin;
  rtmp  = (MatScalar*)PetscMalloc(49*(n+1)*sizeof(MatScalar));CHKPTRQ(rtmp);
  for (i=0; i<n; i++) {
    nz    = bi[i+1] - bi[i];
    ajtmp = bj + bi[i];
    for  (j=0; j<nz; j++) {
      x = rtmp+49*ajtmp[j]; 
      x[0] = x[1] = x[2] = x[3] = x[4] = x[5] = x[6] = x[7] = x[8] = x[9] = 0.0;
      x[10] = x[11] = x[12] = x[13] = x[14] = x[15] = x[16] = x[17] = 0.0;
      x[18] = x[19] = x[20] = x[21] = x[22] = x[23] = x[24] = x[25] = 0.0 ;
      x[26] = x[27] = x[28] = x[29] = x[30] = x[31] = x[32] = x[33] = 0.0 ;
      x[34] = x[35] = x[36] = x[37] = x[38] = x[39] = x[40] = x[41] = 0.0 ;
      x[42] = x[43] = x[44] = x[45] = x[46] = x[47] = x[48] = 0.0 ;
    }
    /* load in initial (unfactored row) */
    nz       = ai[i+1] - ai[i];
    ajtmpold = aj + ai[i];
    v        = aa + 49*ai[i];
    for (j=0; j<nz; j++) {
      x    = rtmp+49*ajtmpold[j];
      x[0] =  v[0];  x[1] =  v[1];  x[2] =  v[2];  x[3] =  v[3];
      x[4] =  v[4];  x[5] =  v[5];  x[6] =  v[6];  x[7] =  v[7]; 
      x[8] =  v[8];  x[9] =  v[9];  x[10] = v[10]; x[11] = v[11]; 
      x[12] = v[12]; x[13] = v[13]; x[14] = v[14]; x[15] = v[15]; 
      x[16] = v[16]; x[17] = v[17]; x[18] = v[18]; x[19] = v[19]; 
      x[20] = v[20]; x[21] = v[21]; x[22] = v[22]; x[23] = v[23]; 
      x[24] = v[24]; x[25] = v[25]; x[26] = v[26]; x[27] = v[27]; 
      x[28] = v[28]; x[29] = v[29]; x[30] = v[30]; x[31] = v[31]; 
      x[32] = v[32]; x[33] = v[33]; x[34] = v[34]; x[35] = v[35]; 
      x[36] = v[36]; x[37] = v[37]; x[38] = v[38]; x[39] = v[39]; 
      x[40] = v[40]; x[41] = v[41]; x[42] = v[42]; x[43] = v[43]; 
      x[44] = v[44]; x[45] = v[45]; x[46] = v[46]; x[47] = v[47]; 
      x[48] = v[48];
      v    += 49;
    }
    row = *ajtmp++;
    while (row < i) {
      pc  = rtmp + 49*row;
      p1  = pc[0];  p2  = pc[1];  p3  = pc[2];  p4  = pc[3];
      p5  = pc[4];  p6  = pc[5];  p7  = pc[6];  p8  = pc[7];
      p9  = pc[8];  p10 = pc[9];  p11 = pc[10]; p12 = pc[11]; 
      p13 = pc[12]; p14 = pc[13]; p15 = pc[14]; p16 = pc[15]; 
      p17 = pc[16]; p18 = pc[17]; p19 = pc[18]; p20 = pc[19];
      p21 = pc[20]; p22 = pc[21]; p23 = pc[22]; p24 = pc[23];
      p25 = pc[24]; p26 = pc[25]; p27 = pc[26]; p28 = pc[27];
      p29 = pc[28]; p30 = pc[29]; p31 = pc[30]; p32 = pc[31];
      p33 = pc[32]; p34 = pc[33]; p35 = pc[34]; p36 = pc[35];
      p37 = pc[36]; p38 = pc[37]; p39 = pc[38]; p40 = pc[39];
      p41 = pc[40]; p42 = pc[41]; p43 = pc[42]; p44 = pc[43];
      p45 = pc[44]; p46 = pc[45]; p47 = pc[46]; p48 = pc[47];
      p49 = pc[48];
      if (p1  != 0.0 || p2  != 0.0 || p3  != 0.0 || p4  != 0.0 || 
          p5  != 0.0 || p6  != 0.0 || p7  != 0.0 || p8  != 0.0 ||
          p9  != 0.0 || p10 != 0.0 || p11 != 0.0 || p12 != 0.0 || 
          p13 != 0.0 || p14 != 0.0 || p15 != 0.0 || p16 != 0.0 ||
          p17 != 0.0 || p18 != 0.0 || p19 != 0.0 || p20 != 0.0 ||
          p21 != 0.0 || p22 != 0.0 || p23 != 0.0 || p24 != 0.0 ||
          p25 != 0.0 || p26 != 0.0 || p27 != 0.0 || p28 != 0.0 ||
          p29 != 0.0 || p30 != 0.0 || p31 != 0.0 || p32 != 0.0 ||
          p33 != 0.0 || p34 != 0.0 || p35 != 0.0 || p36 != 0.0 ||
          p37 != 0.0 || p38 != 0.0 || p39 != 0.0 || p40 != 0.0 ||
          p41 != 0.0 || p42 != 0.0 || p43 != 0.0 || p44 != 0.0 ||
          p45 != 0.0 || p46 != 0.0 || p47 != 0.0 || p48 != 0.0 ||
          p49 != 0.0) { 
        pv = ba + 49*diag_offset[row];
        pj = bj + diag_offset[row] + 1;
	x1  = pv[0];  x2  = pv[1];  x3  = pv[2];  x4  = pv[3];
	x5  = pv[4];  x6  = pv[5];  x7  = pv[6];  x8  = pv[7];
	x9  = pv[8];  x10 = pv[9];  x11 = pv[10]; x12 = pv[11]; 
	x13 = pv[12]; x14 = pv[13]; x15 = pv[14]; x16 = pv[15]; 
	x17 = pv[16]; x18 = pv[17]; x19 = pv[18]; x20 = pv[19];
	x21 = pv[20]; x22 = pv[21]; x23 = pv[22]; x24 = pv[23];
	x25 = pv[24]; x26 = pv[25]; x27 = pv[26]; x28 = pv[27];
	x29 = pv[28]; x30 = pv[29]; x31 = pv[30]; x32 = pv[31];
	x33 = pv[32]; x34 = pv[33]; x35 = pv[34]; x36 = pv[35];
	x37 = pv[36]; x38 = pv[37]; x39 = pv[38]; x40 = pv[39];
	x41 = pv[40]; x42 = pv[41]; x43 = pv[42]; x44 = pv[43];
	x45 = pv[44]; x46 = pv[45]; x47 = pv[46]; x48 = pv[47];
        x49 = pv[48];
        pc[0]  = m1  = p1*x1  + p8*x2   + p15*x3  + p22*x4  + p29*x5  + p36*x6 + p43*x7;
        pc[1]  = m2  = p2*x1  + p9*x2   + p16*x3  + p23*x4  + p30*x5  + p37*x6 + p44*x7;
        pc[2]  = m3  = p3*x1  + p10*x2  + p17*x3  + p24*x4  + p31*x5  + p38*x6 + p45*x7;
        pc[3]  = m4  = p4*x1  + p11*x2  + p18*x3  + p25*x4  + p32*x5  + p39*x6 + p46*x7;
        pc[4]  = m5  = p5*x1  + p12*x2  + p19*x3  + p26*x4  + p33*x5  + p40*x6 + p47*x7;
        pc[5]  = m6  = p6*x1  + p13*x2  + p20*x3  + p27*x4  + p34*x5  + p41*x6 + p48*x7;
        pc[6]  = m7  = p7*x1  + p14*x2  + p21*x3  + p28*x4  + p35*x5  + p42*x6 + p49*x7;

        pc[7]  = m8  = p1*x8  + p8*x9   + p15*x10 + p22*x11 + p29*x12 + p36*x13 + p43*x14;
        pc[8]  = m9  = p2*x8  + p9*x9   + p16*x10 + p23*x11 + p30*x12 + p37*x13 + p44*x14;
        pc[9]  = m10 = p3*x8  + p10*x9  + p17*x10 + p24*x11 + p31*x12 + p38*x13 + p45*x14;
        pc[10] = m11 = p4*x8  + p11*x9  + p18*x10 + p25*x11 + p32*x12 + p39*x13 + p46*x14;
        pc[11] = m12 = p5*x8  + p12*x9  + p19*x10 + p26*x11 + p33*x12 + p40*x13 + p47*x14;
        pc[12] = m13 = p6*x8  + p13*x9  + p20*x10 + p27*x11 + p34*x12 + p41*x13 + p48*x14;
        pc[13] = m14 = p7*x8  + p14*x9  + p21*x10 + p28*x11 + p35*x12 + p42*x13 + p49*x14;

        pc[14] = m15 = p1*x15 + p8*x16  + p15*x17 + p22*x18 + p29*x19 + p36*x20 + p43*x21;
        pc[15] = m16 = p2*x15 + p9*x16  + p16*x17 + p23*x18 + p30*x19 + p37*x20 + p44*x21;
        pc[16] = m17 = p3*x15 + p10*x16 + p17*x17 + p24*x18 + p31*x19 + p38*x20 + p45*x21;
        pc[17] = m18 = p4*x15 + p11*x16 + p18*x17 + p25*x18 + p32*x19 + p39*x20 + p46*x21;
        pc[18] = m19 = p5*x15 + p12*x16 + p19*x17 + p26*x18 + p33*x19 + p40*x20 + p47*x21;
        pc[19] = m20 = p6*x15 + p13*x16 + p20*x17 + p27*x18 + p34*x19 + p41*x20 + p48*x21;
        pc[20] = m21 = p7*x15 + p14*x16 + p21*x17 + p28*x18 + p35*x19 + p42*x20 + p49*x21;

        pc[21] = m22 = p1*x22 + p8*x23  + p15*x24 + p22*x25 + p29*x26 + p36*x27 + p43*x28;
        pc[22] = m23 = p2*x22 + p9*x23  + p16*x24 + p23*x25 + p30*x26 + p37*x27 + p44*x28;
        pc[23] = m24 = p3*x22 + p10*x23 + p17*x24 + p24*x25 + p31*x26 + p38*x27 + p45*x28;
        pc[24] = m25 = p4*x22 + p11*x23 + p18*x24 + p25*x25 + p32*x26 + p39*x27 + p46*x28;
        pc[25] = m26 = p5*x22 + p12*x23 + p19*x24 + p26*x25 + p33*x26 + p40*x27 + p47*x28;
        pc[26] = m27 = p6*x22 + p13*x23 + p20*x24 + p27*x25 + p34*x26 + p41*x27 + p48*x28;
        pc[27] = m28 = p7*x22 + p14*x23 + p21*x24 + p28*x25 + p35*x26 + p42*x27 + p49*x28;

        pc[28] = m29 = p1*x29 + p8*x30  + p15*x31 + p22*x32 + p29*x33 + p36*x34 + p43*x35;
        pc[29] = m30 = p2*x29 + p9*x30  + p16*x31 + p23*x32 + p30*x33 + p37*x34 + p44*x35;
        pc[30] = m31 = p3*x29 + p10*x30 + p17*x31 + p24*x32 + p31*x33 + p38*x34 + p45*x35;
        pc[31] = m32 = p4*x29 + p11*x30 + p18*x31 + p25*x32 + p32*x33 + p39*x34 + p46*x35;
        pc[32] = m33 = p5*x29 + p12*x30 + p19*x31 + p26*x32 + p33*x33 + p40*x34 + p47*x35;
        pc[33] = m34 = p6*x29 + p13*x30 + p20*x31 + p27*x32 + p34*x33 + p41*x34 + p48*x35;
        pc[34] = m35 = p7*x29 + p14*x30 + p21*x31 + p28*x32 + p35*x33 + p42*x34 + p49*x35;

        pc[35] = m36 = p1*x36 + p8*x37  + p15*x38 + p22*x39 + p29*x40 + p36*x41 + p43*x42;
        pc[36] = m37 = p2*x36 + p9*x37  + p16*x38 + p23*x39 + p30*x40 + p37*x41 + p44*x42;
        pc[37] = m38 = p3*x36 + p10*x37 + p17*x38 + p24*x39 + p31*x40 + p38*x41 + p45*x42;
        pc[38] = m39 = p4*x36 + p11*x37 + p18*x38 + p25*x39 + p32*x40 + p39*x41 + p46*x42;
        pc[39] = m40 = p5*x36 + p12*x37 + p19*x38 + p26*x39 + p33*x40 + p40*x41 + p47*x42;
        pc[40] = m41 = p6*x36 + p13*x37 + p20*x38 + p27*x39 + p34*x40 + p41*x41 + p48*x42;
        pc[41] = m42 = p7*x36 + p14*x37 + p21*x38 + p28*x39 + p35*x40 + p42*x41 + p49*x42;

        pc[42] = m43 = p1*x43 + p8*x44  + p15*x45 + p22*x46 + p29*x47 + p36*x48 + p43*x49;
        pc[43] = m44 = p2*x43 + p9*x44  + p16*x45 + p23*x46 + p30*x47 + p37*x48 + p44*x49;
        pc[44] = m45 = p3*x43 + p10*x44 + p17*x45 + p24*x46 + p31*x47 + p38*x48 + p45*x49;
        pc[45] = m46 = p4*x43 + p11*x44 + p18*x45 + p25*x46 + p32*x47 + p39*x48 + p46*x49;
        pc[46] = m47 = p5*x43 + p12*x44 + p19*x45 + p26*x46 + p33*x47 + p40*x48 + p47*x49;
        pc[47] = m48 = p6*x43 + p13*x44 + p20*x45 + p27*x46 + p34*x47 + p41*x48 + p48*x49;
        pc[48] = m49 = p7*x43 + p14*x44 + p21*x45 + p28*x46 + p35*x47 + p42*x48 + p49*x49;

        nz = bi[row+1] - diag_offset[row] - 1;
        pv += 49;
        for (j=0; j<nz; j++) {
	  x1  = pv[0];  x2  = pv[1];  x3  = pv[2];  x4  = pv[3];
	  x5  = pv[4];  x6  = pv[5];  x7  = pv[6];  x8  = pv[7];
	  x9  = pv[8];  x10 = pv[9];  x11 = pv[10]; x12 = pv[11]; 
	  x13 = pv[12]; x14 = pv[13]; x15 = pv[14]; x16 = pv[15]; 
	  x17 = pv[16]; x18 = pv[17]; x19 = pv[18]; x20 = pv[19];
	  x21 = pv[20]; x22 = pv[21]; x23 = pv[22]; x24 = pv[23];
	  x25 = pv[24]; x26 = pv[25]; x27 = pv[26]; x28 = pv[27];
	  x29 = pv[28]; x30 = pv[29]; x31 = pv[30]; x32 = pv[31];
	  x33 = pv[32]; x34 = pv[33]; x35 = pv[34]; x36 = pv[35];
	  x37 = pv[36]; x38 = pv[37]; x39 = pv[38]; x40 = pv[39];
	  x41 = pv[40]; x42 = pv[41]; x43 = pv[42]; x44 = pv[43];
	  x45 = pv[44]; x46 = pv[45]; x47 = pv[46]; x48 = pv[47];
	  x49 = pv[48];
	  x    = rtmp + 49*pj[j];
	  x[0]  -= m1*x1  + m8*x2   + m15*x3  + m22*x4  + m29*x5  + m36*x6 + m43*x7;
	  x[1]  -= m2*x1  + m9*x2   + m16*x3  + m23*x4  + m30*x5  + m37*x6 + m44*x7;
	  x[2]  -= m3*x1  + m10*x2  + m17*x3  + m24*x4  + m31*x5  + m38*x6 + m45*x7;
	  x[3]  -= m4*x1  + m11*x2  + m18*x3  + m25*x4  + m32*x5  + m39*x6 + m46*x7;
	  x[4]  -= m5*x1  + m12*x2  + m19*x3  + m26*x4  + m33*x5  + m40*x6 + m47*x7;
	  x[5]  -= m6*x1  + m13*x2  + m20*x3  + m27*x4  + m34*x5  + m41*x6 + m48*x7;
	  x[6]  -= m7*x1  + m14*x2  + m21*x3  + m28*x4  + m35*x5  + m42*x6 + m49*x7;

	  x[7]  -= m1*x8  + m8*x9   + m15*x10 + m22*x11 + m29*x12 + m36*x13 + m43*x14;
	  x[8]  -= m2*x8  + m9*x9   + m16*x10 + m23*x11 + m30*x12 + m37*x13 + m44*x14;
	  x[9]  -= m3*x8  + m10*x9  + m17*x10 + m24*x11 + m31*x12 + m38*x13 + m45*x14;
	  x[10] -= m4*x8  + m11*x9  + m18*x10 + m25*x11 + m32*x12 + m39*x13 + m46*x14;
	  x[11] -= m5*x8  + m12*x9  + m19*x10 + m26*x11 + m33*x12 + m40*x13 + m47*x14;
	  x[12] -= m6*x8  + m13*x9  + m20*x10 + m27*x11 + m34*x12 + m41*x13 + m48*x14;
	  x[13] -= m7*x8  + m14*x9  + m21*x10 + m28*x11 + m35*x12 + m42*x13 + m49*x14;

	  x[14] -= m1*x15 + m8*x16  + m15*x17 + m22*x18 + m29*x19 + m36*x20 + m43*x21;
	  x[15] -= m2*x15 + m9*x16  + m16*x17 + m23*x18 + m30*x19 + m37*x20 + m44*x21;
	  x[16] -= m3*x15 + m10*x16 + m17*x17 + m24*x18 + m31*x19 + m38*x20 + m45*x21;
	  x[17] -= m4*x15 + m11*x16 + m18*x17 + m25*x18 + m32*x19 + m39*x20 + m46*x21;
	  x[18] -= m5*x15 + m12*x16 + m19*x17 + m26*x18 + m33*x19 + m40*x20 + m47*x21;
	  x[19] -= m6*x15 + m13*x16 + m20*x17 + m27*x18 + m34*x19 + m41*x20 + m48*x21;
	  x[20] -= m7*x15 + m14*x16 + m21*x17 + m28*x18 + m35*x19 + m42*x20 + m49*x21;

	  x[21] -= m1*x22 + m8*x23  + m15*x24 + m22*x25 + m29*x26 + m36*x27 + m43*x28;
	  x[22] -= m2*x22 + m9*x23  + m16*x24 + m23*x25 + m30*x26 + m37*x27 + m44*x28;
	  x[23] -= m3*x22 + m10*x23 + m17*x24 + m24*x25 + m31*x26 + m38*x27 + m45*x28;
	  x[24] -= m4*x22 + m11*x23 + m18*x24 + m25*x25 + m32*x26 + m39*x27 + m46*x28;
	  x[25] -= m5*x22 + m12*x23 + m19*x24 + m26*x25 + m33*x26 + m40*x27 + m47*x28;
	  x[26] -= m6*x22 + m13*x23 + m20*x24 + m27*x25 + m34*x26 + m41*x27 + m48*x28;
	  x[27] -= m7*x22 + m14*x23 + m21*x24 + m28*x25 + m35*x26 + m42*x27 + m49*x28;

	  x[28] -= m1*x29 + m8*x30  + m15*x31 + m22*x32 + m29*x33 + m36*x34 + m43*x35;
	  x[29] -= m2*x29 + m9*x30  + m16*x31 + m23*x32 + m30*x33 + m37*x34 + m44*x35;
	  x[30] -= m3*x29 + m10*x30 + m17*x31 + m24*x32 + m31*x33 + m38*x34 + m45*x35;
	  x[31] -= m4*x29 + m11*x30 + m18*x31 + m25*x32 + m32*x33 + m39*x34 + m46*x35;
	  x[32] -= m5*x29 + m12*x30 + m19*x31 + m26*x32 + m33*x33 + m40*x34 + m47*x35;
	  x[33] -= m6*x29 + m13*x30 + m20*x31 + m27*x32 + m34*x33 + m41*x34 + m48*x35;
	  x[34] -= m7*x29 + m14*x30 + m21*x31 + m28*x32 + m35*x33 + m42*x34 + m49*x35;

	  x[35] -= m1*x36 + m8*x37  + m15*x38 + m22*x39 + m29*x40 + m36*x41 + m43*x42;
	  x[36] -= m2*x36 + m9*x37  + m16*x38 + m23*x39 + m30*x40 + m37*x41 + m44*x42;
	  x[37] -= m3*x36 + m10*x37 + m17*x38 + m24*x39 + m31*x40 + m38*x41 + m45*x42;
	  x[38] -= m4*x36 + m11*x37 + m18*x38 + m25*x39 + m32*x40 + m39*x41 + m46*x42;
	  x[39] -= m5*x36 + m12*x37 + m19*x38 + m26*x39 + m33*x40 + m40*x41 + m47*x42;
	  x[40] -= m6*x36 + m13*x37 + m20*x38 + m27*x39 + m34*x40 + m41*x41 + m48*x42;
	  x[41] -= m7*x36 + m14*x37 + m21*x38 + m28*x39 + m35*x40 + m42*x41 + m49*x42;

	  x[42] -= m1*x43 + m8*x44  + m15*x45 + m22*x46 + m29*x47 + m36*x48 + m43*x49;
	  x[43] -= m2*x43 + m9*x44  + m16*x45 + m23*x46 + m30*x47 + m37*x48 + m44*x49;
	  x[44] -= m3*x43 + m10*x44 + m17*x45 + m24*x46 + m31*x47 + m38*x48 + m45*x49;
	  x[45] -= m4*x43 + m11*x44 + m18*x45 + m25*x46 + m32*x47 + m39*x48 + m46*x49;
	  x[46] -= m5*x43 + m12*x44 + m19*x45 + m26*x46 + m33*x47 + m40*x48 + m47*x49;
	  x[47] -= m6*x43 + m13*x44 + m20*x45 + m27*x46 + m34*x47 + m41*x48 + m48*x49;
	  x[48] -= m7*x43 + m14*x44 + m21*x45 + m28*x46 + m35*x47 + m42*x48 + m49*x49;
          pv   += 49;
        }
        PLogFlops(686*nz+637);
      } 
      row = *ajtmp++;
    }
    /* finished row so stick it into b->a */
    pv = ba + 49*bi[i];
    pj = bj + bi[i];
    nz = bi[i+1] - bi[i];
    for (j=0; j<nz; j++) {
      x      = rtmp+49*pj[j];
      pv[0]  = x[0];  pv[1]  = x[1];  pv[2]  = x[2];  pv[3]  = x[3];
      pv[4]  = x[4];  pv[5]  = x[5];  pv[6]  = x[6];  pv[7]  = x[7]; 
      pv[8]  = x[8];  pv[9]  = x[9];  pv[10] = x[10]; pv[11] = x[11]; 
      pv[12] = x[12]; pv[13] = x[13]; pv[14] = x[14]; pv[15] = x[15]; 
      pv[16] = x[16]; pv[17] = x[17]; pv[18] = x[18]; pv[19] = x[19]; 
      pv[20] = x[20]; pv[21] = x[21]; pv[22] = x[22]; pv[23] = x[23]; 
      pv[24] = x[24]; pv[25] = x[25]; pv[26] = x[26]; pv[27] = x[27]; 
      pv[28] = x[28]; pv[29] = x[29]; pv[30] = x[30]; pv[31] = x[31]; 
      pv[32] = x[32]; pv[33] = x[33]; pv[34] = x[34]; pv[35] = x[35]; 
      pv[36] = x[36]; pv[37] = x[37]; pv[38] = x[38]; pv[39] = x[39]; 
      pv[40] = x[40]; pv[41] = x[41]; pv[42] = x[42]; pv[43] = x[43]; 
      pv[44] = x[44]; pv[45] = x[45]; pv[46] = x[46]; pv[47] = x[47]; 
      pv[48] = x[48];  
      pv   += 49;
    }
    /* invert diagonal block */
    w = ba + 49*diag_offset[i];
    ierr = Kernel_A_gets_inverse_A_7(w);CHKERRQ(ierr);
  }

  ierr = PetscFree(rtmp);CHKERRQ(ierr);
  C->factor    = FACTOR_LU;
  C->assembled = PETSC_TRUE;
  PLogFlops(1.3333*343*b->mbs); /* from inverting diagonal blocks */
  PetscFunctionReturn(0);
}

/* ------------------------------------------------------------*/
/*
      Version for when blocks are 6 by 6
*/
#undef __FUNC__  
#define __FUNC__ "MatCholeskyFactorNumeric_SeqSBAIJ_6"
int MatCholeskyFactorNumeric_SeqSBAIJ_6(Mat A,Mat *B)
{
  Mat          C = *B;
  Mat_SeqBAIJ  *a = (Mat_SeqBAIJ*)A->data,*b = (Mat_SeqBAIJ *)C->data;
  IS           isrow = b->row,isicol = b->icol;
  int          *r,*ic,ierr,i,j,n = a->mbs,*bi = b->i,*bj = b->j;
  int          *ajtmpold,*ajtmp,nz,row;
  int          *diag_offset = b->diag,idx,*ai=a->i,*aj=a->j,*pj;
  MatScalar    *pv,*v,*rtmp,*pc,*w,*x;
  MatScalar    p1,p2,p3,p4,m1,m2,m3,m4,m5,m6,m7,m8,m9,x1,x2,x3,x4;
  MatScalar    p5,p6,p7,p8,p9,x5,x6,x7,x8,x9,x10,x11,x12,x13,x14,x15,x16;
  MatScalar    x17,x18,x19,x20,x21,x22,x23,x24,x25,p10,p11,p12,p13,p14;
  MatScalar    p15,p16,p17,p18,p19,p20,p21,p22,p23,p24,p25,m10,m11,m12;
  MatScalar    m13,m14,m15,m16,m17,m18,m19,m20,m21,m22,m23,m24,m25;
  MatScalar    p26,p27,p28,p29,p30,p31,p32,p33,p34,p35,p36;
  MatScalar    x26,x27,x28,x29,x30,x31,x32,x33,x34,x35,x36;
  MatScalar    m26,m27,m28,m29,m30,m31,m32,m33,m34,m35,m36;
  MatScalar    *ba = b->a,*aa = a->a;

  PetscFunctionBegin;
  ierr  = ISGetIndices(isrow,&r);CHKERRQ(ierr);
  ierr  = ISGetIndices(isicol,&ic);CHKERRQ(ierr);
  rtmp  = (MatScalar*)PetscMalloc(36*(n+1)*sizeof(MatScalar));CHKPTRQ(rtmp);

  for (i=0; i<n; i++) {
    nz    = bi[i+1] - bi[i];
    ajtmp = bj + bi[i];
    for  (j=0; j<nz; j++) {
      x = rtmp+36*ajtmp[j]; 
      x[0] = x[1] = x[2] = x[3] = x[4] = x[5] = x[6] = x[7] = x[8] = x[9] = 0.0;
      x[10] = x[11] = x[12] = x[13] = x[14] = x[15] = x[16] = x[17] = 0.0;
      x[18] = x[19] = x[20] = x[21] = x[22] = x[23] = x[24] = x[25] = 0.0 ;
      x[26] = x[27] = x[28] = x[29] = x[30] = x[31] = x[32] = x[33] = 0.0 ;
      x[34] = x[35] = 0.0 ;
    }
    /* load in initial (unfactored row) */
    idx      = r[i];
    nz       = ai[idx+1] - ai[idx];
    ajtmpold = aj + ai[idx];
    v        = aa + 36*ai[idx];
    for (j=0; j<nz; j++) {
      x    = rtmp+36*ic[ajtmpold[j]];
      x[0] =  v[0];  x[1] =  v[1];  x[2] =  v[2];  x[3] =  v[3];
      x[4] =  v[4];  x[5] =  v[5];  x[6] =  v[6];  x[7] =  v[7]; 
      x[8] =  v[8];  x[9] =  v[9];  x[10] = v[10]; x[11] = v[11]; 
      x[12] = v[12]; x[13] = v[13]; x[14] = v[14]; x[15] = v[15]; 
      x[16] = v[16]; x[17] = v[17]; x[18] = v[18]; x[19] = v[19]; 
      x[20] = v[20]; x[21] = v[21]; x[22] = v[22]; x[23] = v[23]; 
      x[24] = v[24]; x[25] = v[25]; x[26] = v[26]; x[27] = v[27]; 
      x[28] = v[28]; x[29] = v[29]; x[30] = v[30]; x[31] = v[31]; 
      x[32] = v[32]; x[33] = v[33]; x[34] = v[34]; x[35] = v[35]; 
      v    += 36;
    }
    row = *ajtmp++;
    while (row < i) {
      pc  =  rtmp + 36*row;
      p1  = pc[0];  p2  = pc[1];  p3  = pc[2];  p4  = pc[3];
      p5  = pc[4];  p6  = pc[5];  p7  = pc[6];  p8  = pc[7];
      p9  = pc[8];  p10 = pc[9];  p11 = pc[10]; p12 = pc[11]; 
      p13 = pc[12]; p14 = pc[13]; p15 = pc[14]; p16 = pc[15]; 
      p17 = pc[16]; p18 = pc[17]; p19 = pc[18]; p20 = pc[19];
      p21 = pc[20]; p22 = pc[21]; p23 = pc[22]; p24 = pc[23];
      p25 = pc[24]; p26 = pc[25]; p27 = pc[26]; p28 = pc[27];
      p29 = pc[28]; p30 = pc[29]; p31 = pc[30]; p32 = pc[31];
      p33 = pc[32]; p34 = pc[33]; p35 = pc[34]; p36 = pc[35];
      if (p1  != 0.0 || p2  != 0.0 || p3  != 0.0 || p4  != 0.0 || 
          p5  != 0.0 || p6  != 0.0 || p7  != 0.0 || p8  != 0.0 ||
          p9  != 0.0 || p10 != 0.0 || p11 != 0.0 || p12 != 0.0 || 
          p13 != 0.0 || p14 != 0.0 || p15 != 0.0 || p16 != 0.0 ||
          p17 != 0.0 || p18 != 0.0 || p19 != 0.0 || p20 != 0.0 ||
          p21 != 0.0 || p22 != 0.0 || p23 != 0.0 || p24 != 0.0 ||
          p25 != 0.0 || p26 != 0.0 || p27 != 0.0 || p28 != 0.0 ||
          p29 != 0.0 || p30 != 0.0 || p31 != 0.0 || p32 != 0.0 ||
          p33 != 0.0 || p34 != 0.0 || p35 != 0.0 || p36 != 0.0) { 
        pv = ba + 36*diag_offset[row];
        pj = bj + diag_offset[row] + 1;
	x1  = pv[0];  x2  = pv[1];  x3  = pv[2];  x4  = pv[3];
	x5  = pv[4];  x6  = pv[5];  x7  = pv[6];  x8  = pv[7];
	x9  = pv[8];  x10 = pv[9];  x11 = pv[10]; x12 = pv[11]; 
	x13 = pv[12]; x14 = pv[13]; x15 = pv[14]; x16 = pv[15]; 
	x17 = pv[16]; x18 = pv[17]; x19 = pv[18]; x20 = pv[19];
	x21 = pv[20]; x22 = pv[21]; x23 = pv[22]; x24 = pv[23];
	x25 = pv[24]; x26 = pv[25]; x27 = pv[26]; x28 = pv[27];
	x29 = pv[28]; x30 = pv[29]; x31 = pv[30]; x32 = pv[31];
	x33 = pv[32]; x34 = pv[33]; x35 = pv[34]; x36 = pv[35];
        pc[0]  = m1  = p1*x1  + p7*x2   + p13*x3  + p19*x4  + p25*x5  + p31*x6;
        pc[1]  = m2  = p2*x1  + p8*x2   + p14*x3  + p20*x4  + p26*x5  + p32*x6;
        pc[2]  = m3  = p3*x1  + p9*x2   + p15*x3  + p21*x4  + p27*x5  + p33*x6;
        pc[3]  = m4  = p4*x1  + p10*x2  + p16*x3  + p22*x4  + p28*x5  + p34*x6;
        pc[4]  = m5  = p5*x1  + p11*x2  + p17*x3  + p23*x4  + p29*x5  + p35*x6;
        pc[5]  = m6  = p6*x1  + p12*x2  + p18*x3  + p24*x4  + p30*x5  + p36*x6;

        pc[6]  = m7  = p1*x7  + p7*x8   + p13*x9  + p19*x10 + p25*x11 + p31*x12;
        pc[7]  = m8  = p2*x7  + p8*x8   + p14*x9  + p20*x10 + p26*x11 + p32*x12;
        pc[8]  = m9  = p3*x7  + p9*x8   + p15*x9  + p21*x10 + p27*x11 + p33*x12;
        pc[9]  = m10 = p4*x7  + p10*x8  + p16*x9  + p22*x10 + p28*x11 + p34*x12;
        pc[10] = m11 = p5*x7  + p11*x8  + p17*x9  + p23*x10 + p29*x11 + p35*x12;
        pc[11] = m12 = p6*x7  + p12*x8  + p18*x9  + p24*x10 + p30*x11 + p36*x12;

        pc[12] = m13 = p1*x13 + p7*x14  + p13*x15 + p19*x16 + p25*x17 + p31*x18;
        pc[13] = m14 = p2*x13 + p8*x14  + p14*x15 + p20*x16 + p26*x17 + p32*x18;
        pc[14] = m15 = p3*x13 + p9*x14  + p15*x15 + p21*x16 + p27*x17 + p33*x18;
        pc[15] = m16 = p4*x13 + p10*x14 + p16*x15 + p22*x16 + p28*x17 + p34*x18;
        pc[16] = m17 = p5*x13 + p11*x14 + p17*x15 + p23*x16 + p29*x17 + p35*x18;
        pc[17] = m18 = p6*x13 + p12*x14 + p18*x15 + p24*x16 + p30*x17 + p36*x18;

        pc[18] = m19 = p1*x19 + p7*x20  + p13*x21 + p19*x22 + p25*x23 + p31*x24;
        pc[19] = m20 = p2*x19 + p8*x20  + p14*x21 + p20*x22 + p26*x23 + p32*x24;
        pc[20] = m21 = p3*x19 + p9*x20  + p15*x21 + p21*x22 + p27*x23 + p33*x24;
        pc[21] = m22 = p4*x19 + p10*x20 + p16*x21 + p22*x22 + p28*x23 + p34*x24;
        pc[22] = m23 = p5*x19 + p11*x20 + p17*x21 + p23*x22 + p29*x23 + p35*x24;
        pc[23] = m24 = p6*x19 + p12*x20 + p18*x21 + p24*x22 + p30*x23 + p36*x24;

        pc[24] = m25 = p1*x25 + p7*x26  + p13*x27 + p19*x28 + p25*x29 + p31*x30;
        pc[25] = m26 = p2*x25 + p8*x26  + p14*x27 + p20*x28 + p26*x29 + p32*x30;
        pc[26] = m27 = p3*x25 + p9*x26  + p15*x27 + p21*x28 + p27*x29 + p33*x30;
        pc[27] = m28 = p4*x25 + p10*x26 + p16*x27 + p22*x28 + p28*x29 + p34*x30;
        pc[28] = m29 = p5*x25 + p11*x26 + p17*x27 + p23*x28 + p29*x29 + p35*x30;
        pc[29] = m30 = p6*x25 + p12*x26 + p18*x27 + p24*x28 + p30*x29 + p36*x30;

        pc[30] = m31 = p1*x31 + p7*x32  + p13*x33 + p19*x34 + p25*x35 + p31*x36;
        pc[31] = m32 = p2*x31 + p8*x32  + p14*x33 + p20*x34 + p26*x35 + p32*x36;
        pc[32] = m33 = p3*x31 + p9*x32  + p15*x33 + p21*x34 + p27*x35 + p33*x36;
        pc[33] = m34 = p4*x31 + p10*x32 + p16*x33 + p22*x34 + p28*x35 + p34*x36;
        pc[34] = m35 = p5*x31 + p11*x32 + p17*x33 + p23*x34 + p29*x35 + p35*x36;
        pc[35] = m36 = p6*x31 + p12*x32 + p18*x33 + p24*x34 + p30*x35 + p36*x36;

        nz = bi[row+1] - diag_offset[row] - 1;
        pv += 36;
        for (j=0; j<nz; j++) {
	  x1  = pv[0];  x2  = pv[1];  x3  = pv[2];  x4  = pv[3];
	  x5  = pv[4];  x6  = pv[5];  x7  = pv[6];  x8  = pv[7];
	  x9  = pv[8];  x10 = pv[9];  x11 = pv[10]; x12 = pv[11]; 
	  x13 = pv[12]; x14 = pv[13]; x15 = pv[14]; x16 = pv[15]; 
	  x17 = pv[16]; x18 = pv[17]; x19 = pv[18]; x20 = pv[19];
	  x21 = pv[20]; x22 = pv[21]; x23 = pv[22]; x24 = pv[23];
	  x25 = pv[24]; x26 = pv[25]; x27 = pv[26]; x28 = pv[27];
	  x29 = pv[28]; x30 = pv[29]; x31 = pv[30]; x32 = pv[31];
	  x33 = pv[32]; x34 = pv[33]; x35 = pv[34]; x36 = pv[35];
	  x    = rtmp + 36*pj[j];
          x[0]  -= m1*x1  + m7*x2   + m13*x3  + m19*x4  + m25*x5  + m31*x6;
          x[1]  -= m2*x1  + m8*x2   + m14*x3  + m20*x4  + m26*x5  + m32*x6;
          x[2]  -= m3*x1  + m9*x2   + m15*x3  + m21*x4  + m27*x5  + m33*x6;
          x[3]  -= m4*x1  + m10*x2  + m16*x3  + m22*x4  + m28*x5  + m34*x6;
          x[4]  -= m5*x1  + m11*x2  + m17*x3  + m23*x4  + m29*x5  + m35*x6;
          x[5]  -= m6*x1  + m12*x2  + m18*x3  + m24*x4  + m30*x5  + m36*x6;

	  x[6]  -= m1*x7  + m7*x8   + m13*x9  + m19*x10 + m25*x11 + m31*x12;
	  x[7]  -= m2*x7  + m8*x8   + m14*x9  + m20*x10 + m26*x11 + m32*x12;
	  x[8]  -= m3*x7  + m9*x8   + m15*x9  + m21*x10 + m27*x11 + m33*x12;
	  x[9]  -= m4*x7  + m10*x8  + m16*x9  + m22*x10 + m28*x11 + m34*x12;
	  x[10] -= m5*x7  + m11*x8  + m17*x9  + m23*x10 + m29*x11 + m35*x12;
	  x[11] -= m6*x7  + m12*x8  + m18*x9  + m24*x10 + m30*x11 + m36*x12;

	  x[12] -= m1*x13 + m7*x14  + m13*x15 + m19*x16 + m25*x17 + m31*x18;
	  x[13] -= m2*x13 + m8*x14  + m14*x15 + m20*x16 + m26*x17 + m32*x18;
	  x[14] -= m3*x13 + m9*x14  + m15*x15 + m21*x16 + m27*x17 + m33*x18;
	  x[15] -= m4*x13 + m10*x14 + m16*x15 + m22*x16 + m28*x17 + m34*x18;
	  x[16] -= m5*x13 + m11*x14 + m17*x15 + m23*x16 + m29*x17 + m35*x18;
	  x[17] -= m6*x13 + m12*x14 + m18*x15 + m24*x16 + m30*x17 + m36*x18;

	  x[18] -= m1*x19 + m7*x20  + m13*x21 + m19*x22 + m25*x23 + m31*x24;
	  x[19] -= m2*x19 + m8*x20  + m14*x21 + m20*x22 + m26*x23 + m32*x24;
	  x[20] -= m3*x19 + m9*x20  + m15*x21 + m21*x22 + m27*x23 + m33*x24;
	  x[21] -= m4*x19 + m10*x20 + m16*x21 + m22*x22 + m28*x23 + m34*x24;
	  x[22] -= m5*x19 + m11*x20 + m17*x21 + m23*x22 + m29*x23 + m35*x24;
	  x[23] -= m6*x19 + m12*x20 + m18*x21 + m24*x22 + m30*x23 + m36*x24;

	  x[24] -= m1*x25 + m7*x26  + m13*x27 + m19*x28 + m25*x29 + m31*x30;
	  x[25] -= m2*x25 + m8*x26  + m14*x27 + m20*x28 + m26*x29 + m32*x30;
	  x[26] -= m3*x25 + m9*x26  + m15*x27 + m21*x28 + m27*x29 + m33*x30;
	  x[27] -= m4*x25 + m10*x26 + m16*x27 + m22*x28 + m28*x29 + m34*x30;
	  x[28] -= m5*x25 + m11*x26 + m17*x27 + m23*x28 + m29*x29 + m35*x30;
	  x[29] -= m6*x25 + m12*x26 + m18*x27 + m24*x28 + m30*x29 + m36*x30;

	  x[30] -= m1*x31 + m7*x32  + m13*x33 + m19*x34 + m25*x35 + m31*x36;
	  x[31] -= m2*x31 + m8*x32  + m14*x33 + m20*x34 + m26*x35 + m32*x36;
	  x[32] -= m3*x31 + m9*x32  + m15*x33 + m21*x34 + m27*x35 + m33*x36;
	  x[33] -= m4*x31 + m10*x32 + m16*x33 + m22*x34 + m28*x35 + m34*x36;
	  x[34] -= m5*x31 + m11*x32 + m17*x33 + m23*x34 + m29*x35 + m35*x36;
	  x[35] -= m6*x31 + m12*x32 + m18*x33 + m24*x34 + m30*x35 + m36*x36;

          pv   += 36;
        }
        PLogFlops(432*nz+396);
      } 
      row = *ajtmp++;
    }
    /* finished row so stick it into b->a */
    pv = ba + 36*bi[i];
    pj = bj + bi[i];
    nz = bi[i+1] - bi[i];
    for (j=0; j<nz; j++) {
      x      = rtmp+36*pj[j];
      pv[0]  = x[0];  pv[1]  = x[1];  pv[2]  = x[2];  pv[3]  = x[3];
      pv[4]  = x[4];  pv[5]  = x[5];  pv[6]  = x[6];  pv[7]  = x[7]; 
      pv[8]  = x[8];  pv[9]  = x[9];  pv[10] = x[10]; pv[11] = x[11]; 
      pv[12] = x[12]; pv[13] = x[13]; pv[14] = x[14]; pv[15] = x[15]; 
      pv[16] = x[16]; pv[17] = x[17]; pv[18] = x[18]; pv[19] = x[19]; 
      pv[20] = x[20]; pv[21] = x[21]; pv[22] = x[22]; pv[23] = x[23]; 
      pv[24] = x[24]; pv[25] = x[25]; pv[26] = x[26]; pv[27] = x[27]; 
      pv[28] = x[28]; pv[29] = x[29]; pv[30] = x[30]; pv[31] = x[31]; 
      pv[32] = x[32]; pv[33] = x[33]; pv[34] = x[34]; pv[35] = x[35]; 
      pv   += 36;
    }
    /* invert diagonal block */
    w = ba + 36*diag_offset[i];
    ierr = Kernel_A_gets_inverse_A_6(w);CHKERRQ(ierr);
  }

  ierr = PetscFree(rtmp);CHKERRQ(ierr);
  ierr = ISRestoreIndices(isicol,&ic);CHKERRQ(ierr);
  ierr = ISRestoreIndices(isrow,&r);CHKERRQ(ierr);
  C->factor = FACTOR_LU;
  C->assembled = PETSC_TRUE;
  PLogFlops(1.3333*216*b->mbs); /* from inverting diagonal blocks */
  PetscFunctionReturn(0);
}
/*
      Version for when blocks are 6 by 6 Using natural ordering
*/
#undef __FUNC__  
#define __FUNC__ "MatCholeskyFactorNumeric_SeqSBAIJ_6_NaturalOrdering"
int MatCholeskyFactorNumeric_SeqSBAIJ_6_NaturalOrdering(Mat A,Mat *B)
{
  Mat         C = *B;
  Mat_SeqBAIJ *a = (Mat_SeqBAIJ*)A->data,*b = (Mat_SeqBAIJ *)C->data;
  int         ierr,i,j,n = a->mbs,*bi = b->i,*bj = b->j;
  int         *ajtmpold,*ajtmp,nz,row;
  int         *diag_offset = b->diag,*ai=a->i,*aj=a->j,*pj;
  MatScalar   *pv,*v,*rtmp,*pc,*w,*x;
  MatScalar   x1,x2,x3,x4,x5,x6,x7,x8,x9,x10,x11,x12,x13,x14,x15;
  MatScalar   x16,x17,x18,x19,x20,x21,x22,x23,x24,x25;
  MatScalar   p1,p2,p3,p4,p5,p6,p7,p8,p9,p10,p11,p12,p13,p14,p15;
  MatScalar   p16,p17,p18,p19,p20,p21,p22,p23,p24,p25;
  MatScalar   m1,m2,m3,m4,m5,m6,m7,m8,m9,m10,m11,m12,m13,m14,m15;
  MatScalar   m16,m17,m18,m19,m20,m21,m22,m23,m24,m25;
  MatScalar   p26,p27,p28,p29,p30,p31,p32,p33,p34,p35,p36;
  MatScalar   x26,x27,x28,x29,x30,x31,x32,x33,x34,x35,x36;
  MatScalar   m26,m27,m28,m29,m30,m31,m32,m33,m34,m35,m36;
  MatScalar   *ba = b->a,*aa = a->a;

  PetscFunctionBegin;
  rtmp  = (MatScalar*)PetscMalloc(36*(n+1)*sizeof(MatScalar));CHKPTRQ(rtmp);
  for (i=0; i<n; i++) {
    nz    = bi[i+1] - bi[i];
    ajtmp = bj + bi[i];
    for  (j=0; j<nz; j++) {
      x = rtmp+36*ajtmp[j]; 
      x[0] = x[1] = x[2] = x[3] = x[4] = x[5] = x[6] = x[7] = x[8] = x[9] = 0.0;
      x[10] = x[11] = x[12] = x[13] = x[14] = x[15] = x[16] = x[17] = 0.0;
      x[18] = x[19] = x[20] = x[21] = x[22] = x[23] = x[24] = x[25] = 0.0 ;
      x[26] = x[27] = x[28] = x[29] = x[30] = x[31] = x[32] = x[33] = 0.0 ;
      x[34] = x[35] = 0.0 ;
    }
    /* load in initial (unfactored row) */
    nz       = ai[i+1] - ai[i];
    ajtmpold = aj + ai[i];
    v        = aa + 36*ai[i];
    for (j=0; j<nz; j++) {
      x    = rtmp+36*ajtmpold[j];
      x[0] =  v[0];  x[1] =  v[1];  x[2] =  v[2];  x[3] =  v[3];
      x[4] =  v[4];  x[5] =  v[5];  x[6] =  v[6];  x[7] =  v[7]; 
      x[8] =  v[8];  x[9] =  v[9];  x[10] = v[10]; x[11] = v[11]; 
      x[12] = v[12]; x[13] = v[13]; x[14] = v[14]; x[15] = v[15]; 
      x[16] = v[16]; x[17] = v[17]; x[18] = v[18]; x[19] = v[19]; 
      x[20] = v[20]; x[21] = v[21]; x[22] = v[22]; x[23] = v[23]; 
      x[24] = v[24]; x[25] = v[25]; x[26] = v[26]; x[27] = v[27]; 
      x[28] = v[28]; x[29] = v[29]; x[30] = v[30]; x[31] = v[31]; 
      x[32] = v[32]; x[33] = v[33]; x[34] = v[34]; x[35] = v[35]; 
      v    += 36;
    }
    row = *ajtmp++;
    while (row < i) {
      pc  = rtmp + 36*row;
      p1  = pc[0];  p2  = pc[1];  p3  = pc[2];  p4  = pc[3];
      p5  = pc[4];  p6  = pc[5];  p7  = pc[6];  p8  = pc[7];
      p9  = pc[8];  p10 = pc[9];  p11 = pc[10]; p12 = pc[11]; 
      p13 = pc[12]; p14 = pc[13]; p15 = pc[14]; p16 = pc[15]; 
      p17 = pc[16]; p18 = pc[17]; p19 = pc[18]; p20 = pc[19];
      p21 = pc[20]; p22 = pc[21]; p23 = pc[22]; p24 = pc[23];
      p25 = pc[24]; p26 = pc[25]; p27 = pc[26]; p28 = pc[27];
      p29 = pc[28]; p30 = pc[29]; p31 = pc[30]; p32 = pc[31];
      p33 = pc[32]; p34 = pc[33]; p35 = pc[34]; p36 = pc[35];
      if (p1  != 0.0 || p2  != 0.0 || p3  != 0.0 || p4  != 0.0 || 
          p5  != 0.0 || p6  != 0.0 || p7  != 0.0 || p8  != 0.0 ||
          p9  != 0.0 || p10 != 0.0 || p11 != 0.0 || p12 != 0.0 || 
          p13 != 0.0 || p14 != 0.0 || p15 != 0.0 || p16 != 0.0 ||
          p17 != 0.0 || p18 != 0.0 || p19 != 0.0 || p20 != 0.0 ||
          p21 != 0.0 || p22 != 0.0 || p23 != 0.0 || p24 != 0.0 ||
          p25 != 0.0 || p26 != 0.0 || p27 != 0.0 || p28 != 0.0 ||
          p29 != 0.0 || p30 != 0.0 || p31 != 0.0 || p32 != 0.0 ||
          p33 != 0.0 || p34 != 0.0 || p35 != 0.0 || p36 != 0.0) { 
        pv = ba + 36*diag_offset[row];
        pj = bj + diag_offset[row] + 1;
	x1  = pv[0];  x2  = pv[1];  x3  = pv[2];  x4  = pv[3];
	x5  = pv[4];  x6  = pv[5];  x7  = pv[6];  x8  = pv[7];
	x9  = pv[8];  x10 = pv[9];  x11 = pv[10]; x12 = pv[11]; 
	x13 = pv[12]; x14 = pv[13]; x15 = pv[14]; x16 = pv[15]; 
	x17 = pv[16]; x18 = pv[17]; x19 = pv[18]; x20 = pv[19];
	x21 = pv[20]; x22 = pv[21]; x23 = pv[22]; x24 = pv[23];
	x25 = pv[24]; x26 = pv[25]; x27 = pv[26]; x28 = pv[27];
	x29 = pv[28]; x30 = pv[29]; x31 = pv[30]; x32 = pv[31];
	x33 = pv[32]; x34 = pv[33]; x35 = pv[34]; x36 = pv[35];
        pc[0]  = m1  = p1*x1  + p7*x2   + p13*x3  + p19*x4  + p25*x5  + p31*x6;
        pc[1]  = m2  = p2*x1  + p8*x2   + p14*x3  + p20*x4  + p26*x5  + p32*x6;
        pc[2]  = m3  = p3*x1  + p9*x2   + p15*x3  + p21*x4  + p27*x5  + p33*x6;
        pc[3]  = m4  = p4*x1  + p10*x2  + p16*x3  + p22*x4  + p28*x5  + p34*x6;
        pc[4]  = m5  = p5*x1  + p11*x2  + p17*x3  + p23*x4  + p29*x5  + p35*x6;
        pc[5]  = m6  = p6*x1  + p12*x2  + p18*x3  + p24*x4  + p30*x5  + p36*x6;

        pc[6]  = m7  = p1*x7  + p7*x8   + p13*x9  + p19*x10 + p25*x11 + p31*x12;
        pc[7]  = m8  = p2*x7  + p8*x8   + p14*x9  + p20*x10 + p26*x11 + p32*x12;
        pc[8]  = m9  = p3*x7  + p9*x8   + p15*x9  + p21*x10 + p27*x11 + p33*x12;
        pc[9]  = m10 = p4*x7  + p10*x8  + p16*x9  + p22*x10 + p28*x11 + p34*x12;
        pc[10] = m11 = p5*x7  + p11*x8  + p17*x9  + p23*x10 + p29*x11 + p35*x12;
        pc[11] = m12 = p6*x7  + p12*x8  + p18*x9  + p24*x10 + p30*x11 + p36*x12;

        pc[12] = m13 = p1*x13 + p7*x14  + p13*x15 + p19*x16 + p25*x17 + p31*x18;
        pc[13] = m14 = p2*x13 + p8*x14  + p14*x15 + p20*x16 + p26*x17 + p32*x18;
        pc[14] = m15 = p3*x13 + p9*x14  + p15*x15 + p21*x16 + p27*x17 + p33*x18;
        pc[15] = m16 = p4*x13 + p10*x14 + p16*x15 + p22*x16 + p28*x17 + p34*x18;
        pc[16] = m17 = p5*x13 + p11*x14 + p17*x15 + p23*x16 + p29*x17 + p35*x18;
        pc[17] = m18 = p6*x13 + p12*x14 + p18*x15 + p24*x16 + p30*x17 + p36*x18;

        pc[18] = m19 = p1*x19 + p7*x20  + p13*x21 + p19*x22 + p25*x23 + p31*x24;
        pc[19] = m20 = p2*x19 + p8*x20  + p14*x21 + p20*x22 + p26*x23 + p32*x24;
        pc[20] = m21 = p3*x19 + p9*x20  + p15*x21 + p21*x22 + p27*x23 + p33*x24;
        pc[21] = m22 = p4*x19 + p10*x20 + p16*x21 + p22*x22 + p28*x23 + p34*x24;
        pc[22] = m23 = p5*x19 + p11*x20 + p17*x21 + p23*x22 + p29*x23 + p35*x24;
        pc[23] = m24 = p6*x19 + p12*x20 + p18*x21 + p24*x22 + p30*x23 + p36*x24;

        pc[24] = m25 = p1*x25 + p7*x26  + p13*x27 + p19*x28 + p25*x29 + p31*x30;
        pc[25] = m26 = p2*x25 + p8*x26  + p14*x27 + p20*x28 + p26*x29 + p32*x30;
        pc[26] = m27 = p3*x25 + p9*x26  + p15*x27 + p21*x28 + p27*x29 + p33*x30;
        pc[27] = m28 = p4*x25 + p10*x26 + p16*x27 + p22*x28 + p28*x29 + p34*x30;
        pc[28] = m29 = p5*x25 + p11*x26 + p17*x27 + p23*x28 + p29*x29 + p35*x30;
        pc[29] = m30 = p6*x25 + p12*x26 + p18*x27 + p24*x28 + p30*x29 + p36*x30;

        pc[30] = m31 = p1*x31 + p7*x32  + p13*x33 + p19*x34 + p25*x35 + p31*x36;
        pc[31] = m32 = p2*x31 + p8*x32  + p14*x33 + p20*x34 + p26*x35 + p32*x36;
        pc[32] = m33 = p3*x31 + p9*x32  + p15*x33 + p21*x34 + p27*x35 + p33*x36;
        pc[33] = m34 = p4*x31 + p10*x32 + p16*x33 + p22*x34 + p28*x35 + p34*x36;
        pc[34] = m35 = p5*x31 + p11*x32 + p17*x33 + p23*x34 + p29*x35 + p35*x36;
        pc[35] = m36 = p6*x31 + p12*x32 + p18*x33 + p24*x34 + p30*x35 + p36*x36;

        nz = bi[row+1] - diag_offset[row] - 1;
        pv += 36;
        for (j=0; j<nz; j++) {
	  x1  = pv[0];  x2  = pv[1];  x3  = pv[2];  x4  = pv[3];
	  x5  = pv[4];  x6  = pv[5];  x7  = pv[6];  x8  = pv[7];
	  x9  = pv[8];  x10 = pv[9];  x11 = pv[10]; x12 = pv[11]; 
	  x13 = pv[12]; x14 = pv[13]; x15 = pv[14]; x16 = pv[15]; 
	  x17 = pv[16]; x18 = pv[17]; x19 = pv[18]; x20 = pv[19];
	  x21 = pv[20]; x22 = pv[21]; x23 = pv[22]; x24 = pv[23];
	  x25 = pv[24]; x26 = pv[25]; x27 = pv[26]; x28 = pv[27];
	  x29 = pv[28]; x30 = pv[29]; x31 = pv[30]; x32 = pv[31];
	  x33 = pv[32]; x34 = pv[33]; x35 = pv[34]; x36 = pv[35];
	  x    = rtmp + 36*pj[j];
          x[0]  -= m1*x1  + m7*x2   + m13*x3  + m19*x4  + m25*x5  + m31*x6;
          x[1]  -= m2*x1  + m8*x2   + m14*x3  + m20*x4  + m26*x5  + m32*x6;
          x[2]  -= m3*x1  + m9*x2   + m15*x3  + m21*x4  + m27*x5  + m33*x6;
          x[3]  -= m4*x1  + m10*x2  + m16*x3  + m22*x4  + m28*x5  + m34*x6;
          x[4]  -= m5*x1  + m11*x2  + m17*x3  + m23*x4  + m29*x5  + m35*x6;
          x[5]  -= m6*x1  + m12*x2  + m18*x3  + m24*x4  + m30*x5  + m36*x6;

	  x[6]  -= m1*x7  + m7*x8   + m13*x9  + m19*x10 + m25*x11 + m31*x12;
	  x[7]  -= m2*x7  + m8*x8   + m14*x9  + m20*x10 + m26*x11 + m32*x12;
	  x[8]  -= m3*x7  + m9*x8   + m15*x9  + m21*x10 + m27*x11 + m33*x12;
	  x[9]  -= m4*x7  + m10*x8  + m16*x9  + m22*x10 + m28*x11 + m34*x12;
	  x[10] -= m5*x7  + m11*x8  + m17*x9  + m23*x10 + m29*x11 + m35*x12;
	  x[11] -= m6*x7  + m12*x8  + m18*x9  + m24*x10 + m30*x11 + m36*x12;

	  x[12] -= m1*x13 + m7*x14  + m13*x15 + m19*x16 + m25*x17 + m31*x18;
	  x[13] -= m2*x13 + m8*x14  + m14*x15 + m20*x16 + m26*x17 + m32*x18;
	  x[14] -= m3*x13 + m9*x14  + m15*x15 + m21*x16 + m27*x17 + m33*x18;
	  x[15] -= m4*x13 + m10*x14 + m16*x15 + m22*x16 + m28*x17 + m34*x18;
	  x[16] -= m5*x13 + m11*x14 + m17*x15 + m23*x16 + m29*x17 + m35*x18;
	  x[17] -= m6*x13 + m12*x14 + m18*x15 + m24*x16 + m30*x17 + m36*x18;

	  x[18] -= m1*x19 + m7*x20  + m13*x21 + m19*x22 + m25*x23 + m31*x24;
	  x[19] -= m2*x19 + m8*x20  + m14*x21 + m20*x22 + m26*x23 + m32*x24;
	  x[20] -= m3*x19 + m9*x20  + m15*x21 + m21*x22 + m27*x23 + m33*x24;
	  x[21] -= m4*x19 + m10*x20 + m16*x21 + m22*x22 + m28*x23 + m34*x24;
	  x[22] -= m5*x19 + m11*x20 + m17*x21 + m23*x22 + m29*x23 + m35*x24;
	  x[23] -= m6*x19 + m12*x20 + m18*x21 + m24*x22 + m30*x23 + m36*x24;

	  x[24] -= m1*x25 + m7*x26  + m13*x27 + m19*x28 + m25*x29 + m31*x30;
	  x[25] -= m2*x25 + m8*x26  + m14*x27 + m20*x28 + m26*x29 + m32*x30;
	  x[26] -= m3*x25 + m9*x26  + m15*x27 + m21*x28 + m27*x29 + m33*x30;
	  x[27] -= m4*x25 + m10*x26 + m16*x27 + m22*x28 + m28*x29 + m34*x30;
	  x[28] -= m5*x25 + m11*x26 + m17*x27 + m23*x28 + m29*x29 + m35*x30;
	  x[29] -= m6*x25 + m12*x26 + m18*x27 + m24*x28 + m30*x29 + m36*x30;

	  x[30] -= m1*x31 + m7*x32  + m13*x33 + m19*x34 + m25*x35 + m31*x36;
	  x[31] -= m2*x31 + m8*x32  + m14*x33 + m20*x34 + m26*x35 + m32*x36;
	  x[32] -= m3*x31 + m9*x32  + m15*x33 + m21*x34 + m27*x35 + m33*x36;
	  x[33] -= m4*x31 + m10*x32 + m16*x33 + m22*x34 + m28*x35 + m34*x36;
	  x[34] -= m5*x31 + m11*x32 + m17*x33 + m23*x34 + m29*x35 + m35*x36;
	  x[35] -= m6*x31 + m12*x32 + m18*x33 + m24*x34 + m30*x35 + m36*x36;

          pv   += 36;
        }
        PLogFlops(432*nz+396);
      } 
      row = *ajtmp++;
    }
    /* finished row so stick it into b->a */
    pv = ba + 36*bi[i];
    pj = bj + bi[i];
    nz = bi[i+1] - bi[i];
    for (j=0; j<nz; j++) {
      x      = rtmp+36*pj[j];
      pv[0]  = x[0];  pv[1]  = x[1];  pv[2]  = x[2];  pv[3]  = x[3];
      pv[4]  = x[4];  pv[5]  = x[5];  pv[6]  = x[6];  pv[7]  = x[7]; 
      pv[8]  = x[8];  pv[9]  = x[9];  pv[10] = x[10]; pv[11] = x[11]; 
      pv[12] = x[12]; pv[13] = x[13]; pv[14] = x[14]; pv[15] = x[15]; 
      pv[16] = x[16]; pv[17] = x[17]; pv[18] = x[18]; pv[19] = x[19]; 
      pv[20] = x[20]; pv[21] = x[21]; pv[22] = x[22]; pv[23] = x[23]; 
      pv[24] = x[24]; pv[25] = x[25]; pv[26] = x[26]; pv[27] = x[27]; 
      pv[28] = x[28]; pv[29] = x[29]; pv[30] = x[30]; pv[31] = x[31]; 
      pv[32] = x[32]; pv[33] = x[33]; pv[34] = x[34]; pv[35] = x[35]; 
      pv   += 36;
    }
    /* invert diagonal block */
    w = ba + 36*diag_offset[i];
    ierr = Kernel_A_gets_inverse_A_6(w);CHKERRQ(ierr);
  }

  ierr = PetscFree(rtmp);CHKERRQ(ierr);
  C->factor    = FACTOR_LU;
  C->assembled = PETSC_TRUE;
  PLogFlops(1.3333*216*b->mbs); /* from inverting diagonal blocks */
  PetscFunctionReturn(0);
}

/*
      Version for when blocks are 5 by 5
*/
#undef __FUNC__  
#define __FUNC__ "MatCholeskyFactorNumeric_SeqSBAIJ_5"
int MatCholeskyFactorNumeric_SeqSBAIJ_5(Mat A,Mat *B)
{
  Mat         C = *B;
  Mat_SeqBAIJ *a = (Mat_SeqBAIJ*)A->data,*b = (Mat_SeqBAIJ *)C->data;
  IS          isrow = b->row,isicol = b->icol;
  int         *r,*ic,ierr,i,j,n = a->mbs,*bi = b->i,*bj = b->j;
  int         *ajtmpold,*ajtmp,nz,row;
  int         *diag_offset = b->diag,idx,*ai=a->i,*aj=a->j,*pj;
  MatScalar   *pv,*v,*rtmp,*pc,*w,*x;
  MatScalar   p1,p2,p3,p4,m1,m2,m3,m4,m5,m6,m7,m8,m9,x1,x2,x3,x4;
  MatScalar   p5,p6,p7,p8,p9,x5,x6,x7,x8,x9,x10,x11,x12,x13,x14,x15,x16;
  MatScalar   x17,x18,x19,x20,x21,x22,x23,x24,x25,p10,p11,p12,p13,p14;
  MatScalar   p15,p16,p17,p18,p19,p20,p21,p22,p23,p24,p25,m10,m11,m12;
  MatScalar   m13,m14,m15,m16,m17,m18,m19,m20,m21,m22,m23,m24,m25;
  MatScalar   *ba = b->a,*aa = a->a;

  PetscFunctionBegin;
  ierr  = ISGetIndices(isrow,&r);CHKERRQ(ierr);
  ierr  = ISGetIndices(isicol,&ic);CHKERRQ(ierr);
  rtmp  = (MatScalar*)PetscMalloc(25*(n+1)*sizeof(MatScalar));CHKPTRQ(rtmp);

  for (i=0; i<n; i++) {
    nz    = bi[i+1] - bi[i];
    ajtmp = bj + bi[i];
    for  (j=0; j<nz; j++) {
      x = rtmp+25*ajtmp[j]; 
      x[0] = x[1] = x[2] = x[3] = x[4] = x[5] = x[6] = x[7] = x[8] = x[9] = 0.0;
      x[10] = x[11] = x[12] = x[13] = x[14] = x[15] = x[16] = x[17] = 0.0;
      x[18] = x[19] = x[20] = x[21] = x[22] = x[23] = x[24] = 0.0;
    }
    /* load in initial (unfactored row) */
    idx      = r[i];
    nz       = ai[idx+1] - ai[idx];
    ajtmpold = aj + ai[idx];
    v        = aa + 25*ai[idx];
    for (j=0; j<nz; j++) {
      x    = rtmp+25*ic[ajtmpold[j]];
      x[0] = v[0]; x[1] = v[1]; x[2] = v[2]; x[3] = v[3];
      x[4] = v[4]; x[5] = v[5]; x[6] = v[6]; x[7] = v[7]; x[8] = v[8];
      x[9] = v[9]; x[10] = v[10]; x[11] = v[11]; x[12] = v[12]; x[13] = v[13];
      x[14] = v[14]; x[15] = v[15]; x[16] = v[16]; x[17] = v[17]; 
      x[18] = v[18]; x[19] = v[19]; x[20] = v[20]; x[21] = v[21];
      x[22] = v[22]; x[23] = v[23]; x[24] = v[24];
      v    += 25;
    }
    row = *ajtmp++;
    while (row < i) {
      pc = rtmp + 25*row;
      p1 = pc[0]; p2 = pc[1]; p3 = pc[2]; p4 = pc[3];
      p5 = pc[4]; p6 = pc[5]; p7 = pc[6]; p8 = pc[7]; p9 = pc[8];
      p10 = pc[9]; p11 = pc[10]; p12 = pc[11]; p13 = pc[12]; p14 = pc[13];
      p15 = pc[14]; p16 = pc[15]; p17 = pc[16]; p18 = pc[17]; p19 = pc[18];
      p20 = pc[19]; p21 = pc[20]; p22 = pc[21]; p23 = pc[22]; p24 = pc[23];
      p25 = pc[24];
      if (p1 != 0.0 || p2 != 0.0 || p3 != 0.0 || p4 != 0.0 || p5 != 0.0 ||
          p6 != 0.0 || p7 != 0.0 || p8 != 0.0 || p9 != 0.0 || p10 != 0.0 ||
          p11 != 0.0 || p12 != 0.0 || p13 != 0.0 || p14 != 0.0 || p15 != 0.0
          || p16 != 0.0 || p17 != 0.0 || p18 != 0.0 || p19 != 0.0 ||
          p20 != 0.0 || p21 != 0.0 || p22 != 0.0 || p23 != 0.0 || 
          p24 != 0.0 || p25 != 0.0) { 
        pv = ba + 25*diag_offset[row];
        pj = bj + diag_offset[row] + 1;
        x1 = pv[0]; x2 = pv[1]; x3 = pv[2]; x4 = pv[3];
        x5 = pv[4]; x6 = pv[5]; x7 = pv[6]; x8 = pv[7]; x9 = pv[8];
        x10 = pv[9]; x11 = pv[10]; x12 = pv[11]; x13 = pv[12]; x14 = pv[13];
        x15 = pv[14]; x16 = pv[15]; x17 = pv[16]; x18 = pv[17]; 
        x19 = pv[18]; x20 = pv[19]; x21 = pv[20]; x22 = pv[21];
        x23 = pv[22]; x24 = pv[23]; x25 = pv[24];
        pc[0] = m1 = p1*x1 + p6*x2  + p11*x3 + p16*x4 + p21*x5;
        pc[1] = m2 = p2*x1 + p7*x2  + p12*x3 + p17*x4 + p22*x5;
        pc[2] = m3 = p3*x1 + p8*x2  + p13*x3 + p18*x4 + p23*x5;
        pc[3] = m4 = p4*x1 + p9*x2  + p14*x3 + p19*x4 + p24*x5;
        pc[4] = m5 = p5*x1 + p10*x2 + p15*x3 + p20*x4 + p25*x5;

        pc[5] = m6 = p1*x6 + p6*x7  + p11*x8 + p16*x9 + p21*x10;
        pc[6] = m7 = p2*x6 + p7*x7  + p12*x8 + p17*x9 + p22*x10;
        pc[7] = m8 = p3*x6 + p8*x7  + p13*x8 + p18*x9 + p23*x10;
        pc[8] = m9 = p4*x6 + p9*x7  + p14*x8 + p19*x9 + p24*x10;
        pc[9] = m10 = p5*x6 + p10*x7 + p15*x8 + p20*x9 + p25*x10;

        pc[10] = m11 = p1*x11 + p6*x12  + p11*x13 + p16*x14 + p21*x15;
        pc[11] = m12 = p2*x11 + p7*x12  + p12*x13 + p17*x14 + p22*x15;
        pc[12] = m13 = p3*x11 + p8*x12  + p13*x13 + p18*x14 + p23*x15;
        pc[13] = m14 = p4*x11 + p9*x12  + p14*x13 + p19*x14 + p24*x15;
        pc[14] = m15 = p5*x11 + p10*x12 + p15*x13 + p20*x14 + p25*x15;

        pc[15] = m16 = p1*x16 + p6*x17  + p11*x18 + p16*x19 + p21*x20;
        pc[16] = m17 = p2*x16 + p7*x17  + p12*x18 + p17*x19 + p22*x20;
        pc[17] = m18 = p3*x16 + p8*x17  + p13*x18 + p18*x19 + p23*x20;
        pc[18] = m19 = p4*x16 + p9*x17  + p14*x18 + p19*x19 + p24*x20;
        pc[19] = m20 = p5*x16 + p10*x17 + p15*x18 + p20*x19 + p25*x20;

        pc[20] = m21 = p1*x21 + p6*x22  + p11*x23 + p16*x24 + p21*x25;
        pc[21] = m22 = p2*x21 + p7*x22  + p12*x23 + p17*x24 + p22*x25;
        pc[22] = m23 = p3*x21 + p8*x22  + p13*x23 + p18*x24 + p23*x25;
        pc[23] = m24 = p4*x21 + p9*x22  + p14*x23 + p19*x24 + p24*x25;
        pc[24] = m25 = p5*x21 + p10*x22 + p15*x23 + p20*x24 + p25*x25;

        nz = bi[row+1] - diag_offset[row] - 1;
        pv += 25;
        for (j=0; j<nz; j++) {
          x1   = pv[0];  x2 = pv[1];   x3  = pv[2];  x4  = pv[3];
          x5   = pv[4];  x6 = pv[5];   x7  = pv[6];  x8  = pv[7]; x9 = pv[8];
          x10  = pv[9];  x11 = pv[10]; x12 = pv[11]; x13 = pv[12];
          x14  = pv[13]; x15 = pv[14]; x16 = pv[15]; x17 = pv[16]; 
          x18  = pv[17]; x19 = pv[18]; x20 = pv[19]; x21 = pv[20];
          x22  = pv[21]; x23 = pv[22]; x24 = pv[23]; x25 = pv[24];
          x    = rtmp + 25*pj[j];
          x[0] -= m1*x1 + m6*x2  + m11*x3 + m16*x4 + m21*x5;
          x[1] -= m2*x1 + m7*x2  + m12*x3 + m17*x4 + m22*x5;
          x[2] -= m3*x1 + m8*x2  + m13*x3 + m18*x4 + m23*x5;
          x[3] -= m4*x1 + m9*x2  + m14*x3 + m19*x4 + m24*x5;
          x[4] -= m5*x1 + m10*x2 + m15*x3 + m20*x4 + m25*x5;

          x[5] -= m1*x6 + m6*x7  + m11*x8 + m16*x9 + m21*x10;
          x[6] -= m2*x6 + m7*x7  + m12*x8 + m17*x9 + m22*x10;
          x[7] -= m3*x6 + m8*x7  + m13*x8 + m18*x9 + m23*x10;
          x[8] -= m4*x6 + m9*x7  + m14*x8 + m19*x9 + m24*x10;
          x[9] -= m5*x6 + m10*x7 + m15*x8 + m20*x9 + m25*x10;

          x[10] -= m1*x11 + m6*x12  + m11*x13 + m16*x14 + m21*x15;
          x[11] -= m2*x11 + m7*x12  + m12*x13 + m17*x14 + m22*x15;
          x[12] -= m3*x11 + m8*x12  + m13*x13 + m18*x14 + m23*x15;
          x[13] -= m4*x11 + m9*x12  + m14*x13 + m19*x14 + m24*x15;
          x[14] -= m5*x11 + m10*x12 + m15*x13 + m20*x14 + m25*x15;

          x[15] -= m1*x16 + m6*x17  + m11*x18 + m16*x19 + m21*x20;
          x[16] -= m2*x16 + m7*x17  + m12*x18 + m17*x19 + m22*x20;
          x[17] -= m3*x16 + m8*x17  + m13*x18 + m18*x19 + m23*x20;
          x[18] -= m4*x16 + m9*x17  + m14*x18 + m19*x19 + m24*x20;
          x[19] -= m5*x16 + m10*x17 + m15*x18 + m20*x19 + m25*x20;

          x[20] -= m1*x21 + m6*x22  + m11*x23 + m16*x24 + m21*x25;
          x[21] -= m2*x21 + m7*x22  + m12*x23 + m17*x24 + m22*x25;
          x[22] -= m3*x21 + m8*x22  + m13*x23 + m18*x24 + m23*x25;
          x[23] -= m4*x21 + m9*x22  + m14*x23 + m19*x24 + m24*x25;
          x[24] -= m5*x21 + m10*x22 + m15*x23 + m20*x24 + m25*x25;

          pv   += 25;
        }
        PLogFlops(250*nz+225);
      } 
      row = *ajtmp++;
    }
    /* finished row so stick it into b->a */
    pv = ba + 25*bi[i];
    pj = bj + bi[i];
    nz = bi[i+1] - bi[i];
    for (j=0; j<nz; j++) {
      x     = rtmp+25*pj[j];
      pv[0] = x[0]; pv[1] = x[1]; pv[2] = x[2]; pv[3] = x[3];
      pv[4] = x[4]; pv[5] = x[5]; pv[6] = x[6]; pv[7] = x[7]; pv[8] = x[8];
      pv[9] = x[9]; pv[10] = x[10]; pv[11] = x[11]; pv[12] = x[12];
      pv[13] = x[13]; pv[14] = x[14]; pv[15] = x[15]; pv[16] = x[16];
      pv[17] = x[17]; pv[18] = x[18]; pv[19] = x[19]; pv[20] = x[20];
      pv[21] = x[21]; pv[22] = x[22]; pv[23] = x[23]; pv[24] = x[24];
      pv   += 25;
    }
    /* invert diagonal block */
    w = ba + 25*diag_offset[i];
    ierr = Kernel_A_gets_inverse_A_5(w);CHKERRQ(ierr);
  }

  ierr = PetscFree(rtmp);CHKERRQ(ierr);
  ierr = ISRestoreIndices(isicol,&ic);CHKERRQ(ierr);
  ierr = ISRestoreIndices(isrow,&r);CHKERRQ(ierr);
  C->factor = FACTOR_LU;
  C->assembled = PETSC_TRUE;
  PLogFlops(1.3333*125*b->mbs); /* from inverting diagonal blocks */
  PetscFunctionReturn(0);
}
/*
      Version for when blocks are 5 by 5 Using natural ordering
*/
#undef __FUNC__  
#define __FUNC__ "MatCholeskyFactorNumeric_SeqSBAIJ_5_NaturalOrdering"
int MatCholeskyFactorNumeric_SeqSBAIJ_5_NaturalOrdering(Mat A,Mat *B)
{
  Mat         C = *B;
  Mat_SeqBAIJ *a = (Mat_SeqBAIJ*)A->data,*b = (Mat_SeqBAIJ *)C->data;
  int         ierr,i,j,n = a->mbs,*bi = b->i,*bj = b->j;
  int         *ajtmpold,*ajtmp,nz,row;
  int         *diag_offset = b->diag,*ai=a->i,*aj=a->j,*pj;
  MatScalar   *pv,*v,*rtmp,*pc,*w,*x;
  MatScalar   x1,x2,x3,x4,x5,x6,x7,x8,x9,x10,x11,x12,x13,x14,x15;
  MatScalar   x16,x17,x18,x19,x20,x21,x22,x23,x24,x25;
  MatScalar   p1,p2,p3,p4,p5,p6,p7,p8,p9,p10,p11,p12,p13,p14,p15;
  MatScalar   p16,p17,p18,p19,p20,p21,p22,p23,p24,p25;
  MatScalar   m1,m2,m3,m4,m5,m6,m7,m8,m9,m10,m11,m12,m13,m14,m15;
  MatScalar   m16,m17,m18,m19,m20,m21,m22,m23,m24,m25;
  MatScalar   *ba = b->a,*aa = a->a;

  PetscFunctionBegin;
  rtmp  = (MatScalar*)PetscMalloc(25*(n+1)*sizeof(MatScalar));CHKPTRQ(rtmp);
  for (i=0; i<n; i++) {
    nz    = bi[i+1] - bi[i];
    ajtmp = bj + bi[i];
    for  (j=0; j<nz; j++) {
      x = rtmp+25*ajtmp[j]; 
      x[0]  = x[1]  = x[2]  = x[3]  = x[4]  = x[5]  = x[6] = x[7] = x[8] = x[9] = 0.0;
      x[10] = x[11] = x[12] = x[13] = x[14] = x[15] = 0.0;
      x[16] = x[17] = x[18] = x[19] = x[20] = x[21] = x[22] = x[23] = x[24] = 0.0;
    }
    /* load in initial (unfactored row) */
    nz       = ai[i+1] - ai[i];
    ajtmpold = aj + ai[i];
    v        = aa + 25*ai[i];
    for (j=0; j<nz; j++) {
      x    = rtmp+25*ajtmpold[j];
      x[0]  = v[0];  x[1]  = v[1];  x[2]  = v[2];  x[3]  = v[3];
      x[4]  = v[4];  x[5]  = v[5];  x[6]  = v[6];  x[7]  = v[7];  x[8]  = v[8];
      x[9]  = v[9];  x[10] = v[10]; x[11] = v[11]; x[12] = v[12]; x[13] = v[13];
      x[14] = v[14]; x[15] = v[15]; x[16] = v[16]; x[17] = v[17]; x[18] = v[18];
      x[19] = v[19]; x[20] = v[20]; x[21] = v[21]; x[22] = v[22]; x[23] = v[23];
      x[24] = v[24];
      v    += 25;
    }
    row = *ajtmp++;
    while (row < i) {
      pc  = rtmp + 25*row;
      p1  = pc[0];  p2  = pc[1];  p3  = pc[2];  p4  = pc[3];
      p5  = pc[4];  p6  = pc[5];  p7  = pc[6];  p8  = pc[7];  p9  = pc[8];
      p10 = pc[9];  p11 = pc[10]; p12 = pc[11]; p13 = pc[12]; p14 = pc[13];
      p15 = pc[14]; p16 = pc[15]; p17 = pc[16]; p18 = pc[17]; 
      p19 = pc[18]; p20 = pc[19]; p21 = pc[20]; p22 = pc[21]; p23 = pc[22]; 
      p24 = pc[23]; p25 = pc[24]; 
      if (p1 != 0.0 || p2 != 0.0 || p3 != 0.0 || p4 != 0.0 || p5 != 0.0 ||
          p6 != 0.0 || p7 != 0.0 || p8 != 0.0 || p9 != 0.0 || p10 != 0.0 ||
          p11 != 0.0 || p12 != 0.0 || p13 != 0.0 || p14 != 0.0 || p15 != 0.0
          || p16 != 0.0 || p17 != 0.0 || p18 != 0.0 || p19 != 0.0 || p20 != 0.0
          || p21 != 0.0 || p22 != 0.0 || p23 != 0.0 || p24 != 0.0 || p25 != 0.0) {
        pv = ba + 25*diag_offset[row];
        pj = bj + diag_offset[row] + 1;
        x1  = pv[0];  x2  = pv[1];  x3  = pv[2];  x4  = pv[3];
        x5  = pv[4];  x6  = pv[5];  x7  = pv[6];  x8  = pv[7];  x9  = pv[8];
        x10 = pv[9];  x11 = pv[10]; x12 = pv[11]; x13 = pv[12]; x14 = pv[13];
        x15 = pv[14]; x16 = pv[15]; x17 = pv[16]; x18 = pv[17]; x19 = pv[18];
        x20 = pv[19]; x21 = pv[20]; x22 = pv[21]; x23 = pv[22]; x24 = pv[23];
        x25 = pv[24];
        pc[0] = m1 = p1*x1 + p6*x2  + p11*x3 + p16*x4 + p21*x5;
        pc[1] = m2 = p2*x1 + p7*x2  + p12*x3 + p17*x4 + p22*x5;
        pc[2] = m3 = p3*x1 + p8*x2  + p13*x3 + p18*x4 + p23*x5;
        pc[3] = m4 = p4*x1 + p9*x2  + p14*x3 + p19*x4 + p24*x5;
        pc[4] = m5 = p5*x1 + p10*x2 + p15*x3 + p20*x4 + p25*x5;

        pc[5]  = m6  = p1*x6 + p6*x7  + p11*x8 + p16*x9 + p21*x10;
        pc[6]  = m7  = p2*x6 + p7*x7  + p12*x8 + p17*x9 + p22*x10;
        pc[7]  = m8  = p3*x6 + p8*x7  + p13*x8 + p18*x9 + p23*x10;
        pc[8]  = m9  = p4*x6 + p9*x7  + p14*x8 + p19*x9 + p24*x10;
        pc[9]  = m10 = p5*x6 + p10*x7 + p15*x8 + p20*x9 + p25*x10;

        pc[10] = m11 = p1*x11 + p6*x12  + p11*x13 + p16*x14 + p21*x15;
        pc[11] = m12 = p2*x11 + p7*x12  + p12*x13 + p17*x14 + p22*x15;
        pc[12] = m13 = p3*x11 + p8*x12  + p13*x13 + p18*x14 + p23*x15;
        pc[13] = m14 = p4*x11 + p9*x12  + p14*x13 + p19*x14 + p24*x15;
        pc[14] = m15 = p5*x11 + p10*x12 + p15*x13 + p20*x14 + p25*x15;

        pc[15] = m16 = p1*x16 + p6*x17  + p11*x18 + p16*x19 + p21*x20;
        pc[16] = m17 = p2*x16 + p7*x17  + p12*x18 + p17*x19 + p22*x20;
        pc[17] = m18 = p3*x16 + p8*x17  + p13*x18 + p18*x19 + p23*x20;
        pc[18] = m19 = p4*x16 + p9*x17  + p14*x18 + p19*x19 + p24*x20;
        pc[19] = m20 = p5*x16 + p10*x17 + p15*x18 + p20*x19 + p25*x20;

        pc[20] = m21 = p1*x21 + p6*x22  + p11*x23 + p16*x24 + p21*x25;
        pc[21] = m22 = p2*x21 + p7*x22  + p12*x23 + p17*x24 + p22*x25;
        pc[22] = m23 = p3*x21 + p8*x22  + p13*x23 + p18*x24 + p23*x25;
        pc[23] = m24 = p4*x21 + p9*x22  + p14*x23 + p19*x24 + p24*x25;
        pc[24] = m25 = p5*x21 + p10*x22 + p15*x23 + p20*x24 + p25*x25;

        nz = bi[row+1] - diag_offset[row] - 1;
        pv += 25;
        for (j=0; j<nz; j++) {
          x1   = pv[0];  x2  = pv[1];   x3 = pv[2];  x4  = pv[3];
          x5   = pv[4];  x6  = pv[5];   x7 = pv[6];  x8  = pv[7]; x9 = pv[8];
          x10  = pv[9];  x11 = pv[10]; x12 = pv[11]; x13 = pv[12];
          x14  = pv[13]; x15 = pv[14]; x16 = pv[15]; x17 = pv[16]; x18 = pv[17];
          x19 = pv[18];  x20 = pv[19]; x21 = pv[20]; x22 = pv[21]; x23 = pv[22];
          x24 = pv[23];  x25 = pv[24];
          x    = rtmp + 25*pj[j];
          x[0] -= m1*x1 + m6*x2   + m11*x3  + m16*x4 + m21*x5;
          x[1] -= m2*x1 + m7*x2   + m12*x3  + m17*x4 + m22*x5;
          x[2] -= m3*x1 + m8*x2   + m13*x3  + m18*x4 + m23*x5;
          x[3] -= m4*x1 + m9*x2   + m14*x3  + m19*x4 + m24*x5;
          x[4] -= m5*x1 + m10*x2  + m15*x3  + m20*x4 + m25*x5;

          x[5] -= m1*x6 + m6*x7   + m11*x8  + m16*x9 + m21*x10;
          x[6] -= m2*x6 + m7*x7   + m12*x8  + m17*x9 + m22*x10;
          x[7] -= m3*x6 + m8*x7   + m13*x8  + m18*x9 + m23*x10;
          x[8] -= m4*x6 + m9*x7   + m14*x8  + m19*x9 + m24*x10;
          x[9] -= m5*x6 + m10*x7  + m15*x8  + m20*x9 + m25*x10;

          x[10] -= m1*x11 + m6*x12  + m11*x13 + m16*x14 + m21*x15;
          x[11] -= m2*x11 + m7*x12  + m12*x13 + m17*x14 + m22*x15;
          x[12] -= m3*x11 + m8*x12  + m13*x13 + m18*x14 + m23*x15;
          x[13] -= m4*x11 + m9*x12  + m14*x13 + m19*x14 + m24*x15;
          x[14] -= m5*x11 + m10*x12 + m15*x13 + m20*x14 + m25*x15;

          x[15] -= m1*x16 + m6*x17  + m11*x18 + m16*x19 + m21*x20;
          x[16] -= m2*x16 + m7*x17  + m12*x18 + m17*x19 + m22*x20;
          x[17] -= m3*x16 + m8*x17  + m13*x18 + m18*x19 + m23*x20;
          x[18] -= m4*x16 + m9*x17  + m14*x18 + m19*x19 + m24*x20;
          x[19] -= m5*x16 + m10*x17 + m15*x18 + m20*x19 + m25*x20;

          x[20] -= m1*x21 + m6*x22  + m11*x23 + m16*x24 + m21*x25;
          x[21] -= m2*x21 + m7*x22  + m12*x23 + m17*x24 + m22*x25;
          x[22] -= m3*x21 + m8*x22  + m13*x23 + m18*x24 + m23*x25;
          x[23] -= m4*x21 + m9*x22  + m14*x23 + m19*x24 + m24*x25;
          x[24] -= m5*x21 + m10*x22 + m15*x23 + m20*x24 + m25*x25;
          pv   += 25;
        }
        PLogFlops(250*nz+225);
      } 
      row = *ajtmp++;
    }
    /* finished row so stick it into b->a */
    pv = ba + 25*bi[i];
    pj = bj + bi[i];
    nz = bi[i+1] - bi[i];
    for (j=0; j<nz; j++) {
      x      = rtmp+25*pj[j];
      pv[0]  = x[0];  pv[1]  = x[1];  pv[2]  = x[2];  pv[3]  = x[3];
      pv[4]  = x[4];  pv[5]  = x[5];  pv[6]  = x[6];  pv[7]  = x[7]; pv[8] = x[8];
      pv[9]  = x[9];  pv[10] = x[10]; pv[11] = x[11]; pv[12] = x[12];
      pv[13] = x[13]; pv[14] = x[14]; pv[15] = x[15]; pv[16] = x[16]; pv[17] = x[17];
      pv[18] = x[18]; pv[19] = x[19]; pv[20] = x[20]; pv[21] = x[21]; pv[22] = x[22];
      pv[23] = x[23]; pv[24] = x[24];
      pv   += 25;
    }
    /* invert diagonal block */
    w = ba + 25*diag_offset[i];
    ierr = Kernel_A_gets_inverse_A_5(w);CHKERRQ(ierr);
  }

  ierr = PetscFree(rtmp);CHKERRQ(ierr);
  C->factor    = FACTOR_LU;
  C->assembled = PETSC_TRUE;
  PLogFlops(1.3333*125*b->mbs); /* from inverting diagonal blocks */
  PetscFunctionReturn(0);
}

/*
      Version for when blocks are 4 by 4
*/
#undef __FUNC__  
#define __FUNC__ "MatCholeskyFactorNumeric_SeqSBAIJ_4"
int MatCholeskyFactorNumeric_SeqSBAIJ_4(Mat A,Mat *B)
{
  Mat         C = *B;
  Mat_SeqBAIJ *a = (Mat_SeqBAIJ*)A->data,*b = (Mat_SeqBAIJ *)C->data;
  IS          isrow = b->row,isicol = b->icol;
  int         *r,*ic,ierr,i,j,n = a->mbs,*bi = b->i,*bj = b->j;
  int         *ajtmpold,*ajtmp,nz,row;
  int         *diag_offset = b->diag,idx,*ai=a->i,*aj=a->j,*pj;
  MatScalar   *pv,*v,*rtmp,*pc,*w,*x;
  MatScalar   p1,p2,p3,p4,m1,m2,m3,m4,m5,m6,m7,m8,m9,x1,x2,x3,x4;
  MatScalar   p5,p6,p7,p8,p9,x5,x6,x7,x8,x9,x10,x11,x12,x13,x14,x15,x16;
  MatScalar   p10,p11,p12,p13,p14,p15,p16,m10,m11,m12;
  MatScalar   m13,m14,m15,m16;
  MatScalar   *ba = b->a,*aa = a->a;

  PetscFunctionBegin;
  ierr  = ISGetIndices(isrow,&r);CHKERRQ(ierr);
  ierr  = ISGetIndices(isicol,&ic);CHKERRQ(ierr);
  rtmp  = (MatScalar*)PetscMalloc(16*(n+1)*sizeof(MatScalar));CHKPTRQ(rtmp);

  for (i=0; i<n; i++) {
    nz    = bi[i+1] - bi[i];
    ajtmp = bj + bi[i];
    for  (j=0; j<nz; j++) {
      x = rtmp+16*ajtmp[j]; 
      x[0]  = x[1]  = x[2]  = x[3]  = x[4]  = x[5]  = x[6] = x[7] = x[8] = x[9] = 0.0;
      x[10] = x[11] = x[12] = x[13] = x[14] = x[15] = 0.0;
    }
    /* load in initial (unfactored row) */
    idx      = r[i];
    nz       = ai[idx+1] - ai[idx];
    ajtmpold = aj + ai[idx];
    v        = aa + 16*ai[idx];
    for (j=0; j<nz; j++) {
      x    = rtmp+16*ic[ajtmpold[j]];
      x[0]  = v[0];  x[1]  = v[1];  x[2]  = v[2];  x[3]  = v[3];
      x[4]  = v[4];  x[5]  = v[5];  x[6]  = v[6];  x[7]  = v[7];  x[8]  = v[8];
      x[9]  = v[9];  x[10] = v[10]; x[11] = v[11]; x[12] = v[12]; x[13] = v[13];
      x[14] = v[14]; x[15] = v[15]; 
      v    += 16;
    }
    row = *ajtmp++;
    while (row < i) {
      pc  = rtmp + 16*row;
      p1  = pc[0];  p2  = pc[1];  p3  = pc[2];  p4  = pc[3];
      p5  = pc[4];  p6  = pc[5];  p7  = pc[6];  p8  = pc[7];  p9  = pc[8];
      p10 = pc[9];  p11 = pc[10]; p12 = pc[11]; p13 = pc[12]; p14 = pc[13];
      p15 = pc[14]; p16 = pc[15]; 
      if (p1 != 0.0 || p2 != 0.0 || p3 != 0.0 || p4 != 0.0 || p5 != 0.0 ||
          p6 != 0.0 || p7 != 0.0 || p8 != 0.0 || p9 != 0.0 || p10 != 0.0 ||
          p11 != 0.0 || p12 != 0.0 || p13 != 0.0 || p14 != 0.0 || p15 != 0.0
          || p16 != 0.0) {
        pv = ba + 16*diag_offset[row];
        pj = bj + diag_offset[row] + 1;
        x1  = pv[0];  x2  = pv[1];  x3  = pv[2];  x4  = pv[3];
        x5  = pv[4];  x6  = pv[5];  x7  = pv[6];  x8  = pv[7];  x9  = pv[8];
        x10 = pv[9];  x11 = pv[10]; x12 = pv[11]; x13 = pv[12]; x14 = pv[13];
        x15 = pv[14]; x16 = pv[15]; 
        pc[0] = m1 = p1*x1 + p5*x2  + p9*x3  + p13*x4;
        pc[1] = m2 = p2*x1 + p6*x2  + p10*x3 + p14*x4;
        pc[2] = m3 = p3*x1 + p7*x2  + p11*x3 + p15*x4;
        pc[3] = m4 = p4*x1 + p8*x2  + p12*x3 + p16*x4;

        pc[4] = m5 = p1*x5 + p5*x6  + p9*x7  + p13*x8;
        pc[5] = m6 = p2*x5 + p6*x6  + p10*x7 + p14*x8;
        pc[6] = m7 = p3*x5 + p7*x6  + p11*x7 + p15*x8;
        pc[7] = m8 = p4*x5 + p8*x6  + p12*x7 + p16*x8;

        pc[8]  = m9  = p1*x9 + p5*x10  + p9*x11  + p13*x12;
        pc[9]  = m10 = p2*x9 + p6*x10  + p10*x11 + p14*x12;
        pc[10] = m11 = p3*x9 + p7*x10  + p11*x11 + p15*x12;
        pc[11] = m12 = p4*x9 + p8*x10  + p12*x11 + p16*x12;

        pc[12] = m13 = p1*x13 + p5*x14  + p9*x15  + p13*x16;
        pc[13] = m14 = p2*x13 + p6*x14  + p10*x15 + p14*x16;
        pc[14] = m15 = p3*x13 + p7*x14  + p11*x15 + p15*x16;
        pc[15] = m16 = p4*x13 + p8*x14  + p12*x15 + p16*x16;

        nz = bi[row+1] - diag_offset[row] - 1;
        pv += 16;
        for (j=0; j<nz; j++) {
          x1   = pv[0];  x2  = pv[1];   x3 = pv[2];  x4  = pv[3];
          x5   = pv[4];  x6  = pv[5];   x7 = pv[6];  x8  = pv[7]; x9 = pv[8];
          x10  = pv[9];  x11 = pv[10]; x12 = pv[11]; x13 = pv[12];
          x14  = pv[13]; x15 = pv[14]; x16 = pv[15];
          x    = rtmp + 16*pj[j];
          x[0] -= m1*x1 + m5*x2  + m9*x3  + m13*x4;
          x[1] -= m2*x1 + m6*x2  + m10*x3 + m14*x4;
          x[2] -= m3*x1 + m7*x2  + m11*x3 + m15*x4;
          x[3] -= m4*x1 + m8*x2  + m12*x3 + m16*x4;

          x[4] -= m1*x5 + m5*x6  + m9*x7  + m13*x8;
          x[5] -= m2*x5 + m6*x6  + m10*x7 + m14*x8;
          x[6] -= m3*x5 + m7*x6  + m11*x7 + m15*x8;
          x[7] -= m4*x5 + m8*x6  + m12*x7 + m16*x8;

          x[8]  -= m1*x9 + m5*x10 + m9*x11  + m13*x12;
          x[9]  -= m2*x9 + m6*x10 + m10*x11 + m14*x12;
          x[10] -= m3*x9 + m7*x10 + m11*x11 + m15*x12;
          x[11] -= m4*x9 + m8*x10 + m12*x11 + m16*x12;

          x[12] -= m1*x13 + m5*x14  + m9*x15  + m13*x16;
          x[13] -= m2*x13 + m6*x14  + m10*x15 + m14*x16;
          x[14] -= m3*x13 + m7*x14  + m11*x15 + m15*x16;
          x[15] -= m4*x13 + m8*x14  + m12*x15 + m16*x16;

          pv   += 16;
        }
        PLogFlops(128*nz+112);
      }
      row = *ajtmp++;
    }
    /* finished row so stick it into b->a */
    pv = ba + 16*bi[i];
    pj = bj + bi[i];
    nz = bi[i+1] - bi[i];
    for (j=0; j<nz; j++) {
      x      = rtmp+16*pj[j];
      pv[0]  = x[0];  pv[1]  = x[1];  pv[2]  = x[2];  pv[3]  = x[3];
      pv[4]  = x[4];  pv[5]  = x[5];  pv[6]  = x[6];  pv[7]  = x[7]; pv[8] = x[8];
      pv[9]  = x[9];  pv[10] = x[10]; pv[11] = x[11]; pv[12] = x[12];
      pv[13] = x[13]; pv[14] = x[14]; pv[15] = x[15];
      pv   += 16;
    }
    /* invert diagonal block */
    w = ba + 16*diag_offset[i];
    ierr = Kernel_A_gets_inverse_A_4(w);CHKERRQ(ierr);
  }

  ierr = PetscFree(rtmp);CHKERRQ(ierr);
  ierr = ISRestoreIndices(isicol,&ic);CHKERRQ(ierr);
  ierr = ISRestoreIndices(isrow,&r);CHKERRQ(ierr);
  C->factor = FACTOR_LU;
  C->assembled = PETSC_TRUE;
  PLogFlops(1.3333*64*b->mbs); /* from inverting diagonal blocks */
  PetscFunctionReturn(0);
}
/*
      Version for when blocks are 4 by 4 Using natural ordering
*/
#undef __FUNC__  
#define __FUNC__ "MatCholeskyFactorNumeric_SeqSBAIJ_4_NaturalOrdering"
int MatCholeskyFactorNumeric_SeqSBAIJ_4_NaturalOrdering(Mat A,Mat *B)
{
  Mat         C = *B;
  Mat_SeqBAIJ *a = (Mat_SeqBAIJ*)A->data,*b = (Mat_SeqBAIJ *)C->data;
  int         ierr,i,j,n = a->mbs,*bi = b->i,*bj = b->j;
  int         *ajtmpold,*ajtmp,nz,row;
  int         *diag_offset = b->diag,*ai=a->i,*aj=a->j,*pj;
  MatScalar   *pv,*v,*rtmp,*pc,*w,*x;
  MatScalar   p1,p2,p3,p4,m1,m2,m3,m4,m5,m6,m7,m8,m9,x1,x2,x3,x4;
  MatScalar   p5,p6,p7,p8,p9,x5,x6,x7,x8,x9,x10,x11,x12,x13,x14,x15,x16;
  MatScalar   p10,p11,p12,p13,p14,p15,p16,m10,m11,m12;
  MatScalar   m13,m14,m15,m16;
  MatScalar   *ba = b->a,*aa = a->a;

  PetscFunctionBegin;
  rtmp  = (MatScalar*)PetscMalloc(16*(n+1)*sizeof(MatScalar));CHKPTRQ(rtmp);

  for (i=0; i<n; i++) {
    nz    = bi[i+1] - bi[i];
    ajtmp = bj + bi[i];
    for  (j=0; j<nz; j++) {
      x = rtmp+16*ajtmp[j]; 
      x[0]  = x[1]  = x[2]  = x[3]  = x[4]  = x[5]  = x[6] = x[7] = x[8] = x[9] = 0.0;
      x[10] = x[11] = x[12] = x[13] = x[14] = x[15] = 0.0;
    }
    /* load in initial (unfactored row) */
    nz       = ai[i+1] - ai[i];
    ajtmpold = aj + ai[i];
    v        = aa + 16*ai[i];
    for (j=0; j<nz; j++) {
      x    = rtmp+16*ajtmpold[j];
      x[0]  = v[0];  x[1]  = v[1];  x[2]  = v[2];  x[3]  = v[3];
      x[4]  = v[4];  x[5]  = v[5];  x[6]  = v[6];  x[7]  = v[7];  x[8]  = v[8];
      x[9]  = v[9];  x[10] = v[10]; x[11] = v[11]; x[12] = v[12]; x[13] = v[13];
      x[14] = v[14]; x[15] = v[15]; 
      v    += 16;
    }
    row = *ajtmp++;
    while (row < i) {
      pc  = rtmp + 16*row;
      p1  = pc[0];  p2  = pc[1];  p3  = pc[2];  p4  = pc[3];
      p5  = pc[4];  p6  = pc[5];  p7  = pc[6];  p8  = pc[7];  p9  = pc[8];
      p10 = pc[9];  p11 = pc[10]; p12 = pc[11]; p13 = pc[12]; p14 = pc[13];
      p15 = pc[14]; p16 = pc[15]; 
      if (p1 != 0.0 || p2 != 0.0 || p3 != 0.0 || p4 != 0.0 || p5 != 0.0 ||
          p6 != 0.0 || p7 != 0.0 || p8 != 0.0 || p9 != 0.0 || p10 != 0.0 ||
          p11 != 0.0 || p12 != 0.0 || p13 != 0.0 || p14 != 0.0 || p15 != 0.0
          || p16 != 0.0) {
        pv = ba + 16*diag_offset[row];
        pj = bj + diag_offset[row] + 1;
        x1  = pv[0];  x2  = pv[1];  x3  = pv[2];  x4  = pv[3];
        x5  = pv[4];  x6  = pv[5];  x7  = pv[6];  x8  = pv[7];  x9  = pv[8];
        x10 = pv[9];  x11 = pv[10]; x12 = pv[11]; x13 = pv[12]; x14 = pv[13];
        x15 = pv[14]; x16 = pv[15]; 
        pc[0] = m1 = p1*x1 + p5*x2  + p9*x3  + p13*x4;
        pc[1] = m2 = p2*x1 + p6*x2  + p10*x3 + p14*x4;
        pc[2] = m3 = p3*x1 + p7*x2  + p11*x3 + p15*x4;
        pc[3] = m4 = p4*x1 + p8*x2  + p12*x3 + p16*x4;

        pc[4] = m5 = p1*x5 + p5*x6  + p9*x7  + p13*x8;
        pc[5] = m6 = p2*x5 + p6*x6  + p10*x7 + p14*x8;
        pc[6] = m7 = p3*x5 + p7*x6  + p11*x7 + p15*x8;
        pc[7] = m8 = p4*x5 + p8*x6  + p12*x7 + p16*x8;

        pc[8]  = m9  = p1*x9 + p5*x10  + p9*x11  + p13*x12;
        pc[9]  = m10 = p2*x9 + p6*x10  + p10*x11 + p14*x12;
        pc[10] = m11 = p3*x9 + p7*x10  + p11*x11 + p15*x12;
        pc[11] = m12 = p4*x9 + p8*x10  + p12*x11 + p16*x12;

        pc[12] = m13 = p1*x13 + p5*x14  + p9*x15  + p13*x16;
        pc[13] = m14 = p2*x13 + p6*x14  + p10*x15 + p14*x16;
        pc[14] = m15 = p3*x13 + p7*x14  + p11*x15 + p15*x16;
        pc[15] = m16 = p4*x13 + p8*x14  + p12*x15 + p16*x16;

        nz = bi[row+1] - diag_offset[row] - 1;
        pv += 16;
        for (j=0; j<nz; j++) {
          x1   = pv[0];  x2  = pv[1];   x3 = pv[2];  x4  = pv[3];
          x5   = pv[4];  x6  = pv[5];   x7 = pv[6];  x8  = pv[7]; x9 = pv[8];
          x10  = pv[9];  x11 = pv[10]; x12 = pv[11]; x13 = pv[12];
          x14  = pv[13]; x15 = pv[14]; x16 = pv[15];
          x    = rtmp + 16*pj[j];
          x[0] -= m1*x1 + m5*x2  + m9*x3  + m13*x4;
          x[1] -= m2*x1 + m6*x2  + m10*x3 + m14*x4;
          x[2] -= m3*x1 + m7*x2  + m11*x3 + m15*x4;
          x[3] -= m4*x1 + m8*x2  + m12*x3 + m16*x4;

          x[4] -= m1*x5 + m5*x6  + m9*x7  + m13*x8;
          x[5] -= m2*x5 + m6*x6  + m10*x7 + m14*x8;
          x[6] -= m3*x5 + m7*x6  + m11*x7 + m15*x8;
          x[7] -= m4*x5 + m8*x6  + m12*x7 + m16*x8;

          x[8]  -= m1*x9 + m5*x10 + m9*x11  + m13*x12;
          x[9]  -= m2*x9 + m6*x10 + m10*x11 + m14*x12;
          x[10] -= m3*x9 + m7*x10 + m11*x11 + m15*x12;
          x[11] -= m4*x9 + m8*x10 + m12*x11 + m16*x12;

          x[12] -= m1*x13 + m5*x14  + m9*x15  + m13*x16;
          x[13] -= m2*x13 + m6*x14  + m10*x15 + m14*x16;
          x[14] -= m3*x13 + m7*x14  + m11*x15 + m15*x16;
          x[15] -= m4*x13 + m8*x14  + m12*x15 + m16*x16;

          pv   += 16;
        }
        PLogFlops(128*nz+112);
      } 
      row = *ajtmp++;
    }
    /* finished row so stick it into b->a */
    pv = ba + 16*bi[i];
    pj = bj + bi[i];
    nz = bi[i+1] - bi[i];
    for (j=0; j<nz; j++) {
      x      = rtmp+16*pj[j];
      pv[0]  = x[0];  pv[1]  = x[1];  pv[2]  = x[2];  pv[3]  = x[3];
      pv[4]  = x[4];  pv[5]  = x[5];  pv[6]  = x[6];  pv[7]  = x[7]; pv[8] = x[8];
      pv[9]  = x[9];  pv[10] = x[10]; pv[11] = x[11]; pv[12] = x[12];
      pv[13] = x[13]; pv[14] = x[14]; pv[15] = x[15];
      pv   += 16;
    }
    /* invert diagonal block */
    w = ba + 16*diag_offset[i];
    ierr = Kernel_A_gets_inverse_A_4(w);CHKERRQ(ierr);
  }

  ierr = PetscFree(rtmp);CHKERRQ(ierr);
  C->factor    = FACTOR_LU;
  C->assembled = PETSC_TRUE;
  PLogFlops(1.3333*64*b->mbs); /* from inverting diagonal blocks */
  PetscFunctionReturn(0);
}

/*
      Version for when blocks are 3 by 3
*/
#undef __FUNC__  
#define __FUNC__ "MatCholeskyFactorNumeric_SeqSBAIJ_3"
int MatCholeskyFactorNumeric_SeqSBAIJ_3(Mat A,Mat *B)
{
  Mat         C = *B;
  Mat_SeqBAIJ *a = (Mat_SeqBAIJ*)A->data,*b = (Mat_SeqBAIJ *)C->data;
  IS          isrow = b->row,isicol = b->icol;
  int         *r,*ic,ierr,i,j,n = a->mbs,*bi = b->i,*bj = b->j;
  int         *ajtmpold,*ajtmp,nz,row,*ai=a->i,*aj=a->j;
  int         *diag_offset = b->diag,idx,*pj;
  MatScalar   *pv,*v,*rtmp,*pc,*w,*x;
  MatScalar   p1,p2,p3,p4,m1,m2,m3,m4,m5,m6,m7,m8,m9,x1,x2,x3,x4;
  MatScalar   p5,p6,p7,p8,p9,x5,x6,x7,x8,x9;
  MatScalar   *ba = b->a,*aa = a->a;

  PetscFunctionBegin;
  ierr  = ISGetIndices(isrow,&r);CHKERRQ(ierr);
  ierr  = ISGetIndices(isicol,&ic);CHKERRQ(ierr);
  rtmp  = (MatScalar*)PetscMalloc(9*(n+1)*sizeof(MatScalar));CHKPTRQ(rtmp);

  for (i=0; i<n; i++) {
    nz    = bi[i+1] - bi[i];
    ajtmp = bj + bi[i];
    for  (j=0; j<nz; j++) {
      x = rtmp + 9*ajtmp[j]; 
      x[0] = x[1] = x[2] = x[3] = x[4] = x[5] = x[6] = x[7] = x[8] = 0.0;
    }
    /* load in initial (unfactored row) */
    idx      = r[i];
    nz       = ai[idx+1] - ai[idx];
    ajtmpold = aj + ai[idx];
    v        = aa + 9*ai[idx];
    for (j=0; j<nz; j++) {
      x    = rtmp + 9*ic[ajtmpold[j]];
      x[0] = v[0]; x[1] = v[1]; x[2] = v[2]; x[3] = v[3];
      x[4] = v[4]; x[5] = v[5]; x[6] = v[6]; x[7] = v[7]; x[8] = v[8];
      v    += 9;
    }
    row = *ajtmp++;
    while (row < i) {
      pc = rtmp + 9*row;
      p1 = pc[0]; p2 = pc[1]; p3 = pc[2]; p4 = pc[3];
      p5 = pc[4]; p6 = pc[5]; p7 = pc[6]; p8 = pc[7]; p9 = pc[8];
      if (p1 != 0.0 || p2 != 0.0 || p3 != 0.0 || p4 != 0.0 || p5 != 0.0 ||
          p6 != 0.0 || p7 != 0.0 || p8 != 0.0 || p9 != 0.0) { 
        pv = ba + 9*diag_offset[row];
        pj = bj + diag_offset[row] + 1;
        x1 = pv[0]; x2 = pv[1]; x3 = pv[2]; x4 = pv[3];
        x5 = pv[4]; x6 = pv[5]; x7 = pv[6]; x8 = pv[7]; x9 = pv[8];
        pc[0] = m1 = p1*x1 + p4*x2 + p7*x3;
        pc[1] = m2 = p2*x1 + p5*x2 + p8*x3;
        pc[2] = m3 = p3*x1 + p6*x2 + p9*x3;

        pc[3] = m4 = p1*x4 + p4*x5 + p7*x6;
        pc[4] = m5 = p2*x4 + p5*x5 + p8*x6;
        pc[5] = m6 = p3*x4 + p6*x5 + p9*x6;

        pc[6] = m7 = p1*x7 + p4*x8 + p7*x9;
        pc[7] = m8 = p2*x7 + p5*x8 + p8*x9;
        pc[8] = m9 = p3*x7 + p6*x8 + p9*x9;
        nz = bi[row+1] - diag_offset[row] - 1;
        pv += 9;
        for (j=0; j<nz; j++) {
          x1   = pv[0]; x2 = pv[1]; x3 = pv[2]; x4 = pv[3];
          x5   = pv[4]; x6 = pv[5]; x7 = pv[6]; x8 = pv[7]; x9 = pv[8];
          x    = rtmp + 9*pj[j];
          x[0] -= m1*x1 + m4*x2 + m7*x3;
          x[1] -= m2*x1 + m5*x2 + m8*x3;
          x[2] -= m3*x1 + m6*x2 + m9*x3;
 
          x[3] -= m1*x4 + m4*x5 + m7*x6;
          x[4] -= m2*x4 + m5*x5 + m8*x6;
          x[5] -= m3*x4 + m6*x5 + m9*x6;

          x[6] -= m1*x7 + m4*x8 + m7*x9;
          x[7] -= m2*x7 + m5*x8 + m8*x9;
          x[8] -= m3*x7 + m6*x8 + m9*x9;
          pv   += 9;
        }
        PLogFlops(54*nz+36);
      } 
      row = *ajtmp++;
    }
    /* finished row so stick it into b->a */
    pv = ba + 9*bi[i];
    pj = bj + bi[i];
    nz = bi[i+1] - bi[i];
    for (j=0; j<nz; j++) {
      x     = rtmp + 9*pj[j];
      pv[0] = x[0]; pv[1] = x[1]; pv[2] = x[2]; pv[3] = x[3];
      pv[4] = x[4]; pv[5] = x[5]; pv[6] = x[6]; pv[7] = x[7]; pv[8] = x[8];
      pv   += 9;
    }
    /* invert diagonal block */
    w = ba + 9*diag_offset[i];
    ierr = Kernel_A_gets_inverse_A_3(w);CHKERRQ(ierr);
  }

  ierr = PetscFree(rtmp);CHKERRQ(ierr);
  ierr = ISRestoreIndices(isicol,&ic);CHKERRQ(ierr);
  ierr = ISRestoreIndices(isrow,&r);CHKERRQ(ierr);
  C->factor = FACTOR_LU;
  C->assembled = PETSC_TRUE;
  PLogFlops(1.3333*27*b->mbs); /* from inverting diagonal blocks */
  PetscFunctionReturn(0);
}
/*
      Version for when blocks are 3 by 3 Using natural ordering
*/
#undef __FUNC__  
#define __FUNC__ "MatCholeskyFactorNumeric_SeqSBAIJ_3_NaturalOrdering"
int MatCholeskyFactorNumeric_SeqSBAIJ_3_NaturalOrdering(Mat A,Mat *B)
{
  Mat                C = *B;
  Mat_SeqBAIJ        *a = (Mat_SeqBAIJ*)A->data,*b = (Mat_SeqBAIJ *)C->data;
  int                ierr,i,j,n = a->mbs,*bi = b->i,*bj = b->j;
  int                *ajtmpold,*ajtmp,nz,row;
  int                *diag_offset = b->diag,*ai=a->i,*aj=a->j,*pj;
  MatScalar          *pv,*v,*rtmp,*pc,*w,*x;
  MatScalar          p1,p2,p3,p4,m1,m2,m3,m4,m5,m6,m7,m8,m9,x1,x2,x3,x4;
  MatScalar          p5,p6,p7,p8,p9,x5,x6,x7,x8,x9;
  MatScalar          *ba = b->a,*aa = a->a;

  PetscFunctionBegin;
  rtmp  = (MatScalar*)PetscMalloc(9*(n+1)*sizeof(MatScalar));CHKPTRQ(rtmp);

  for (i=0; i<n; i++) {
    nz    = bi[i+1] - bi[i];
    ajtmp = bj + bi[i];
    for  (j=0; j<nz; j++) {
      x = rtmp+9*ajtmp[j]; 
      x[0]  = x[1]  = x[2]  = x[3]  = x[4]  = x[5]  = x[6] = x[7] = x[8] = 0.0;
    }
    /* load in initial (unfactored row) */
    nz       = ai[i+1] - ai[i];
    ajtmpold = aj + ai[i];
    v        = aa + 9*ai[i];
    for (j=0; j<nz; j++) {
      x    = rtmp+9*ajtmpold[j];
      x[0]  = v[0];  x[1]  = v[1];  x[2]  = v[2];  x[3]  = v[3];
      x[4]  = v[4];  x[5]  = v[5];  x[6]  = v[6];  x[7]  = v[7];  x[8]  = v[8];
      v    += 9;
    }
    row = *ajtmp++;
    while (row < i) {
      pc  = rtmp + 9*row;
      p1  = pc[0];  p2  = pc[1];  p3  = pc[2];  p4  = pc[3];
      p5  = pc[4];  p6  = pc[5];  p7  = pc[6];  p8  = pc[7];  p9  = pc[8];
      if (p1 != 0.0 || p2 != 0.0 || p3 != 0.0 || p4 != 0.0 || p5 != 0.0 ||
          p6 != 0.0 || p7 != 0.0 || p8 != 0.0 || p9 != 0.0) { 
        pv = ba + 9*diag_offset[row];
        pj = bj + diag_offset[row] + 1;
        x1  = pv[0];  x2  = pv[1];  x3  = pv[2];  x4  = pv[3];
        x5  = pv[4];  x6  = pv[5];  x7  = pv[6];  x8  = pv[7];  x9  = pv[8];
        pc[0] = m1 = p1*x1 + p4*x2 + p7*x3;
        pc[1] = m2 = p2*x1 + p5*x2 + p8*x3;
        pc[2] = m3 = p3*x1 + p6*x2 + p9*x3;

        pc[3] = m4 = p1*x4 + p4*x5 + p7*x6;
        pc[4] = m5 = p2*x4 + p5*x5 + p8*x6;
        pc[5] = m6 = p3*x4 + p6*x5 + p9*x6;

        pc[6] = m7 = p1*x7 + p4*x8 + p7*x9;
        pc[7] = m8 = p2*x7 + p5*x8 + p8*x9;
        pc[8] = m9 = p3*x7 + p6*x8 + p9*x9;

        nz = bi[row+1] - diag_offset[row] - 1;
        pv += 9;
        for (j=0; j<nz; j++) {
          x1   = pv[0];  x2  = pv[1];   x3 = pv[2];  x4  = pv[3];
          x5   = pv[4];  x6  = pv[5];   x7 = pv[6];  x8  = pv[7]; x9 = pv[8];
          x    = rtmp + 9*pj[j];
          x[0] -= m1*x1 + m4*x2 + m7*x3;
          x[1] -= m2*x1 + m5*x2 + m8*x3;
          x[2] -= m3*x1 + m6*x2 + m9*x3;
 
          x[3] -= m1*x4 + m4*x5 + m7*x6;
          x[4] -= m2*x4 + m5*x5 + m8*x6;
          x[5] -= m3*x4 + m6*x5 + m9*x6;

          x[6] -= m1*x7 + m4*x8 + m7*x9;
          x[7] -= m2*x7 + m5*x8 + m8*x9;
          x[8] -= m3*x7 + m6*x8 + m9*x9;
          pv   += 9;
        }
        PLogFlops(54*nz+36);
      } 
      row = *ajtmp++;
    }
    /* finished row so stick it into b->a */
    pv = ba + 9*bi[i];
    pj = bj + bi[i];
    nz = bi[i+1] - bi[i];
    for (j=0; j<nz; j++) {
      x      = rtmp+9*pj[j];
      pv[0]  = x[0];  pv[1]  = x[1];  pv[2]  = x[2];  pv[3]  = x[3];
      pv[4]  = x[4];  pv[5]  = x[5];  pv[6]  = x[6];  pv[7]  = x[7]; pv[8] = x[8];
      pv   += 9;
    }
    /* invert diagonal block */
    w = ba + 9*diag_offset[i];
    ierr = Kernel_A_gets_inverse_A_3(w);CHKERRQ(ierr);
  }

  ierr = PetscFree(rtmp);CHKERRQ(ierr);
  C->factor    = FACTOR_LU;
  C->assembled = PETSC_TRUE;
  PLogFlops(1.3333*27*b->mbs); /* from inverting diagonal blocks */
  PetscFunctionReturn(0);
}

/*
      Version for when blocks are 2 by 2
*/
#undef __FUNC__  
#define __FUNC__ "MatCholeskyFactorNumeric_SeqSBAIJ_2"
int MatCholeskyFactorNumeric_SeqSBAIJ_2(Mat A,Mat *B)
{
  Mat                C = *B;
  Mat_SeqBAIJ        *a = (Mat_SeqBAIJ*)A->data,*b = (Mat_SeqBAIJ *)C->data;
  IS                 isrow = b->row,isicol = b->icol;
  int                *r,*ic,ierr,i,j,n = a->mbs,*bi = b->i,*bj = b->j;
  int                *ajtmpold,*ajtmp,nz,row;
  int                *diag_offset=b->diag,idx,*ai=a->i,*aj=a->j,*pj;
  MatScalar          *pv,*v,*rtmp,m1,m2,m3,m4,*pc,*w,*x,x1,x2,x3,x4;
  MatScalar          p1,p2,p3,p4;
  MatScalar          *ba = b->a,*aa = a->a;

  PetscFunctionBegin;
  ierr  = ISGetIndices(isrow,&r);CHKERRQ(ierr);
  ierr  = ISGetIndices(isicol,&ic);CHKERRQ(ierr);
  rtmp  = (MatScalar*)PetscMalloc(4*(n+1)*sizeof(MatScalar));CHKPTRQ(rtmp);

  for (i=0; i<n; i++) {
    nz    = bi[i+1] - bi[i];
    ajtmp = bj + bi[i];
    for  (j=0; j<nz; j++) {
      x = rtmp+4*ajtmp[j]; x[0] = x[1] = x[2] = x[3] = 0.0;
    }
    /* load in initial (unfactored row) */
    idx      = r[i];
    nz       = ai[idx+1] - ai[idx];
    ajtmpold = aj + ai[idx];
    v        = aa + 4*ai[idx];
    for (j=0; j<nz; j++) {
      x    = rtmp+4*ic[ajtmpold[j]];
      x[0] = v[0]; x[1] = v[1]; x[2] = v[2]; x[3] = v[3];
      v    += 4;
    }
    row = *ajtmp++;
    while (row < i) {
      pc = rtmp + 4*row;
      p1 = pc[0]; p2 = pc[1]; p3 = pc[2]; p4 = pc[3];
      if (p1 != 0.0 || p2 != 0.0 || p3 != 0.0 || p4 != 0.0) { 
        pv = ba + 4*diag_offset[row];
        pj = bj + diag_offset[row] + 1;
        x1 = pv[0]; x2 = pv[1]; x3 = pv[2]; x4 = pv[3];
        pc[0] = m1 = p1*x1 + p3*x2;
        pc[1] = m2 = p2*x1 + p4*x2;
        pc[2] = m3 = p1*x3 + p3*x4;
        pc[3] = m4 = p2*x3 + p4*x4;
        nz = bi[row+1] - diag_offset[row] - 1;
        pv += 4;
        for (j=0; j<nz; j++) {
          x1   = pv[0]; x2 = pv[1]; x3 = pv[2]; x4 = pv[3];
          x    = rtmp + 4*pj[j];
          x[0] -= m1*x1 + m3*x2;
          x[1] -= m2*x1 + m4*x2;
          x[2] -= m1*x3 + m3*x4;
          x[3] -= m2*x3 + m4*x4;
          pv   += 4;
        }
        PLogFlops(16*nz+12);
      } 
      row = *ajtmp++;
    }
    /* finished row so stick it into b->a */
    pv = ba + 4*bi[i];
    pj = bj + bi[i];
    nz = bi[i+1] - bi[i];
    for (j=0; j<nz; j++) {
      x     = rtmp+4*pj[j];
      pv[0] = x[0]; pv[1] = x[1]; pv[2] = x[2]; pv[3] = x[3];
      pv   += 4;
    }
    /* invert diagonal block */
    w = ba + 4*diag_offset[i];
    ierr = Kernel_A_gets_inverse_A_2(w);CHKERRQ(ierr);
    /*Kernel_A_gets_inverse_A(bs,w,v_pivots,v_work);*/
  }

  ierr = PetscFree(rtmp);CHKERRQ(ierr);
  ierr = ISRestoreIndices(isicol,&ic);CHKERRQ(ierr);
  ierr = ISRestoreIndices(isrow,&r);CHKERRQ(ierr);
  C->factor = FACTOR_LU;
  C->assembled = PETSC_TRUE;
  PLogFlops(1.3333*8*b->mbs); /* from inverting diagonal blocks */
  PetscFunctionReturn(0);
}
/*
      Version for when blocks are 2 by 2 Using natural ordering
*/
#undef __FUNC__  
#define __FUNC__ "MatCholeskyFactorNumeric_SeqSBAIJ_2_NaturalOrdering"
int MatCholeskyFactorNumeric_SeqSBAIJ_2_NaturalOrdering(Mat A,Mat *B)
{
  Mat                C = *B;
  Mat_SeqBAIJ        *a = (Mat_SeqBAIJ*)A->data,*b = (Mat_SeqBAIJ *)C->data;
  int                ierr,i,j,n = a->mbs,*bi = b->i,*bj = b->j;
  int                *ajtmpold,*ajtmp,nz,row;
  int                *diag_offset = b->diag,*ai=a->i,*aj=a->j,*pj;
  MatScalar          *pv,*v,*rtmp,*pc,*w,*x;
  MatScalar          p1,p2,p3,p4,m1,m2,m3,m4,x1,x2,x3,x4;
  MatScalar          *ba = b->a,*aa = a->a;

  PetscFunctionBegin;
  rtmp  = (MatScalar*)PetscMalloc(4*(n+1)*sizeof(MatScalar));CHKPTRQ(rtmp);

  for (i=0; i<n; i++) {
    nz    = bi[i+1] - bi[i];
    ajtmp = bj + bi[i];
    for  (j=0; j<nz; j++) {
      x = rtmp+4*ajtmp[j]; 
      x[0]  = x[1]  = x[2]  = x[3]  = 0.0;
    }
    /* load in initial (unfactored row) */
    nz       = ai[i+1] - ai[i];
    ajtmpold = aj + ai[i];
    v        = aa + 4*ai[i];
    for (j=0; j<nz; j++) {
      x    = rtmp+4*ajtmpold[j];
      x[0]  = v[0];  x[1]  = v[1];  x[2]  = v[2];  x[3]  = v[3];
      v    += 4;
    }
    row = *ajtmp++;
    while (row < i) {
      pc  = rtmp + 4*row;
      p1  = pc[0];  p2  = pc[1];  p3  = pc[2];  p4  = pc[3];
      if (p1 != 0.0 || p2 != 0.0 || p3 != 0.0 || p4 != 0.0) { 
        pv = ba + 4*diag_offset[row];
        pj = bj + diag_offset[row] + 1;
        x1  = pv[0];  x2  = pv[1];  x3  = pv[2];  x4  = pv[3];
        pc[0] = m1 = p1*x1 + p3*x2;
        pc[1] = m2 = p2*x1 + p4*x2;
        pc[2] = m3 = p1*x3 + p3*x4;
        pc[3] = m4 = p2*x3 + p4*x4;
        nz = bi[row+1] - diag_offset[row] - 1;
        pv += 4;
        for (j=0; j<nz; j++) {
          x1   = pv[0];  x2  = pv[1];   x3 = pv[2];  x4  = pv[3];
          x    = rtmp + 4*pj[j];
          x[0] -= m1*x1 + m3*x2;
          x[1] -= m2*x1 + m4*x2;
          x[2] -= m1*x3 + m3*x4;
          x[3] -= m2*x3 + m4*x4;
          pv   += 4;
        }
        PLogFlops(16*nz+12);
      } 
      row = *ajtmp++;
    }
    /* finished row so stick it into b->a */
    pv = ba + 4*bi[i];
    pj = bj + bi[i];
    nz = bi[i+1] - bi[i];
    for (j=0; j<nz; j++) {
      x      = rtmp+4*pj[j];
      pv[0]  = x[0];  pv[1]  = x[1];  pv[2]  = x[2];  pv[3]  = x[3];
      pv   += 4;
    }
    /* invert diagonal block */
    w = ba + 4*diag_offset[i];
    ierr = Kernel_A_gets_inverse_A_2(w);CHKERRQ(ierr);
    /*Kernel_A_gets_inverse_A(bs,w,v_pivots,v_work);*/
  }

  ierr = PetscFree(rtmp);CHKERRQ(ierr);
  C->factor    = FACTOR_LU;
  C->assembled = PETSC_TRUE;
  PLogFlops(1.3333*8*b->mbs); /* from inverting diagonal blocks */
  PetscFunctionReturn(0);
}

/*
     Version for when blocks are 1 by 1.
*/
#undef __FUNC__  
#define __FUNC__ "MatCholeskyFactorNumeric_SeqSBAIJ_1"
int MatCholeskyFactorNumeric_SeqSBAIJ_1(Mat A,Mat *B)
{
  Mat                C = *B;
  Mat_SeqSBAIJ       *a = (Mat_SeqSBAIJ*)A->data,*b = (Mat_SeqSBAIJ *)C->data;
  IS                 ip = b->row;
  int                *rip,*riip,ierr,i,j,mbs = a->mbs,*bi = b->i,*bj = b->j;
  int                *ai,*aj,*r;
  MatScalar          *rtmp;
  MatScalar          *ba = b->a,*aa,ak;
  MatScalar          dk,uikdi;
  int                k,jmin,jmax,*jl,*il,vj,nexti,juj,ili;

  PetscFunctionBegin;
  ierr  = ISGetIndices(ip,&rip);CHKERRQ(ierr);
  riip = rip;

  if (!a->permute){
    ai = a->i; aj = a->j; aa = a->a;
  } else {
    ai = a->inew; aj = a->jnew; 
    aa = (MatScalar*)PetscMalloc(ai[mbs]*sizeof(MatScalar));CHKPTRQ(aa); 
    ierr = PetscMemcpy(aa,a->a,ai[mbs]*sizeof(MatScalar));CHKERRQ(ierr);
    r   = (int*)PetscMalloc(ai[mbs]*sizeof(int));CHKPTRQ(r); 
    ierr= PetscMemcpy(r,a->a2anew,(ai[mbs])*sizeof(int));CHKERRQ(ierr);

    jmin = ai[0]; jmax = ai[mbs];
    for (j=jmin; j<jmax; j++){
      while (r[j] != j){  
        k = r[j]; r[j] = r[k]; r[k] = k;     
        ak = aa[k]; aa[k] = aa[j]; aa[j] = ak;         
      }
    }
    ierr = PetscFree(r);CHKERRA(ierr); 
  }
  
  /* INITIALIZATION */
  /* il and jl record the first nonzero element in each row of the accessing 
     window U(0:k, k:mbs-1).
     jl:    list of rows to be added to uneliminated rows 
            i>= k: jl(i) is the first row to be added to row i
            i<  k: jl(i) is the row following row i in some list of rows
            jl(i) = mbs indicates the end of a list                        
     il(i): points to the first nonzero element in columns k,...,mbs-1 of 
            row i of U */
  rtmp  = (MatScalar*)PetscMalloc(mbs*sizeof(MatScalar));CHKPTRQ(rtmp);
  il = (int*)PetscMalloc(mbs*sizeof(int));CHKPTRQ(il);
  jl = (int*)PetscMalloc(mbs*sizeof(int));CHKPTRQ(jl);
  for (i=0; i<mbs; i++) {
    rtmp[i] = 0.0; jl[i] = mbs; il[0] = 0;
  }
  
  /* FOR EACH ROW K */
  for (k = 0; k<mbs; k++){

    /* INITIALIZE K-TH ROW WITH ELEMENTS NONZERO IN ROW P(K) OF A */
    jmin = ai[rip[k]]; jmax = ai[rip[k]+1];
    if (jmin < jmax) {
      for (j = jmin; j < jmax; j++){
        vj = riip[aj[j]];
        if (k <= vj) rtmp[vj] = aa[j];
      } 
    } 

    /* MODIFY K-TH ROW BY ADDING IN THOSE ROWS I WITH U(I,K) NE 0
       FOR EACH ROW I TO BE ADDED IN */
    dk = rtmp[k];
    i = jl[k]; /* first row to be added to k_th row  */  
    /* printf(" k=%d, pivot row = %d\n",k,i); */

    while (i < mbs){
      nexti = jl[i]; /* next row to be added to k_th row */
      /* printf("      pivot row = %d\n", nexti); */

      /* COMPUTE MULTIPLIER AND UPDATE DIAGONAL ELEMENT */
      ili = il[i];  /* index of first nonzero element in U(i,k:bms-1) */
      uikdi = - ba[ili]*ba[i];  
      dk += uikdi*ba[ili];
      ba[ili] = uikdi; /* update U(i,k) */

      /* ADD MULTIPLE OF ROW I TO K-TH ROW ... */
      jmin = ili + 1; jmax = bi[i+1];
      if (jmin < jmax){
        for (j=jmin; j<jmax; j++) rtmp[bj[j]] += uikdi*ba[j];         
        /* ... AND ADD I TO ROW LIST FOR NEXT NONZERO ENTRY */
         il[i] = jmin;             /* update il(i) in column k+1, ... mbs-1 */
         j     = bj[jmin];
         jl[i] = jl[j]; jl[j] = i; /* update jl */
      }      
      i = nexti;
      /* printf("                  pivot row i=%d\n",i);  */        
    }

    /* CHECK FOR ZERO PIVOT AND SAVE DIAGONAL ELEMENT */
    if (dk == 0.0){
      SETERRQ(PETSC_ERR_MAT_LU_ZRPVT,0,"Zero pivot");
    }                                               

    /* SAVE NONZERO ENTRIES IN K-TH ROW OF U ... */
    ba[k] = 1.0/dk;
    jmin = bi[k]; jmax = bi[k+1];
    if (jmin < jmax) {
      for (j=jmin; j<jmax; j++){
         juj = bj[j]; ba[j] = rtmp[juj]; rtmp[juj] = 0.0;
      } 
      
      /* ... AND ADD K TO ROW LIST FOR FIRST NONZERO ENTRY IN K-TH ROW */
      il[k] = jmin;
      i     = bj[jmin];
      jl[k] = jl[i]; jl[i] = k;
    }        
  } 
  
  ierr = PetscFree(rtmp);CHKERRQ(ierr);
  ierr = PetscFree(il);CHKERRQ(ierr);
  ierr = PetscFree(jl);CHKERRQ(ierr); 
  if (a->permute){
    ierr = PetscFree(aa);CHKERRQ(ierr);
  }

  ierr = ISRestoreIndices(ip,&rip);CHKERRQ(ierr);
  C->factor    = FACTOR_LU;
  C->assembled = PETSC_TRUE;
  PLogFlops(b->mbs);
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "MatCholeskyFactor_SeqSBAIJ"
int MatCholeskyFactor_SeqSBAIJ(Mat A,IS perm,PetscReal f)
{
  Mat_SeqSBAIJ    *mat = (Mat_SeqSBAIJ*)A->data;
  int            ierr,refct;
  Mat            C;
  PetscOps *Abops;
  MatOps   Aops;

  PetscFunctionBegin;
  ierr = MatCholeskyFactorSymbolic(A,perm,f,&C);CHKERRQ(ierr);
  ierr = MatCholeskyFactorNumeric(A,&C);CHKERRQ(ierr);

  /* free all the data structures from mat */
  ierr = PetscFree(mat->a);CHKERRQ(ierr);
  if (!mat->singlemalloc) {
    ierr = PetscFree(mat->i);CHKERRQ(ierr);
    ierr = PetscFree(mat->j);CHKERRQ(ierr);
  }
  if (mat->diag) {ierr = PetscFree(mat->diag);CHKERRQ(ierr);}
  if (mat->ilen) {ierr = PetscFree(mat->ilen);CHKERRQ(ierr);}
  if (mat->imax) {ierr = PetscFree(mat->imax);CHKERRQ(ierr);}
  if (mat->solve_work) {ierr = PetscFree(mat->solve_work);CHKERRQ(ierr);}
  if (mat->mult_work) {ierr = PetscFree(mat->mult_work);CHKERRQ(ierr);}
  if (mat->icol) {ierr = ISDestroy(mat->icol);CHKERRQ(ierr);}
  ierr = PetscFree(mat);CHKERRQ(ierr);

  ierr = MapDestroy(A->rmap);CHKERRQ(ierr);
  ierr = MapDestroy(A->cmap);CHKERRQ(ierr);

  /*
       This is horrible,horrible code. We need to keep the 
    A pointers for the bops and ops but copy everything 
    else from C.
  */
  Abops = A->bops;
  Aops  = A->ops;
  refct = A->refct;
  ierr  = PetscMemcpy(A,C,sizeof(struct _p_Mat));CHKERRQ(ierr);
  mat   = (Mat_SeqSBAIJ*)A->data;
  PLogObjectParent(A,mat->icol); 

  A->bops  = Abops;
  A->ops   = Aops;
  A->qlist = 0;
  A->refct = refct;
  /* copy over the type_name and name */
  ierr     = PetscStrallocpy(C->type_name,&A->type_name);CHKERRQ(ierr);
  ierr     = PetscStrallocpy(C->name,&A->name);CHKERRQ(ierr);

  PetscHeaderDestroy(C);
  PetscFunctionReturn(0);
}


