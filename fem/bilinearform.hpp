// Copyright (c) 2010-2023, Lawrence Livermore National Security, LLC. Produced
// at the Lawrence Livermore National Laboratory. All Rights reserved. See files
// LICENSE and NOTICE for details. LLNL-CODE-806117.
//
// This file is part of the MFEM library. For more information and source code
// availability visit https://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the BSD-3 license. We welcome feedback and contributions, see file
// CONTRIBUTING.md for details.

#ifndef MFEM_BILINEARFORM
#define MFEM_BILINEARFORM

#include "../config/config.hpp"
#include "../linalg/linalg.hpp"
#include "fespace.hpp"
#include "gridfunc.hpp"
#include "linearform.hpp"
#include "bilininteg.hpp"
#include "bilinearform_ext.hpp"
#include "staticcond.hpp"
#include "hybridization.hpp"

namespace mfem
{

/** @brief Enumeration defining the assembly level for bilinear and nonlinear
    form classes derived from Operator. For more details, see
    https://mfem.org/howto/assembly_levels */
enum class AssemblyLevel
{
   /// In the case of a BilinearForm LEGACY corresponds to a fully assembled
   /// form, i.e. a global sparse matrix in MFEM, Hypre or PETSC format.
   /// In the case of a NonlinearForm LEGACY corresponds to an operator that
   /// is fully evaluated on the fly.
   /// This assembly level is ALWAYS performed on the host.
   LEGACY = 0,
   /// @deprecated Use LEGACY instead.
   LEGACYFULL = 0,
   /// Fully assembled form, i.e. a global sparse matrix in MFEM format. This
   /// assembly is compatible with device execution.
   FULL,
   /// Form assembled at element level, which computes and stores dense element
   /// matrices.
   ELEMENT,
   /// Partially-assembled form, which computes and stores data only at
   /// quadrature points.
   PARTIAL,
   /// "Matrix-free" form that computes all of its action on-the-fly without any
   /// substantial storage.
   NONE,
};


/** @brief A "square matrix" operator for the associated FE space and
    BLFIntegrators The sum of all the BLFIntegrators can be used form the matrix
    M. This class also supports other assembly levels specified via the
    SetAssemblyLevel() function. */
class BilinearForm : public Matrix
{
   friend FABilinearFormExtension;

protected:
   /// Sparse matrix \f$ M \f$ to be associated with the form. Owned.
   SparseMatrix *mat;

   /** @brief Sparse Matrix \f$ M_e \f$ used to store the eliminations
        from the b.c.  Owned.
       \f$ M + M_e = M_{original} \f$ */
   SparseMatrix *mat_e;

   /// FE space on which the form lives. Not owned.
   FiniteElementSpace *fes;

   /// The assembly level of the form (full, partial, etc.)
   AssemblyLevel assembly;
   /// Element batch size used in the form action (1, 8, num_elems, etc.)
   int batch;
   /** @brief Extension for supporting Full Assembly (FA), Element Assembly (EA),
       Partial Assembly (PA), or Matrix Free assembly (MF). */
   BilinearFormExtension *ext;
   /** Indicates if the sparse matrix is sorted after assembly when using
       Full Assembly (FA). */
   bool sort_sparse_matrix = false;

   /** @brief Indicates the Mesh::sequence corresponding to the current state of
       the BilinearForm. */
   long sequence;

   /** @brief Indicates the BilinearFormIntegrator%s stored in #domain_integs,
       #boundary_integs, #interior_face_integs, and #boundary_face_integs are
       owned by another BilinearForm. */
   int extern_bfs;

   /// Set of Domain Integrators to be applied.
   Array<BilinearFormIntegrator*> domain_integs;
   /// Element attribute marker (should be of length mesh->attributes.Max() or
   /// 0 if mesh->attributes is empty)
   /// Includes all by default.
   /// 0 - ignore attribute
   /// 1 - include attribute
   Array<Array<int>*> domain_integs_marker; ///< Entries are not owned.

   /// Set of Boundary Integrators to be applied.
   Array<BilinearFormIntegrator*> boundary_integs;
   Array<Array<int>*> boundary_integs_marker; ///< Entries are not owned.

   /// Set of interior face Integrators to be applied.
   Array<BilinearFormIntegrator*> interior_face_integs;

   /// Set of boundary face Integrators to be applied.
   Array<BilinearFormIntegrator*> boundary_face_integs;
   Array<Array<int>*> boundary_face_integs_marker; ///< Entries are not owned.

   DenseMatrix elemmat;
   Array<int>  vdofs;

   DenseTensor *element_matrices; ///< Owned.

   StaticCondensation *static_cond; ///< Owned.
   Hybridization *hybridization; ///< Owned.

   /** This data member allows one to specify what should be done to the
       diagonal matrix entries and corresponding RHS values upon elimination of
       the constrained DoFs. */
   DiagonalPolicy diag_policy;

   int precompute_sparsity;
   // Allocate appropriate SparseMatrix and assign it to mat
   void AllocMat();

   void ConformingAssemble();

   // may be used in the construction of derived classes
   BilinearForm() : Matrix (0)
   {
      fes = NULL; sequence = -1;
      mat = mat_e = NULL; extern_bfs = 0; element_matrices = NULL;
      static_cond = NULL; hybridization = NULL;
      precompute_sparsity = 0;
      diag_policy = DIAG_KEEP;
      assembly = AssemblyLevel::LEGACY;
      batch = 1;
      ext = NULL;
   }

private:
   /// Copy construction is not supported; body is undefined.
   BilinearForm(const BilinearForm &);

   /// Copy assignment is not supported; body is undefined.
   BilinearForm &operator=(const BilinearForm &);

public:
   /// Creates bilinear form associated with FE space @a *f.
   /** The pointer @a f is not owned by the newly constructed object. */
   BilinearForm(FiniteElementSpace *f);

