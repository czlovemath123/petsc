static char help[] = "This example demonstrates the use of DMNetwork interface with subnetworks for solving a coupled nonlinear \n\
                      electric power grid and water pipe problem.\n\
                      The available solver options are in the ex1options file \n\
                      and the data files are in the datafiles of subdirectories.\n\
                      This example shows the use of subnetwork feature in DMNetwork. \n\
                      Run this program: mpiexec -n <n> ./ex1 \n\\n";

/* T
   Concepts: DMNetwork
   Concepts: PETSc SNES solver
*/

#include "pflow/pf.h"
#include "waternet/waternet.h"

PetscErrorCode FormFunction_Power(DM networkdm,Vec localX, Vec localF,PetscInt nv,PetscInt ne,const PetscInt* vtx,const PetscInt* edges,void* appctx)
{
  PetscErrorCode    ierr;
  UserCtx_Power     *User=(UserCtx_Power*)appctx;
  PetscInt          e,v,vfrom,vto;
  const PetscScalar *xarr;
  PetscScalar       *farr;
  PetscInt          offsetfrom,offsetto,offset;

  PetscFunctionBegin;
  ierr = VecGetArrayRead(localX,&xarr);CHKERRQ(ierr);
  ierr = VecGetArray(localF,&farr);CHKERRQ(ierr);

  for (v=0; v<nv; v++) {
    PetscInt    i,j,key;
    PetscScalar Vm;
    PetscScalar Sbase=User->Sbase;
    VERTEX_Power  bus=NULL;
    GEN         gen;
    LOAD        load;
    PetscBool   ghostvtex;
    PetscInt    numComps;
    void*       component;

    ierr = DMNetworkIsGhostVertex(networkdm,vtx[v],&ghostvtex);CHKERRQ(ierr);
    ierr = DMNetworkGetNumComponents(networkdm,vtx[v],&numComps);CHKERRQ(ierr);
    ierr = DMNetworkGetVariableOffset(networkdm,vtx[v],&offset);CHKERRQ(ierr);
    for (j = 0; j < numComps; j++) {
      ierr = DMNetworkGetComponent(networkdm,vtx[v],j,&key,&component);CHKERRQ(ierr);
      if (key == 1) {
        PetscInt       nconnedges;
	const PetscInt *connedges;

	bus = (VERTEX_Power)(component);
	/* Handle reference bus constrained dofs */
	if (bus->ide == REF_BUS || bus->ide == ISOLATED_BUS) {
	  farr[offset] = xarr[offset] - bus->va*PETSC_PI/180.0;
	  farr[offset+1] = xarr[offset+1] - bus->vm;
	  break;
	}

	if (!ghostvtex) {
	  Vm = xarr[offset+1];

	  /* Shunt injections */
	  farr[offset] += Vm*Vm*bus->gl/Sbase;
	  if(bus->ide != PV_BUS) farr[offset+1] += -Vm*Vm*bus->bl/Sbase;
	}

	ierr = DMNetworkGetSupportingEdges(networkdm,vtx[v],&nconnedges,&connedges);CHKERRQ(ierr);
	for (i=0; i < nconnedges; i++) {
	  EDGE_Power       branch;
	  PetscInt       keye;
          PetscScalar    Gff,Bff,Gft,Bft,Gtf,Btf,Gtt,Btt;
          const PetscInt *cone;
          PetscScalar    Vmf,Vmt,thetaf,thetat,thetaft,thetatf;

	  e = connedges[i];
	  ierr = DMNetworkGetComponent(networkdm,e,0,&keye,(void**)&branch);CHKERRQ(ierr);
	  if (!branch->status) continue;
	  Gff = branch->yff[0];
	  Bff = branch->yff[1];
	  Gft = branch->yft[0];
	  Bft = branch->yft[1];
	  Gtf = branch->ytf[0];
	  Btf = branch->ytf[1];
	  Gtt = branch->ytt[0];
	  Btt = branch->ytt[1];

	  ierr = DMNetworkGetConnectedVertices(networkdm,e,&cone);CHKERRQ(ierr);
	  vfrom = cone[0];
	  vto   = cone[1];

	  ierr = DMNetworkGetVariableOffset(networkdm,vfrom,&offsetfrom);CHKERRQ(ierr);
	  ierr = DMNetworkGetVariableOffset(networkdm,vto,&offsetto);CHKERRQ(ierr);

	  thetaf = xarr[offsetfrom];
	  Vmf     = xarr[offsetfrom+1];
	  thetat = xarr[offsetto];
	  Vmt     = xarr[offsetto+1];
	  thetaft = thetaf - thetat;
	  thetatf = thetat - thetaf;

	  if (vfrom == vtx[v]) {
	    farr[offsetfrom]   += Gff*Vmf*Vmf + Vmf*Vmt*(Gft*PetscCosScalar(thetaft) + Bft*PetscSinScalar(thetaft));
	    farr[offsetfrom+1] += -Bff*Vmf*Vmf + Vmf*Vmt*(-Bft*PetscCosScalar(thetaft) + Gft*PetscSinScalar(thetaft));
	  } else {
	    farr[offsetto]   += Gtt*Vmt*Vmt + Vmt*Vmf*(Gtf*PetscCosScalar(thetatf) + Btf*PetscSinScalar(thetatf));
	    farr[offsetto+1] += -Btt*Vmt*Vmt + Vmt*Vmf*(-Btf*PetscCosScalar(thetatf) + Gtf*PetscSinScalar(thetatf));
	  }
	}
      } else if (key == 2) {
	if (!ghostvtex) {
	  gen = (GEN)(component);
	  if (!gen->status) continue;
	  farr[offset] += -gen->pg/Sbase;
	  farr[offset+1] += -gen->qg/Sbase;
	}
      } else if (key == 3) {
	if (!ghostvtex) {
	  load = (LOAD)(component);
	  farr[offset] += load->pl/Sbase;
	  farr[offset+1] += load->ql/Sbase;
	}
      }
    }
    if (bus && bus->ide == PV_BUS) {
      farr[offset+1] = xarr[offset+1] - bus->vm;
    }
  }
  ierr = VecRestoreArrayRead(localX,&xarr);CHKERRQ(ierr);
  ierr = VecRestoreArray(localF,&farr);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

PetscErrorCode FormFunction_Water(DM networkdm,Vec localX, Vec localF,PetscInt nv,PetscInt ne,const PetscInt* vtx,const PetscInt* edges,void* appctx)
{
  PetscErrorCode    ierr;
  const PetscScalar *xarr;
  const PetscInt    *cone;
  PetscScalar       *farr,hf,ht,flow;
  PetscInt          i,key,vnode1,vnode2;
  PetscBool         ghostvtex;
  PetscInt          offsetnode1,offsetnode2,offset;
  VERTEX_Water      vertex,vertexnode1,vertexnode2;
  EDGE_Water        edge;
  Pipe              *pipe;
  Pump              *pump;
  Reservoir         *reservoir;
  Tank              *tank;

  PetscFunctionBegin;
  /* Get arrays for the vectors */
  ierr = VecGetArrayRead(localX,&xarr);CHKERRQ(ierr);
  ierr = VecGetArray(localF,&farr);CHKERRQ(ierr);

  for (i=0; i<ne; i++) { /* for each edge */
    /* Get the offset and the key for the component for edge number e[i] */
    ierr = DMNetworkGetComponent(networkdm,edges[i],0,&key,(void**)&edge);CHKERRQ(ierr);
    if (key != 4) SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_ARG_WRONG,"key %d, not an edge_water componet",key);

    /* Get the numbers for the vertices covering this edge */
    ierr = DMNetworkGetConnectedVertices(networkdm,edges[i],&cone);CHKERRQ(ierr);
    vnode1 = cone[0];
    vnode2 = cone[1];

    /* Get the components at the two vertices */
    ierr = DMNetworkGetComponent(networkdm,vnode1,0,&key,(void**)&vertexnode1);CHKERRQ(ierr);
    if (key != 5) SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_ARG_WRONG,"key %d, not a vertex_water componet",key);
    ierr = DMNetworkGetComponent(networkdm,vnode2,0,&key,(void**)&vertexnode2);CHKERRQ(ierr);
    if (key != 5) SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_ARG_WRONG,"key %d, not a vertex_water componet",key);

    /* Get the variable offset (the starting location for the variables in the farr array) for node1 and node2 */
    ierr = DMNetworkGetVariableOffset(networkdm,vnode1,&offsetnode1);CHKERRQ(ierr);
    ierr = DMNetworkGetVariableOffset(networkdm,vnode2,&offsetnode2);CHKERRQ(ierr);

    /* Variables at node1 and node 2 */
    hf = xarr[offsetnode1];
    ht = xarr[offsetnode2];

    if (edge->type == EDGE_TYPE_PIPE) {
      pipe = &edge->pipe;
      flow = Flow_Pipe(pipe,hf,ht);
    } else if (edge->type == EDGE_TYPE_PUMP) {
      pump = &edge->pump;
      flow = Flow_Pump(pump,hf,ht);
    }
    /* Convention: Node 1 has outgoing flow and Node 2 has incoming flow */
    if (vertexnode1->type == VERTEX_TYPE_JUNCTION) farr[offsetnode1] -= flow;
    if (vertexnode2->type == VERTEX_TYPE_JUNCTION) farr[offsetnode2] += flow;
  }

  /* Subtract Demand flows at the vertices */
  for (i=0; i<nv; i++) {
    ierr = DMNetworkIsGhostVertex(networkdm,vtx[i],&ghostvtex);CHKERRQ(ierr);
    if(ghostvtex) continue;

    ierr = DMNetworkGetVariableOffset(networkdm,vtx[i],&offset);CHKERRQ(ierr);
    ierr = DMNetworkGetComponent(networkdm,vtx[i],0,&key,(void**)&vertex);CHKERRQ(ierr);
    if (key != 5) SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_ARG_WRONG,"key %d, not a vertex_water componet",key);

    if (vertex->type == VERTEX_TYPE_JUNCTION) {
      farr[offset] -= vertex->junc.demand;
    } else if (vertex->type == VERTEX_TYPE_RESERVOIR) {
      reservoir = &vertex->res;
      farr[offset] = xarr[offset] - reservoir->head;
    } else if(vertex->type == VERTEX_TYPE_TANK) {
      tank = &vertex->tank;
      farr[offset] = xarr[offset] - (tank->elev + tank->initlvl);
    }
  }

  ierr = VecRestoreArrayRead(localX,&xarr);CHKERRQ(ierr);
  ierr = VecRestoreArray(localF,&farr);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

