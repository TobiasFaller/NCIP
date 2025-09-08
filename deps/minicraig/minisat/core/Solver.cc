/***************************************************************************************[Solver.cc]
Copyright (c) 2003-2006, Niklas Een, Niklas Sorensson
Copyright (c) 2007-2010, Niklas Sorensson

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
**************************************************************************************************/

#include <math.h>
#include <algorithm>
#include <iostream>

#include "../mtl/Alg.h"
#include "../mtl/Sort.h"
#include "../utils/System.h"
#include "../core/Solver.h"

#ifndef MiniCraig_Solver_cc_WITH_CRAIG

// Normal include of the header without craig interpolation
# ifdef CRAIG_INTERPOLATION
#  define MiniCraig_Solver_cc_ENABLE_CRAIG_INTERPOLATION
#  undef CRAIG_INTERPOLATION
# endif

#else

// Include with craig interpolation from CraigSolver.h
# ifndef CRAIG_INTERPOLATION
#  define MiniCraig_Solver_cc_DISABLE_CRAIG_INTERPOLATION
#  define CRAIG_INTERPOLATION
# endif
#endif

using namespace MiniCraig;

//=================================================================================================
// Options:


static const char* _cat = "CORE";

static DoubleOption  opt_var_decay         (_cat, "var-decay",   "The variable activity decay factor",            0.95,     DoubleRange(0, false, 1, false));
static DoubleOption  opt_clause_decay      (_cat, "cla-decay",   "The clause activity decay factor",              0.999,    DoubleRange(0, false, 1, false));
static DoubleOption  opt_random_var_freq   (_cat, "rnd-freq",    "The frequency with which the decision heuristic tries to choose a random variable", 0, DoubleRange(0, true, 1, true));
static DoubleOption  opt_random_seed       (_cat, "rnd-seed",    "Used by the random variable selection",         91648253, DoubleRange(0, false, HUGE_VAL, false));
static IntOption     opt_ccmin_mode        (_cat, "ccmin-mode",  "Controls conflict clause minimization (0=none, 1=basic, 2=deep)", 2, IntRange(0, 2));
static IntOption     opt_phase_saving      (_cat, "phase-saving", "Controls the level of phase saving (0=none, 1=limited, 2=full)", 2, IntRange(0, 2));
static BoolOption    opt_rnd_init_act      (_cat, "rnd-init",    "Randomize the initial activity", false);
static BoolOption    opt_luby_restart      (_cat, "luby",        "Use the Luby restart sequence", true);
static IntOption     opt_restart_first     (_cat, "rfirst",      "The base restart interval", 100, IntRange(1, INT32_MAX));
static DoubleOption  opt_restart_inc       (_cat, "rinc",        "Restart interval increase factor", 2, DoubleRange(1, false, HUGE_VAL, false));
static DoubleOption  opt_garbage_frac      (_cat, "gc-frac",     "The fraction of wasted memory allowed before a garbage collection is triggered",  0.20, DoubleRange(0, false, HUGE_VAL, false));
static IntOption     opt_min_learnts_lim   (_cat, "min-learnts", "Minimum learnt clause limit",  0, IntRange(0, INT32_MAX));


//=================================================================================================
// Constructor/Destructor:


Solver::Solver(bool two_aig_managers, bool pref_dominant) :

    // Parameters (user settable):
    //
    verbosity        (0)
  , var_decay        (opt_var_decay)
  , clause_decay     (opt_clause_decay)
  , random_var_freq  (opt_random_var_freq)
  , random_seed      (opt_random_seed)
  , luby_restart     (opt_luby_restart)
  , ccmin_mode       (opt_ccmin_mode)
  , phase_saving     (opt_phase_saving)
  , rnd_pol          (false)
  , rnd_init_act     (opt_rnd_init_act)
  , garbage_frac     (opt_garbage_frac)
  , min_learnts_lim  (opt_min_learnts_lim)
  , restart_first    (opt_restart_first)
  , restart_inc      (opt_restart_inc)

    // Parameters (the rest):
    //
  , learntsize_factor((double)1/(double)3), learntsize_inc(1.1)

    // Parameters (experimental):
    //
  , learntsize_adjust_start_confl (100)
  , learntsize_adjust_inc         (1.5)

    // Statistics: (formerly in 'SolverStats')
    //
  , solves(0), starts(0), decisions(0), rnd_decisions(0), propagations(0), conflicts(0)
  , dec_vars(0), num_clauses(0), num_learnts(0), clauses_literals(0), learnts_literals(0), max_literals(0), tot_literals(0)

  , watches            (WatcherDeleted(ca))
  , order_heap         (VarOrderLt(activity))
  , ok                 (true)
  , cla_inc            (1)
  , var_inc            (1)
  , qhead              (0)
  , simpDB_assigns     (-1)
  , simpDB_props       (0)
  , progress_estimate  (0)
  , remove_satisfied   (true)
  , next_var           (0)

    // Resource constraints:
    //
  , conflict_budget    (-1)
  , propagation_budget (-1)
  , asynch_interrupt   (false)

#ifdef CRAIG_INTERPOLATION
  , craig_construction(CraigConstruction::NONE)
  , craig_id(0)
  , craig_clause_data()
  , craig_unit_clause_data()
  , craig_var_types()
  , craig_assignment()
  , craig_assignment_counter(-1)
  , craig_lit_red()
  , craig_interpolant()
  , craig_aig_sym()
  , craig_aig_asym()
  , craig_aig_dual_sym()
  , craig_aig_dual_asym()
#endif /* CRAIG_INTERPOLATION */
{
#ifdef CRAIG_INTERPOLATION
  craig_interpolant = {
    .partial_interpolant_sym = craig_aig_sym.getTrue(),
    .partial_interpolant_asym = craig_aig_asym.getTrue(),
    .partial_interpolant_dual_sym = craig_aig_dual_sym.getTrue(),
    .partial_interpolant_dual_asym = craig_aig_dual_asym.getTrue(),
    .craig_type = CraigClauseType::L_CLAUSE,
    .craig_id = std::numeric_limits<size_t>::max()
  };
#endif
}

Solver::~Solver()
{
}


//=================================================================================================
// Minor methods:


// Creates a new SAT variable in the solver. If 'decision' is cleared, variable will not be
// used as a decision variable (NOTE! This has effects on the meaning of a SATISFIABLE result).
//
#ifndef CRAIG_INTERPOLATION
Var Solver::newVar(lbool upol, bool dvar)
#else /* CRAIG_INTERPOLATION */
Var Solver::newVar(CraigVarType type, lbool upol, bool dvar)
#endif /* CRAIG_INTERPOLATION */
{
    Var v;
    if (free_vars.size() > 0){
        v = free_vars.last();
        free_vars.pop();
    }else
        v = next_var++;

    watches  .init(mkLit(v, false));
    watches  .init(mkLit(v, true ));
    assigns  .insert(v, l_Undef);
    vardata  .insert(v, mkVarData(CRef_Undef, 0));
    activity .insert(v, rnd_init_act ? drand(random_seed) * 0.00001 : 0);
    seen     .insert(v, 0);
    polarity .insert(v, true);
    user_pol .insert(v, upol);
    decision .reserve(v);
    trail    .capacity(v+1);
    setDecisionVar(v, dvar);

#ifdef CRAIG_INTERPOLATION
    craig_var_types.insert(v, type);
    craig_assignment.insert(v, -1);

#ifdef CRAIG_INTERPOLATION_FUZZ_CRAIG
    std::cerr << "fuzz: var ";
    if (type == CraigVarType::A_LOCAL) std::cerr << "A";
    if (type == CraigVarType::B_LOCAL) std::cerr << "B";
    if (type == CraigVarType::GLOBAL) std::cerr << "G";
    std::cerr << "; " << (v + 1) << std::endl;
#endif /* CRAIG_INTERPOLATION_FUZZ_CRAIG */
#endif /* CRAIG_INTERPOLATION */

    return v;
}


#ifndef CRAIG_INTERPOLATION
// Note: at the moment, only unassigned variable will be released (this is to avoid duplicate
// releases of the same variable).
void Solver::releaseVar(Lit l)
{
    if (value(l) == l_Undef){
        addClause(l);
        released_vars.push(var(l));
    }
}
#endif


