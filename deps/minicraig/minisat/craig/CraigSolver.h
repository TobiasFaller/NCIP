#ifndef MiniCraig_CraigSolver_h
#define MiniCraig_CraigSolver_h

// ----------------------------------------------------------------------------
// Begin ugly source sharing hack
// ----------------------------------------------------------------------------

#define MiniCraig_SimpSolver_h_WITH_CRAIG
#define MiniCraig_SimpSolver_cc_WITH_CRAIG
#define MiniCraig_Solver_h_WITH_CRAIG
#define MiniCraig_Solver_cc_WITH_CRAIG
#define SimpSolver CraigSimpSolver
#define Solver CraigSolver

#include "../simp/SimpSolver.h"

#undef SimpSolver
#undef Solver
#undef MiniCraig_SimpSolver_h_WITH_CRAIG
#undef MiniCraig_SimpSolver_cc_WITH_CRAIG
#undef MiniCraig_Solver_h_WITH_CRAIG
#undef MiniCraig_Solver_cc_WITH_CRAIG

namespace cip {

    using ClauseType = MiniCraig::CraigClauseType;
    using VarType = MiniCraig::CraigVarType;

    const MiniCraig::BmcClauseType INITIAL_CLAUSE = MiniCraig::BmcClauseType::INIT_CLAUSE;
    const MiniCraig::BmcClauseType TRANS_CLAUSE = MiniCraig::BmcClauseType::TRANS_CLAUSE;
    const MiniCraig::BmcClauseType TARGET_CLAUSE = MiniCraig::BmcClauseType::TARGET_CLAUSE;
    const MiniCraig::BmcClauseType CRAIG_CLAUSE = MiniCraig::BmcClauseType::CRAIG_CLAUSE;

    const MiniCraig::CraigClauseType A_CLAUSE = MiniCraig::CraigClauseType::A_CLAUSE;
    const MiniCraig::CraigClauseType B_CLAUSE = MiniCraig::CraigClauseType::B_CLAUSE;

    const MiniCraig::CraigVarType A_LOCAL = MiniCraig::CraigVarType::A_LOCAL;
    const MiniCraig::CraigVarType B_LOCAL = MiniCraig::CraigVarType::B_LOCAL;
    const MiniCraig::CraigVarType GLOBAL = MiniCraig::CraigVarType::GLOBAL;

    using CONSTRULES = MiniCraig::CraigConstruction;
    const CONSTRULES ALL = MiniCraig::CraigConstruction::ALL;
    const CONSTRULES SYM = MiniCraig::CraigConstruction::SYMMETRIC;
    const CONSTRULES ASYM = MiniCraig::CraigConstruction::ASYMMETRIC;
    const CONSTRULES DUALASYM = MiniCraig::CraigConstruction::DUAL_ASYMMETRIC;

    using INTERPOLANTTYPE = MiniCraig::CraigInterpolant;
    const INTERPOLANTTYPE SYMINTER = MiniCraig::CraigInterpolant::SYMMETRIC;
    const INTERPOLANTTYPE ASYMINTER = MiniCraig::CraigInterpolant::ASYMMETRIC;
    const INTERPOLANTTYPE DUALASYMINTER = MiniCraig::CraigInterpolant::DUAL_SYMMETRIC;
    const INTERPOLANTTYPE INTERSECTIONINTER = MiniCraig::CraigInterpolant::INTERSECTION;
    const INTERPOLANTTYPE UNIONINTER = MiniCraig::CraigInterpolant::UNION;

};

// ----------------------------------------------------------------------------
// End ugly source sharing hack
// ----------------------------------------------------------------------------

#endif