PetscErrorCode FormFunction(SNES snes,Vec X, Vec F,void *appctx)
{
  PetscErrorCode ierr;
  DM             networkdm;
  Vec            localX,localF;
  PetscInt       nv,ne;
  const PetscInt *vtx,*edges;
  PetscMPIInt    rank;
  MPI_Comm       comm;

  PetscFunctionBegin;
  ierr = SNESGetDM(snes,&networkdm);CHKERRQ(ierr);
  ierr = PetscObjectGetComm((PetscObject)networkdm,&comm);CHKERRQ(ierr);
  ierr = MPI_Comm_rank(comm,&rank);CHKERRQ(ierr);

  ierr = DMGetLocalVector(networkdm,&localX);CHKERRQ(ierr);
  ierr = DMGetLocalVector(networkdm,&localF);CHKERRQ(ierr);
  ierr = VecSet(F,0.0);CHKERRQ(ierr);
  ierr = VecSet(localF,0.0);CHKERRQ(ierr);

  ierr = DMGlobalToLocalBegin(networkdm,X,INSERT_VALUES,localX);CHKERRQ(ierr);
  ierr = DMGlobalToLocalEnd(networkdm,X,INSERT_VALUES,localX);CHKERRQ(ierr);

  /* Form Function for power subnetwork */
  ierr = DMNetworkGetSubnetworkInfo(networkdm,0,&nv,&ne,&vtx,&edges);CHKERRQ(ierr);
  ierr = FormFunction_Power(networkdm,localX,localF,nv,ne,vtx,edges,appctx);CHKERRQ(ierr);

  /* Form Function for water subnetwork */
  ierr = DMNetworkGetSubnetworkInfo(networkdm,1,&nv,&ne,&vtx,&edges);CHKERRQ(ierr);
  ierr = FormFunction_Water(networkdm,localX,localF,nv,ne,vtx,edges,appctx);CHKERRQ(ierr);

  /* Form Function for the coupling subnetwork */
  ierr = DMNetworkGetSubnetworkInfo(networkdm,2,&nv,&ne,&vtx,&edges);CHKERRQ(ierr);
  if (ne) {
    const PetscInt *cone;
    PetscInt       key,offset,i,j,numComps;
    PetscBool      ghostvtex;
    PetscScalar    *farr;
    void*          component;

    ierr = VecGetArray(localF,&farr);CHKERRQ(ierr);
    ierr = DMNetworkGetConnectedVertices(networkdm,edges[0],&cone);CHKERRQ(ierr);
#if 0
    PetscInt       vid[2];
    ierr = DMNetworkGetGlobalVertexIndex(networkdm,cone[0],&vid[0]);CHKERRQ(ierr);
    ierr = DMNetworkGetGlobalVertexIndex(networkdm,cone[1],&vid[1]);CHKERRQ(ierr);
    ierr = PetscPrintf(PETSC_COMM_SELF,"[%d] Formfunction, coupling subnetwork: nv %d, ne %d; connected vertices %d %d\n",rank,nv,ne,vid[0],vid[1]);CHKERRQ(ierr);
#endif

    for (i=0; i<2; i++) {
      ierr = DMNetworkIsGhostVertex(networkdm,cone[i],&ghostvtex);CHKERRQ(ierr);
      if (!ghostvtex) {
        ierr = DMNetworkGetNumComponents(networkdm,cone[i],&numComps);CHKERRQ(ierr);
        for (j=0; j<numComps; j++) {
          ierr = DMNetworkGetComponent(networkdm,cone[i],j,&key,&component);CHKERRQ(ierr);
          if (key == 3) { /* a load vertex in power subnet */
            ierr = DMNetworkGetVariableOffset(networkdm,cone[i],&offset);CHKERRQ(ierr);
            printf("[%d] v_power load: ...Flocal[%d]= %g, %g\n",rank,offset,farr[offset],farr[offset+1]);
          } else if (key == 5) { /* a vertex in water subnet */
            ierr = DMNetworkGetVariableOffset(networkdm,cone[i],&offset);CHKERRQ(ierr);
            printf("[%d] v_water:     ...Flocal[%d]= %g\n",rank,offset,farr[offset]);
          }
        }
      }
    }
    ierr = VecRestoreArray(localF,&farr);CHKERRQ(ierr);
  }

  ierr = DMRestoreLocalVector(networkdm,&localX);CHKERRQ(ierr);

  ierr = DMLocalToGlobalBegin(networkdm,localF,ADD_VALUES,F);CHKERRQ(ierr);
  ierr = DMLocalToGlobalEnd(networkdm,localF,ADD_VALUES,F);CHKERRQ(ierr);
  ierr = DMRestoreLocalVector(networkdm,&localF);CHKERRQ(ierr);
  /* ierr = VecView(F,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr); */
  PetscFunctionReturn(0);
}