#ifndef CRAIG_INTERPOLATION
bool Solver::addClause_(vec<Lit>& ps) {
#else
bool Solver::addClause_(vec<Lit>& ps, const CraigData& craig_data, bool use_interpolant) {
#endif /* CRAIG_INTERPOLATION */

    assert(decisionLevel() == 0);
    if (!ok) return false;

#ifdef CRAIG_INTERPOLATION
#ifdef CRAIG_INTERPOLATION_FUZZ_CRAIG
    int original_size = ps.size();
#endif /* CRAIG_INTERPOLATION_FUZZ_CRAIG */
#ifdef CRAIG_INTERPOLATION_DEBUG
    std::cerr << "Add Clause: ";
    if (craig_data.craig_type == CraigClauseType::A_CLAUSE) std::cerr << "A (";
    if (craig_data.craig_type == CraigClauseType::B_CLAUSE) std::cerr << "B (";
    if (craig_data.craig_type == CraigClauseType::L_CLAUSE) std::cerr << "L (";
    for (size_t i { 0u }; i < ps.size(); i++) {
        if (i != 0) std::cerr << ", ";
        if (craig_var_types[var(ps[i])] == CraigVarType::A_LOCAL) std::cerr << "a: ";
        if (craig_var_types[var(ps[i])] == CraigVarType::B_LOCAL) std::cerr << "b: ";
        if (craig_var_types[var(ps[i])] == CraigVarType::GLOBAL) std::cerr << "g: ";
        std::cerr << (sign(ps[i]) ? "-" : "") << (var(ps[i]) + 1);
    }
    std::cerr << ")" << std::endl;
#endif /* CRAIG_INTERPOLATION_DEBUG */

    CraigData partial_interpolant = use_interpolant
        ? craig_data
        : createCraigInterpolantForPureClause(ps, craig_data.craig_type, false);

#ifdef CRAIG_INTERPOLATION_FUZZ_CRAIG
    if (use_interpolant) {
        std::cerr << "fuzz: learn ";
    } else {
        std::cerr << "fuzz: add ";
    }
    if (partial_interpolant.craig_type == CraigClauseType::A_CLAUSE) std::cerr << "A;";
    if (partial_interpolant.craig_type == CraigClauseType::B_CLAUSE) std::cerr << "B;";
    if (partial_interpolant.craig_type == CraigClauseType::L_CLAUSE) std::cerr << "L;";
    for (size_t i { 0u }; i < ps.size(); i++) {
        std::cerr << " " << (sign(ps[i]) ? "-" : "") << (var(ps[i]) + 1);
    }
    std::cerr << ";"
        << " " << partial_interpolant.partial_interpolant_sym.getIndex()
        << " " << partial_interpolant.partial_interpolant_asym.getIndex()
        << " " << partial_interpolant.partial_interpolant_dual_sym.getIndex()
        << " " << partial_interpolant.partial_interpolant_dual_asym.getIndex()
        << "; addClause 1"
        << std::endl;
#endif /* CRAIG_INTERPOLATION_FUZZ_CRAIG */
#endif /* CRAIG_INTERPOLATION */

    // Check if clause is satisfied and remove false/duplicate literals:
    sort(ps);
    Lit p; int i, j;
    for (i = j = 0, p = lit_Undef; i < ps.size(); i++) {
        if (value(ps[i]) == l_True || ps[i] == ~p) {  // Literal is constant true
            return true;
        } else if (value(ps[i]) != l_False && ps[i] != p) { // Literal is not duplicated
            ps[j++] = p = ps[i];
            continue;
        }
#ifdef CRAIG_INTERPOLATION
    	else if (value(ps[i]) == l_False) { // Literal is false by unit propagation
            extendCraigInterpolantWithResolution(partial_interpolant, ps[i], craig_unit_clause_data[toInt(~ps[i])]);
        }
#endif /* CRAIG_INTERPOLATION */
    }
    ps.shrink(i - j);

#ifdef CRAIG_INTERPOLATION
#ifdef CRAIG_INTERPOLATION_DEBUG
    std::cerr << "    Simplified Clause: ";
    if (partial_interpolant.craig_type == CraigClauseType::A_CLAUSE) std::cerr << "A (";
    if (partial_interpolant.craig_type == CraigClauseType::B_CLAUSE) std::cerr << "B (";
    if (partial_interpolant.craig_type == CraigClauseType::L_CLAUSE) std::cerr << "L (";
    for (size_t i { 0u }; i < ps.size(); i++) {
        if (i != 0) std::cerr << ", ";
        if (craig_var_types[var(ps[i])] == CraigVarType::A_LOCAL) std::cerr << "a: ";
        if (craig_var_types[var(ps[i])] == CraigVarType::B_LOCAL) std::cerr << "b: ";
        if (craig_var_types[var(ps[i])] == CraigVarType::GLOBAL) std::cerr << "g: ";
        std::cerr << (sign(ps[i]) ? "-" : "") << (var(ps[i]) + 1);
    }
    std::cerr << ")" << std::endl;
#endif /* CRAIG_INTERPOLATION_DEBUG */

#ifdef CRAIG_INTERPOLATION_FUZZ_CRAIG
    if (ps.size() < original_size) {
        std::cerr << "fuzz: learn ";
        if (partial_interpolant.craig_type == CraigClauseType::A_CLAUSE) std::cerr << "A;";
        if (partial_interpolant.craig_type == CraigClauseType::B_CLAUSE) std::cerr << "B;";
        if (partial_interpolant.craig_type == CraigClauseType::L_CLAUSE) std::cerr << "L;";
        for (size_t i { 0u }; i < ps.size(); i++) {
            std::cerr << " " << (sign(ps[i]) ? "-" : "") << (var(ps[i]) + 1);
        }
        std::cerr << ";"
            << " " << partial_interpolant.partial_interpolant_sym.getIndex()
            << " " << partial_interpolant.partial_interpolant_asym.getIndex()
            << " " << partial_interpolant.partial_interpolant_dual_sym.getIndex()
            << " " << partial_interpolant.partial_interpolant_dual_asym.getIndex()
            << "; addClause 2"
            << std::endl;
    }
#endif /* CRAIG_INTERPOLATION_FUZZ_CRAIG */
#endif /* CRAIG_INTERPOLATION */

    if (ps.size() == 0) {
#ifdef CRAIG_INTERPOLATION
        // the computed interpolant is the final craig interpolant
        // as we found an empty clause
        craig_interpolant = partial_interpolant;
#endif /* CRAIG_INTERPOLATION */
        return ok = false;

    } else if (ps.size() == 1) {
#ifdef CRAIG_INTERPOLATION
        craig_unit_clause_data.insert(toInt(ps[0]), partial_interpolant);
#endif /* CRAIG_INTERPOLATION */
        uncheckedEnqueue(ps[0]);

        CRef confl = propagate();
        ok = (confl == CRef_Undef);

#ifdef CRAIG_INTERPOLATION
        if (!ok) {
            craig_interpolant = createCraigInterpolantForConflict(confl);
#ifdef CRAIG_INTERPOLATION_FUZZ_CRAIG
            std::cerr << "fuzz: learn ";
            if (craig_interpolant.craig_type == CraigClauseType::A_CLAUSE) std::cerr << "A;";
            if (craig_interpolant.craig_type == CraigClauseType::B_CLAUSE) std::cerr << "B;";
            if (craig_interpolant.craig_type == CraigClauseType::L_CLAUSE) std::cerr << "L;";
            std::cerr << ";"
                << " " << craig_interpolant.partial_interpolant_sym.getIndex()
                << " " << craig_interpolant.partial_interpolant_asym.getIndex()
                << " " << craig_interpolant.partial_interpolant_dual_sym.getIndex()
                << " " << craig_interpolant.partial_interpolant_dual_asym.getIndex()
                << "; addClause 3"
                << std::endl;
#endif /* CRAIG_INTERPOLATION_FUZZ_CRAIG */
        }
#endif /* CRAIG_INTERPOLATION */

        return ok;
    } else {
        CRef cr = ca.alloc(ps, false);
#ifdef CRAIG_INTERPOLATION
        craig_clause_data.insert(cr, partial_interpolant);
#endif /* CRAIG_INTERPOLATION */
        clauses.push(cr);
        attachClause(cr);
    }

    return true;
}

void Solver::attachClause(CRef cr){
    const Clause& c = ca[cr];
    assert(c.size() > 1);
    watches[~c[0]].push(Watcher(cr, c[1]));
    watches[~c[1]].push(Watcher(cr, c[0]));
    if (c.learnt()) num_learnts++, learnts_literals += c.size();
    else            num_clauses++, clauses_literals += c.size();
}


void Solver::detachClause(CRef cr, bool strict){
    const Clause& c = ca[cr];
    assert(c.size() > 1);

    // Strict or lazy detaching:
    if (strict){
        remove(watches[~c[0]], Watcher(cr, c[1]));
        remove(watches[~c[1]], Watcher(cr, c[0]));
    }else{
        watches.smudge(~c[0]);
        watches.smudge(~c[1]);
    }

    if (c.learnt()) num_learnts--, learnts_literals -= c.size();
    else            num_clauses--, clauses_literals -= c.size();
}


void Solver::removeClause(CRef cr) {
    Clause& c = ca[cr];
    detachClause(cr);
    // Don't leave pointers to free'd memory!
    if (locked(c)) vardata[var(c[0])].reason = CRef_Undef;
    c.mark(1);
    ca.free(cr);

#ifdef CRAIG_INTERPOLATION
    if (craig_clause_data.has(cr)) {
        craig_clause_data.remove(cr);
    }
#endif /* CRAIG_INTERPOLATION */
}


bool Solver::satisfied(const Clause& c) const {
    for (int i = 0; i < c.size(); i++)
        if (value(c[i]) == l_True)
            return true;
    return false; }


// Revert to the state at given level (keeping all assignment at 'level' but not beyond).
//
void Solver::cancelUntil(int level) {
#ifdef CRAIG_INTERPOLATION
#ifdef CRAIG_INTERPOLATION_DEBUG
            std::cerr << "Cancel until level " << level << std::endl;
#endif /* CRAIG_INTERPOLATION_DEBUG */
#endif /* CRAIG_INTERPOLATION */

    if (decisionLevel() > level){
        for (int c = trail.size()-1; c >= trail_lim[level]; c--){
            Var      x  = var(trail[c]);
            assigns [x] = l_Undef;
#ifdef CRAIG_INTERPOLATION
            // Do not delete the Craig interpolant if it exists since it might
            // be required for an assumption on level > 0 later.
            craig_assignment[x] = -1;
            craig_assignment_counter--;
#endif /* CRAIG_INTERPOLATION */
            if (phase_saving > 1 || (phase_saving == 1 && c > trail_lim.last()))
                polarity[x] = sign(trail[c]);
            insertVarOrder(x); }
        qhead = trail_lim[level];
        trail.shrink(trail.size() - trail_lim[level]);
        trail_lim.shrink(trail_lim.size() - level);
    } }


//=================================================================================================
// Major methods:


Lit Solver::pickBranchLit()
{
    Var next = var_Undef;

    // Random decision:
    if (drand(random_seed) < random_var_freq && !order_heap.empty()){
        next = order_heap[irand(random_seed,order_heap.size())];
        if (value(next) == l_Undef && decision[next])
            rnd_decisions++; }

    // Activity based decision:
    while (next == var_Undef || value(next) != l_Undef || !decision[next])
        if (order_heap.empty()){
            next = var_Undef;
            break;
        }else
            next = order_heap.removeMin();

    // Choose polarity based on different polarity modes (global or per-variable):
    if (next == var_Undef)
        return lit_Undef;
    else if (user_pol[next] != l_Undef)
        return mkLit(next, user_pol[next] == l_True);
    else if (rnd_pol)
        return mkLit(next, drand(random_seed) < 0.5);
    else
        return mkLit(next, polarity[next]);
}


/*_________________________________________________________________________________________________
|
|  analyze : (confl : Clause*) (out_learnt : vec<Lit>&) (out_btlevel : int&)  ->  [void]
|
|  Description:
|    Analyze conflict and produce a reason clause.
|
|    Pre-conditions:
|      * 'out_learnt' is assumed to be cleared.
|      * Current decision level must be greater than root level.
|
|    Post-conditions:
|      * 'out_learnt[0]' is the asserting literal at level 'out_btlevel'.
|      * If out_learnt.size() > 1 then 'out_learnt[1]' has the greatest decision level of the
|        rest of literals. There may be others from the same level though.
|
|________________________________________________________________________________________________@*/
#ifndef CRAIG_INTERPOLATION
void Solver::analyze(CRef confl, vec<Lit>& out_learnt, int& out_btlevel)
#else /* CRAIG_INTERPOLATION */
void Solver::analyze(CRef confl, vec<Lit>& out_learnt, int& out_btlevel, CraigData& out_craig_data)
#endif /* CRAIG_INTERPOLATION */
{
    int pathC = 0;
    Lit p     = lit_Undef;

#ifdef CRAIG_INTERPOLATION
#ifdef CRAIG_INTERPOLATION_DEBUG
    std::cerr << "Analyze Conflict: ";
    CraigData& craig_data = craig_clause_data[confl];
    Clause& clause = ca[confl];
    if (craig_data.craig_type == CraigClauseType::A_CLAUSE) std::cerr << "A (";
    if (craig_data.craig_type == CraigClauseType::B_CLAUSE) std::cerr << "B (";
    if (craig_data.craig_type == CraigClauseType::L_CLAUSE) std::cerr << "L (";
    for (size_t i { 0u }; i < clause.size(); i++) {
        if (i != 0) std::cerr << ", ";
        if (craig_var_types[var(clause[i])] == CraigVarType::A_LOCAL) std::cerr << "a: ";
        if (craig_var_types[var(clause[i])] == CraigVarType::B_LOCAL) std::cerr << "b: ";
        if (craig_var_types[var(clause[i])] == CraigVarType::GLOBAL) std::cerr << "g: ";
        std::cerr << (sign(clause[i]) ? "-" : "") << (var(clause[i]) + 1);
    }
    std::cerr << ")" << std::endl;
#endif /* CRAIG_INTERPOLATION_DEBUG */
#endif /* CRAIG_INTERPOLATION */

    // Generate conflict clause:
    //
    out_learnt.push();      // (leave room for the asserting literal)
    int index   = trail.size() - 1;

#ifdef CRAIG_INTERPOLATION
    bool extended = false;
    bool do_resolution = false;
    CRef original_confl = confl;
    CraigData partial_interpolant = craig_clause_data[original_confl];
#ifdef CRAIG_INTERPOLATION_DEBUG
    std::cerr << "    Clone Interpolant i" << partial_interpolant.craig_id << " to i" << craig_id << std::endl;
#endif /* CRAIG_INTERPOLATION_DEBUG */
    partial_interpolant.craig_id = craig_id++;
#endif /* CRAIG_INTERPOLATION */

    do{
        assert(confl != CRef_Undef); // (otherwise should be UIP)
        Clause& c = ca[confl];

#ifdef CRAIG_INTERPOLATION
        if (do_resolution) {
            extendCraigInterpolantWithResolution(partial_interpolant, ~p, craig_clause_data[confl]);
            extended = true;
        }
#endif /* CRAIG_INTERPOLATION */

        if (c.learnt())
            claBumpActivity(c);

#ifdef CRAIG_INTERPOLATION
#ifdef CRAIG_INTERPOLATION_DEBUG
    std::cerr << "    Analyze Clause: ";
    CraigData& craig_data = craig_clause_data[confl];
    if (craig_data.craig_type == CraigClauseType::A_CLAUSE) std::cerr << "A (";
    if (craig_data.craig_type == CraigClauseType::B_CLAUSE) std::cerr << "B (";
    if (craig_data.craig_type == CraigClauseType::L_CLAUSE) std::cerr << "L (";
    for (size_t i { 0u }; i < c.size(); i++) {
        if (i != 0) std::cerr << ", ";
        if (craig_var_types[var(c[i])] == CraigVarType::A_LOCAL) std::cerr << "a: ";
        if (craig_var_types[var(c[i])] == CraigVarType::B_LOCAL) std::cerr << "b: ";
        if (craig_var_types[var(c[i])] == CraigVarType::GLOBAL) std::cerr << "g: ";
        std::cerr << (sign(c[i]) ? "-" : "") << (var(c[i]) + 1);
    }
    std::cerr << ")" << std::endl;
#endif /* CRAIG_INTERPOLATION_DEBUG */
#endif /* CRAIG_INTERPOLATION */

        for (int j = (p == lit_Undef) ? 0 : 1; j < c.size(); j++){
            Lit q = c[j];

            if (!seen[var(q)] && level(var(q)) > 0){
                varBumpActivity(var(q));
                seen[var(q)] = 1;
                if (level(var(q)) >= decisionLevel())
                    pathC++;
                else
                    out_learnt.push(q);
            }
#ifdef CRAIG_INTERPOLATION
            else if (!seen[var(q)]) {
                extendCraigInterpolantWithResolution(partial_interpolant, q, craig_unit_clause_data[toInt(~q)]);
                extended = true;
            }
#endif /* CRAIG_INTERPOLATION */
        }

        // Select next clause to look at:
        while (!seen[var(trail[index--])]);
        p     = trail[index+1];
        confl = reason(var(p));
        seen[var(p)] = 0;
        pathC--;

#ifdef CRAIG_INTERPOLATION
        do_resolution = true;
#endif /* CRAIG_INTERPOLATION */

    }while (pathC > 0);
    out_learnt[0] = ~p;

#ifdef CRAIG_INTERPOLATION
#ifdef CRAIG_INTERPOLATION_FUZZ_CRAIG
    std::cerr << "fuzz: learn ";
    if (partial_interpolant.craig_type == CraigClauseType::A_CLAUSE) std::cerr << "A;";
    if (partial_interpolant.craig_type == CraigClauseType::B_CLAUSE) std::cerr << "B;";
    if (partial_interpolant.craig_type == CraigClauseType::L_CLAUSE) std::cerr << "L;";
    for (size_t i { 0u }; i < out_learnt.size(); i++) {
        std::cerr << " " << (sign(out_learnt[i]) ? "-" : "") << (var(out_learnt[i]) + 1);
    }
    std::cerr << ";"
        << " " << partial_interpolant.partial_interpolant_sym.getIndex()
        << " " << partial_interpolant.partial_interpolant_asym.getIndex()
        << " " << partial_interpolant.partial_interpolant_dual_sym.getIndex()
        << " " << partial_interpolant.partial_interpolant_dual_asym.getIndex()
        << "; analyze 1"
        << std::endl;
#endif /* CRAIG_INTERPOLATION_FUZZ_CRAIG */
#endif /* CRAIG_INTERPOLATION */

#ifdef CRAIG_INTERPOLATION
    if (extended && partial_interpolant.isPure()) {
        partial_interpolant = createCraigInterpolantForPureClause(out_learnt, craig_clause_data[original_confl].craig_type, true);
        extended = false;
    }

#ifdef CRAIG_INTERPOLATION_FUZZ_CRAIG
    std::cerr << "fuzz: learn ";
    if (partial_interpolant.craig_type == CraigClauseType::A_CLAUSE) std::cerr << "A;";
    if (partial_interpolant.craig_type == CraigClauseType::B_CLAUSE) std::cerr << "B;";
    if (partial_interpolant.craig_type == CraigClauseType::L_CLAUSE) std::cerr << "L;";
    for (size_t i { 0u }; i < out_learnt.size(); i++) {
        std::cerr << " " << (sign(out_learnt[i]) ? "-" : "") << (var(out_learnt[i]) + 1);
    }
    std::cerr << ";"
        << " " << partial_interpolant.partial_interpolant_sym.getIndex()
        << " " << partial_interpolant.partial_interpolant_asym.getIndex()
        << " " << partial_interpolant.partial_interpolant_dual_sym.getIndex()
        << " " << partial_interpolant.partial_interpolant_dual_asym.getIndex()
        << "; analyze 2"
        << std::endl;
#endif /* CRAIG_INTERPOLATION_FUZZ_CRAIG */

    vec<std::pair<int, Lit>> to_resolve;
#endif /* CRAIG_INTERPOLATION */

    // Simplify conflict clause:
    //
    int i, j;
    out_learnt.copyTo(analyze_toclear);
    if (ccmin_mode == 2){
#ifdef CRAIG_INTERPOLATION
        int analyze_toclear_offset = analyze_toclear.size();
#endif /* CRAIG_INTERPOLATION */
        for (i = j = 1; i < out_learnt.size(); i++) {
            // litRedundant(..) has side effect when craig interpolation is enabled
            if (reason(var(out_learnt[i])) == CRef_Undef || !litRedundant(out_learnt[i]))
                out_learnt[j++] = out_learnt[i];
#ifdef CRAIG_INTERPOLATION
            else {
                // craig_lit_red is filled with every litRedundant call.
                // It contains the literals that need to be resolved with
                // (including the removed literal).
                for (int c = 0; c < craig_lit_red.size(); c++) {
                    to_resolve.push(std::make_pair(craig_assignment[var(craig_lit_red[c])], craig_lit_red[c]));
                }
            }
            craig_lit_red.clear();
            for (int k = analyze_toclear_offset; k < analyze_toclear.size(); k++) {
                // Clear seen_removable = 2, seen_failed = 3 as we need the full resolution tree
                // to be constructed in litRedundant.
                if (seen[var(analyze_toclear[k])] > 1) {
                    seen[var(analyze_toclear[k])] = 0;
                } else {
                    analyze_toclear[analyze_toclear_offset++] = analyze_toclear[k];
                }
            }
#endif /* CRAIG_INTERPOLATION */
        }
    }else if (ccmin_mode == 1){
        for (i = j = 1; i < out_learnt.size(); i++){
            Var x = var(out_learnt[i]);

            if (reason(x) == CRef_Undef)
                out_learnt[j++] = out_learnt[i];
            else{
#ifdef CRAIG_INTERPOLATION
                bool do_resolution = true;
#endif /* CRAIG_INTERPOLATION */
                Clause& c = ca[reason(var(out_learnt[i]))];
                for (int k = 1; k < c.size(); k++)
                    if (!seen[var(c[k])] && level(var(c[k])) > 0) {
                        out_learnt[j++] = out_learnt[i];
#ifdef CRAIG_INTERPOLATION
                        do_resolution = false;
#endif /* CRAIG_INTERPOLATION */
                        break;
                    }

#ifdef CRAIG_INTERPOLATION
                if (do_resolution) {
                    to_resolve.push(std::make_pair(craig_assignment[var(out_learnt[i])], out_learnt[i]));
                }
#endif /* CRAIG_INTERPOLATION */
            }
        }
    }else
        i = j = out_learnt.size();

#ifdef CRAIG_INTERPOLATION
    sort(to_resolve);
    for (int k = to_resolve.size() - 1; k >= 0; k--) {
        auto literal = to_resolve[k].second;
        extendCraigInterpolantWithResolution(partial_interpolant, literal, craig_clause_data[reason(var(literal))]);
        extended = true;
    }
#endif /* CRAIG_INTERPOLATION */

    max_literals += out_learnt.size();
    out_learnt.shrink(i - j);
    tot_literals += out_learnt.size();

#ifdef CRAIG_INTERPOLATION
#ifdef CRAIG_INTERPOLATION_FUZZ_CRAIG
    std::cerr << "fuzz: learn ";
    if (partial_interpolant.craig_type == CraigClauseType::A_CLAUSE) std::cerr << "A;";
    if (partial_interpolant.craig_type == CraigClauseType::B_CLAUSE) std::cerr << "B;";
    if (partial_interpolant.craig_type == CraigClauseType::L_CLAUSE) std::cerr << "L;";
    for (size_t i { 0u }; i < out_learnt.size(); i++) {
        std::cerr << " " << (sign(out_learnt[i]) ? "-" : "") << (var(out_learnt[i]) + 1);
    }
    std::cerr << ";"
        << " " << partial_interpolant.partial_interpolant_sym.getIndex()
        << " " << partial_interpolant.partial_interpolant_asym.getIndex()
        << " " << partial_interpolant.partial_interpolant_dual_sym.getIndex()
        << " " << partial_interpolant.partial_interpolant_dual_asym.getIndex()
        << "; analyze 3"
        << std::endl;
#endif /* CRAIG_INTERPOLATION_FUZZ_CRAIG */

    if (extended && partial_interpolant.isPure()) {
        partial_interpolant = createCraigInterpolantForPureClause(out_learnt, craig_clause_data[original_confl].craig_type, true);
        extended = false;
    }

#ifdef CRAIG_INTERPOLATION_DEBUG
    std::cerr << "    Resulting Learnt Clause: ";
    if (partial_interpolant.craig_type == CraigClauseType::A_CLAUSE) std::cerr << "A (";
    if (partial_interpolant.craig_type == CraigClauseType::B_CLAUSE) std::cerr << "B (";
    if (partial_interpolant.craig_type == CraigClauseType::L_CLAUSE) std::cerr << "L (";
    for (size_t i { 0u }; i < out_learnt.size(); i++) {
        if (i != 0) std::cerr << ", ";
        if (craig_var_types[var(out_learnt[i])] == CraigVarType::A_LOCAL) std::cerr << "a: ";
        if (craig_var_types[var(out_learnt[i])] == CraigVarType::B_LOCAL) std::cerr << "b: ";
        if (craig_var_types[var(out_learnt[i])] == CraigVarType::GLOBAL) std::cerr << "g: ";
        std::cerr << (sign(out_learnt[i]) ? "-" : "") << (var(out_learnt[i]) + 1);
    }
    std::cerr << ")" << std::endl;

    std::cerr << "    Resulting Interpolant:" << std::endl;
    debugCraigInterpolants(partial_interpolant);
#endif /* CRAIG_INTERPOLATION_DEBUG */

    out_craig_data = partial_interpolant;
#endif /* CRAIG_INTERPOLATION */

    // Find correct backtrack level:
    //
    if (out_learnt.size() == 1)
        out_btlevel = 0;
    else{
        int max_i = 1;
        // Find the first literal assigned at the next-highest level:
        for (int i = 2; i < out_learnt.size(); i++)
            if (level(var(out_learnt[i])) > level(var(out_learnt[max_i])))
                max_i = i;
        // Swap-in this literal at index 1:
        Lit p             = out_learnt[max_i];
        out_learnt[max_i] = out_learnt[1];
        out_learnt[1]     = p;
        out_btlevel       = level(var(p));
    }

    for (int j = 0; j < analyze_toclear.size(); j++) seen[var(analyze_toclear[j])] = 0;    // ('seen[]' is now cleared)
}


// Check if 'p' can be removed from a conflict clause.
bool Solver::litRedundant(Lit p)
{
    enum { seen_undef = 0, seen_source = 1, seen_removable = 2, seen_failed = 3 };
    assert(seen[var(p)] == seen_undef || seen[var(p)] == seen_source);
    assert(reason(var(p)) != CRef_Undef);

#ifdef CRAIG_INTERPOLATION
    craig_lit_red.clear();
#endif /* CRAIG_INTERPOLATION */

    Clause*               c     = &ca[reason(var(p))];
    vec<ShrinkStackElem>& stack = analyze_stack;
    stack.clear();

    for (uint32_t i = 1; ; i++){
        if (i < (uint32_t)c->size()){
            // Checking 'p'-parents 'l':
            Lit l = (*c)[i];

            // Variable at level 0 or previously removable:
            if (level(var(l)) == 0 || seen[var(l)] == seen_source || seen[var(l)] == seen_removable)
                continue;

            // Check variable can not be removed for some local reason:
            if (reason(var(l)) == CRef_Undef || seen[var(l)] == seen_failed){
                stack.push(ShrinkStackElem(0, p));
                for (int i = 0; i < stack.size(); i++)
                    if (seen[var(stack[i].l)] == seen_undef){
                        seen[var(stack[i].l)] = seen_failed;
                        analyze_toclear.push(stack[i].l);
                    }

                return false;
            }

            // Recursively check 'l':
            stack.push(ShrinkStackElem(i, p));
            i  = 0;
            p  = l;
            c  = &ca[reason(var(p))];
        }else{
            // Finished with current element 'p' and reason 'c':
            if (seen[var(p)] == seen_undef){
                seen[var(p)] = seen_removable;
                analyze_toclear.push(p);
            }

#ifdef CRAIG_INTERPOLATION
            // Record resolution steps required for removal of the literal.
            if (reason(var(p)) != CRef_Undef && level(var(p)) > 0) {
                craig_lit_red.push(p);
            }
#endif /* CRAIG_INTERPOLATION */

            // Terminate with success if stack is empty:
            if (stack.size() == 0) break;

            // Continue with top element on stack:
            i  = stack.last().i;
            p  = stack.last().l;
            c  = &ca[reason(var(p))];

            stack.pop();
        }
    }

    return true;
}


/*_________________________________________________________________________________________________
|
|  analyzeFinal : (p : Lit)  ->  [void]
|
|  Description:
|    Specialized analysis procedure to express the final conflict in terms of assumptions.
|    Calculates the (possibly empty) set of assumptions that led to the assignment of 'p', and
|    stores the result in 'out_conflict'.
|________________________________________________________________________________________________@*/
void Solver::analyzeFinal(Lit p, LSet& out_conflict)
{
    out_conflict.clear();
    out_conflict.insert(p);

    if (decisionLevel() == 0) {
#ifndef CRAIG_INTERPOLATION
        return;
#else /* CRAIG_INTERPOLATION */
        if (reason(var(p)) != CRef_Undef) {
            Clause& cl = ca[reason(var(p))];
            for (int t = 0; t < cl.size(); ++t) {
                seen[var(cl[t])] = 1;
            }
        }
#endif /* CRAIG_INTERPOLATION */
    }

    seen[var(p)] = 1;

#ifdef CRAIG_INTERPOLATION
    CraigData partial_interpolant = (reason(var(p)) != CRef_Undef)
            ? craig_clause_data[reason(var(p))]
            : craig_unit_clause_data[toInt(p)];
#ifdef CRAIG_INTERPOLATION_DEBUG
    std::cerr << "Analyze final (";
    if (craig_var_types[var(p)] == CraigVarType::A_LOCAL) std::cerr << "a: ";
    if (craig_var_types[var(p)] == CraigVarType::B_LOCAL) std::cerr << "b: ";
    if (craig_var_types[var(p)] == CraigVarType::GLOBAL) std::cerr << "g: ";
    std::cerr << (sign(p) ? "-" : "") << (var(p) + 1) << ")" << std::endl;

    std::cerr << "    Clone Interpolant i" << partial_interpolant.craig_id << " to " << craig_id << std::endl;
#endif /* CRAIG_INTERPOLATION_DEBUG */
    partial_interpolant.craig_id = craig_id++;
#endif /* CRAIG_INTERPOLATION */

    for (int i = trail.size()-1; i >= 0; i--){
        Var x = var(trail[i]);
        if (seen[x]){
#ifdef CRAIG_INTERPOLATION
#ifdef CRAIG_INTERPOLATION_DEBUG
            std::cerr << "Interpolant extend with trail " << i << ": " << (sign(trail[i]) ? "-" : "") << (toInt(x) + 1) << std::endl;
#endif /* CRAIG_INTERPOLATION_DEBUG */
#endif /* CRAIG_INTERPOLATION */

            if (reason(x) == CRef_Undef){
                assert(decisionLevel() <= assumptions.size() || level(x) > 0);

#ifdef CRAIG_INTERPOLATION
                if (level(x) == 0)
                    extendCraigInterpolantWithResolution(partial_interpolant, ~trail[i], craig_unit_clause_data[toInt(trail[i])]);
#endif /* CRAIG_INTERPOLATION */

                out_conflict.insert(~trail[i]);
            }else{
                Clause& c = ca[reason(x)];

#ifdef CRAIG_INTERPOLATION
                extendCraigInterpolantWithResolution(partial_interpolant, ~trail[i], craig_clause_data[reason(x)]);
#endif /* CRAIG_INTERPOLATION */

                for (int j = 0; j < c.size(); j++)
                    seen[var(c[j])] = 1;
            }

            seen[x] = 0;
        }
    }

#ifdef CRAIG_INTERPOLATION
    craig_interpolant = partial_interpolant;
#ifdef CRAIG_INTERPOLATION_FUZZ_CRAIG
    std::cerr << "fuzz: learn ";
    if (craig_interpolant.craig_type == CraigClauseType::A_CLAUSE) std::cerr << "A;";
    if (craig_interpolant.craig_type == CraigClauseType::B_CLAUSE) std::cerr << "B;";
    if (craig_interpolant.craig_type == CraigClauseType::L_CLAUSE) std::cerr << "L;";
    std::cerr << ";"
        << " " << craig_interpolant.partial_interpolant_sym.getIndex()
        << " " << craig_interpolant.partial_interpolant_asym.getIndex()
        << " " << craig_interpolant.partial_interpolant_dual_sym.getIndex()
        << " " << craig_interpolant.partial_interpolant_dual_asym.getIndex()
        << "; analyzeFinal 1"
        << std::endl;
#endif /* CRAIG_INTERPOLATION_FUZZ_CRAIG */
#endif /* CRAIG_INTERPOLATION */

    seen[var(p)] = 0;
}


void Solver::uncheckedEnqueue(Lit p, CRef from)
{
    assert(value(p) == l_Undef);
    assigns[var(p)] = lbool(!sign(p));
    vardata[var(p)] = mkVarData(from, decisionLevel());
    trail.push_(p);

#ifdef CRAIG_INTERPOLATION
    craig_assignment[var(p)] = craig_assignment_counter++;

#ifdef CRAIG_INTERPOLATION_DEBUG
    std::cerr << "Enqueue (";
    if (craig_var_types[var(p)] == CraigVarType::A_LOCAL) std::cerr << "a: ";
    if (craig_var_types[var(p)] == CraigVarType::B_LOCAL) std::cerr << "b: ";
    if (craig_var_types[var(p)] == CraigVarType::GLOBAL) std::cerr << "g: ";
    std::cerr << (sign(p) ? "-" : "") << (var(p) + 1) << ")";

    if (from == CRef_Undef) {
        std::cerr << " without reason";
    } else {
        std::cerr << " with reason ";
        const auto& clause = ca[from];
        if (craig_clause_data[from].craig_type == CraigClauseType::A_CLAUSE) std::cerr << "A (";
        if (craig_clause_data[from].craig_type == CraigClauseType::B_CLAUSE) std::cerr << "B (";
        if (craig_clause_data[from].craig_type == CraigClauseType::L_CLAUSE) std::cerr << "L (";
        for (size_t i { 0u }; i < clause.size(); i++) {
            if (i != 0) std::cerr << ", ";
            if (craig_var_types[var(clause[i])] == CraigVarType::A_LOCAL) std::cerr << "a: ";
            if (craig_var_types[var(clause[i])] == CraigVarType::B_LOCAL) std::cerr << "b: ";
            if (craig_var_types[var(clause[i])] == CraigVarType::GLOBAL) std::cerr << "g: ";
            std::cerr << (sign(clause[i]) ? "-" : "") << (var(clause[i]) + 1);
        }
        std::cerr << ")";
    }

    if (craig_unit_clause_data.has(toInt(p))) std::cerr << " with Interpolant i" << craig_unit_clause_data[toInt(p)].craig_id;
    std::cerr << std::endl;
#endif /* CRAIG_INTERPOLATION_DEBUG */

    if (decisionLevel() == 0 && from != CRef_Undef) {
        const auto& clause = ca[from];

        bool extended = false;
        CraigData partial_interpolant = craig_clause_data[from];
#ifdef CRAIG_INTERPOLATION_DEBUG
        std::cerr << "    Clone Interpolant i" << partial_interpolant.craig_id << " to i" << craig_id << std::endl;
#endif /* CRAIG_INTERPOLATION_DEBUG */
        partial_interpolant.craig_id = craig_id++;

        // Make sure to resolve with unit literals which are not included
        // in the clause's Craig interplant.
        for (int t = 1; t < clause.size(); ++t) {
            extendCraigInterpolantWithResolution(partial_interpolant, clause[t], craig_unit_clause_data[toInt(~clause[t])]);
            extended = true;
        }

        // Simplify Craig interpolant if possible for the unit clause.
        if (extended && partial_interpolant.isPure()) {
            vec<Lit> unit_clause(1, p);
            partial_interpolant = createCraigInterpolantForPureClause(unit_clause, craig_clause_data[from].craig_type, true);
        }

        craig_unit_clause_data.insert(toInt(p), partial_interpolant);

#ifdef CRAIG_INTERPOLATION_FUZZ_CRAIG
        std::cerr << "fuzz: learn ";
        if (partial_interpolant.craig_type == CraigClauseType::A_CLAUSE) std::cerr << "A;";
        if (partial_interpolant.craig_type == CraigClauseType::B_CLAUSE) std::cerr << "B;";
        if (partial_interpolant.craig_type == CraigClauseType::L_CLAUSE) std::cerr << "L;";
        std::cerr << " " << (sign(p) ? "-" : "") << (var(p) + 1);
        std::cerr << ";"
            << " " << partial_interpolant.partial_interpolant_sym.getIndex()
            << " " << partial_interpolant.partial_interpolant_asym.getIndex()
            << " " << partial_interpolant.partial_interpolant_dual_sym.getIndex()
            << " " << partial_interpolant.partial_interpolant_dual_asym.getIndex()
            << "; uncheckedEnqueue 1"
            << std::endl;
#endif /* CRAIG_INTERPOLATION_FUZZ_CRAIG */
    }
#endif
}


/*_________________________________________________________________________________________________
|
|  propagate : [void]  ->  [Clause*]
|
|  Description:
|    Propagates all enqueued facts. If a conflict arises, the conflicting clause is returned,
|    otherwise CRef_Undef.
|
|    Post-conditions:
|      * the propagation queue is empty, even if there was a conflict.
|________________________________________________________________________________________________@*/
CRef Solver::propagate()
{
#ifdef CRAIG_INTERPOLATION
#ifdef CRAIG_INTERPOLATION_DEBUG
    std::cerr << "Propagate" << std::endl;
#endif /* CRAIG_INTERPOLATION_DEBUG */
#endif /* CRAIG_INTERPOLATION */

    CRef    confl     = CRef_Undef;
    int     num_props = 0;

    while (qhead < trail.size()){
        Lit            p   = trail[qhead++];     // 'p' is enqueued fact to propagate.
        vec<Watcher>&  ws  = watches.lookup(p);
        Watcher        *i, *j, *end;
        num_props++;

        for (i = j = (Watcher*)ws, end = i + ws.size();  i != end;){
            // Try to avoid inspecting the clause:
            Lit blocker = i->blocker;
            if (value(blocker) == l_True){
                *j++ = *i++; continue; }

            // Make sure the false literal is data[1]:
            CRef     cr        = i->cref;
            Clause&  c         = ca[cr];
            Lit      false_lit = ~p;
            if (c[0] == false_lit)
                c[0] = c[1], c[1] = false_lit;
            assert(c[1] == false_lit);
            i++;

            // If 0th watch is true, then clause is already satisfied.
            Lit     first = c[0];
            Watcher w     = Watcher(cr, first);
            if (first != blocker && value(first) == l_True){
                *j++ = w; continue; }

            // Look for new watch:
            for (int k = 2; k < c.size(); k++)
                if (value(c[k]) != l_False){
                    c[1] = c[k]; c[k] = false_lit;
                    watches[~c[1]].push(w);
                    goto NextClause; }

            // Did not find watch -- clause is unit under assignment:
            *j++ = w;
            if (value(first) == l_False){
                confl = cr;
                qhead = trail.size();
                // Copy the remaining watches:
                while (i < end)
                    *j++ = *i++;
            }else
                uncheckedEnqueue(first, cr);

        NextClause:;
        }
        ws.shrink(i - j);
    }
    propagations += num_props;
    simpDB_props -= num_props;

    return confl;
}


/*_________________________________________________________________________________________________
|
|  reduceDB : ()  ->  [void]
|
|  Description:
|    Remove half of the learnt clauses, minus the clauses locked by the current assignment. Locked
|    clauses are clauses that are reason to some assignment. Binary clauses are never removed.
|________________________________________________________________________________________________@*/
struct reduceDB_lt {
    ClauseAllocator& ca;
    reduceDB_lt(ClauseAllocator& ca_) : ca(ca_) {}
    bool operator () (CRef x, CRef y) {
        return ca[x].size() > 2 && (ca[y].size() == 2 || ca[x].activity() < ca[y].activity()); }
};
void Solver::reduceDB()
{
    int     i, j;
    double  extra_lim = cla_inc / learnts.size();    // Remove any clause below this activity

    sort(learnts, reduceDB_lt(ca));
    // Don't delete binary or locked clauses. From the rest, delete clauses from the first half
    // and clauses with activity smaller than 'extra_lim':
    for (i = j = 0; i < learnts.size(); i++){
        Clause& c = ca[learnts[i]];
        if (c.size() > 2 && !locked(c) && (i < learnts.size() / 2 || c.activity() < extra_lim))
            removeClause(learnts[i]);
        else
            learnts[j++] = learnts[i];
    }
    learnts.shrink(i - j);
    checkGarbage();
}


void Solver::removeSatisfied(vec<CRef>& cs)
{
    int i, j;
    for (i = j = 0; i < cs.size(); i++){
        Clause& c = ca[cs[i]];
        if (satisfied(c))
            removeClause(cs[i]);
        else{
            // Trim clause:
            assert(value(c[0]) == l_Undef && value(c[1]) == l_Undef);
            for (int k = 2; k < c.size(); k++)
                if (value(c[k]) == l_False){

#ifdef CRAIG_INTERPOLATION
                    CraigData& partial_interpolant = craig_clause_data[cs[i]];
                    extendCraigInterpolantWithResolution(partial_interpolant, c[k], craig_unit_clause_data[toInt(~c[k])]);
#endif /* CRAIG_INTERPOLATION */

                    c[k--] = c[c.size()-1];
                    c.pop();

#ifdef CRAIG_INTERPOLATION
#ifdef CRAIG_INTERPOLATION_FUZZ_CRAIG
                    std::cerr << "fuzz: learn ";
                    if (partial_interpolant.craig_type == CraigClauseType::A_CLAUSE) std::cerr << "A;";
                    if (partial_interpolant.craig_type == CraigClauseType::B_CLAUSE) std::cerr << "B;";
                    if (partial_interpolant.craig_type == CraigClauseType::L_CLAUSE) std::cerr << "L;";
                    for (size_t i { 0u }; i < c.size(); i++) {
                        std::cerr << " " << (sign(c[i]) ? "-" : "") << (var(c[i]) + 1);
                    }
                    std::cerr << ";"
                        << " " << partial_interpolant.partial_interpolant_sym.getIndex()
                        << " " << partial_interpolant.partial_interpolant_asym.getIndex()
                        << " " << partial_interpolant.partial_interpolant_dual_sym.getIndex()
                        << " " << partial_interpolant.partial_interpolant_dual_asym.getIndex()
                        << "; removeSatisfied 1"
                        << std::endl;
#endif /* CRAIG_INTERPOLATION_FUZZ_CRAIG */
#endif /* CRAIG_INTERPOLATION */
                }
            cs[j++] = cs[i];
        }
    }
    cs.shrink(i - j);
}


void Solver::rebuildOrderHeap()
{
    vec<Var> vs;
    for (Var v = 0; v < nVars(); v++)
        if (decision[v] && value(v) == l_Undef)
            vs.push(v);
    order_heap.build(vs);
}


/*_________________________________________________________________________________________________
|
|  simplify : [void]  ->  [bool]
|
|  Description:
|    Simplify the clause database according to the current top-level assigment. Currently, the only
|    thing done here is the removal of satisfied clauses, but more things can be put here.
|________________________________________________________________________________________________@*/
bool Solver::simplify()
{
    assert(decisionLevel() == 0);

    if (!ok)
        return false;

    CRef confl = propagate();
    if (confl != CRef_Undef) {
#ifdef CRAIG_INTERPOLATION
        craig_interpolant = createCraigInterpolantForConflict(confl);
#ifdef CRAIG_INTERPOLATION_FUZZ_CRAIG
        std::cerr << "fuzz: learn ";
        if (craig_interpolant.craig_type == CraigClauseType::A_CLAUSE) std::cerr << "A;";
        if (craig_interpolant.craig_type == CraigClauseType::B_CLAUSE) std::cerr << "B;";
        if (craig_interpolant.craig_type == CraigClauseType::L_CLAUSE) std::cerr << "L;";
        std::cerr << ";"
            << " " << craig_interpolant.partial_interpolant_sym.getIndex()
            << " " << craig_interpolant.partial_interpolant_asym.getIndex()
            << " " << craig_interpolant.partial_interpolant_dual_sym.getIndex()
            << " " << craig_interpolant.partial_interpolant_dual_asym.getIndex()
            << "; simplify 1"
            << std::endl;
#endif /* CRAIG_INTERPOLATION_FUZZ_CRAIG */
#endif /* CRAIG_INTERPOLATION */
        return ok = false;
    }

    if (nAssigns() == simpDB_assigns || (simpDB_props > 0))
        return true;

    // Remove satisfied clauses:
    removeSatisfied(learnts);
    if (remove_satisfied){       // Can be turned off.
        removeSatisfied(clauses);

        // TODO: what todo in if 'remove_satisfied' is false?

        // Remove all released variables from the trail:
        for (int i = 0; i < released_vars.size(); i++){
            assert(seen[released_vars[i]] == 0);
            seen[released_vars[i]] = 1;
        }

        int i, j;
        for (i = j = 0; i < trail.size(); i++)
            if (seen[var(trail[i])] == 0)
                trail[j++] = trail[i];
        trail.shrink(i - j);
        //printf("trail.size()= %d, qhead = %d\n", trail.size(), qhead);
        qhead = trail.size();

        for (int i = 0; i < released_vars.size(); i++)
            seen[released_vars[i]] = 0;

        // Released variables are now ready to be reused:
        append(released_vars, free_vars);
        released_vars.clear();
    }
    checkGarbage();
    rebuildOrderHeap();

    simpDB_assigns = nAssigns();
    simpDB_props   = clauses_literals + learnts_literals;   // (shouldn't depend on stats really, but it will do for now)

    return true;
}


/*_________________________________________________________________________________________________
|
|  search : (nof_conflicts : int) (params : const SearchParams&)  ->  [lbool]
|
|  Description:
|    Search for a model the specified number of conflicts.
|    NOTE! Use negative value for 'nof_conflicts' indicate infinity.
|
|  Output:
|    'l_True' if a partial assigment that is consistent with respect to the clauseset is found. If
|    all variables are decision variables, this means that the clause set is satisfiable. 'l_False'
|    if the clause set is unsatisfiable. 'l_Undef' if the bound on number of conflicts is reached.
|________________________________________________________________________________________________@*/
lbool Solver::search(int nof_conflicts)
{
    assert(ok);
    int         backtrack_level;
    int         conflictC = 0;
    vec<Lit>    learnt_clause;
    starts++;

    for (;;){
        CRef confl = propagate();
        if (confl != CRef_Undef){
            // CONFLICT
            conflicts++; conflictC++;
            if (decisionLevel() == 0) {
#ifdef CRAIG_INTERPOLATION
                craig_interpolant = createCraigInterpolantForConflict(confl);
#ifdef CRAIG_INTERPOLATION_FUZZ_CRAIG
                std::cerr << "fuzz: learn ";
                if (craig_interpolant.craig_type == CraigClauseType::A_CLAUSE) std::cerr << "A;";
                if (craig_interpolant.craig_type == CraigClauseType::B_CLAUSE) std::cerr << "B;";
                if (craig_interpolant.craig_type == CraigClauseType::L_CLAUSE) std::cerr << "L;";
                std::cerr << ";"
                    << " " << craig_interpolant.partial_interpolant_sym.getIndex()
                    << " " << craig_interpolant.partial_interpolant_asym.getIndex()
                    << " " << craig_interpolant.partial_interpolant_dual_sym.getIndex()
                    << " " << craig_interpolant.partial_interpolant_dual_asym.getIndex()
                    << "; search 1"
                    << std::endl;
#endif /* CRAIG_INTERPOLATION_FUZZ_CRAIG */
#endif /* CRAIG_INTERPOLATION */
                return l_False;
            }

            learnt_clause.clear();
#ifndef CRAIG_INTERPOLATION
            analyze(confl, learnt_clause, backtrack_level);
#else /* CRAIG_INTERPOLATION */
            CraigData partial_interpolant;
            analyze(confl, learnt_clause, backtrack_level, partial_interpolant);
#endif /* CRAIG_INTERPOLATION */
            cancelUntil(backtrack_level);

            if (learnt_clause.size() == 1){
#ifdef CRAIG_INTERPOLATION
                craig_unit_clause_data.insert(toInt(learnt_clause[0]), partial_interpolant);
#endif /* CRAIG_INTERPOLATION */
                uncheckedEnqueue(learnt_clause[0]);
            }else{
                CRef cr = ca.alloc(learnt_clause, true);
#ifdef CRAIG_INTERPOLATION
                craig_clause_data.insert(cr, partial_interpolant);
#ifdef CRAIG_INTERPOLATION_FUZZ_CRAIG
                std::cerr << "fuzz: learn ";
                if (partial_interpolant.craig_type == CraigClauseType::A_CLAUSE) std::cerr << "A;";
                if (partial_interpolant.craig_type == CraigClauseType::B_CLAUSE) std::cerr << "B;";
                if (partial_interpolant.craig_type == CraigClauseType::L_CLAUSE) std::cerr << "L;";
                for (size_t i { 0u }; i < learnt_clause.size(); i++) {
                    std::cerr << " " << (sign(learnt_clause[i]) ? "-" : "") << (var(learnt_clause[i]) + 1);
                }
                std::cerr << ";"
                    << " " << partial_interpolant.partial_interpolant_sym.getIndex()
                    << " " << partial_interpolant.partial_interpolant_asym.getIndex()
                    << " " << partial_interpolant.partial_interpolant_dual_sym.getIndex()
                    << " " << partial_interpolant.partial_interpolant_dual_asym.getIndex()
                    << "; search 2"
                    << std::endl;
#endif /* CRAIG_INTERPOLATION_FUZZ_CRAIG */
#endif /* CRAIG_INTERPOLATION */
                learnts.push(cr);
                attachClause(cr);
                claBumpActivity(ca[cr]);
                uncheckedEnqueue(learnt_clause[0], cr);
            }

            varDecayActivity();
            claDecayActivity();

            if (--learntsize_adjust_cnt == 0){
                learntsize_adjust_confl *= learntsize_adjust_inc;
                learntsize_adjust_cnt    = (int)learntsize_adjust_confl;
                max_learnts             *= learntsize_inc;

                if (verbosity >= 1)
                    printf("| %9d | %7d %8d %8d | %8d %8d %6.0f | %6.3f %% |\n",
                           (int)conflicts,
                           (int)dec_vars - (trail_lim.size() == 0 ? trail.size() : trail_lim[0]), nClauses(), (int)clauses_literals,
                           (int)max_learnts, nLearnts(), (double)learnts_literals/nLearnts(), progressEstimate()*100);
            }

        }else{
            // NO CONFLICT
            if ((nof_conflicts >= 0 && conflictC >= nof_conflicts) || !withinBudget()){
                // Reached bound on number of conflicts:
                progress_estimate = progressEstimate();
                cancelUntil(0);
                return l_Undef; }

            // Simplify the set of problem clauses:
            if (decisionLevel() == 0 && !simplify())
                return l_False;

            if (learnts.size()-nAssigns() >= max_learnts)
                // Reduce the set of learnt clauses:
                reduceDB();

            Lit next = lit_Undef;
            while (decisionLevel() < assumptions.size()){
                // Perform user provided assumption:
                Lit p = assumptions[decisionLevel()];
#ifdef CRAIG_INTERPOLATION
#ifdef CRAIG_INTERPOLATION_DEBUG
                std::cerr << "Add assumption (";
                if (craig_var_types[var(p)] == CraigVarType::A_LOCAL) std::cerr << "a: ";
                if (craig_var_types[var(p)] == CraigVarType::B_LOCAL) std::cerr << "b: ";
                if (craig_var_types[var(p)] == CraigVarType::GLOBAL) std::cerr << "g: ";
                std::cerr << (sign(p) ? "-" : "") << (var(p) + 1) << ")" << std::endl;
#endif /* CRAIG_INTERPOLATION_DEBUG */
#endif /* CRAIG_INTERPOLATION */
                if (value(p) == l_True){
                    // Dummy decision level:
                    newDecisionLevel();
                }else if (value(p) == l_False){
                    analyzeFinal(~p, conflict);
                    return l_False;
                }else{
                    next = p;
                    break;
                }
            }

            if (next == lit_Undef){
                // New variable decision:
                decisions++;
                next = pickBranchLit();

                if (next == lit_Undef)
                    // Model found:
                    return l_True;
            }

#ifdef CRAIG_INTERPOLATION
#ifdef CRAIG_INTERPOLATION_DEBUG
            std::cerr << "Decide " << (sign(next) ? "-" : "") << (var(next) + 1) << " for level " << (decisionLevel() + 1) << std::endl;
#endif /* CRAIG_INTERPOLATION_DEBUG */
#endif /* CRAIG_INTERPOLATION */

            // Increase decision level and enqueue 'next'
            newDecisionLevel();
            uncheckedEnqueue(next);
        }
    }
}


double Solver::progressEstimate() const
{
    double  progress = 0;
    double  F = 1.0 / nVars();

    for (int i = 0; i <= decisionLevel(); i++){
        int beg = i == 0 ? 0 : trail_lim[i - 1];
        int end = i == decisionLevel() ? trail.size() : trail_lim[i];
        progress += pow(F, i) * (end - beg);
    }

    return progress / nVars();
}

/*
  Finite subsequences of the Luby-sequence:

  0: 1
  1: 1 1 2
  2: 1 1 2 1 1 2 4
  3: 1 1 2 1 1 2 4 1 1 2 1 1 2 4 8
  ...


 */

static double luby(double y, int x){

    // Find the finite subsequence that contains index 'x', and the
    // size of that subsequence:
    int size, seq;
    for (size = 1, seq = 0; size < x+1; seq++, size = 2*size+1);

    while (size-1 != x){
        size = (size-1)>>1;
        seq--;
        x = x % size;
    }

    return pow(y, seq);
}

// NOTE: assumptions passed in member-variable 'assumptions'.
lbool Solver::solve_()
{
    start_time = time(NULL);
    if (timeout <= 0.0) {
        return  l_Undef;
    }

#ifdef CRAIG_INTERPOLATION
#ifdef CRAIG_INTERPOLATION_FUZZ_CRAIG
    std::cerr << "fuzz: solve prepare" << std::endl;
#endif /* CRAIG_INTERPOLATION_FUZZ_CRAIG */
#ifdef CRAIG_INTERPOLATION_DEBUG_SOLVE
    std::cerr << "SAT Problem:" << std::endl;
    for (int t = 0; t < clauses.size(); ++t) {
        auto& cl = ca[clauses[t]];
        auto const& craig_data = craig_clause_data[clauses[t]];
        if (craig_type == CraigClauseType::A_CLAUSE) {
            std::cerr << "    A-clause: (";
        } else if (craig_type == CraigClauseType::B_CLAUSE) {
            std::cerr << "    B-clause: (";
        } else {
            std::cerr << "    L-clause: (";
        }
        for (int i = 0; i < cl.size(); ++i) {
            if (i != 0)  std::cerr << ", ";
            std::cerr << toInt(cl[i]);
        }
        std::cerr << ")" << std::endl;
    }
    for (int t = 0; t < assumptions.size(); ++t) {
        std::cerr << "    Assumption: (" << toInt(assumptions[t]) << ")" << std::endl;
    }
#endif /* CRAIG_INTERPOLATION_DEBUG_SOLVE */

    for (int t = 0; t < assumptions.size(); ++t) {
        CraigData craig_data = createCraigInterpolantForAssumption(assumptions[t]);
        craig_unit_clause_data.insert(toInt(assumptions[t]), craig_data);

#ifdef CRAIG_INTERPOLATION_FUZZ_CRAIG
        CraigVarType varType = craig_var_types[var(assumptions[t])];
        std::cerr << "fuzz: assume";
        if (varType == CraigVarType::A_LOCAL) std::cerr << " A";
        if (varType == CraigVarType::B_LOCAL) std::cerr << " B";
        if (varType == CraigVarType::GLOBAL) std::cerr << " L";
        std::cerr << "; " << (sign(assumptions[t]) ? "-" : "") << (var(assumptions[t]) + 1);
        std::cerr << ";"
            << " " << craig_data.partial_interpolant_sym.getIndex()
            << " " << craig_data.partial_interpolant_asym.getIndex()
            << " " << craig_data.partial_interpolant_dual_sym.getIndex()
            << " " << craig_data.partial_interpolant_dual_asym.getIndex()
            << "; solve 1"
            << std::endl;
#endif /* CRAIG_INTERPOLATION_FUZZ_CRAIG */
    }
#endif /* CRAIG_INTERPOLATION */

#ifdef CRAIG_INTERPOLATION_FUZZ_CRAIG
    std::cerr << "fuzz: solve begin" << std::endl;
#endif /* CRAIG_INTERPOLATION_FUZZ_CRAIG */

    model.clear();
    conflict.clear();
    if (!ok) {
#ifdef CRAIG_INTERPOLATION
#ifdef CRAIG_INTERPOLATION_FUZZ_CRAIG
        std::cerr << "fuzz: aigs ";
        craig_aig_sym.toDumpOutput(std::cerr); std::cerr << "; ";
        craig_aig_asym.toDumpOutput(std::cerr); std::cerr << "; ";
        craig_aig_dual_sym.toDumpOutput(std::cerr); std::cerr << "; ";
        craig_aig_dual_asym.toDumpOutput(std::cerr); std::cerr << std::endl;
        std::cerr << "fuzz: solve end" << std::endl;
#endif /* CRAIG_INTERPOLATION_FUZZ_CRAIG */
#endif /* CRAIG_INTERPOLATION */
        return l_False;
    }

    solves++;

    max_learnts = nClauses() * learntsize_factor;
    if (max_learnts < min_learnts_lim)
        max_learnts = min_learnts_lim;

    learntsize_adjust_confl   = learntsize_adjust_start_confl;
    learntsize_adjust_cnt     = (int)learntsize_adjust_confl;
    lbool   status            = l_Undef;

    if (verbosity >= 1){
        printf("============================[ Search Statistics ]==============================\n");
        printf("| Conflicts |          ORIGINAL         |          LEARNT          | Progress |\n");
        printf("|           |    Vars  Clauses Literals |    Limit  Clauses Lit/Cl |          |\n");
        printf("===============================================================================\n");
    }

    // Search:
    int curr_restarts = 0;
    while (status == l_Undef){
        double rest_base = luby_restart ? luby(restart_inc, curr_restarts) : pow(restart_inc, curr_restarts);
        status = search(rest_base * restart_first);
        if (!withinBudget()) break;
        curr_restarts++;
    }

    if (verbosity >= 1)
        printf("===============================================================================\n");


    if (status == l_True){
        // Extend & copy model:
        model.growTo(nVars());
        for (int i = 0; i < nVars(); i++) model[i] = value(i);
    }else if (status == l_False && conflict.size() == 0)
        ok = false;

    if (status == l_False) {
        computeReason();
    }

    cancelUntil(0);
#ifdef CRAIG_INTERPOLATION
    for (int t = 0; t < assumptions.size(); ++t) {
        craig_unit_clause_data.remove(toInt(assumptions[t]));
    }
#ifdef CRAIG_INTERPOLATION_FUZZ_CRAIG
    std::cerr << "fuzz: aigs ";
    craig_aig_sym.toDumpOutput(std::cerr); std::cerr << "; ";
    craig_aig_asym.toDumpOutput(std::cerr); std::cerr << "; ";
    craig_aig_dual_sym.toDumpOutput(std::cerr); std::cerr << "; ";
    craig_aig_dual_asym.toDumpOutput(std::cerr); std::cerr << std::endl;
    std::cerr << "fuzz: solve end" << std::endl;
#endif /* CRAIG_INTERPOLATION_FUZZ_CRAIG */
#endif /* CRAIG_INTERPOLATION */
    return status;
}


bool Solver::implies(const vec<Lit>& assumps, vec<Lit>& out)
{
    trail_lim.push(trail.size());
    for (int i = 0; i < assumps.size(); i++){
        Lit a = assumps[i];

        if (value(a) == l_False){
            cancelUntil(0);
            return false;
        }else if (value(a) == l_Undef)
            uncheckedEnqueue(a);
    }

    unsigned trail_before = trail.size();
    bool     ret          = true;
    if (propagate() == CRef_Undef){
        out.clear();
        for (int j = trail_before; j < trail.size(); j++)
            out.push(trail[j]);
    }else
        ret = false;

    cancelUntil(0);
    return ret;
}

#ifdef CRAIG_INTERPOLATION
bool Solver::isConstructionEnabled(const CraigConstruction& check) {
    return static_cast<uint8_t>(check) & static_cast<uint8_t>(craig_construction);
}

void Solver::setCraigConstruction(CraigConstruction construction) {
    craig_construction = construction;
#ifdef CRAIG_INTERPOLATION_FUZZ_CRAIG
    std::cerr << "fuzz: construction"
        << (isConstructionEnabled(CraigConstruction::SYMMETRIC) ? " 1" : " 0")
        << (isConstructionEnabled(CraigConstruction::ASYMMETRIC) ? " 1" : " 0")
        << (isConstructionEnabled(CraigConstruction::DUAL_SYMMETRIC) ? " 1" : " 0")
        << (isConstructionEnabled(CraigConstruction::DUAL_ASYMMETRIC) ? " 1" : " 0")
        << std::endl;
#endif /* CRAIG_INTERPOLATION_FUZZ_CRAIG */
}

CraigData Solver::createCraigInterpolantForAssumption(const Lit& literal) {
    CraigVarType varType = craig_var_types[var(literal)];

#ifdef CRAIG_INTERPOLATION_DEBUG
    std::cerr << "Create Assumption Interplant i" << craig_id << ": ";
    if (varType == CraigVarType::A_LOCAL) std::cerr << "A (a: ";
    if (varType == CraigVarType::B_LOCAL) std::cerr << "B (b: ";
    if (varType == CraigVarType::GLOBAL) std::cerr << "B (g: ";
    std::cerr << (sign(literal) ? "-" : "") << (var(literal) + 1) << "):" << std::endl;
#endif /* CRAIG_INTERPOLATION_DEBUG */

    CraigData result;
    if (varType == CraigVarType::A_LOCAL) {
        result = {
            .partial_interpolant_sym = craig_aig_sym.getFalse(),
            .partial_interpolant_asym = craig_aig_asym.getFalse(),
            .partial_interpolant_dual_sym = craig_aig_dual_sym.getTrue(),
            .partial_interpolant_dual_asym = craig_aig_dual_asym.getFalse(),
            .craig_type = CraigClauseType::A_CLAUSE,
            .craig_id = craig_id++
        };
    } else if (varType == CraigVarType::B_LOCAL) {
        result = {
            .partial_interpolant_sym = craig_aig_sym.getTrue(),
            .partial_interpolant_asym = craig_aig_asym.getTrue(),
            .partial_interpolant_dual_sym = craig_aig_dual_sym.getFalse(),
            .partial_interpolant_dual_asym = craig_aig_dual_asym.getTrue(),
            .craig_type = CraigClauseType::B_CLAUSE,
            .craig_id = craig_id++
        };
    } else if (varType == CraigVarType::GLOBAL) {
        result = {
            .partial_interpolant_sym = craig_aig_sym.getTrue(),
            .partial_interpolant_asym = craig_aig_asym.getTrue(),
            .partial_interpolant_dual_sym = craig_aig_dual_sym.getFalse(),
            .partial_interpolant_dual_asym = craig_aig_dual_asym.getFalse(),
            .craig_type = CraigClauseType::L_CLAUSE,
            .craig_id = craig_id++
        };
    }

#ifdef CRAIG_INTERPOLATION_DEBUG
    debugCraigInterpolants(result);
#endif /* CRAIG_INTERPOLATION_DEBUG */

    return result;
}

CraigData Solver::createCraigInterpolantForPureClause(const vec<Lit>& clause, const CraigClauseType& craig_type, bool learnt) {
    CraigData result {
        .partial_interpolant_sym = craig_aig_sym.getTrue(),
        .partial_interpolant_asym = craig_aig_asym.getTrue(),
        .partial_interpolant_dual_sym = craig_aig_dual_sym.getTrue(),
        .partial_interpolant_dual_asym = craig_aig_dual_asym.getTrue(),
        .craig_type = craig_type,
        .craig_id = craig_id++
    };

#ifdef CRAIG_INTERPOLATION_DEBUG
    std::cerr << "    Create Interpolant i" << result.craig_id << ": ";
    if (craig_type == CraigClauseType::A_CLAUSE) std::cerr << "A (";
    if (craig_type == CraigClauseType::B_CLAUSE) std::cerr << "B (";
    if (craig_type == CraigClauseType::L_CLAUSE) std::cerr << "L (";
    for (size_t i { 0u }; i < clause.size(); i++) {
        if (i != 0) std::cerr << ", ";
        if (craig_var_types[var(clause[i])] == CraigVarType::A_LOCAL) std::cerr << "a: ";
        if (craig_var_types[var(clause[i])] == CraigVarType::B_LOCAL) std::cerr << "b: ";
        if (craig_var_types[var(clause[i])] == CraigVarType::GLOBAL) std::cerr << "g: ";
        std::cerr << (sign(clause[i]) ? "-" : "") << (var(clause[i]) + 1);
    }
    std::cerr << ")" << std::endl;
#endif /* CRAIG_INTERPOLATION_DEBUG */

    if (isConstructionEnabled(CraigConstruction::SYMMETRIC)) {
        if (craig_type == CraigClauseType::A_CLAUSE) {
            result.partial_interpolant_sym = craig_aig_sym.getFalse();
        } else if (craig_type == CraigClauseType::B_CLAUSE) {
            result.partial_interpolant_sym = craig_aig_sym.getTrue();
        }
    }
    if (isConstructionEnabled(CraigConstruction::ASYMMETRIC)) {
        if (craig_type == CraigClauseType::A_CLAUSE) {
            std::vector<AigEdge> literals;
            for (int i { 0 }; i < clause.size(); ++i) {
                if (craig_var_types[var(clause[i])] == CraigVarType::GLOBAL) {
                    literals.push_back(craig_aig_asym.createLiteral(clause[i]));
                }
            }
            result.partial_interpolant_asym = craig_aig_asym.createOr(literals);
        } else if (craig_type == CraigClauseType::B_CLAUSE) {
            result.partial_interpolant_asym = craig_aig_asym.getTrue();
        }
    }
    if (isConstructionEnabled(CraigConstruction::DUAL_SYMMETRIC)) {
        if (craig_type == CraigClauseType::A_CLAUSE) {
            result.partial_interpolant_dual_sym = craig_aig_dual_sym.getTrue();
        } else if (craig_type == CraigClauseType::B_CLAUSE) {
            result.partial_interpolant_dual_sym = craig_aig_dual_sym.getFalse();
        }
    }
    if (isConstructionEnabled(CraigConstruction::DUAL_ASYMMETRIC)) {
        if (craig_type == CraigClauseType::A_CLAUSE) {
            result.partial_interpolant_dual_asym = craig_aig_dual_asym.getFalse();
        } else if (craig_type == CraigClauseType::B_CLAUSE) {
            std::vector<AigEdge> literals;
            for (int i { 0 }; i < clause.size(); ++i) {
                if (craig_var_types[var(clause[i])] == CraigVarType::GLOBAL) {
                    literals.push_back(craig_aig_dual_asym.createLiteral(~clause[i]));
                }
            }
            result.partial_interpolant_dual_asym = craig_aig_dual_asym.createAnd(literals);
        }
    }

#ifdef CRAIG_INTERPOLATION_DEBUG
    debugCraigInterpolants(result);
#endif /* CRAIG_INTERPOLATION_DEBUG */

    return result;
}

void Solver::extendCraigInterpolantWithResolution(CraigData& result, const Lit& literal, const CraigData& craig_data) {
#ifdef CRAIG_INTERPOLATION_DEBUG
    std::cerr << "    Extend Interpolant i" << result.craig_id << " with i" << craig_data.craig_id << ": ";
    if (craig_data.craig_type == CraigClauseType::A_CLAUSE) std::cerr << "pivot A (";
    if (craig_data.craig_type == CraigClauseType::B_CLAUSE) std::cerr << "pivot B (";
    if (craig_data.craig_type == CraigClauseType::L_CLAUSE) std::cerr << "pivot L (";
    if (craig_var_types[var(literal)] == CraigVarType::A_LOCAL) std::cerr << "a: ";
    if (craig_var_types[var(literal)] == CraigVarType::B_LOCAL) std::cerr << "b: ";
    if (craig_var_types[var(literal)] == CraigVarType::GLOBAL) std::cerr << "g: ";
    std::cerr << (sign(literal) ? "-" : "") << (var(literal) + 1) << ")" << std::endl;
#endif /* CRAIG_INTERPOLATION_DEBUG */

    if (result.craig_type != craig_data.craig_type) {
        result.craig_type = CraigClauseType::L_CLAUSE;
    }

    if (isConstructionEnabled(CraigConstruction::SYMMETRIC)) {
        if (craig_var_types[var(literal)] == CraigVarType::A_LOCAL) {
            result.partial_interpolant_sym = craig_aig_sym.createOr(
                result.partial_interpolant_sym,
                craig_data.partial_interpolant_sym
            );
        } else if (craig_var_types[var(literal)] == CraigVarType::B_LOCAL) {
            result.partial_interpolant_sym = craig_aig_sym.createAnd(
                result.partial_interpolant_sym,
                craig_data.partial_interpolant_sym
            );
        } else {
            result.partial_interpolant_sym = craig_aig_sym.createAnd(
                craig_aig_sym.createOr(result.partial_interpolant_sym,     craig_aig_sym.createLiteral( literal)),
                craig_aig_sym.createOr(craig_data.partial_interpolant_sym, craig_aig_sym.createLiteral(~literal))
            );
        }
    }
    if (isConstructionEnabled(CraigConstruction::ASYMMETRIC)) {
        if (craig_var_types[var(literal)] == CraigVarType::A_LOCAL) {
            result.partial_interpolant_asym = craig_aig_asym.createOr(
                result.partial_interpolant_asym,
                craig_data.partial_interpolant_asym
            );
        } else {
            result.partial_interpolant_asym = craig_aig_asym.createAnd(
                result.partial_interpolant_asym,
                craig_data.partial_interpolant_asym
            );
        }
    }
    if (isConstructionEnabled(CraigConstruction::DUAL_SYMMETRIC)) {
        if (craig_var_types[var(literal)] == CraigVarType::A_LOCAL) {
            result.partial_interpolant_dual_sym = craig_aig_dual_sym.createAnd(
                result.partial_interpolant_dual_sym,
                craig_data.partial_interpolant_dual_sym
            );
        } else if (craig_var_types[var(literal)] == CraigVarType::B_LOCAL) {
            result.partial_interpolant_dual_sym = craig_aig_dual_sym.createOr(
                result.partial_interpolant_dual_sym,
                craig_data.partial_interpolant_dual_sym
            );
        } else {
            result.partial_interpolant_dual_sym = craig_aig_dual_sym.createOr(
                craig_aig_dual_sym.createAnd(result.partial_interpolant_dual_sym,     craig_aig_dual_sym.createLiteral(~literal)),
                craig_aig_dual_sym.createAnd(craig_data.partial_interpolant_dual_sym, craig_aig_dual_sym.createLiteral( literal))
            );
        }
    }
    if (isConstructionEnabled(CraigConstruction::DUAL_ASYMMETRIC)) {
        if (craig_var_types[var(literal)] == CraigVarType::B_LOCAL) {
            result.partial_interpolant_dual_asym = craig_aig_dual_asym.createAnd(
                result.partial_interpolant_dual_asym,
                craig_data.partial_interpolant_dual_asym
            );
        } else {
            result.partial_interpolant_dual_asym = craig_aig_dual_asym.createOr(
                result.partial_interpolant_dual_asym,
                craig_data.partial_interpolant_dual_asym
            );
        }
    }

#ifdef CRAIG_INTERPOLATION_DEBUG
    std::cerr << "    Resulting Interpolant:" << std::endl;
    debugCraigInterpolants(result);
#endif /* CRAIG_INTERPOLATION_DEBUG */
}

CraigData Solver::createCraigInterpolantForConflict(const CRef& confl) {
    CraigData result = craig_clause_data[confl];
    Clause& clause = ca[confl];

#ifdef CRAIG_INTERPOLATION_DEBUG
    std::cerr << "Create Interpolant for conflict: (";
    for (size_t i { 0u }; i < clause.size(); i++) {
        if (i != 0) std::cerr << ", ";
        if (craig_var_types[var(clause[i])] == CraigVarType::A_LOCAL) std::cerr << "a: ";
        if (craig_var_types[var(clause[i])] == CraigVarType::B_LOCAL) std::cerr << "b: ";
        if (craig_var_types[var(clause[i])] == CraigVarType::GLOBAL) std::cerr << "g: ";
        std::cerr << (sign(clause[i]) ? "-" : "") << (var(clause[i]) + 1);
    }
    std::cerr << ")" << std::endl;

    std::cerr << "    Clone Interpolant i" << craig_clause_data[confl].craig_id << " to i" << craig_id << std::endl;
#endif /* CRAIG_INTERPOLATION_DEBUG */
    result.craig_id = craig_id++;

    for (int i = 0; i < clause.size(); i++) {
        extendCraigInterpolantWithResolution(result, clause[i], craig_unit_clause_data[toInt(~clause[i])]);
    }

#ifdef CRAIG_INTERPOLATION_DEBUG
    std::cerr << "    Conflict Interpolant:" << std::endl;
    debugCraigInterpolants(result);
#endif /* CRAIG_INTERPOLATION_DEBUG */

    return result;
}

void Solver::debugCraigInterpolants(const CraigData& craig_data) {
#ifdef CRAIG_INTERPOLATION_DEBUG
    std::cerr << "        Craig type: ";
    if (craig_data.craig_type == CraigClauseType::A_CLAUSE) std::cerr << "A";
    if (craig_data.craig_type == CraigClauseType::B_CLAUSE) std::cerr << "B";
    if (craig_data.craig_type == CraigClauseType::L_CLAUSE) std::cerr << "L";
    std::cerr << std::endl;

    std::cerr << "        Craig SYMMETRIC: ";
    if (isConstructionEnabled(CraigConstruction::SYMMETRIC)) {
        craig_aig_sym.toShortOutput(craig_data.partial_interpolant_sym, std::cerr);
    } else {
        std::cerr << "disabled";
    }
    std::cerr << std::endl;

    std::cerr << "        Craig ASYMMETRIC: ";
    if (isConstructionEnabled(CraigConstruction::ASYMMETRIC)) {
        craig_aig_asym.toShortOutput(craig_data.partial_interpolant_asym, std::cerr);
    } else {
        std::cerr << "disabled";
    }
    std::cerr << std::endl;

    std::cerr << "        Craig DUAL_SYMMETRIC (final inverted): ";
    if (isConstructionEnabled(CraigConstruction::DUAL_SYMMETRIC)) {
        craig_aig_dual_sym.toShortOutput(craig_data.partial_interpolant_dual_sym, std::cerr);
    } else {
        std::cerr << "disabled";
    }
    std::cerr << std::endl;

    std::cerr << "        Craig DUAL_ASYMMETRIC: ";
    if (isConstructionEnabled(CraigConstruction::DUAL_ASYMMETRIC)) {
        craig_aig_dual_asym.toShortOutput(craig_data.partial_interpolant_dual_asym, std::cerr);
    } else {
        std::cerr << "disabled";
    }
    std::cerr << std::endl;
#endif /* CRAIG_INTERPOLATION_DEBUG */
}

CraigCnfType Solver::createCraigCnf(CraigInterpolant interpolant, vec<vec<Lit>>& cnf, Var& nextFreeVariable) {
    assert(craig_interpolant.craig_id != std::numeric_limits<size_t>::max());
    cnf.clear();

#ifdef CRAIG_INTERPOLATION_DEBUG
    std::cerr << "Final Craig Interpolant i" << craig_interpolant.craig_id << ":" << std::endl;
    debugCraigInterpolants(craig_interpolant);
#endif /* CRAIG_INTERPOLATION_DEBUG */

    bool build_cnf_sym = false;
    bool build_cnf_asym = false;
    bool build_cnf_dual_sym = false;
    bool build_cnf_dual_asym = false;
    switch (interpolant) {
        case CraigInterpolant::SYMMETRIC:
            build_cnf_sym = isConstructionEnabled(CraigConstruction::SYMMETRIC);
            break;
        case CraigInterpolant::ASYMMETRIC:
            build_cnf_asym = isConstructionEnabled(CraigConstruction::ASYMMETRIC);
            break;
        case CraigInterpolant::DUAL_SYMMETRIC:
            build_cnf_dual_sym = isConstructionEnabled(CraigConstruction::DUAL_SYMMETRIC);
            break;
        case CraigInterpolant::DUAL_ASYMMETRIC:
            build_cnf_dual_asym = isConstructionEnabled(CraigConstruction::DUAL_ASYMMETRIC);
            break;
        case CraigInterpolant::INTERSECTION:
        case CraigInterpolant::UNION:
        case CraigInterpolant::SMALLEST:
        case CraigInterpolant::LARGEST:
            build_cnf_sym = isConstructionEnabled(CraigConstruction::SYMMETRIC);
            build_cnf_asym = isConstructionEnabled(CraigConstruction::ASYMMETRIC);
            build_cnf_dual_sym = isConstructionEnabled(CraigConstruction::DUAL_SYMMETRIC);
            build_cnf_dual_asym = isConstructionEnabled(CraigConstruction::DUAL_ASYMMETRIC);
            break;
        default: __builtin_unreachable();
    }

    vec<vec<Lit>> craig_cnf_sym;
    vec<vec<Lit>> craig_cnf_asym;
    vec<vec<Lit>> craig_cnf_dual_sym;
    vec<vec<Lit>> craig_cnf_dual_asym;
    CraigCnfType craig_cnf_type_sym = CraigCnfType::None;
    CraigCnfType craig_cnf_type_asym = CraigCnfType::None;
    CraigCnfType craig_cnf_type_dual_sym = CraigCnfType::None;
    CraigCnfType craig_cnf_type_dual_asym = CraigCnfType::None;

    if (build_cnf_sym)
        craig_cnf_type_sym = craig_aig_sym.createCnf(craig_interpolant.partial_interpolant_sym, craig_cnf_sym, nextFreeVariable);
    if (build_cnf_asym)
        craig_cnf_type_asym = craig_aig_asym.createCnf(craig_interpolant.partial_interpolant_asym, craig_cnf_asym, nextFreeVariable);
    if (build_cnf_dual_sym)
        craig_cnf_type_dual_sym = craig_aig_dual_sym.createCnf(craig_interpolant.partial_interpolant_dual_sym, craig_cnf_dual_sym, nextFreeVariable);
    if (build_cnf_dual_asym)
        craig_cnf_type_dual_asym = craig_aig_dual_asym.createCnf(craig_interpolant.partial_interpolant_dual_asym, craig_cnf_dual_asym, nextFreeVariable);

    // Dual Craig interpolants have to be inverted.
    // However, the construction rules for the dual asymmetric interpolant already incorporates the negation.
    // So only the dual symmetric interpolant needs to be negated.
    if (craig_cnf_type_dual_sym == CraigCnfType::Constant1) {
        craig_cnf_dual_sym.push();
        craig_cnf_type_dual_sym = CraigCnfType::Constant0;
    } else if (craig_cnf_type_dual_sym == CraigCnfType::Constant0) {
        craig_cnf_dual_sym.pop();
        craig_cnf_type_dual_sym = CraigCnfType::Constant1;
    } else if (craig_cnf_type_dual_sym == CraigCnfType::Normal) {
        craig_cnf_dual_sym.last()[0] = ~craig_cnf_dual_sym.last()[0];
    }

    if (interpolant == CraigInterpolant::NONE) {
        return CraigCnfType::None;
    } else if (interpolant == CraigInterpolant::SYMMETRIC) {
        craig_cnf_sym.moveTo(cnf);
        return craig_cnf_type_sym;
    } else if (interpolant == CraigInterpolant::ASYMMETRIC) {
        craig_cnf_asym.moveTo(cnf);
        return craig_cnf_type_asym;
    } else if (interpolant == CraigInterpolant::DUAL_SYMMETRIC) {
        craig_cnf_dual_sym.moveTo(cnf);
        return craig_cnf_type_dual_sym;
    } else if (interpolant == CraigInterpolant::DUAL_ASYMMETRIC) {
        craig_cnf_dual_asym.moveTo(cnf);
        return craig_cnf_type_dual_asym;
    }

    std::vector<std::tuple<decltype(craig_cnf_sym)*, CraigCnfType>> craigCnfs { };
    if (craig_cnf_type_sym != CraigCnfType::None) craigCnfs.push_back({ &craig_cnf_sym, craig_cnf_type_sym });
    if (craig_cnf_type_asym != CraigCnfType::None) craigCnfs.push_back({ &craig_cnf_asym, craig_cnf_type_asym });
    if (craig_cnf_type_dual_sym != CraigCnfType::None) craigCnfs.push_back({ &craig_cnf_dual_sym, craig_cnf_type_dual_sym });
    if (craig_cnf_type_dual_asym != CraigCnfType::None) craigCnfs.push_back({ &craig_cnf_dual_asym, craig_cnf_type_dual_asym });

    if (craigCnfs.size() == 0) {
        return CraigCnfType::None;
    } else if (craigCnfs.size() == 1) {
        std::get<0>(craigCnfs[0])->moveTo(cnf);
        return std::get<1>(craigCnfs[0]);
    }

    // We have at least two Craig interpolants for the following computations.
    if (interpolant == CraigInterpolant::UNION) {
        bool allConstantOne = true;
        for (auto [craigCnf, craigCnfType] : craigCnfs) {
            if (craigCnfType == CraigCnfType::Constant0) {
                craigCnf->moveTo(cnf);
                return CraigCnfType::Constant0;
            }
            allConstantOne &= (craigCnfType == CraigCnfType::Constant1);
        }
        if (allConstantOne) {
            return CraigCnfType::Constant1;
        }

        for (auto [craigCnf, craigCnfType] : craigCnfs) {
            if (craigCnfType == CraigCnfType::Normal) {
                int i = 0, j = cnf.size(); cnf.growTo(cnf.size() + craigCnf->size() - 1);
                for (; i < craigCnf->size() - 1u; i++, j++) (*craigCnf)[i].moveTo(cnf[j]);
            }
        }

        // Create trigger (t) that enforces all CNF parts.
        Var craig_trigger = nextFreeVariable++;
        vec<Lit> craig_trigger_clause(1, mkLit(craig_trigger, false));
        for (auto [craigCnf, craigCnfType] : craigCnfs) {
            if (craigCnfType == CraigCnfType::Normal) {
                // The positive trigger implies that all CNF parts are enabled: (t -> t_1) = (-t v t_1)
                cnf.push();
                cnf.last().growTo(2);
                cnf.last()[0] = mkLit(craig_trigger, true);
                cnf.last()[1] = craigCnf->last()[0];
                // The negative trigger implies that at least one of the CNF parts is not enabled: (-t -> (-t_1 v ... v -t_n)) = (t v -t_1 v ... -t_n)
                craig_trigger_clause.push(~(craigCnf->last()[0]));
            }
        }
        cnf.push(); craig_trigger_clause.moveTo(cnf.last());
        cnf.push(); cnf.last().push(mkLit(craig_trigger, false));

        return CraigCnfType::Normal;
    } else if (interpolant == CraigInterpolant::INTERSECTION) {
        bool allConstantZero = true;
        for (auto [craigCnf, craigCnfType] : craigCnfs) {
            if (craigCnfType == CraigCnfType::Constant1) {
                craigCnf->moveTo(cnf);
                return CraigCnfType::Constant1;
            }
            allConstantZero &= (craigCnfType == CraigCnfType::Constant0);
        }
        if (allConstantZero) {
            cnf.push();
            return CraigCnfType::Constant0;
        }

        for (auto [craigCnf, craigCnfType] : craigCnfs) {
            if (craigCnfType == CraigCnfType::Normal) {
                int i = 0, j = cnf.size(); cnf.growTo(cnf.size() + craigCnf->size() - 1);
                for (; i < craigCnf->size() - 1u; i++, j++) (*craigCnf)[i].moveTo(cnf[j]);
            }
        }

        // Create trigger (t) that enforces all CNF parts.
        Var craig_trigger = nextFreeVariable++;
        vec<Lit> craig_trigger_clause(1, mkLit(craig_trigger, true));
        for (auto [craigCnf, craigCnfType] : craigCnfs) {
            if (craigCnfType == CraigCnfType::Normal) {
                // The positive trigger implies that one of the CNF parts is enabled: (t -> (t_1 v ... v t_n)) = (-t v t_1 v ... t_n)
                craig_trigger_clause.push(craigCnf->last()[0]);
                // The negative trigger implies that at all CNF parts are not enabled: (-t -> -t_1) = (t v -t_1)
                cnf.push();
                cnf.last().growTo(2);
                cnf.last()[0] = mkLit(craig_trigger, false);
                cnf.last()[1] = ~(craigCnf->last()[0]);
            }
        }
        cnf.push(); craig_trigger_clause.moveTo(cnf.last());
        cnf.push(); cnf.last().push(mkLit(craig_trigger, false));

        return CraigCnfType::Normal;
    } else if (interpolant == CraigInterpolant::SMALLEST) {
        auto minimum = std::min_element(craigCnfs.begin(), craigCnfs.end(), [](auto const& elem1, auto const& elem2) {
            auto const& [elem1Cnf, elem1CnfType] = elem1;
            auto const& [elem2Cnf, elem2CnfType] = elem2;
            return (elem1Cnf->size() < elem2Cnf->size());
        });
        auto [minCnf, minCnfType] = *minimum;
        minCnf->moveTo(cnf);
        return minCnfType;
    } else if (interpolant == CraigInterpolant::LARGEST) {
        auto maximum = std::max_element(craigCnfs.begin(), craigCnfs.end(), [](auto const& elem1, auto const& elem2) {
            auto const& [elem1Cnf, elem1CnfType] = elem1;
            auto const& [elem2Cnf, elem2CnfType] = elem2;
            return (elem1Cnf->size() < elem2Cnf->size());
        });
        auto [maxCnf, maxCnfType] = *maximum;
        maxCnf->moveTo(cnf);
        return maxCnfType;
    } else {
        throw std::runtime_error("Seleted craig interpolation type not supported");
    }
}

#endif /* CRAIG_INTERPOLATION */

//=================================================================================================
// Writing CNF to DIMACS:
//
// FIXME: this needs to be rewritten completely.

static Var mapVar(Var x, vec<Var>& map, Var& max)
{
    if (map.size() <= x || map[x] == -1){
        map.growTo(x+1, -1);
        map[x] = max++;
    }
    return map[x];
}


void Solver::toDimacs(FILE* f, Clause& c, vec<Var>& map, Var& max)
{
    if (satisfied(c)) return;

    for (int i = 0; i < c.size(); i++)
        if (value(c[i]) != l_False)
            fprintf(f, "%s%d ", sign(c[i]) ? "-" : "", mapVar(var(c[i]), map, max)+1);
    fprintf(f, "0\n");
}


void Solver::toDimacs(const char *file, const vec<Lit>& assumps)
{
    FILE* f = fopen(file, "wr");
    if (f == NULL)
        fprintf(stderr, "could not open file %s\n", file), exit(1);
    toDimacs(f, assumps);
    fclose(f);
}


void Solver::toDimacs(FILE* f, const vec<Lit>& assumps)
{
    // Handle case when solver is in contradictory state:
    if (!ok){
        fprintf(f, "p cnf 1 2\n1 0\n-1 0\n");
        return; }

    vec<Var> map; Var max = 0;

    // Cannot use removeClauses here because it is not safe
    // to deallocate them at this point. Could be improved.
    int cnt = 0;
    for (int i = 0; i < clauses.size(); i++)
        if (!satisfied(ca[clauses[i]]))
            cnt++;

    for (int i = 0; i < clauses.size(); i++)
        if (!satisfied(ca[clauses[i]])){
            Clause& c = ca[clauses[i]];
            for (int j = 0; j < c.size(); j++)
                if (value(c[j]) != l_False)
                    mapVar(var(c[j]), map, max);
        }

    // Assumptions are added as unit clauses:
    cnt += assumps.size();

    fprintf(f, "p cnf %d %d\n", max, cnt);

    for (int i = 0; i < assumps.size(); i++){
        assert(value(assumps[i]) != l_False);
        fprintf(f, "%s%d 0\n", sign(assumps[i]) ? "-" : "", mapVar(var(assumps[i]), map, max)+1);
    }

    for (int i = 0; i < clauses.size(); i++)
        toDimacs(f, ca[clauses[i]], map, max);

    if (verbosity > 0)
        printf("Wrote DIMACS with %d variables and %d clauses.\n", max, cnt);
}


void Solver::printStats() const
{
    double cpu_time = cpuTime();
    double mem_used = memUsedPeak();
    printf("restarts              : %" PRIu64 "\n", starts);
    printf("conflicts             : %-12" PRIu64 "   (%.0f /sec)\n", conflicts   , conflicts   /cpu_time);
    printf("decisions             : %-12" PRIu64 "   (%4.2f %% random) (%.0f /sec)\n", decisions, (float)rnd_decisions*100 / (float)decisions, decisions   /cpu_time);
    printf("propagations          : %-12" PRIu64 "   (%.0f /sec)\n", propagations, propagations/cpu_time);
    printf("conflict literals     : %-12" PRIu64 "   (%4.2f %% deleted)\n", tot_literals, (max_literals - tot_literals)*100 / (double)max_literals);
    if (mem_used != 0) printf("Memory used           : %.2f MB\n", mem_used);
    printf("CPU time              : %g s\n", cpu_time);
}


//=================================================================================================
// Garbage Collection methods:

void Solver::relocAll(ClauseAllocator& to)
{
#ifdef CRAIG_INTERPOLATION
    const auto craig_reloc = [&](CRef cr_from, CRef cr_to) {
        assert(ca[cr_from].size() > 1);
        assert(craig_clause_data.has(cr_from));
        if (reloc_craig_clause_data.has(cr_to)) return;
        reloc_craig_clause_data.insert(cr_to, craig_clause_data[cr_from]);
    };
#endif

    // All watchers:
    //
    watches.cleanAll();
    for (int v = 0; v < nVars(); v++)
        for (int s = 0; s < 2; s++){
            Lit p = mkLit(v, s);
            vec<Watcher>& ws = watches[p];
            for (int j = 0; j < ws.size(); j++) {
#ifdef CRAIG_INTERPOLATION
                CRef cr = ws[j].cref;
                ca.reloc(ws[j].cref, to);
                craig_reloc(cr, ws[j].cref);
#else /* CRAIG_INTERPOLATION */
                ca.reloc(ws[j].cref, to);
#endif /* CRAIG_INTERPOLATION */
            }
        }

    // All reasons:
    //
    for (int i = 0; i < trail.size(); i++){
        Var v = var(trail[i]);

        // Note: it is not safe to call 'locked()' on a relocated clause. This is why we keep
        // 'dangling' reasons here. It is safe and does not hurt.
        if (reason(v) != CRef_Undef && (ca[reason(v)].reloced() || locked(ca[reason(v)]))){
            assert(!isRemoved(reason(v)));

#ifdef CRAIG_INTERPOLATION
            CRef cr = vardata[v].reason;
            ca.reloc(vardata[v].reason, to);
            craig_reloc(cr, vardata[v].reason);
#else /* CRAIG_INTERPOLATION */
            ca.reloc(vardata[v].reason, to);
#endif /* CRAIG_INTERPOLATION */
        }
    }

    // All learnt:
    //
    int i, j;
    for (i = j = 0; i < learnts.size(); i++)
        if (!isRemoved(learnts[i])){
#ifdef CRAIG_INTERPOLATION
            CRef cr = learnts[i];
            ca.reloc(learnts[i], to);
            craig_reloc(cr, learnts[i]);
#else /* CRAIG_INTERPOLATION */
            ca.reloc(learnts[i], to);
#endif /* CRAIG_INTERPOLATION */

            learnts[j++] = learnts[i];
        }
    learnts.shrink(i - j);

    // All original:
    //
    for (i = j = 0; i < clauses.size(); i++)
        if (!isRemoved(clauses[i])){
#ifdef CRAIG_INTERPOLATION
            CRef cr = clauses[i];
            ca.reloc(clauses[i], to);
            craig_reloc(cr, clauses[i]);
#else /* CRAIG_INTERPOLATION */
            ca.reloc(clauses[i], to);
#endif /* CRAIG_INTERPOLATION */

            clauses[j++] = clauses[i];
        }
    clauses.shrink(i - j);
}


void Solver::garbageCollect()
{
    // Initialize the next region to a size corresponding to the estimated utilization degree. This
    // is not precise but should avoid some unnecessary reallocations for the new region:
    ClauseAllocator to(ca.size() - ca.wasted());

    relocAll(to);
    if (verbosity >= 2)
        printf("|  Garbage collection:   %12d bytes => %12d bytes             |\n",
               ca.size()*ClauseAllocator::Unit_Size, to.size()*ClauseAllocator::Unit_Size);
    to.moveTo(ca);

#ifdef CRAIG_INTERPOLATION
    reloc_craig_clause_data.moveTo(craig_clause_data);
#endif /* CRAIG_INTERPOLATION */
}

#ifdef MiniCraig_Solver_cc_ENABLE_CRAIG_INTERPOLATION
# undef MiniCraig_Solver_cc_ENABLE_CRAIG_INTERPOLATION
# define CRAIG_INTERPOLATION
#endif

#ifdef MiniCraig_Solver_cc_DISABLE_CRAIG_INTERPOLATION
# undef MiniCraig_Solver_cc_DISABLE_CRAIG_INTERPOLATION
# undef CRAIG_INTERPOLATION
#endif
