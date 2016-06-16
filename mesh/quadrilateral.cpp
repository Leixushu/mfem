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


#include "mesh_headers.hpp"

namespace mfem
{

// TODO - This code can be refactored into a generic class with some static vectors,
// one per element type, or better yet MFEM arrays.  If the datastore enhanced the
// data buffer class to provide an attribute to denote a 'last used index' or similar
// when filling up a data buffer, it would be similar in capability to an MFEM array.
// DataBuffer already has the alloc and realloc capability.
 

const int Quadrilateral::edges[4][2] =
{{0, 1}, {1, 2}, {2, 3}, {3, 0}};

Quadrilateral::Quadrilateral( const int *ind, int attr, int_ptr_pair p )
   : Element(Geometry::SQUARE, p.first, 4, p.second)
{
   attribute = attr;
   for (int i=0; i<4; i++)
   {
      indices[i] = ind[i];
   }
}

Quadrilateral::Quadrilateral( int ind1, int ind2, int ind3, int ind4,
                              int attr, int_ptr_pair p )  
   : Element(Geometry::SQUARE, p.first, 4, p.second)
{
   attribute  = attr;
   indices[0] = ind1;
   indices[1] = ind2;
   indices[2] = ind3;
   indices[3] = ind4;
}

void Quadrilateral::SetVertices(const int *ind)
{
   for (int i=0; i<4; i++)
   {
      indices[i] = ind[i];
   }
}

void Quadrilateral::GetVertices( Array<int> &v ) const
{
   v.SetSize( 4 );
   for (int i=0; i<4; i++)
   {
      v[i] = indices[i];
   }
}

BiLinear2DFiniteElement QuadrilateralFE;

}