PetscErrorCode SetInitialGuess_Power(DM networkdm,Vec localX,PetscInt nv,PetscInt ne, const PetscInt *vtx, const PetscInt *edges,void* appctx)
{
  PetscErrorCode ierr;
  VERTEX_Power     bus;
  PetscInt       i;
  GEN            gen;
  PetscBool      ghostvtex;
  PetscScalar    *xarr;
  PetscInt       key,numComps,j,offset;
  void*          component;

  PetscFunctionBegin;
  ierr = VecGetArray(localX,&xarr);CHKERRQ(ierr);
  for (i = 0; i < nv; i++) {
    ierr = DMNetworkIsGhostVertex(networkdm,vtx[i],&ghostvtex);CHKERRQ(ierr);
    if (ghostvtex) continue;

    ierr = DMNetworkGetVariableOffset(networkdm,vtx[i],&offset);CHKERRQ(ierr);
    ierr = DMNetworkGetNumComponents(networkdm,vtx[i],&numComps);CHKERRQ(ierr);
    for (j=0; j < numComps; j++) {
      ierr = DMNetworkGetComponent(networkdm,vtx[i],j,&key,&component);CHKERRQ(ierr);
      if (key == 1) {
	bus = (VERTEX_Power)(component);
	xarr[offset] = bus->va*PETSC_PI/180.0;
	xarr[offset+1] = bus->vm;
      } else if(key == 2) {
	gen = (GEN)(component);
	if (!gen->status) continue;
	xarr[offset+1] = gen->vs;
	break;
      }
    }
  }
  ierr = VecRestoreArray(localX,&xarr);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

PetscErrorCode SetInitialGuess_Water(DM networkdm,Vec localX,PetscInt nv,PetscInt ne, const PetscInt *vtx, const PetscInt *edges,void* appctx)
{
  PetscErrorCode ierr;
  PetscInt       i,offset,key;
  PetscBool      ghostvtex;
  VERTEX_Water   vertex;
  PetscScalar    *xarr;

  PetscFunctionBegin;
  ierr = VecGetArray(localX,&xarr);CHKERRQ(ierr);
  for (i=0; i < nv; i++) {
    ierr = DMNetworkIsGhostVertex(networkdm,vtx[i],&ghostvtex);CHKERRQ(ierr);
    if (ghostvtex) continue;
    ierr = DMNetworkGetVariableOffset(networkdm,vtx[i],&offset);CHKERRQ(ierr);
    ierr = DMNetworkGetComponent(networkdm,vtx[i],0,&key,(void**)&vertex);CHKERRQ(ierr);
    if (key != 5) SETERRQ1(PETSC_COMM_SELF,0,"not a VERTEX_Water, key = %d",key);

    if (vertex->type == VERTEX_TYPE_JUNCTION) {
      xarr[offset] = 100;
    } else if (vertex->type == VERTEX_TYPE_RESERVOIR) {
      xarr[offset] = vertex->res.head;
    } else {
      xarr[offset] = vertex->tank.initlvl + vertex->tank.elev;
    }
  }
  ierr = VecRestoreArray(localX,&xarr);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

PetscErrorCode SetInitialGuess(DM networkdm,Vec X,void* appctx)
{
  PetscErrorCode ierr;
  PetscInt       nv,ne;
  const PetscInt *vtx,*edges;
  Vec            localX;
  PetscMPIInt    rank;
  MPI_Comm       comm;

  PetscFunctionBegin;
  ierr = PetscObjectGetComm((PetscObject)networkdm,&comm);CHKERRQ(ierr);
  ierr = MPI_Comm_rank(comm,&rank);CHKERRQ(ierr);
  ierr = DMGetLocalVector(networkdm,&localX);CHKERRQ(ierr);

  ierr = VecSet(X,0.0);CHKERRQ(ierr);
  ierr = DMGlobalToLocalBegin(networkdm,X,INSERT_VALUES,localX);CHKERRQ(ierr);
  ierr = DMGlobalToLocalEnd(networkdm,X,INSERT_VALUES,localX);CHKERRQ(ierr);

  /* Set initial guess for first subnetwork */
  ierr = DMNetworkGetSubnetworkInfo(networkdm,0,&nv,&ne,&vtx,&edges);CHKERRQ(ierr);
  ierr = SetInitialGuess_Power(networkdm,localX,nv,ne,vtx,edges,appctx);CHKERRQ(ierr);
  ierr = PetscSynchronizedPrintf(PETSC_COMM_WORLD,"[%d] Power subnetwork: nv %d, ne %d\n",rank,nv,ne);
  ierr = PetscSynchronizedFlush(PETSC_COMM_WORLD,PETSC_STDOUT);CHKERRQ(ierr);

  /* Set initial guess for second subnetwork */
  ierr = DMNetworkGetSubnetworkInfo(networkdm,1,&nv,&ne,&vtx,&edges);CHKERRQ(ierr);
  ierr = SetInitialGuess_Water(networkdm,localX,nv,ne,vtx,edges,appctx);CHKERRQ(ierr);
  ierr = PetscSynchronizedPrintf(PETSC_COMM_WORLD,"[%d] Water subnetwork: nv %d, ne %d\n",rank,nv,ne);CHKERRQ(ierr);
  ierr = PetscSynchronizedFlush(PETSC_COMM_WORLD,PETSC_STDOUT);CHKERRQ(ierr);

  /* Set initial guess for the coupling subnet */
  ierr = DMNetworkGetSubnetworkInfo(networkdm,2,&nv,&ne,&vtx,&edges);CHKERRQ(ierr);
  if (ne) {
    const PetscInt *cone;
    ierr = DMNetworkGetConnectedVertices(networkdm,edges[0],&cone);CHKERRQ(ierr);
  }

  ierr = DMLocalToGlobalBegin(networkdm,localX,ADD_VALUES,X);CHKERRQ(ierr);
  ierr = DMLocalToGlobalEnd(networkdm,localX,ADD_VALUES,X);CHKERRQ(ierr);

  ierr = DMRestoreLocalVector(networkdm,&localX);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

int main(int argc,char **argv)
{
  PetscErrorCode   ierr;
  PetscInt         numEdges1=0,numVertices1=0,numEdges2=0,numVertices2=0,componentkey[6];
  DM               networkdm;
  UserCtx_Power    User;
  PetscLogStage    stage1,stage2,stage3;
  PetscMPIInt      crank;
  PetscInt         nsubnet = 3,numVertices[3],NumVertices[3],numEdges[3],NumEdges[3];
  PetscInt         i,j,*edgelist[3],nv,ne;
  const PetscInt   *vtx,*edges;
  Vec              X,F;
  SNES             snes;
  Mat              Jac;
  PetscBool        viewJ=PETSC_FALSE,viewX=PETSC_FALSE;

  char             pfdata_file[PETSC_MAX_PATH_LEN]="pflow/datafiles/case9.m";
  PFDATA           *pfdata;
  PetscInt         genj,loadj;
  int              *edgelist_power=NULL;

  WATERDATA        *waterdata;
  char             waterdata_file[PETSC_MAX_PATH_LEN]="waternet/sample1.inp";
  int              *edgelist_water=NULL;

  PetscInt         numEdges3=0,numVertices3=0;
  int              *edgelist_couple=NULL;

  ierr = PetscInitialize(&argc,&argv,"ex1options",help);CHKERRQ(ierr);
  ierr = MPI_Comm_rank(PETSC_COMM_WORLD,&crank);CHKERRQ(ierr);

  /* (1) Read Data - Only rank 0 reads the data */
  /*--------------------------------------------*/
  ierr = PetscLogStageRegister("Read Data",&stage1);CHKERRQ(ierr);
  PetscLogStagePush(stage1);

  /* READ THE DATA FOR THE FIRST SUBNETWORK: Electric Power Grid */
  if (!crank) {
    ierr = PetscOptionsGetString(NULL,NULL,"-pfdata",pfdata_file,PETSC_MAX_PATH_LEN-1,NULL);CHKERRQ(ierr);
    ierr = PetscNew(&pfdata);CHKERRQ(ierr);
    ierr = PFReadMatPowerData(pfdata,pfdata_file);CHKERRQ(ierr);
    User.Sbase = pfdata->sbase;

    numEdges1    = pfdata->nbranch;
    numVertices1 = pfdata->nbus;

    ierr = PetscMalloc1(2*numEdges1,&edgelist_power);CHKERRQ(ierr);
    ierr = GetListofEdges_Power(pfdata,edgelist_power);CHKERRQ(ierr);
    printf("edgelist_power:\n");
    for (i=0; i<numEdges1; i++) {
      ierr = PetscPrintf(PETSC_COMM_SELF,"[%D %D]",edgelist_power[2*i],edgelist_power[2*i+1]);CHKERRQ(ierr);
    }
    printf("\n");
  }
  /* Broadcast power Sbase to all processors */
  ierr = MPI_Bcast(&User.Sbase,1,MPIU_SCALAR,0,PETSC_COMM_WORLD);CHKERRQ(ierr);

  /* GET DATA FOR THE SECOND SUBNETWORK: Waternet */
  ierr = PetscNew(&waterdata);CHKERRQ(ierr);
  if (!crank) {
    ierr = PetscOptionsGetString(NULL,NULL,"-waterdata",waterdata_file,PETSC_MAX_PATH_LEN-1,NULL);CHKERRQ(ierr);
    ierr = WaterNetReadData(waterdata,waterdata_file);CHKERRQ(ierr);

    ierr = PetscCalloc1(2*waterdata->nedge,&edgelist_water);CHKERRQ(ierr);
    ierr = GetListofEdges_Water(waterdata,edgelist_water);CHKERRQ(ierr);
    numEdges2    = waterdata->nedge;
    numVertices2 = waterdata->nvertex;

    printf("edgelist_water:\n");
    for (i=0; i<numEdges2; i++) {
      ierr = PetscPrintf(PETSC_COMM_SELF,"[%D %D]",edgelist_water[2*i],edgelist_water[2*i+1]);CHKERRQ(ierr);
    }
    printf("\n");
  }

  /* Get data for the coupling subnetwork */
  if (!crank) {
    numEdges3 = 1; numVertices3 = 0;
    ierr = PetscMalloc1(4*numEdges3,&edgelist_couple);CHKERRQ(ierr);
    edgelist_couple[0] = 0; edgelist_couple[1] = 4; /* from node: net[0] vertex[4] */
    edgelist_couple[2] = 1; edgelist_couple[3] = 0; /* to node:   net[1] vertex[0] */
  }
  PetscLogStagePop();

  /* (2) Create network */
  /*--------------------*/
  ierr = MPI_Barrier(PETSC_COMM_WORLD);CHKERRQ(ierr);
  ierr = PetscLogStageRegister("Create network",&stage2);CHKERRQ(ierr);
  PetscLogStagePush(stage2);

  /* Create an empty network object */
  ierr = DMNetworkCreate(PETSC_COMM_WORLD,&networkdm);CHKERRQ(ierr);

  /* Register the components in the network */
  ierr = DMNetworkRegisterComponent(networkdm,"branchstruct",sizeof(struct _p_EDGE_Power),&componentkey[0]);CHKERRQ(ierr);
  ierr = DMNetworkRegisterComponent(networkdm,"busstruct",sizeof(struct _p_VERTEX_Power),&componentkey[1]);CHKERRQ(ierr);
  ierr = DMNetworkRegisterComponent(networkdm,"genstruct",sizeof(struct _p_GEN),&componentkey[2]);CHKERRQ(ierr);
  ierr = DMNetworkRegisterComponent(networkdm,"loadstruct",sizeof(struct _p_LOAD),&componentkey[3]);CHKERRQ(ierr);

  ierr = DMNetworkRegisterComponent(networkdm,"edge_water",sizeof(struct _p_EDGE_Water),&componentkey[4]);CHKERRQ(ierr);
  ierr = DMNetworkRegisterComponent(networkdm,"vertex_water",sizeof(struct _p_VERTEX_Water),&componentkey[5]);CHKERRQ(ierr);

  /* Set local or global number of vertices and edges */
  numVertices[0] = numVertices1; NumVertices[0] = PETSC_DETERMINE;
  numVertices[1] = numVertices2; NumVertices[1] = PETSC_DETERMINE;
  numVertices[2] = numVertices3; NumVertices[2] = PETSC_DETERMINE; /* coupling vertices */

  numEdges[0] = numEdges1; NumEdges[0] = PETSC_DETERMINE;
  numEdges[1] = numEdges2; NumEdges[1] = PETSC_DETERMINE;
  numEdges[2] = numEdges3; NumEdges[2] = PETSC_DETERMINE; /* coupling edges */

  ierr = PetscPrintf(PETSC_COMM_SELF,"[%d] local nvertices %d %d; nedges %d %d\n",crank,numVertices[0],numVertices[1],numEdges[0],numEdges[1]);
  ierr = DMNetworkSetSizes(networkdm,nsubnet,numVertices,numEdges,NumVertices,NumEdges);CHKERRQ(ierr);

  /* Add edge connectivity */
  edgelist[0] = edgelist_power;
  edgelist[1] = edgelist_water;
  edgelist[2] = edgelist_couple;
  ierr = DMNetworkSetEdgeList(networkdm,edgelist);CHKERRQ(ierr);

  /* Set up the network layout */
  ierr = DMNetworkLayoutSetUpCoupled(networkdm);CHKERRQ(ierr);

  /* Add network components - only process[0] has any data to add */
  /* ADD VARIABLES AND COMPONENTS FOR THE POWER SUBNETWORK */
  ierr = DMNetworkGetSubnetworkInfo(networkdm,0,&nv,&ne,&vtx,&edges);CHKERRQ(ierr);
  if (!crank) printf("[%d] Power network: nv %d, ne %d\n",crank,nv,ne);
  genj = 0; loadj = 0;
  for (i = 0; i < ne; i++) {
    ierr = DMNetworkAddComponent(networkdm,edges[i],componentkey[0],&pfdata->branch[i]);CHKERRQ(ierr);
  }

  for (i = 0; i < nv; i++) {
    ierr = DMNetworkAddComponent(networkdm,vtx[i],componentkey[1],&pfdata->bus[i]);CHKERRQ(ierr);
    if (pfdata->bus[i].ngen) {
      for (j = 0; j < pfdata->bus[i].ngen; j++) {
        ierr = DMNetworkAddComponent(networkdm,vtx[i],componentkey[2],&pfdata->gen[genj++]);CHKERRQ(ierr);
      }
    }
    if (pfdata->bus[i].nload) {
      for (j=0; j < pfdata->bus[i].nload; j++) {
        ierr = DMNetworkAddComponent(networkdm,vtx[i],componentkey[3],&pfdata->load[loadj++]);CHKERRQ(ierr);
      }
    }
    /* Add number of variables */
    ierr = DMNetworkAddNumVariables(networkdm,vtx[i],2);CHKERRQ(ierr);
  }

  /* ADD VARIABLES AND COMPONENTS FOR THE WATER SUBNETWORK */
  ierr = DMNetworkGetSubnetworkInfo(networkdm,1,&nv,&ne,&vtx,&edges);CHKERRQ(ierr);
  if (!crank) printf("[%d] Water network: nv %d, ne %d\n",crank,nv,ne);
  for (i = 0; i < ne; i++) {
    ierr = DMNetworkAddComponent(networkdm,edges[i],componentkey[4],&waterdata->edge[i]);CHKERRQ(ierr);
  }

  for (i = 0; i < nv; i++) {
    ierr = DMNetworkAddComponent(networkdm,vtx[i],componentkey[5],&waterdata->vertex[i]);CHKERRQ(ierr);
    /* Add number of variables */
    ierr = DMNetworkAddNumVariables(networkdm,vtx[i],1);CHKERRQ(ierr);
  }

  /* Set up DM for use */
  ierr = DMSetUp(networkdm);CHKERRQ(ierr);

  /* Distribute networkdm to multiple processes */
  ierr = DMNetworkDistribute(&networkdm,0);CHKERRQ(ierr);

  PetscLogStagePop();

  /* (3) Solve */
  /*-----------*/
  ierr = PetscLogStageRegister("SNES Solve",&stage3);CHKERRQ(ierr);
  PetscLogStagePush(stage3);

  ierr = DMCreateGlobalVector(networkdm,&X);CHKERRQ(ierr);
  ierr = VecDuplicate(X,&F);CHKERRQ(ierr);

  ierr = SetInitialGuess(networkdm,X,&User);CHKERRQ(ierr);
  //ierr = VecView(X,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);

  /* HOOK UP SOLVER */
  ierr = SNESCreate(PETSC_COMM_WORLD,&snes);CHKERRQ(ierr);
  ierr = SNESSetDM(snes,networkdm);CHKERRQ(ierr);
  ierr = SNESSetOptionsPrefix(snes,"coupled_");CHKERRQ(ierr);
  ierr = SNESSetFunction(snes,F,FormFunction,&User);CHKERRQ(ierr);
  ierr = SNESSetFromOptions(snes);CHKERRQ(ierr);

  ierr = PetscOptionsGetBool(NULL,NULL,"-viewJ",&viewJ,NULL);CHKERRQ(ierr);
  if (viewJ) {
    /* View Jac structure */
    ierr = SNESSetUp(snes);CHKERRQ(ierr);
    ierr = SNESGetJacobian(snes,&Jac,NULL,NULL,NULL);CHKERRQ(ierr);
    ierr = MatView(Jac,PETSC_VIEWER_DRAW_WORLD);CHKERRQ(ierr);
  }

  ierr = SNESSolve(snes,NULL,X);CHKERRQ(ierr);
  PetscLogStagePop();

  ierr = PetscOptionsGetBool(NULL,NULL,"-viewX",&viewX,NULL);CHKERRQ(ierr);
  if (viewX) {
    if (!crank) printf("Solution:\n");
    ierr = VecView(X,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);
  }
  PetscLogStagePop();

  if (viewJ) {
    /* View assembled Jac */
    ierr = MatView(Jac,PETSC_VIEWER_DRAW_WORLD);CHKERRQ(ierr);
  }

  ierr = SNESDestroy(&snes);CHKERRQ(ierr);
  ierr = VecDestroy(&X);CHKERRQ(ierr);
  ierr = VecDestroy(&F);CHKERRQ(ierr);

  if (!crank) {
    ierr = PetscFree(edgelist_power);CHKERRQ(ierr);
    ierr = PetscFree(pfdata->bus);CHKERRQ(ierr);
    ierr = PetscFree(pfdata->gen);CHKERRQ(ierr);
    ierr = PetscFree(pfdata->branch);CHKERRQ(ierr);
    ierr = PetscFree(pfdata->load);CHKERRQ(ierr);
    ierr = PetscFree(pfdata);CHKERRQ(ierr);

    ierr = PetscFree(edgelist_water);CHKERRQ(ierr);
    ierr = PetscFree(waterdata->vertex);CHKERRQ(ierr);
    ierr = PetscFree(waterdata->edge);CHKERRQ(ierr);

    ierr = PetscFree(edgelist_couple);CHKERRQ(ierr);
  }
  ierr = PetscFree(waterdata);CHKERRQ(ierr);
  ierr = DMDestroy(&networkdm);CHKERRQ(ierr);

  ierr = PetscFinalize();
  return ierr;
}
