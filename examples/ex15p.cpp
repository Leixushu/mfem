//                       MFEM Example 15 - Parallel Version
//
// Compile with: make ex15p
//
// Sample runs:  mpirun -np 4 ex15p
//               mpirun -np 4 ex15p -o 1 -y 0.2
//               mpirun -np 4 ex15p -o 4 -y 0.1
//               mpirun -np 4 ex15p -n 5
//               mpirun -np 4 ex15p -p 1 -n 3
//
//               Other meshes:
//
//               mpirun -np 4 ex15p -m ../data/square-disc-nurbs.mesh
//               mpirun -np 4 ex15p -m ../data/disc-nurbs.mesh
//               mpirun -np 4 ex15p -m ../data/fichera.mesh
//               mpirun -np 4 ex15p -m ../data/ball-nurbs.mesh
//               mpirun -np 4 ex15p -m ../data/star-surf.mesh
//               mpirun -np 4 ex15p -m ../data/amr-quad.mesh
//
//               Conforming meshes (no load balancing and derefinement):
//
//               mpirun -np 4 ex15p -m ../data/square-disc.mesh
//               mpirun -np 4 ex15p -m ../data/escher.mesh -o 1
//               mpirun -np 4 ex15p -m ../data/square-disc-surf.mesh
//
// Description:  Building on Example 6, this example demostrates dynamic AMR.
//               The mesh is adapted to a time-dependent solution by refinement
//               as well as by derefinement. For simplicity, the solution is
//               prescribed and no time integration is done. However, the error
//               estimation and refinement/derefinement decisions are realistic.
//
//               At each outer iteration the right hand side function is changed
//               to mimic a time dependent problem.  Within each inner iteration
//               the problem is solved on a sequence of meshes which are locally
//               refined according to a simple ZZ error estimator.  At the end
//               of the inner iteration the error estimates are also used to
//               identify any elements which may be over-refined and a single
//               derefinement step is performed.  After each refinement or
//               derefinement step a rebalance operation is performed to keep
//               the mesh evenly distributed among the available processors.
//
//               The example demonstrates MFEM's capability to refine, derefine
//               and load balance nonconforming meshes, in 2D and 3D, and on
//               linear, curved and surface meshes. Interpolation of functions
//               between coarse and fine meshes, persistent GLVis visualization,
//               and saving of time-dependent fields for external visualization
//               with VisIt (visit.llnl.gov) are also illustrated.
//
//               We recommend viewing Examples 1, 6 and 9 before viewing this
//               example.

#include "mfem.hpp"
#include <fstream>
#include <iostream>

using namespace std;
using namespace mfem;

// Choices for the problem setup. Affect bdr_func and rhs_func.
int problem;
int nfeatures;

// Prescribed time-dependent boundary and right-hand side functions.
double bdr_func(const Vector &pt, double t);
double rhs_func(const Vector &pt, double t);

// Estimate the solution errors with a simple (ZZ-type) error estimator.
double EstimateErrors(int order, int dim, int sdim, ParMesh & pmesh,
                      const ParGridFunction & x, Vector & errors);

// Update the finite element space, interpolate the solution and perform
// parallel load balancing.
void UpdateAndRebalance(ParMesh &pmesh, ParFiniteElementSpace &fespace,
                        ParGridFunction &x, ParBilinearForm &a,
                        ParLinearForm &b);


