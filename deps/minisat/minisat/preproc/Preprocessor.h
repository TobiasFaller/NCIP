#ifndef Minisat_PreprocSolver_h
#define Minisat_PreprocSolver_h

#include "../simp/SimpSolver.h"
#include "../mtl/Vec.h"

namespace Minisat {

class Preprocessor : public SimpSolver {
  public:
    Preprocessor();

    bool  preprocess(const vec<Lit>& dontouch);
    void  getSimplifiedClauses(vec<vec<Lit>>& output);

    bool  solve        (                     bool do_simp, bool turn_off_simp)     = delete;
    bool  solve        (Lit p       ,        bool do_simp, bool turn_off_simp)     = delete;
    bool  solve        (Lit p, Lit q,        bool do_simp, bool turn_off_simp)     = delete;
    bool  solve        (Lit p, Lit q, Lit r, bool do_simp, bool turn_off_simp)     = delete;
    bool  solve        (const vec<Lit>& assumps, bool do_simp, bool turn_off_simp) = delete;
    lbool solveLimited (const vec<Lit>& assumps, bool do_simp, bool turn_off_simp) = delete;
    bool  eliminate    (bool turn_off_elim = false)                                = delete;

    void  setResLength (int maxSize) { clause_lim = maxSize; } // Length limit for resulting clauses for variable elimination
    void  setUseAsymm  (bool enable) { use_asymm = enable; } // Shrink clauses by asymmetric branching
    void  setUseBce    (bool enable) { use_bce = enable; } // perform blocked clause elimination
    void  setUseUpla   (bool enable) { use_upla = enable; } // perform unit propagation lookahead to detect equivalences
    void  setUseRcheck (bool enable) { use_rcheck = enable; } // Check if a clause is already implied. Prett costly, and subsumes subsumptions :)

protected:
    lbool solve_                   (bool do_simp = true, bool turn_off_simp = false);
    bool  eliminate_               (bool turn_off_elim = false);  // Perform variable elimination based simplification.

    void  applyBce                 ();
    void  applyUpla                ();
    void  cleanUpClauses           ();

    void  blockedClauseElim1       ();
    void  blockedClauseElim2       ();
    void  unitPropagationLookAhead (Var v, vec<int>& to_check);

  protected:
    bool    use_bce;
    bool    use_upla;

    vec<bool>           dont_touch;
    vec<Lit>            unit_clauses;
};

}

#endif
