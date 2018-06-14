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

#ifndef MFEM_BACKENDS_KERNELS_ENGINE_HPP
#define MFEM_BACKENDS_KERNELS_ENGINE_HPP

#include "../../../config/config.hpp"
#if defined(MFEM_USE_BACKENDS) && defined(MFEM_USE_KERNELS)

namespace mfem
{

namespace kernels
{

class Engine : public mfem::Engine
{
protected:
   kernels::device *dev=NULL;

   void Init(const std::string &engine_spec);

public:
   Engine(const std::string &engine_spec);

#ifdef MFEM_USE_MPI
   Engine(MPI_Comm comm, const std::string &engine_spec);
#endif

   virtual ~Engine() { }

   /**
       @name KERNELS specific interface, used by other objects in the KERNELS backend
    */
   ///@{
   kernels::device GetDevice(int idx = 0) const { return *dev; }

   ///@}
   // End: KERNELS specific interface

   /**
       @name Virtual interface: finite element data structures and algorithms
    */
   ///@{

   virtual DLayout MakeLayout(std::size_t size) const;

   virtual DLayout MakeLayout(const mfem::Array<std::size_t> &offsets) const;

   virtual DArray MakeArray(PLayout &layout, std::size_t item_size) const;

   virtual DVector MakeVector(PLayout &layout,
                              int type_id = ScalarId<double>::value) const;

   /*#ifdef MFEM_USE_MPI
      virtual DFiniteElementSpace MakeFESpace(mfem::ParFiniteElementSpace &) const;
      #else*/
   virtual DFiniteElementSpace MakeFESpace(mfem::FiniteElementSpace &) const;
   //#endif

   virtual DBilinearForm MakeBilinearForm(mfem::BilinearForm &bf) const;

   /// FIXME - What will the actual parameters be?
   virtual void AssembleLinearForm(LinearForm &l_form) const;

   /// FIXME - What will the actual parameters be?
   virtual mfem::Operator *MakeOperator(const MixedBilinearForm &mbl_form) const;

   /// FIXME - What will the actual parameters be?
   virtual mfem::Operator *MakeOperator(const NonlinearForm &nl_form) const;

   ///@}
   // End: Virtual interface
};

} // namespace mfem::kernels

} // namespace mfem

#endif // defined(MFEM_USE_BACKENDS) && defined(MFEM_USE_KERNELS)

#endif // MFEM_BACKENDS_KERNELS_ENGINE_HPP