int main(int argc, char *argv[])
{
   // 1. Initialize MPI.
   int num_procs, myid;
   MPI_Init(&argc, &argv);
   MPI_Comm_size(MPI_COMM_WORLD, &num_procs);
   MPI_Comm_rank(MPI_COMM_WORLD, &myid);

   // 2. Parse command-line options.
   problem = 0;
   nfeatures = 1;
   const char *mesh_file = "../data/star-hilbert.mesh";
   int order = 2;
   double max_elem_error = 1.0e-4;
   double hysteresis = 0.25; // derefinement safety coefficient
   int nc_limit = 3;         // maximum level of hanging nodes
   bool visualization = true;
   bool visit = false;

   OptionsParser args(argc, argv);
   args.AddOption(&mesh_file, "-m", "--mesh",
                  "Mesh file to use.");
   args.AddOption(&problem, "-p", "--problem",
                  "Problem setup to use: 0 = spherical front, 1 = ball.");
   args.AddOption(&nfeatures, "-n", "--nfeatures",
                  "Number of solution features (fronts/balls).");
   args.AddOption(&order, "-o", "--order",
                  "Finite element order (polynomial degree).");
   args.AddOption(&max_elem_error, "-e", "--max-err",
                  "Maximum element error");
   args.AddOption(&hysteresis, "-y", "--hysteresis",
                  "Derefinement safety coefficient.");
   args.AddOption(&nc_limit, "-l", "--nc-limit",
                  "Maximum level of hanging nodes.");
   args.AddOption(&visualization, "-vis", "--visualization", "-no-vis",
                  "--no-visualization",
                  "Enable or disable GLVis visualization.");
   args.AddOption(&visit, "-visit", "--visit-datafiles", "-no-visit",
                  "--no-visit-datafiles",
                  "Save data files for VisIt (visit.llnl.gov) visualization.");
   args.Parse();
   if (!args.Good())
   {
      if (myid == 0)
      {
         args.PrintUsage(cout);
      }
      MPI_Finalize();
      return 1;
   }
   if (myid == 0)
   {
      args.PrintOptions(cout);
   }

   // 3. Read the (serial) mesh from the given mesh file on all processors.  We
   //    can handle triangular, quadrilateral, tetrahedral, hexahedral, surface
   //    and volume meshes with the same code.
   Mesh *mesh;
   ifstream imesh(mesh_file);
   if (!imesh)
   {
      if (myid == 0)
      {
         cerr << "\nCan not open mesh file: " << mesh_file << '\n' << endl;
      }
      MPI_Finalize();
      return 2;
   }
   mesh = new Mesh(imesh, 1, 1);
   imesh.close();
   int dim = mesh->Dimension();
   int sdim = mesh->SpaceDimension();

   // 4. Project a NURBS mesh to a piecewise-quadratic curved mesh. Make sure
   //    that the mesh is non-conforming if it has quads or hexes.
   if (mesh->NURBSext)
   {
      mesh->UniformRefinement();
      mesh->SetCurvature(2);
   }
   mesh->EnsureNCMesh();

   // 5. Define a parallel mesh by partitioning the serial mesh.  Once the
   //    parallel mesh is defined, the serial mesh can be deleted.
   ParMesh pmesh(MPI_COMM_WORLD, *mesh);
   delete mesh;

   MFEM_VERIFY(pmesh.bdr_attributes.Size() > 0,
               "Boundary attributes required in the mesh.");
   Array<int> ess_bdr(pmesh.bdr_attributes.Max());
   ess_bdr = 1;

   // 6. Define a finite element space on the mesh. The polynomial order is one
   //    (linear) by default, but this can be changed on the command line.
   H1_FECollection fec(order, dim);
   ParFiniteElementSpace fespace(&pmesh, &fec);

   // 7. As in Example 1p, we set up bilinear and linear forms corresponding to
   //    the Laplace problem -\Delta u = 1. We don't assemble the discrete
   //    problem yet, this will be done in the inner loop.
   ParBilinearForm a(&fespace);
   ParLinearForm b(&fespace);

   ConstantCoefficient one(1.0);
   FunctionCoefficient bdr(bdr_func);
   FunctionCoefficient rhs(rhs_func);

   a.AddDomainIntegrator(new DiffusionIntegrator(one));
   b.AddDomainIntegrator(new DomainLFIntegrator(rhs));

   // 8. The solution vector x and the associated finite element grid function
   //    will be maintained over the AMR iterations.
   ParGridFunction x(&fespace);

   // 9. Connect to GLVis. Prepare for VisIt output.
   char vishost[] = "localhost";
   int  visport   = 19916;

   socketstream sout;
   if (visualization)
   {
      sout.open(vishost, visport);
      if (!sout)
      {
         if (myid == 0)
         {
            cout << "Unable to connect to GLVis server at "
                 << vishost << ':' << visport << endl;
            cout << "GLVis visualization disabled.\n";
         }
         visualization = false;
      }
      sout.precision(8);
   }

   VisItDataCollection visit_dc("Example15-Parallel", &pmesh);
   visit_dc.RegisterField("solution", &x);
   int vis_cycle = 0;

   // 10. The outer time loop. In each iteration we update the right hand side,
   //     solve the problem on the current mesh, visualize the solution,
   //     estimate the error on all elements, refine bad elements and update all
   //     objects to work with the new mesh.  Then we derefine any elements
   //     which have very small errors.
   for (double time = 0.0; time < 1.0 + 1e-10; time += 0.01)
   {
      if (myid == 0)
      {
         cout << "\nTime " << time << "\n\nRefinement:" << endl;
      }

      // Set the current time in the coefficients
      bdr.SetTime(time);
      rhs.SetTime(time);

      Vector errors;

      // 11. The inner refinement loop. At the end we want to have the current
      //     time step resolved to the prescribed tolerance in each element.
      for (int ref_it = 1; ; ref_it++)
      {
         HYPRE_Int global_dofs = fespace.GlobalTrueVSize();
         if (myid == 0)
         {
            cout << "Iteration: " << ref_it << ", number of unknowns: "
                 << global_dofs << flush;
         }

         // 11a. Recompute the field on the current mesh: assemble the
         //      stiffness matrix and the right-hand side.
         a.Assemble();
         b.Assemble();

         // 11b. Project the exact solution to the essential DOFs.
         x.ProjectBdrCoefficient(bdr, ess_bdr);

         // 11c. Create and solve the parallel linear system.
         Array<int> ess_tdof_list;
         fespace.GetEssentialTrueDofs(ess_bdr, ess_tdof_list);

         HypreParMatrix A;
         Vector B, X;
         a.FormLinearSystem(ess_tdof_list, x, b, A, X, B);

         HypreBoomerAMG amg(A);
         amg.SetPrintLevel(0);
         HyprePCG pcg(A);
         pcg.SetTol(1e-12);
         pcg.SetMaxIter(200);
         pcg.SetPrintLevel(0);
         pcg.SetPreconditioner(amg);
         pcg.Mult(B, X);

         // 11d. Extract the local solution on each processor.
         a.RecoverFEMSolution(X, b, x);

         // 11e. Send the solution by socket to a GLVis server and optionally
         //      save it in VisIt format.
         if (visualization)
         {
            sout << "parallel " << num_procs << " " << myid << "\n";
            sout << "solution\n" << pmesh << x << flush;
         }
         if (visit)
         {
            visit_dc.SetCycle(vis_cycle++);
            visit_dc.SetTime(time);
            visit_dc.Save();
         }

         // 11f. Estimate element errors using the Zienkiewicz-Zhu error
         //      estimator. The bilinear form integrator must have the
         //      'ComputeElementFlux' method defined.
         double tot_error = EstimateErrors(order, dim, sdim, pmesh, x, errors);

         if (myid == 0)
         {
            cout << ", total error: " << tot_error << endl;
         }

         // 11g. Refine elements
         if (!pmesh.RefineByError(errors, max_elem_error, -1, nc_limit))
         {
            break;
         }

         // 11h. Update the space, interpolate the solution, rebalance the mesh.
         UpdateAndRebalance(pmesh, fespace, x, a, b);
      }

      // 12. Use error estimates from the last iterations to check for possible
      //     derefinements.
      if (pmesh.Nonconforming())
      {
         double threshold = hysteresis * max_elem_error;
         if (pmesh.DerefineByError(errors, threshold, nc_limit))
         {
            if (myid == 0)
            {
               cout << "\nDerefined elements." << endl;
            }

            // 12a. Update the space and the solution, rebalance the mesh.
            UpdateAndRebalance(pmesh, fespace, x, a, b);
         }
      }
   }

   // 13. Exit
   MPI_Finalize();
   return 0;
}