   /** @brief Create a BilinearForm on the FiniteElementSpace @a f, using the
       same integrators as the BilinearForm @a bf.

       The pointer @a f is not owned by the newly constructed object.

       The integrators in @a bf are copied as pointers and they are not owned by
       the newly constructed BilinearForm.

       The optional parameter @a ps is used to initialize the internal flag
       #precompute_sparsity, see UsePrecomputedSparsity() for details. */
   BilinearForm(FiniteElementSpace *f, BilinearForm *bf, int ps = 0);

   /// Get the size of the BilinearForm as a square matrix.
   int Size() const { return height; }

   /// Set the desired assembly level.
   /** Valid choices are:

       - AssemblyLevel::LEGACY (default)
       - AssemblyLevel::FULL
       - AssemblyLevel::PARTIAL
       - AssemblyLevel::ELEMENT
       - AssemblyLevel::NONE

       If used, this method must be called before assembly. */
   void SetAssemblyLevel(AssemblyLevel assembly_level);

   /** @brief Force the sparse matrix column indices to be sorted when using
       AssemblyLevel::FULL.

       When assembling on device the assembly algorithm uses atomic operations
       to insert values in the sparse matrix, which can result in different
       column index orderings across runs. Calling this method with @a enable_it
       set to @a true forces a sorting algorithm to be called at the end of the
       assembly procedure to ensure sorted column indices (and therefore
       deterministic results).
   */
   void EnableSparseMatrixSorting(bool enable_it)
   {
      sort_sparse_matrix = enable_it;
   }

   /// Returns the assembly level
   AssemblyLevel GetAssemblyLevel() const { return assembly; }

   Hybridization *GetHybridization() const { return hybridization; }

   /** @brief Enable the use of static condensation. For details see the
       description for class StaticCondensation in fem/staticcond.hpp This method
       should be called before assembly. If the number of unknowns after static
       condensation is not reduced, it is not enabled. */
   void EnableStaticCondensation();

   /** @brief Check if static condensation was actually enabled by a previous
       call to EnableStaticCondensation(). */
   bool StaticCondensationIsEnabled() const { return static_cond; }

   /// Return the trace FE space associated with static condensation.
   FiniteElementSpace *SCFESpace() const
   { return static_cond ? static_cond->GetTraceFESpace() : NULL; }

   /// Enable hybridization.
   /** For details see the description for class
       Hybridization in fem/hybridization.hpp. This method should be called
       before assembly. */
   void EnableHybridization(FiniteElementSpace *constr_space,
                            BilinearFormIntegrator *constr_integ,
                            const Array<int> &ess_tdof_list);

   /** @brief For scalar FE spaces, precompute the sparsity pattern of the matrix
       (assuming dense element matrices) based on the types of integrators
       present in the bilinear form. */
   void UsePrecomputedSparsity(int ps = 1) { precompute_sparsity = ps; }

   /** @brief Use the given CSR sparsity pattern to allocate the internal
       SparseMatrix.

       - The @a I and @a J arrays must define a square graph with size equal to
         GetVSize() of the associated FiniteElementSpace.
       - This method should be called after enabling static condensation or
         hybridization, if used.
       - In the case of static condensation, @a I and @a J are not used.
       - The ownership of the arrays @a I and @a J remains with the caller. */
   void UseSparsity(int *I, int *J, bool isSorted);

   /// Use the sparsity of @a A to allocate the internal SparseMatrix.
   void UseSparsity(SparseMatrix &A);

   /// Pre-allocate the internal SparseMatrix before assembly.
   /**  If the flag 'precompute sparsity'
       is set, the matrix is allocated in CSR format (i.e.
       finalized) and the entries are initialized with zeros. */
   void AllocateMatrix() { if (mat == NULL) { AllocMat(); } }

   /// Access all the integrators added with AddDomainIntegrator().
   Array<BilinearFormIntegrator*> *GetDBFI() { return &domain_integs; }

   /// @brief Access all boundary markers added with AddDomainIntegrator().
   ///
   /// If no marker was specified when the integrator was added, the
   /// corresponding pointer (to Array<int>) will be NULL. */
   Array<Array<int>*> *GetDBFI_Marker() { return &domain_integs_marker; }

   /// Access all the integrators added with AddBoundaryIntegrator().
   Array<BilinearFormIntegrator*> *GetBBFI() { return &boundary_integs; }
   /** @brief Access all boundary markers added with AddBoundaryIntegrator().
       If no marker was specified when the integrator was added, the
       corresponding pointer (to Array<int>) will be NULL. */
   Array<Array<int>*> *GetBBFI_Marker() { return &boundary_integs_marker; }

   /// Access all integrators added with AddInteriorFaceIntegrator().
   Array<BilinearFormIntegrator*> *GetFBFI() { return &interior_face_integs; }

   /// Access all integrators added with AddBdrFaceIntegrator().
   Array<BilinearFormIntegrator*> *GetBFBFI() { return &boundary_face_integs; }
   /** @brief Access all boundary markers added with AddBdrFaceIntegrator().
       If no marker was specified when the integrator was added, the
       corresponding pointer (to Array<int>) will be NULL. */
   Array<Array<int>*> *GetBFBFI_Marker()
   { return &boundary_face_integs_marker; }

   /// Returns a reference to: \f$ M_{ij} \f$
   const double &operator()(int i, int j) { return (*mat)(i,j); }

   /// Returns a reference to: \f$ M_{ij} \f$
   virtual double &Elem(int i, int j);

   /// Returns constant reference to: \f$ M_{ij} \f$
   virtual const double &Elem(int i, int j) const;

   /// Matrix vector multiplication:  \f$ y = M x \f$
   virtual void Mult(const Vector &x, Vector &y) const;

   /** @brief Matrix vector multiplication with the original uneliminated
       matrix.  The original matrix is \f$ M + M_e \f$ so we have:
       \f$ y = M x + M_e x \f$ */
   void FullMult(const Vector &x, Vector &y) const
   { mat->Mult(x, y); mat_e->AddMult(x, y); }

