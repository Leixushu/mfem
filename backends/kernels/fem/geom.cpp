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

#include "../../../config/config.hpp"
#if defined(MFEM_USE_BACKENDS) && defined(MFEM_USE_KERNELS)

#include "../kernels.hpp"

namespace mfem
{

namespace kernels
{
// *****************************************************************************
static KernelsGeometry *geom=NULL;

// ***************************************************************************
// * ~ KernelsGeometry
// ***************************************************************************
KernelsGeometry::~KernelsGeometry()
{
   push();
   free(geom->meshNodes);
   free(geom->J);
   free(geom->invJ);
   free(geom->detJ);
   delete[] geom;
   pop();
}

// *****************************************************************************
// * KernelsGeometry Get: use this one to fetch nodes from vector Sx
// *****************************************************************************
KernelsGeometry* KernelsGeometry::Get(KernelsFiniteElementSpace& fes,
                                const IntegrationRule& ir,
                                const KernelsVector& Sx)
{
   push();
   const Mesh *mesh = fes.GetFESpace()->GetMesh();
   const GridFunction *nodes = mesh->GetNodes();
   const mfem::FiniteElementSpace *fespace = nodes->FESpace();
   const mfem::FiniteElement *fe = fespace->GetFE(0);
   const int dims     = fe->GetDim();
   const int numDofs  = fe->GetDof();
   const int numQuad  = ir.GetNPoints();
   const int elements = fespace->GetNE();
   const int ndofs    = fespace->GetNDofs();
   const KernelsDofQuadMaps* maps = KernelsDofQuadMaps::GetSimplexMaps(*fe, ir);
   push();
   rNodeCopyByVDim(elements,numDofs,ndofs,dims,geom->eMap,Sx,geom->meshNodes);
   pop();
   push();
   rIniGeom(dims,numDofs,numQuad,elements,
            maps->dofToQuadD,
            geom->meshNodes,
            geom->J,
            geom->invJ,
            geom->detJ);
   pop();
   pop();
   return geom;
}


// *****************************************************************************
KernelsGeometry* KernelsGeometry::Get(KernelsFiniteElementSpace& fes,
                                const IntegrationRule& ir)
{
   push();
   Mesh& mesh = *(fes.GetFESpace()->GetMesh());
   const bool geom_to_allocate =
      (!geom) || config::Get().GeomNeedsUpdate(mesh.GetSequence());
   if (geom_to_allocate) { geom=new KernelsGeometry(); }
   if (!mesh.GetNodes()) { mesh.SetCurvature(1, false, -1, Ordering::byVDIM); }
   GridFunction& nodes = *(mesh.GetNodes());
   const mfem::FiniteElementSpace& fespace = *(nodes.FESpace());
   const mfem::FiniteElement& fe = *(fespace.GetFE(0));
   const int dims     = fe.GetDim();
   const int elements = fespace.GetNE();
   const int numDofs  = fe.GetDof();
   const int numQuad  = ir.GetNPoints();
   const bool orderedByNODES = (fespace.GetOrdering() == Ordering::byNODES);

   if (orderedByNODES) { ReorderByVDim(nodes); }
   const int asize = dims*numDofs*elements;
   mfem::Array<double> meshNodes(asize);
   const Table& e2dTable = fespace.GetElementToDofTable();
   const int* elementMap = e2dTable.GetJ();
   mfem::Array<int> eMap(numDofs*elements);
   {
      push();
      for (int e = 0; e < elements; ++e)
      {
         for (int d = 0; d < numDofs; ++d)
         {
            const int lid = d+numDofs*e;
            const int gid = elementMap[lid];
            eMap[lid]=gid;
            for (int v = 0; v < dims; ++v)
            {
               const int moffset = v+dims*lid;
               const int xoffset = v+dims*gid;
               meshNodes[moffset] = nodes[xoffset];
            }
         }
      }
      pop();
   }
   if (geom_to_allocate)
   {
      geom->meshNodes.allocate(dims, numDofs, elements);
      geom->eMap.allocate(numDofs, elements);
   }
   {
      push();
      geom->meshNodes = meshNodes;
      geom->eMap = eMap;
      pop();
   }
   // Reorder the original gf back
   if (orderedByNODES) { ReorderByNodes(nodes); }
   if (geom_to_allocate)
   {
      geom->J.allocate(dims, dims, numQuad, elements);
      geom->invJ.allocate(dims, dims, numQuad, elements);
      geom->detJ.allocate(numQuad, elements);
   }

   const KernelsDofQuadMaps* maps = KernelsDofQuadMaps::GetSimplexMaps(fe, ir);
   {
      push(rIniGeom,SteelBlue);
      rIniGeom(dims,numDofs,numQuad,elements,
               maps->dofToQuadD,
               geom->meshNodes,
               geom->J,
               geom->invJ,
               geom->detJ);
      pop();
   }
   pop();
   return geom;
}

// ***************************************************************************
void KernelsGeometry::ReorderByVDim(GridFunction& nodes)
{
   push();
   const mfem::FiniteElementSpace *fes=nodes.FESpace();
   const int size = nodes.Size();
   const int vdim = fes->GetVDim();
   const int ndofs = fes->GetNDofs();
   double *data = nodes.GetData();
   double *temp = new double[size];
   int k=0;
   for (int d = 0; d < ndofs; d++)
      for (int v = 0; v < vdim; v++)
      {
         temp[k++] = data[d+v*ndofs];
      }
   for (int i = 0; i < size; i++)
   {
      data[i] = temp[i];
   }
   delete [] temp;
   pop();
}

// ***************************************************************************
void KernelsGeometry::ReorderByNodes(GridFunction& nodes)
{
   push();
   const mfem::FiniteElementSpace *fes=nodes.FESpace();
   const int size = nodes.Size();
   const int vdim = fes->GetVDim();
   const int ndofs = fes->GetNDofs();
   double *data = nodes.GetData();
   double *temp = new double[size];
   int k = 0;
   for (int j = 0; j < ndofs; j++)
      for (int i = 0; i < vdim; i++)
      {
         temp[j+i*ndofs] = data[k++];
      }
   for (int i = 0; i < size; i++)
   {
      data[i] = temp[i];
   }
   delete [] temp;
   pop();
}

} // namespace mfem::kernels

} // namespace mfem

#endif // defined(MFEM_USE_BACKENDS) && defined(MFEM_USE_KERNELS)