double EstimateErrors(int order, int dim, int sdim, ParMesh &pmesh,
                      const ParGridFunction &x, Vector &errors)
{
   // Space for the discontinuous (original) flux
   DiffusionIntegrator flux_integrator;
   L2_FECollection flux_fec(order, dim);
   ParFiniteElementSpace flux_fes(&pmesh, &flux_fec, sdim);

   // Space for the smoothed (conforming) flux
   double norm_p = 1;
   RT_FECollection smooth_flux_fec(order-1, dim);
   ParFiniteElementSpace smooth_flux_fes(&pmesh, &smooth_flux_fec);

   // Another possible set of options for the smoothed flux space:
   // norm_p = 1;
   // H1_FECollection smooth_flux_fec(order, dim);
   // ParFiniteElementSpace smooth_flux_fes(&pmesh, &smooth_flux_fec, dim);

   return L2ZZErrorEstimator(flux_integrator, x,
                             smooth_flux_fes, flux_fes, errors, norm_p);
}


void UpdateAndRebalance(ParMesh &pmesh, ParFiniteElementSpace &fespace,
                        ParGridFunction &x, ParBilinearForm &a,
                        ParLinearForm &b)
{
   // Update the space: recalculate the number of DOFs and construct a
   // matrix that will adjust any GridFunctions to the new mesh state.
   fespace.Update();

   // Interpolate the solution on the new mesh by applying the transformation
   // matrix prepared by the space. Multiple GridFunctions could be updated here.
   x.Update();

   if (pmesh.Nonconforming())
   {
      // Load balance the mesh.
      pmesh.Rebalance();

      // Update the space again, this time a GridFunction redistribution matrix
      // is created. Apply it to the solution.
      fespace.Update();
      x.Update();
   }

   // Let also the forms know that the space has changed.
   a.Update();
   b.Update();

   // Free any transformation matrices to save memory.
   fespace.UpdatesFinished();
}