   /// Add the matrix vector multiple to a vector:  \f$ y += a M x \f$
   virtual void AddMult(const Vector &x, Vector &y, const double a = 1.0) const
   { mat -> AddMult (x, y, a); }

   /** @brief Add the original uneliminated matrix vector multiple to a vector.
       The original matrix is \f$ M + Me \f$ so we have:
       \f$ y += M x + M_e x \f$ */
   void FullAddMult(const Vector &x, Vector &y) const
   { mat->AddMult(x, y); mat_e->AddMult(x, y); }

   /// Add the matrix transpose vector multiplication:  \f$ y += a M^T x \f$
   virtual void AddMultTranspose(const Vector & x, Vector & y,
                                 const double a = 1.0) const
   { mat->AddMultTranspose(x, y, a); }

   /** @brief Add the original uneliminated matrix transpose vector
       multiple to a vector. The original matrix is \f$ M + M_e \f$
       so we have: \f$ y += M^T x + {M_e}^T x \f$ */
   void FullAddMultTranspose(const Vector & x, Vector & y) const
   { mat->AddMultTranspose(x, y); mat_e->AddMultTranspose(x, y); }

   /// Matrix transpose vector multiplication:  \f$ y = M^T x \f$
   virtual void MultTranspose(const Vector & x, Vector & y) const;

   /// Compute \f$ y^T M x \f$
   double InnerProduct(const Vector &x, const Vector &y) const
   { return mat->InnerProduct (x, y); }

   /// Returns a pointer to (approximation) of the matrix inverse:  \f$ M^{-1} \f$
   virtual MatrixInverse *Inverse() const;

   /// Finalizes the matrix initialization.
   virtual void Finalize(int skip_zeros = 1);

   /** @brief Returns a const reference to the sparse matrix:  \f$ M \f$

       This will fail if HasSpMat() is false. */
   const SparseMatrix &SpMat() const
   {
      MFEM_VERIFY(mat, "mat is NULL and can't be dereferenced");
      return *mat;
   }

   /** @brief Returns a reference to the sparse matrix:  \f$ M \f$

       This will fail if HasSpMat() is false. */
   SparseMatrix &SpMat()
   {
      MFEM_VERIFY(mat, "mat is NULL and can't be dereferenced");
      return *mat;
   }

   /** @brief Returns true if the sparse matrix is not null, false otherwise.

       @sa SpMat(). */
   bool HasSpMat()
   {
      return mat != nullptr;
   }


   /**  @brief Nullifies the internal matrix \f$ M \f$ and returns a pointer
        to it.  Used for transferring ownership. */
   SparseMatrix *LoseMat() { SparseMatrix *tmp = mat; mat = NULL; return tmp; }

   /** @brief Returns a const reference to the sparse matrix of eliminated b.c.:
       \f$ M_e \f$

       This will fail if HasSpMatElim() is false. */
   const SparseMatrix &SpMatElim() const
   {
      MFEM_VERIFY(mat_e, "mat_e is NULL and can't be dereferenced");
      return *mat_e;
   }

   /** @brief Returns a reference to the sparse matrix of eliminated b.c.:
       \f$ M_e \f$

       This will fail if HasSpMatElim() is false. */
   SparseMatrix &SpMatElim()
   {
      MFEM_VERIFY(mat_e, "mat_e is NULL and can't be dereferenced");
      return *mat_e;
   }

   /**  @brief Returns true if the sparse matrix of eliminated b.c.s is not null,
        false otherwise.

        @sa SpMatElim(). */
   bool HasSpMatElim()
   {
      return mat_e != nullptr;
   }

   /// Adds new Domain Integrator. Assumes ownership of @a bfi.
   void AddDomainIntegrator(BilinearFormIntegrator *bfi);
   /// Adds new Domain Integrator restricted to certain elements specified by
   /// the @a elem_attr_marker.
   void AddDomainIntegrator(BilinearFormIntegrator *bfi,
                            Array<int> &elem_marker);

   /// Adds new Boundary Integrator. Assumes ownership of @a bfi.
   void AddBoundaryIntegrator(BilinearFormIntegrator *bfi);

   /** @brief Adds new Boundary Integrator, restricted to specific boundary
       attributes.

       Assumes ownership of @a bfi. The array @a bdr_marker is stored internally
       as a pointer to the given Array<int> object. */
   void AddBoundaryIntegrator(BilinearFormIntegrator *bfi,
                              Array<int> &bdr_marker);

   /// Adds new interior Face Integrator. Assumes ownership of @a bfi.
   void AddInteriorFaceIntegrator(BilinearFormIntegrator *bfi);

   /// Adds new boundary Face Integrator. Assumes ownership of @a bfi.
   void AddBdrFaceIntegrator(BilinearFormIntegrator *bfi);

   /** @brief Adds new boundary Face Integrator, restricted to specific boundary
       attributes.

       Assumes ownership of @a bfi. The array @a bdr_marker is stored internally
       as a pointer to the given Array<int> object. */
   void AddBdrFaceIntegrator(BilinearFormIntegrator *bfi,
                             Array<int> &bdr_marker);

   /// Sets all sparse values of \f$ M \f$ and \f$ M_e \f$ to 'a'.
   void operator=(const double a)
   {
      if (mat != NULL) { *mat = a; }
      if (mat_e != NULL) { *mat_e = a; }
   }

   /// Assembles the form i.e. sums over all domain/bdr integrators.
   void Assemble(int skip_zeros = 1);

   /** @brief Assemble the diagonal of the bilinear form into @a diag. Note that
       @a diag is a tdof Vector.

       When the AssemblyLevel is not LEGACY, and the mesh has hanging nodes,
       this method returns |P^T| d_l, where d_l is the diagonal of the form
       before applying conforming assembly, P^T is the transpose of the
       conforming prolongation, and |.| denotes the entry-wise absolute value.
       In general, this is just an approximation of the exact diagonal for this
       case. */
   virtual void AssembleDiagonal(Vector &diag) const;

