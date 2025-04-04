#include "../craig/CraigSolver.h"

// ----------------------------------------------------------------------------
// Begin ugly source sharing hack
// ----------------------------------------------------------------------------

#define MiniCraig_SimpSolver_h_WITH_CRAIG
#define MiniCraig_SimpSolver_cc_WITH_CRAIG
#define MiniCraig_Solver_h_WITH_CRAIG
#define MiniCraig_Solver_cc_WITH_CRAIG
#define SimpSolver CraigSimpSolver
#define Solver CraigSolver

#include "../core/Solver.cc"

// ----------------------------------------------------------------------------
// End ugly source sharing hack
// ----------------------------------------------------------------------------