const double alpha = 0.02;

// Spherical front with a Gaussian cross section and radius t
double front(double x, double y, double z, double t, int)
{
   double r = sqrt(x*x + y*y + z*z);
   return exp(-0.5*pow((r - t)/alpha, 2));
}

double front_laplace(double x, double y, double z, double t, int dim)
{
   double x2 = x*x, y2 = y*y, z2 = z*z, t2 = t*t;
   double r = sqrt(x2 + y2 + z2);
   double a2 = alpha*alpha, a4 = a2*a2;
   return -exp(-0.5*pow((r - t)/alpha, 2)) / a4 *
          (-2*t*(x2 + y2 + z2 - (dim-1)*a2/2)/r + x2 + y2 + z2 + t2 - dim*a2);
}

// Smooth spherical step function with radius t
double ball(double x, double y, double z, double t, int)
{
   double r = sqrt(x*x + y*y + z*z);
   return -atan(2*(r - t)/alpha);
}

double ball_laplace(double x, double y, double z, double t, int dim)
{
   double x2 = x*x, y2 = y*y, z2 = z*z, t2 = 4*t*t;
   double r = sqrt(x2 + y2 + z2);
   double a2 = alpha*alpha;
   double den = pow(-a2 - 4*(x2 + y2 + z2 - 2*r*t) - t2, 2.0);
   return (dim == 2) ? 2*alpha*(a2 + t2 - 4*x2 - 4*y2)/r/den
          /*      */ : 4*alpha*(a2 + t2 - 4*r*t)/r/den;
}

// Composes several features into one function
template<typename F0, typename F1>
double composite_func(const Vector &pt, double t, F0 f0, F1 f1)
{
   int dim = pt.Size();
   double x = pt(0), y = pt(1), z = 0.0;
   if (dim == 3) { z = pt(2); }

   if (problem == 0)
   {
      if (nfeatures <= 1)
      {
         return f0(x, y, z, t, dim);
      }
      else
      {
         double sum = 0.0;
         for (int i = 0; i < nfeatures; i++)
         {
            double x0 = 0.5*cos(2*M_PI * i / nfeatures);
            double y0 = 0.5*sin(2*M_PI * i / nfeatures);
            sum += f0(x - x0, y - y0, z, t, dim);
         }
         return sum;
      }
   }
   else
   {
      double sum = 0.0;
      for (int i = 0; i < nfeatures; i++)
      {
         double x0 = 0.5*cos(2*M_PI * i / nfeatures + M_PI*t);
         double y0 = 0.5*sin(2*M_PI * i / nfeatures + M_PI*t);
         sum += f1(x - x0, y - y0, z, 0.25, dim);
      }
      return sum;
   }
}

// Exact solution, used for the Dirichlet BC.
double bdr_func(const Vector &pt, double t)
{
   composite_func(pt, t, front, ball);
}

// Laplace of the exact solution, used for the right hand side.
double rhs_func(const Vector &pt, double t)
{
   composite_func(pt, t, front_laplace, ball_laplace);
}