   /// Get the finite element space prolongation operator.
   virtual const Operator *GetProlongation() const
   { return fes->GetConformingProlongation(); }
   /// Get the finite element space restriction operator
   virtual const Operator *GetRestriction() const
   { return fes->GetConformingRestriction(); }
   /// Get the output finite element space prolongation matrix
   virtual const Operator *GetOutputProlongation() const
   { return GetProlongation(); }
   /** @brief Returns the output fe space restriction matrix, transposed

       Logically, this is the transpose of GetOutputRestriction, but in
       practice it is convenient to have it in transposed form for
       construction of RAP operators in matrix-free methods. */
   virtual const Operator *GetOutputRestrictionTranspose() const
   { return fes->GetRestrictionTransposeOperator(); }
   /// Get the output finite element space restriction matrix
   virtual const Operator *GetOutputRestriction() const
   { return GetRestriction(); }

   /// @brief Compute serial RAP operator and store it in @a A as a SparseMatrix.
   void SerialRAP(OperatorHandle &A)
   {
      MFEM_ASSERT(mat, "SerialRAP requires the SparseMatrix to be assembled.");
      ConformingAssemble();
      A.Reset(mat, false);
   }

   /** @brief Form the linear system A X = B, corresponding to this bilinear
       form and the linear form @a b(.). */
   /** This method applies any necessary transformations to the linear system
       such as: eliminating boundary conditions; applying conforming constraints
       for non-conforming AMR; parallel assembly; static condensation;
       hybridization.

       The GridFunction-size vector @a x must contain the essential b.c. The
       BilinearForm and the LinearForm-size vector @a b must be assembled.

       The vector @a X is initialized with a suitable initial guess: when using
       hybridization, the vector @a X is set to zero; otherwise, the essential
       entries of @a X are set to the corresponding b.c. and all other entries
       are set to zero (@a copy_interior == 0) or copied from @a x
       (@a copy_interior != 0).

       This method can be called multiple times (with the same @a ess_tdof_list
       array) to initialize different right-hand sides and boundary condition
       values.

       After solving the linear system, the finite element solution @a x can be
       recovered by calling RecoverFEMSolution() (with the same vectors @a X,
       @a b, and @a x).

       NOTE: If there are no transformations, @a X simply reuses the data of
             @a x. */
   virtual void FormLinearSystem(const Array<int> &ess_tdof_list, Vector &x,
                                 Vector &b, OperatorHandle &A, Vector &X,
                                 Vector &B, int copy_interior = 0);

   /** @brief Form the linear system A X = B, corresponding to this bilinear
       form and the linear form @a b(.). */
   /** Version of the method FormLinearSystem() where the system matrix is
       returned in the variable @a A, of type OpType, holding a *reference* to
       the system matrix (created with the method OpType::MakeRef()). The
       reference will be invalidated when SetOperatorType(), Update(), or the
       destructor is called. */
   template <typename OpType>
   void FormLinearSystem(const Array<int> &ess_tdof_list, Vector &x, Vector &b,
                         OpType &A, Vector &X, Vector &B,
                         int copy_interior = 0)
   {
      OperatorHandle Ah;
      FormLinearSystem(ess_tdof_list, x, b, Ah, X, B, copy_interior);
      OpType *A_ptr = Ah.Is<OpType>();
      MFEM_VERIFY(A_ptr, "invalid OpType used");
      A.MakeRef(*A_ptr);
   }

   /// Form the linear system matrix @a A, see FormLinearSystem() for details.
   virtual void FormSystemMatrix(const Array<int> &ess_tdof_list,
                                 OperatorHandle &A);

   /// Form the linear system matrix A, see FormLinearSystem() for details.
   /** Version of the method FormSystemMatrix() where the system matrix is
       returned in the variable @a A, of type OpType, holding a *reference* to
       the system matrix (created with the method OpType::MakeRef()). The
       reference will be invalidated when SetOperatorType(), Update(), or the
       destructor is called. */
   template <typename OpType>
   void FormSystemMatrix(const Array<int> &ess_tdof_list, OpType &A)
   {
      OperatorHandle Ah;
      FormSystemMatrix(ess_tdof_list, Ah);
      OpType *A_ptr = Ah.Is<OpType>();
      MFEM_VERIFY(A_ptr, "invalid OpType used");
      A.MakeRef(*A_ptr);
   }

   /// Recover the solution of a linear system formed with FormLinearSystem().
   /** Call this method after solving a linear system constructed using the
       FormLinearSystem() method to recover the solution as a GridFunction-size
       vector in @a x. Use the same arguments as in the FormLinearSystem() call.
   */
   virtual void RecoverFEMSolution(const Vector &X, const Vector &b, Vector &x);

   /// Compute and store internally all element matrices.
   void ComputeElementMatrices();

   /// Free the memory used by the element matrices.
   void FreeElementMatrices()
   { delete element_matrices; element_matrices = NULL; }

   /// Compute the element matrix of the given element
   /** The element matrix is computed by calling the domain integrators
       or the one stored internally by a prior call of ComputeElementMatrices()
       is returned when available.
   */
   void ComputeElementMatrix(int i, DenseMatrix &elmat);

   /// Compute the boundary element matrix of the given boundary element
   void ComputeBdrElementMatrix(int i, DenseMatrix &elmat);

   /// Assemble the given element matrix
   /** The element matrix @a elmat is assembled for the element @a i, i.e.
       added to the system matrix. The flag @a skip_zeros skips the zero
       elements of the matrix, unless they are breaking the symmetry of
       the system matrix.
   */
   void AssembleElementMatrix(int i, const DenseMatrix &elmat,
                              int skip_zeros = 1);

   /// Assemble the given element matrix
   /** The element matrix @a elmat is assembled for the element @a i, i.e.
       added to the system matrix. The vdofs of the element are returned
       in @a vdofs. The flag @a skip_zeros skips the zero elements of the
       matrix, unless they are breaking the symmetry of the system matrix.
   */
   void AssembleElementMatrix(int i, const DenseMatrix &elmat,
                              Array<int> &vdofs, int skip_zeros = 1);

   /// Assemble the given boundary element matrix
   /** The boundary element matrix @a elmat is assembled for the boundary
       element @a i, i.e. added to the system matrix. The flag @a skip_zeros
       skips the zero elements of the matrix, unless they are breaking the
       symmetry of the system matrix.
   */
   void AssembleBdrElementMatrix(int i, const DenseMatrix &elmat,
                                 int skip_zeros = 1);

   /// Assemble the given boundary element matrix
   /** The boundary element matrix @a elmat is assembled for the boundary
       element @a i, i.e. added to the system matrix. The vdofs of the element
       are returned in @a vdofs. The flag @a skip_zeros skips the zero elements
       of the matrix, unless they are breaking the symmetry of the system matrix.
   */
   void AssembleBdrElementMatrix(int i, const DenseMatrix &elmat,
                                 Array<int> &vdofs, int skip_zeros = 1);

   /// Eliminate essential boundary DOFs from the system.
   /** The array @a bdr_attr_is_ess marks boundary attributes that constitute
       the essential part of the boundary. By default, the diagonal at the
       essential DOFs is set to 1.0. This behavior is controlled by the argument
       @a dpolicy. */
   void EliminateEssentialBC(const Array<int> &bdr_attr_is_ess,
                             const Vector &sol, Vector &rhs,
                             DiagonalPolicy dpolicy = DIAG_ONE);

   /// Eliminate essential boundary DOFs from the system matrix.
   void EliminateEssentialBC(const Array<int> &bdr_attr_is_ess,
                             DiagonalPolicy dpolicy = DIAG_ONE);
   /// Perform elimination and set the diagonal entry to the given value
   void EliminateEssentialBCDiag(const Array<int> &bdr_attr_is_ess,
                                 double value);

   /// Eliminate the given @a vdofs. NOTE: here, @a vdofs is a list of DOFs.
   /** In this case the eliminations are applied to the internal \f$ M \f$
       and @a rhs without storing the elimination matrix \f$ M_e \f$. */
   void EliminateVDofs(const Array<int> &vdofs, const Vector &sol, Vector &rhs,
                       DiagonalPolicy dpolicy = DIAG_ONE);

   /// Eliminate the given @a vdofs, storing the eliminated part internally in \f$ M_e \f$.
   /** This method works in conjunction with EliminateVDofsInRHS() and allows
       elimination of boundary conditions in multiple right-hand sides. In this
       method, @a vdofs is a list of DOFs. */
   void EliminateVDofs(const Array<int> &vdofs,
                       DiagonalPolicy dpolicy = DIAG_ONE);

   /** @brief Similar to
       EliminateVDofs(const Array<int> &, const Vector &, Vector &, DiagonalPolicy)
       but here @a ess_dofs is a marker (boolean) array on all vector-dofs
       (@a ess_dofs[i] < 0 is true). */
   void EliminateEssentialBCFromDofs(const Array<int> &ess_dofs, const Vector &sol,
                                     Vector &rhs, DiagonalPolicy dpolicy = DIAG_ONE);

   /** @brief Similar to EliminateVDofs(const Array<int> &, DiagonalPolicy) but
       here @a ess_dofs is a marker (boolean) array on all vector-dofs
       (@a ess_dofs[i] < 0 is true). */
   void EliminateEssentialBCFromDofs(const Array<int> &ess_dofs,
                                     DiagonalPolicy dpolicy = DIAG_ONE);
   /// Perform elimination and set the diagonal entry to the given value
   void EliminateEssentialBCFromDofsDiag(const Array<int> &ess_dofs,
                                         double value);

   /** @brief Use the stored eliminated part of the matrix (see
       EliminateVDofs(const Array<int> &, DiagonalPolicy)) to modify the r.h.s.
       @a b; @a vdofs is a list of DOFs (non-directional, i.e. >= 0). */
   void EliminateVDofsInRHS(const Array<int> &vdofs, const Vector &x,
                            Vector &b);

   /// Compute inner product for full uneliminated matrix \f$ y^T M x + y^T M_e x \f$
   double FullInnerProduct(const Vector &x, const Vector &y) const
   { return mat->InnerProduct(x, y) + mat_e->InnerProduct(x, y); }

   /// Update the @a FiniteElementSpace and delete all data associated with the old one.
   virtual void Update(FiniteElementSpace *nfes = NULL);

   /// (DEPRECATED) Return the FE space associated with the BilinearForm.
   /** @deprecated Use FESpace() instead. */
   MFEM_DEPRECATED FiniteElementSpace *GetFES() { return fes; }

   /// Return the FE space associated with the BilinearForm.
   FiniteElementSpace *FESpace() { return fes; }
   /// Read-only access to the associated FiniteElementSpace.
   const FiniteElementSpace *FESpace() const { return fes; }

   /// Sets diagonal policy used upon construction of the linear system.
   /** Policies include:

       - DIAG_ZERO (Set the diagonal values to zero)
       - DIAG_ONE  (Set the diagonal values to one)
       - DIAG_KEEP (Keep the diagonal values)
   */
   void SetDiagonalPolicy(DiagonalPolicy policy);

   /// Indicate that integrators are not owned by the BilinearForm
   void UseExternalIntegrators() { extern_bfs = 1; }

   /// Destroys bilinear form.
   virtual ~BilinearForm();
};


/**
   Class for assembling of bilinear forms `a(u,v)` defined on different
   trial and test spaces. The assembled matrix `M` is such that

       a(u,v) = V^t M U

   where `U` and `V` are the vectors representing the functions `u` and `v`,
   respectively.  The first argument, `u`, of `a(,)` is in the trial space
   and the second argument, `v`, is in the test space. Thus,

       # of rows of M = dimension of the test space and
       # of cols of M = dimension of the trial space.

   Both trial and test spaces should be defined on the same mesh.
*/
class MixedBilinearForm : public Matrix
{
protected:
   SparseMatrix *mat; ///< Owned.
   SparseMatrix *mat_e; ///< Owned.

   FiniteElementSpace *trial_fes, ///< Not owned
                      *test_fes;  ///< Not owned

   /// The form assembly level (full, partial, etc.)
   AssemblyLevel assembly;

   /** Extension for supporting Full Assembly (FA), Element Assembly (EA),
       Partial Assembly (PA), or Matrix Free assembly (MF). */
   MixedBilinearFormExtension *ext;

   /** @brief Indicates the BilinearFormIntegrator%s stored in #domain_integs,
       #boundary_integs, #trace_face_integs and #boundary_trace_face_integs
       are owned by another MixedBilinearForm. */
   int extern_bfs;

   /// Domain integrators.
   Array<BilinearFormIntegrator*> domain_integs;
   /// Entries are not owned.
   Array<Array<int>*> domain_integs_marker;

   /// Boundary integrators.
   Array<BilinearFormIntegrator*> boundary_integs;
   /// Entries are not owned.
   Array<Array<int>*> boundary_integs_marker;

   /// Trace face (skeleton) integrators.
   Array<BilinearFormIntegrator*> trace_face_integs;

   /// Boundary trace face (skeleton) integrators.
   Array<BilinearFormIntegrator*> boundary_trace_face_integs;
   /// Entries are not owned.
   Array<Array<int>*> boundary_trace_face_integs_marker;

   DenseMatrix elemmat;
   Array<int>  trial_vdofs, test_vdofs;

private:
   /// Copy construction is not supported; body is undefined.
   MixedBilinearForm(const MixedBilinearForm &);

   /// Copy assignment is not supported; body is undefined.
   MixedBilinearForm &operator=(const MixedBilinearForm &);

public:
   /** @brief Construct a MixedBilinearForm on the given trial, @a tr_fes, and
       test, @a te_fes, FiniteElementSpace%s. */
   /** The pointers @a tr_fes and @a te_fes are not owned by the newly
       constructed object. */
   MixedBilinearForm(FiniteElementSpace *tr_fes,
                     FiniteElementSpace *te_fes);

   /** @brief Create a MixedBilinearForm on the given trial, @a tr_fes, and
       test, @a te_fes, FiniteElementSpace%s, using the same integrators as the
       MixedBilinearForm @a mbf.

       The pointers @a tr_fes and @a te_fes are not owned by the newly
       constructed object.

       The integrators in @a mbf are copied as pointers and they are not owned
       by the newly constructed MixedBilinearForm. */
   MixedBilinearForm(FiniteElementSpace *tr_fes,
                     FiniteElementSpace *te_fes,
                     MixedBilinearForm *mbf);

   /// Returns a reference to: \f$ M_{ij} \f$
   virtual double &Elem(int i, int j);

   /// Returns a reference to: \f$ M_{ij} \f$
   virtual const double &Elem(int i, int j) const;

   /// Matrix multiplication: \f$ y = M x \f$
   virtual void Mult(const Vector & x, Vector & y) const;

   virtual void AddMult(const Vector & x, Vector & y,
                        const double a = 1.0) const;

   virtual void MultTranspose(const Vector & x, Vector & y) const;
   virtual void AddMultTranspose(const Vector & x, Vector & y,
                                 const double a = 1.0) const;

   virtual MatrixInverse *Inverse() const;

   /// Finalizes the matrix initialization.
   virtual void Finalize(int skip_zeros = 1);

   /** Extract the associated matrix as SparseMatrix blocks. The number of
       block rows and columns is given by the vector dimensions (vdim) of the
       test and trial spaces, respectively. */
   void GetBlocks(Array2D<SparseMatrix *> &blocks) const;

   /// Returns a const reference to the sparse matrix:  \f$ M \f$
   const SparseMatrix &SpMat() const { return *mat; }

   /// Returns a reference to the sparse matrix:  \f$ M \f$
   SparseMatrix &SpMat() { return *mat; }

   /**  @brief Nullifies the internal matrix \f$ M \f$ and returns a pointer
        to it.  Used for transferring ownership. */
   SparseMatrix *LoseMat() { SparseMatrix *tmp = mat; mat = NULL; return tmp; }

   /// Adds a domain integrator. Assumes ownership of @a bfi.
   void AddDomainIntegrator(BilinearFormIntegrator *bfi);

   /// Adds a domain integrator. Assumes ownership of @a bfi.
   void AddDomainIntegrator(BilinearFormIntegrator *bfi,
                            Array<int> &elem_marker);

   /// Adds a boundary integrator. Assumes ownership of @a bfi.
   void AddBoundaryIntegrator(BilinearFormIntegrator *bfi);

   /// Adds a boundary integrator. Assumes ownership of @a bfi.
   void AddBoundaryIntegrator(BilinearFormIntegrator * bfi,
                              Array<int> &bdr_marker);

   /** @brief Add a trace face integrator. Assumes ownership of @a bfi.

       This type of integrator assembles terms over all faces of the mesh using
       the face FE from the trial space and the two adjacent volume FEs from the
       test space. */
   void AddTraceFaceIntegrator(BilinearFormIntegrator *bfi);

   /// Adds a boundary trace face integrator. Assumes ownership of @a bfi.
   void AddBdrTraceFaceIntegrator(BilinearFormIntegrator * bfi);

   /// Adds a boundary trace face integrator. Assumes ownership of @a bfi.
   void AddBdrTraceFaceIntegrator(BilinearFormIntegrator * bfi,
                                  Array<int> &bdr_marker);

   /// Access all integrators added with AddDomainIntegrator().
   Array<BilinearFormIntegrator*> *GetDBFI() { return &domain_integs; }
   /** @brief Access all domain markers added with AddDomainIntegrator().
       If no marker was specified when the integrator was added, the
       corresponding pointer (to Array<int>) will be NULL. */
   Array<Array<int>*> *GetDBFI_Marker() { return &domain_integs_marker; }

   /// Access all integrators added with AddBoundaryIntegrator().
   Array<BilinearFormIntegrator*> *GetBBFI() { return &boundary_integs; }
   /** @brief Access all boundary markers added with AddBoundaryIntegrator().
       If no marker was specified when the integrator was added, the
       corresponding pointer (to Array<int>) will be NULL. */
   Array<Array<int>*> *GetBBFI_Marker() { return &boundary_integs_marker; }

   /// Access all integrators added with AddTraceFaceIntegrator().
   Array<BilinearFormIntegrator*> *GetTFBFI() { return &trace_face_integs; }

   /// Access all integrators added with AddBdrTraceFaceIntegrator().
   Array<BilinearFormIntegrator*> *GetBTFBFI()
   { return &boundary_trace_face_integs; }
   /** @brief Access all boundary markers added with AddBdrTraceFaceIntegrator().
       If no marker was specified when the integrator was added, the
       corresponding pointer (to Array<int>) will be NULL. */
   Array<Array<int>*> *GetBTFBFI_Marker()
   { return &boundary_trace_face_integs_marker; }

   /// Sets all sparse values of \f$ M \f$ to @a a.
   void operator=(const double a) { *mat = a; }

   /// Set the desired assembly level. The default is AssemblyLevel::LEGACY.
   /** This method must be called before assembly. */
   void SetAssemblyLevel(AssemblyLevel assembly_level);

   void Assemble(int skip_zeros = 1);

   /** @brief Assemble the diagonal of ADA^T into diag, where A is this mixed
       bilinear form and D is a diagonal. */
   void AssembleDiagonal_ADAt(const Vector &D, Vector &diag) const;

   /// Get the input finite element space prolongation matrix
   virtual const Operator *GetProlongation() const
   { return trial_fes->GetProlongationMatrix(); }

   /// Get the input finite element space restriction matrix
   virtual const Operator *GetRestriction() const
   { return trial_fes->GetRestrictionMatrix(); }

   /// Get the test finite element space prolongation matrix
   virtual const Operator *GetOutputProlongation() const
   { return test_fes->GetProlongationMatrix(); }

   /// Get the test finite element space restriction matrix
   virtual const Operator *GetOutputRestriction() const
   { return test_fes->GetRestrictionMatrix(); }

   /** For partially conforming trial and/or test FE spaces, complete the
       assembly process by performing A := P2^t A P1 where A is the internal
       sparse matrix; P1 and P2 are the conforming prolongation matrices of the
       trial and test FE spaces, respectively. After this call the
       MixedBilinearForm becomes an operator on the conforming FE spaces. */
   void ConformingAssemble();

   /// Compute the element matrix of the given element
   void ComputeElementMatrix(int i, DenseMatrix &elmat);

   /// Compute the boundary element matrix of the given boundary element
   void ComputeBdrElementMatrix(int i, DenseMatrix &elmat);

   /// Assemble the given element matrix
   /** The element matrix @a elmat is assembled for the element @a i, i.e.
       added to the system matrix. The flag @a skip_zeros skips the zero
       elements of the matrix, unless they are breaking the symmetry of
       the system matrix.
   */
   void AssembleElementMatrix(int i, const DenseMatrix &elmat,
                              int skip_zeros = 1);

   /// Assemble the given element matrix
   /** The element matrix @a elmat is assembled for the element @a i, i.e.
       added to the system matrix. The vdofs of the element are returned
       in @a trial_vdofs and @a test_vdofs. The flag @a skip_zeros skips
       the zero elements of the matrix, unless they are breaking the symmetry
       of the system matrix.
   */
   void AssembleElementMatrix(int i, const DenseMatrix &elmat,
                              Array<int> &trial_vdofs, Array<int> &test_vdofs,
                              int skip_zeros = 1);

   /// Assemble the given boundary element matrix
   /** The boundary element matrix @a elmat is assembled for the boundary
       element @a i, i.e. added to the system matrix. The flag @a skip_zeros
       skips the zero elements of the matrix, unless they are breaking the
       symmetry of the system matrix.
   */
   void AssembleBdrElementMatrix(int i, const DenseMatrix &elmat,
                                 int skip_zeros = 1);

   /// Assemble the given boundary element matrix
   /** The boundary element matrix @a elmat is assembled for the boundary
       element @a i, i.e. added to the system matrix. The vdofs of the element
       are returned in @a trial_vdofs and @a test_vdofs. The flag @a skip_zeros
       skips the zero elements of the matrix, unless they are breaking the
       symmetry of the system matrix.
   */
   void AssembleBdrElementMatrix(int i, const DenseMatrix &elmat,
                                 Array<int> &trial_vdofs, Array<int> &test_vdofs,
                                 int skip_zeros = 1);

   void EliminateTrialDofs(const Array<int> &bdr_attr_is_ess,
                           const Vector &sol, Vector &rhs);

   void EliminateEssentialBCFromTrialDofs(const Array<int> &marked_vdofs,
                                          const Vector &sol, Vector &rhs);

   virtual void EliminateTestDofs(const Array<int> &bdr_attr_is_ess);

   /** @brief Return in @a A that is column-constrained.

      This returns the same operator as FormRectangularLinearSystem(), but does
      without the transformations of the right-hand side. */
   virtual void FormRectangularSystemMatrix(const Array<int> &trial_tdof_list,
                                            const Array<int> &test_tdof_list,
                                            OperatorHandle &A);

   /** @brief Form the column-constrained linear system matrix A.
       See FormRectangularSystemMatrix() for details.

       Version of the method FormRectangularSystemMatrix() where the system matrix is
       returned in the variable @a A, of type OpType, holding a *reference* to
       the system matrix (created with the method OpType::MakeRef()). The
       reference will be invalidated when SetOperatorType(), Update(), or the
       destructor is called. */
   template <typename OpType>
   void FormRectangularSystemMatrix(const Array<int> &trial_tdof_list,
                                    const Array<int> &test_tdof_list, OpType &A)
   {
      OperatorHandle Ah;
      FormRectangularSystemMatrix(trial_tdof_list, test_tdof_list, Ah);
      OpType *A_ptr = Ah.Is<OpType>();
      MFEM_VERIFY(A_ptr, "invalid OpType used");
      A.MakeRef(*A_ptr);
   }

   /** @brief Form the linear system A X = B, corresponding to this mixed bilinear
       form and the linear form @a b(.).

       Return in @a A a *reference* to the system matrix that is column-constrained.
       The reference will be invalidated when SetOperatorType(), Update(), or the
       destructor is called. */
   virtual void FormRectangularLinearSystem(const Array<int> &trial_tdof_list,
                                            const Array<int> &test_tdof_list,
                                            Vector &x, Vector &b,
                                            OperatorHandle &A, Vector &X,
                                            Vector &B);

   /** @brief Form the linear system A X = B, corresponding to this bilinear
       form and the linear form @a b(.).

       Version of the method FormRectangularLinearSystem() where the system matrix is
       returned in the variable @a A, of type OpType, holding a *reference* to
       the system matrix (created with the method OpType::MakeRef()). The
       reference will be invalidated when SetOperatorType(), Update(), or the
       destructor is called. */
   template <typename OpType>
   void FormRectangularLinearSystem(const Array<int> &trial_tdof_list,
                                    const Array<int> &test_tdof_list,
                                    Vector &x, Vector &b,
                                    OpType &A, Vector &X, Vector &B)
   {
      OperatorHandle Ah;
      FormRectangularLinearSystem(trial_tdof_list, test_tdof_list, x, b, Ah, X, B);
      OpType *A_ptr = Ah.Is<OpType>();
      MFEM_VERIFY(A_ptr, "invalid OpType used");
      A.MakeRef(*A_ptr);
   }

   void Update();

   /// Return the trial FE space associated with the BilinearForm.
   FiniteElementSpace *TrialFESpace() { return trial_fes; }
   /// Read-only access to the associated trial FiniteElementSpace.
   const FiniteElementSpace *TrialFESpace() const { return trial_fes; }

   /// Return the test FE space associated with the BilinearForm.
   FiniteElementSpace *TestFESpace() { return test_fes; }
   /// Read-only access to the associated test FiniteElementSpace.
   const FiniteElementSpace *TestFESpace() const { return test_fes; }

   virtual ~MixedBilinearForm();
};


/**
   Class for constructing the matrix representation of a linear operator,
   `v = L u`, from one FiniteElementSpace (domain) to another FiniteElementSpace
   (range). The constructed matrix `A` is such that

       V = A U

   where `U` and `V` are the vectors of degrees of freedom representing the
   functions `u` and `v`, respectively. The dimensions of `A` are

       number of rows of A = dimension of the range space and
       number of cols of A = dimension of the domain space.

   This class is very similar to MixedBilinearForm. One difference is that
   the linear operator `L` is defined using a special kind of
   BilinearFormIntegrator (we reuse its functionality instead of defining a
   new class). The other difference with the MixedBilinearForm class is that
   the "assembly" process overwrites the global matrix entries using the
   local element matrices instead of adding them.

   Note that if we define the bilinear form `b(u,v) := (Lu,v)` using an inner
   product in the range space, then its matrix representation, `B`, is

       B = M A, (since V^t B U = b(u,v) = (Lu,v) = V^t M A U)

   where `M` denotes the mass matrix for the inner product in the range space:
   `V1^t M V2 = (v1,v2)`. Similarly, if `c(u,w) := (Lu,Lw)` then

       C = A^t M A.
*/
class DiscreteLinearOperator : public MixedBilinearForm
{
private:
   /// Copy construction is not supported; body is undefined.
   DiscreteLinearOperator(const DiscreteLinearOperator &);

   /// Copy assignment is not supported; body is undefined.
   DiscreteLinearOperator &operator=(const DiscreteLinearOperator &);

public:
   /** @brief Construct a DiscreteLinearOperator on the given
       FiniteElementSpace%s @a domain_fes and @a range_fes. */
   /** The pointers @a domain_fes and @a range_fes are not owned by the newly
       constructed object. */
   DiscreteLinearOperator(FiniteElementSpace *domain_fes,
                          FiniteElementSpace *range_fes)
      : MixedBilinearForm(domain_fes, range_fes) { }

   /// Adds a domain interpolator. Assumes ownership of @a di.
   void AddDomainInterpolator(DiscreteInterpolator *di)
   { AddDomainIntegrator(di); }
   void AddDomainInterpolator(DiscreteInterpolator *di,
                              Array<int> &elem_marker)
   { AddDomainIntegrator(di, elem_marker); }

   /// Adds a trace face interpolator. Assumes ownership of @a di.
   void AddTraceFaceInterpolator(DiscreteInterpolator *di)
   { AddTraceFaceIntegrator(di); }

   /// Access all interpolators added with AddDomainInterpolator().
   Array<BilinearFormIntegrator*> *GetDI() { return &domain_integs; }
   Array<Array<int>*> *GetDI_Marker() { return &domain_integs_marker; }

   /// Set the desired assembly level. The default is AssemblyLevel::FULL.
   /** This method must be called before assembly. */
   void SetAssemblyLevel(AssemblyLevel assembly_level);

   /** @brief Construct the internal matrix representation of the discrete
       linear operator. */
   virtual void Assemble(int skip_zeros = 1);

   /** @brief Get the output finite element space restriction matrix in
       transposed form. */
   virtual const Operator *GetOutputRestrictionTranspose() const
   { return test_fes->GetRestrictionTransposeOperator(); }
};

}

#endif
