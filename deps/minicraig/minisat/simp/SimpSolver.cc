/***********************************************************************************[SimpSolver.cc]
Copyright (c) 2006,      Niklas Een, Niklas Sorensson
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

#include "../mtl/Sort.h"
#include "../simp/SimpSolver.h"
#include "../utils/System.h"

#include <iostream>

#ifndef MiniCraig_SimpSolver_cc_WITH_CRAIG

// Normal include of the header without craig interpolation
# ifdef CRAIG_INTERPOLATION
#  define MiniCraig_SimpSolver_cc_ENABLE_CRAIG_INTERPOLATION
#  undef CRAIG_INTERPOLATION
# endif

#else

// Include with craig interpolation from CraigSolver.h
# ifndef CRAIG_INTERPOLATION
#  define MiniCraig_SimpSolver_cc_DISABLE_CRAIG_INTERPOLATION
#  define CRAIG_INTERPOLATION
# endif
#endif

using namespace MiniCraig;

//=================================================================================================
// Options:


static const char* _cat = "SIMP";

static BoolOption   opt_use_asymm        (_cat, "asymm",        "Shrink clauses by asymmetric branching.", false);
static BoolOption   opt_use_rcheck       (_cat, "rcheck",       "Check if a clause is already implied. (costly)", false);
static BoolOption   opt_use_elim         (_cat, "elim",         "Perform variable elimination.", true);
static IntOption    opt_grow             (_cat, "grow",         "Allow a variable elimination step to grow by a number of clauses.", 0);
static IntOption    opt_clause_lim       (_cat, "cl-lim",       "Variables are not eliminated if it produces a resolvent with a length above this limit. -1 means no limit", 20,   IntRange(-1, INT32_MAX));
static IntOption    opt_subsumption_lim  (_cat, "sub-lim",      "Do not check if subsumption against a clause larger than this. -1 means no limit.", 1000, IntRange(-1, INT32_MAX));
static DoubleOption opt_simp_garbage_frac(_cat, "simp-gc-frac", "The fraction of wasted memory allowed before a garbage collection is triggered during simplification.",  0.5, DoubleRange(0, false, HUGE_VAL, false));


//=================================================================================================
// Constructor/Destructor:

SimpSolver::SimpSolver(bool two_aig_managers, bool pref_dominant) :
    grow               (opt_grow)
  , clause_lim         (opt_clause_lim)
  , subsumption_lim    (opt_subsumption_lim)
  , simp_garbage_frac  (opt_simp_garbage_frac)
  , use_asymm          (opt_use_asymm)
  , use_rcheck         (opt_use_rcheck)
  , use_elim           (opt_use_elim)
  , extend_model       (true)
  , merges             (0)
  , asymm_lits         (0)
  , eliminated_vars    (0)
  , elimorder          (1)
  , use_simplification (true)
  , occurs             (ClauseDeleted(ca))
  , elim_heap          (ElimLt(n_occ))
  , bwdsub_assigns     (0)
  , n_touched          (0)
{
    vec<Lit> dummy(1,lit_Undef);
    ca.extra_clause_field = true; // NOTE: must happen before allocating the dummy clause below.
    bwdsub_tmpunit        = ca.alloc(dummy);
    remove_satisfied      = false;
}

SimpSolver::~SimpSolver()
{
}


#ifndef CRAIG_INTERPOLATION
Var SimpSolver::newVar(lbool upol, bool dvar) {
    Var v = Solver::newVar(upol, dvar);
#else /* CRAIG_INTERPOLATION */
Var SimpSolver::newVar(CraigVarType type, lbool upol, bool dvar) {
    Var v = Solver::newVar(type, upol, dvar);
#endif /* CRAIG_INTERPOLATION */

    frozen    .insert(v, (char)false);
    eliminated.insert(v, (char)false);

    if (use_simplification){
        n_occ     .insert( mkLit(v), 0);
        n_occ     .insert(~mkLit(v), 0);
        occurs    .init  (v);
        touched   .insert(v, 0);
        elim_heap .insert(v);
    }
    return v; }


#ifndef CRAIG_INTERPOLATION
void SimpSolver::releaseVar(Lit l)
{
    assert(!isEliminated(var(l)));
    if (!use_simplification && var(l) >= max_simp_var)
        // Note: Guarantees that no references to this variable is
        // left in model extension datastructure. Could be improved!
        Solver::releaseVar(l);
    else
        // Otherwise, don't allow variable to be reused.
        Solver::addClause(l);
}
#endif /* CRAIG_INTERPOLATION */


lbool SimpSolver::solve_(bool do_simp, bool turn_off_simp)
{
    vec<Var> extra_frozen;
    lbool    result = l_True;

    do_simp &= use_simplification;

    if (do_simp){
        // Assumptions must be temporarily frozen to run variable elimination:
        for (int i = 0; i < assumptions.size(); i++){
            Var v = var(assumptions[i]);

            // If an assumption has been eliminated, remember it.
            assert(!isEliminated(v));

            if (!frozen[v]){
                // Freeze and store.
                setFrozen(v, true);
                extra_frozen.push(v);
            } }

        result = lbool(eliminate(turn_off_simp));
    }

    if (result == l_True)
        result = Solver::solve_();
    else if (verbosity >= 1)
        printf("===============================================================================\n");

    if (result == l_True && extend_model)
        extendModel();

    if (result == l_False) {
        computeReason();
    }

    if (do_simp)
        // Unfreeze the assumptions that were frozen:
        for (int i = 0; i < extra_frozen.size(); i++)
            setFrozen(extra_frozen[i], false);

    return result;
}



#ifndef CRAIG_INTERPOLATION
bool SimpSolver::addClause_(vec<Lit>& ps)
#else /* CRAIG_INTERPOLATION */
bool SimpSolver::addClause_(vec<Lit>& ps, const CraigData& craig_data, bool use_interpolant)
#endif /* CRAIG_INTERPOLATION */
{
#ifndef NDEBUG
    for (int i = 0; i < ps.size(); i++)
        assert(!isEliminated(var(ps[i])));
#endif

    int nclauses = clauses.size();

    if (use_rcheck && implied(ps))
        return true;

#ifndef CRAIG_INTERPOLATION
    if (!Solver::addClause_(ps))
#else /* CRAIG_INTERPOLATION */
    if (!Solver::addClause_(ps, craig_data, use_interpolant))
#endif /* CRAIG_INTERPOLATION */
        return false;

    if (use_simplification && clauses.size() == nclauses + 1){
        CRef          cr = clauses.last();
        const Clause& c  = ca[cr];

        // NOTE: the clause is added to the queue immediately and then
        // again during 'gatherTouchedClauses()'. If nothing happens
        // in between, it will only be checked once. Otherwise, it may
        // be checked twice unnecessarily. This is an unfortunate
        // consequence of how backward subsumption is used to mimic
        // forward subsumption.
        subsumption_queue.insert(cr);
        for (int i = 0; i < c.size(); i++){
            occurs[var(c[i])].push(cr);
            n_occ[c[i]]++;
            touched[var(c[i])] = 1;
            n_touched++;
            if (elim_heap.inHeap(var(c[i])))
                elim_heap.increase(var(c[i]));
        }
    }

    return true;
}


void SimpSolver::removeClause(CRef cr)
{
    const Clause& c = ca[cr];

    if (use_simplification)
        for (int i = 0; i < c.size(); i++){
            n_occ[c[i]]--;
            updateElimHeap(var(c[i]));
            occurs.smudge(var(c[i]));
        }

    Solver::removeClause(cr);
}


#ifndef CRAIG_INTERPOLATION
bool SimpSolver::strengthenClause(CRef cr, Lit l)
#else /* CRAIG_INTERPOLATION */
bool SimpSolver::strengthenClause(CRef cr, Lit l, const CraigData& craig_data)
#endif /* CRAIG_INTERPOLATION */
{
    Clause& c = ca[cr];
    assert(decisionLevel() == 0);
    assert(use_simplification);

    // FIX: this is too inefficient but would be nice to have (properly implemented)
    // if (!find(subsumption_queue, &c))
    subsumption_queue.insert(cr);

#ifdef CRAIG_INTERPOLATION
    CraigData partial_interpolant = craig_clause_data[cr];
    extendCraigInterpolantWithResolution(partial_interpolant, l, craig_data);

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
        << "; strengthenClause 1"
        << std::endl;
#endif /* CRAIG_INTERPOLATION_FUZZ_CRAIG */
#endif /* CRAIG_INTERPOLATION */

    if (c.size() == 2){
        removeClause(cr);
        c.strengthen(l);
    }else{
        detachClause(cr, true);
        c.strengthen(l);
        attachClause(cr);
        remove(occurs[var(l)], cr);
        n_occ[l]--;
        updateElimHeap(var(l));

#ifdef CRAIG_INTERPOLATION
        craig_clause_data[cr] = partial_interpolant;
#endif /* CRAIG_INTERPOLATION */
        return true;
    }

#ifdef CRAIG_INTERPOLATION
    craig_unit_clause_data.insert(toInt(c[0]), partial_interpolant);
#endif /* CRAIG_INTERPOLATION */

    if (!enqueue(c[0])) {
#ifdef CRAIG_INTERPOLATION
        extendCraigInterpolantWithResolution(partial_interpolant, c[0], craig_unit_clause_data[toInt(~c[0])]);
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
            << "; strengthenClause 2"
            << std::endl;
#endif /* CRAIG_INTERPOLATION_FUZZ_CRAIG */
#endif /* CRAIG_INTERPOLATION */
        return false;
    }

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
            << "; strengthenClause 3"
            << std::endl;
#endif /* CRAIG_INTERPOLATION_FUZZ_CRAIG */
    }
#endif /* CRAIG_INTERPOLATION */

    return ok;
}


// Returns FALSE if clause is always satisfied ('out_clause' should not be used).
bool SimpSolver::merge(const Clause& _ps, const Clause& _qs, Var v, vec<Lit>& out_clause)
{
    merges++;
    out_clause.clear();

    bool  ps_smallest = _ps.size() < _qs.size();
    const Clause& ps  =  ps_smallest ? _qs : _ps;
    const Clause& qs  =  ps_smallest ? _ps : _qs;

    for (int i = 0; i < qs.size(); i++){
        if (var(qs[i]) != v){
            for (int j = 0; j < ps.size(); j++)
                if (var(ps[j]) == var(qs[i])){
                    if (ps[j] == ~qs[i])
                        return false;
                    else
                        goto next;
                }
            out_clause.push(qs[i]);
        }
        next:;
    }

    for (int i = 0; i < ps.size(); i++)
        if (var(ps[i]) != v)
            out_clause.push(ps[i]);

    return true;
}


// Returns FALSE if clause is always satisfied.
bool SimpSolver::merge(const Clause& _ps, const Clause& _qs, Var v, int& size)
{
    merges++;

    bool  ps_smallest = _ps.size() < _qs.size();
    const Clause& ps  =  ps_smallest ? _qs : _ps;
    const Clause& qs  =  ps_smallest ? _ps : _qs;
    const Lit*  __ps  = (const Lit*)ps;
    const Lit*  __qs  = (const Lit*)qs;

    size = ps.size()-1;

    for (int i = 0; i < qs.size(); i++){
        if (var(__qs[i]) != v){
            for (int j = 0; j < ps.size(); j++)
                if (var(__ps[j]) == var(__qs[i])){
                    if (__ps[j] == ~__qs[i])
                        return false;
                    else
                        goto next;
                }
            size++;
        }
        next:;
    }

    return true;
}


void SimpSolver::gatherTouchedClauses()
{
    if (n_touched == 0) return;

    int i,j;
    for (i = j = 0; i < subsumption_queue.size(); i++)
        if (ca[subsumption_queue[i]].mark() == 0)
            ca[subsumption_queue[i]].mark(2);

    for (i = 0; i < nVars(); i++)
        if (touched[i]){
            const vec<CRef>& cs = occurs.lookup(i);
            for (j = 0; j < cs.size(); j++)
                if (ca[cs[j]].mark() == 0){
                    subsumption_queue.insert(cs[j]);
                    ca[cs[j]].mark(2);
                }
            touched[i] = 0;
        }

    for (i = 0; i < subsumption_queue.size(); i++)
        if (ca[subsumption_queue[i]].mark() == 2)
            ca[subsumption_queue[i]].mark(0);

    n_touched = 0;
}


bool SimpSolver::implied(const vec<Lit>& c)
{
    assert(decisionLevel() == 0);

    trail_lim.push(trail.size());
    for (int i = 0; i < c.size(); i++)
        if (value(c[i]) == l_True){
            cancelUntil(0);
            return true;
        }else if (value(c[i]) != l_False){
            assert(value(c[i]) == l_Undef);
            uncheckedEnqueue(~c[i]);
        }

    bool result = propagate() != CRef_Undef;
    cancelUntil(0);
    return result;
}


// Backward subsumption + backward subsumption resolution
bool SimpSolver::backwardSubsumptionCheck(bool verbose)
{
    int cnt = 0;
    int subsumed = 0;
    int deleted_literals = 0;
    assert(decisionLevel() == 0);

    while (subsumption_queue.size() > 0 || bwdsub_assigns < trail.size()){

        // Empty subsumption queue and return immediately on user-interrupt:
        if (asynch_interrupt){
            subsumption_queue.clear();
            bwdsub_assigns = trail.size();
            break; }

        // Check top-level assignments by creating a dummy clause and placing it in the queue:
        if (subsumption_queue.size() == 0 && bwdsub_assigns < trail.size()){
            Lit l = trail[bwdsub_assigns++];
            ca[bwdsub_tmpunit][0] = l;
            ca[bwdsub_tmpunit].calcAbstraction();
            subsumption_queue.insert(bwdsub_tmpunit); }

        CRef    cr = subsumption_queue.peek(); subsumption_queue.pop();
        Clause& c  = ca[cr];

        if (c.mark()) continue;

        if (verbose && verbosity >= 2 && cnt++ % 1000 == 0)
            printf("subsumption left: %10d (%10d subsumed, %10d deleted literals)\r", subsumption_queue.size(), subsumed, deleted_literals);

        assert(c.size() > 1 || value(c[0]) == l_True);    // Unit-clauses should have been propagated before this point.

        // Find best variable to scan:
        Var best = var(c[0]);
        for (int i = 1; i < c.size(); i++)
            if (occurs[var(c[i])].size() < occurs[best].size())
                best = var(c[i]);

        // Search all candidates:
        vec<CRef>& _cs = occurs.lookup(best);
        CRef*       cs = (CRef*)_cs;

        for (int j = 0; j < _cs.size(); j++)
            if (c.mark())
                break;
            else if (!ca[cs[j]].mark() && cs[j] != cr && (subsumption_lim == -1 || ca[cs[j]].size() < subsumption_lim)){
                Lit l = c.subsumes(ca[cs[j]]);

                if (l == lit_Undef)
                    subsumed++, removeClause(cs[j]);
                else if (l != lit_Error){
                    deleted_literals++;

#ifndef CRAIG_INTERPOLATION
                    if (!strengthenClause(cs[j], ~l))
#else /* CRAIG_INTERPOLATION */
                    if (!strengthenClause(cs[j], ~l, (c.size() > 1) ? craig_clause_data[cr] : craig_unit_clause_data[toInt(l)]))
#endif /* CRAIG_INTERPOLATION */
                        return false;

                    // Did current candidate get deleted from cs? Then check candidate at index j again:
                    if (var(l) == best)
                        j--;
                }
            }
    }

    return true;
}


bool SimpSolver::asymm(Var v, CRef cr)
{
    Clause& c = ca[cr];
    assert(decisionLevel() == 0);

#ifdef CRAIG_INTERPOLATION
#ifdef CRAIG_INTERPOLATION_DEBUG
    std::cerr << "Asymmetric Branching for " << (toInt(v) + 1) << " and ";
    if (craig_clause_data[cr].craig_type == CraigClauseType::A_CLAUSE) std::cerr << "A (";
    if (craig_clause_data[cr].craig_type == CraigClauseType::B_CLAUSE) std::cerr << "B (";
    if (craig_clause_data[cr].craig_type == CraigClauseType::L_CLAUSE) std::cerr << "L (";
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

    if (c.mark() || satisfied(c)) return true;

    trail_lim.push(trail.size());
    Lit l = lit_Undef;
    for (int i = 0; i < c.size(); i++)
        if (var(c[i]) != v && value(c[i]) != l_False)
            uncheckedEnqueue(~c[i]);
        else
            l = c[i];

    CRef confl = propagate();
    if (confl != CRef_Undef){
#ifdef CRAIG_INTERPOLATION
        CraigData partial_interpolant = craig_clause_data[confl];
#ifdef CRAIG_INTERPOLATION_DEBUG
        std::cerr << "    Clone Interpolant i" << partial_interpolant.craig_id << " to i" << craig_id << std::endl;
#endif /* CRAIG_INTERPOLATION_DEBUG */
        partial_interpolant.craig_id = craig_id++;

        Clause& cl = ca[confl];
        for (int t = 0; t < cl.size(); ++t) {
            seen[var(cl[t])] = 1;
        }

        for (int i = trail.size()-1; i >= 0; i--) {
            Var x = var(trail[i]);
            if (seen[x]) {
                if (reason(x) == CRef_Undef) {
                    if (level(x) == 0)
                        extendCraigInterpolantWithResolution(partial_interpolant, ~trail[i], craig_unit_clause_data[toInt(trail[i])]);
                } else {
                    Clause& c = ca[reason(x)];
                    extendCraigInterpolantWithResolution(partial_interpolant, ~trail[i], craig_clause_data[reason(x)]);
                    for (int j = 0; j < c.size(); j++)
                        seen[var(c[j])] = 1;
                }

                seen[x] = 0;
            }
        }
#endif /* CRAIG_INTERPOLATION */

        cancelUntil(0);
        asymm_lits++;

#ifndef CRAIG_INTERPOLATION
        if (!strengthenClause(cr, l))
#else /* CRAIG_INTERPOLATION */
        if (!strengthenClause(cr, l, partial_interpolant))
#endif /* CRAIG_INTERPOLATION */
            return false;
    }else
        cancelUntil(0);

    return true;
}


bool SimpSolver::asymmVar(Var v)
{
    assert(use_simplification);

    const vec<CRef>& cls = occurs.lookup(v);

    if (value(v) != l_Undef || cls.size() == 0)
        return true;

    for (int i = 0; i < cls.size(); i++)
        if (!asymm(v, cls[i]))
            return false;

    return backwardSubsumptionCheck();
}


static void mkElimClause(vec<uint32_t>& elimclauses, Lit x)
{
    elimclauses.push(toInt(x));
    elimclauses.push(1);
}


static void mkElimClause(vec<uint32_t>& elimclauses, Var v, Clause& c)
{
    int first = elimclauses.size();
    int v_pos = -1;

    // Copy clause to elimclauses-vector. Remember position where the
    // variable 'v' occurs:
    for (int i = 0; i < c.size(); i++){
        elimclauses.push(toInt(c[i]));
        if (var(c[i]) == v)
            v_pos = i + first;
    }
    assert(v_pos != -1);

    // Swap the first literal with the 'v' literal, so that the literal
    // containing 'v' will occur first in the clause:
    uint32_t tmp = elimclauses[v_pos];
    elimclauses[v_pos] = elimclauses[first];
    elimclauses[first] = tmp;

    // Store the length of the clause last:
    elimclauses.push(c.size());
}



bool SimpSolver::eliminateVar(Var v)
{
    assert(!frozen[v]);
    assert(!isEliminated(v));
    assert(value(v) == l_Undef);

    // Split the occurrences into positive and negative:
    //
    const vec<CRef>& cls = occurs.lookup(v);
    vec<CRef>        pos, neg;
    for (int i = 0; i < cls.size(); i++)
        (find(ca[cls[i]], mkLit(v)) ? pos : neg).push(cls[i]);

    // Check wether the increase in number of clauses stays within the allowed ('grow'). Moreover, no
    // clause must exceed the limit on the maximal clause size (if it is set):
    //
    int cnt         = 0;
    int clause_size = 0;

    for (int i = 0; i < pos.size(); i++)
        for (int j = 0; j < neg.size(); j++)
            if (merge(ca[pos[i]], ca[neg[j]], v, clause_size) &&
                (++cnt > cls.size() + grow || (clause_lim != -1 && clause_size > clause_lim)))
                return true;

    // Delete and store old clauses:
    eliminated[v] = true;
    setDecisionVar(v, false);
    eliminated_vars++;

    if (pos.size() > neg.size()){
        for (int i = 0; i < neg.size(); i++)
            mkElimClause(elimclauses, v, ca[neg[i]]);
        mkElimClause(elimclauses, mkLit(v));
    }else{
        for (int i = 0; i < pos.size(); i++)
            mkElimClause(elimclauses, v, ca[pos[i]]);
        mkElimClause(elimclauses, ~mkLit(v));
    }

#ifdef CRAIG_INTERPOLATION
    assert(reloc_craig_clause_data.elems() == 0);
#endif /* CRAIG_INTERPOLATION */
    for (int i = 0; i < cls.size(); i++) {
#ifdef CRAIG_INTERPOLATION
        reloc_craig_clause_data.insert(cls[i], craig_clause_data[cls[i]]);
#endif /* CRAIG_INTERPOLATION */

        removeClause(cls[i]);
    }

    // Produce clauses in cross product:
    vec<Lit>& resolvent = add_tmp;
    for (int i = 0; i < pos.size(); i++)
        for (int j = 0; j < neg.size(); j++)
#ifndef CRAIG_INTERPOLATION
            if (merge(ca[pos[i]], ca[neg[j]], v, resolvent) && !addClause_(resolvent))
                return false;
#else /* CRAIG_INTERPOLATION */
            if (merge(ca[pos[i]], ca[neg[j]], v, resolvent)) {
                CraigData partial_interpolant = reloc_craig_clause_data[pos[i]];
#ifdef CRAIG_INTERPOLATION_DEBUG
                std::cerr << "    Clone Interpolant i" << partial_interpolant.craig_id << " to i" << craig_id << std::endl;
#endif /* CRAIG_INTERPOLATION_DEBUG */
                partial_interpolant.craig_id = craig_id++;
                extendCraigInterpolantWithResolution(partial_interpolant, mkLit(v, false), reloc_craig_clause_data[neg[j]]);

                if (partial_interpolant.isPure()) {
                    partial_interpolant = createCraigInterpolantForPureClause(resolvent, reloc_craig_clause_data[pos[i]].craig_type, true);
                }

                if (!addClause_(resolvent, partial_interpolant, true)) {
                    reloc_craig_clause_data.clear();
                    return false;
                }
            }

    reloc_craig_clause_data.clear();
#endif /* CRAIG_INTERPOLATION */

    // Free occurs list for this variable:
    occurs[v].clear(true);

    // Free watchers lists for this variable, if possible:
    if (watches[ mkLit(v)].size() == 0) watches[ mkLit(v)].clear(true);
    if (watches[~mkLit(v)].size() == 0) watches[~mkLit(v)].clear(true);

    return backwardSubsumptionCheck();
}


#ifndef CRAIG_INTERPOLATION
bool SimpSolver::substitute(Var v, Lit x)
{
    assert(!frozen[v]);
    assert(!isEliminated(v));
    assert(value(v) == l_Undef);

    if (!ok) return false;

    eliminated[v] = true;
    setDecisionVar(v, false);
    const vec<CRef>& cls = occurs.lookup(v);

    vec<Lit>& subst_clause = add_tmp;
    for (int i = 0; i < cls.size(); i++){
        Clause& c = ca[cls[i]];

        subst_clause.clear();
        for (int j = 0; j < c.size(); j++){
            Lit p = c[j];
            subst_clause.push(var(p) == v ? x ^ sign(p) : p);
        }

        removeClause(cls[i]);

        if (!addClause_(subst_clause))
            return ok = false;
    }

    return true;
}
#endif /* CRAIG_INTERPOLATION */


void SimpSolver::extendModel()
{
    int i, j;
    Lit x;

    for (i = elimclauses.size()-1; i > 0; i -= j){
        for (j = elimclauses[i--]; j > 1; j--, i--)
            if (modelValue(toLit(elimclauses[i])) != l_False)
                goto next;

        x = toLit(elimclauses[i]);
        model[var(x)] = lbool(!sign(x));
    next:;
    }
}


bool SimpSolver::eliminate(bool turn_off_elim)
{
    if (!simplify())
        return false;
    else if (!use_simplification)
        return true;

    // Main simplification loop:
    //
    while (n_touched > 0 || bwdsub_assigns < trail.size() || elim_heap.size() > 0){

        gatherTouchedClauses();
        // printf("  ## (time = %6.2f s) BWD-SUB: queue = %d, trail = %d\n", cpuTime(), subsumption_queue.size(), trail.size() - bwdsub_assigns);
        if ((subsumption_queue.size() > 0 || bwdsub_assigns < trail.size()) &&
            !backwardSubsumptionCheck(true)){
            ok = false; goto cleanup; }

        // Empty elim_heap and return immediately on user-interrupt:
        if (asynch_interrupt){
            assert(bwdsub_assigns == trail.size());
            assert(subsumption_queue.size() == 0);
            assert(n_touched == 0);
            elim_heap.clear();
            goto cleanup; }

        // printf("  ## (time = %6.2f s) ELIM: vars = %d\n", cpuTime(), elim_heap.size());
        for (int cnt = 0; !elim_heap.empty(); cnt++){
            Var elim = elim_heap.removeMin();

            if (asynch_interrupt) break;

            if (isEliminated(elim) || value(elim) != l_Undef) continue;

            if (verbosity >= 2 && cnt % 100 == 0)
                printf("elimination left: %10d\r", elim_heap.size());

            if (use_asymm){
                // Temporarily freeze variable. Otherwise, it would immediately end up on the queue again:
                bool was_frozen = frozen[elim];
                frozen[elim] = true;
                if (!asymmVar(elim)){
                    ok = false; goto cleanup; }
                frozen[elim] = was_frozen; }

            // At this point, the variable may have been set by assymetric branching, so check it
            // again. Also, don't eliminate frozen variables:
            if (use_elim && value(elim) == l_Undef && !frozen[elim] && !eliminateVar(elim)){
                ok = false; goto cleanup; }

            checkGarbage(simp_garbage_frac);
        }

        assert(subsumption_queue.size() == 0);
    }
 cleanup:

    // If no more simplification is needed, free all simplification-related data structures:
    if (turn_off_elim){
        touched  .clear(true);
        occurs   .clear(true);
        n_occ    .clear(true);
        elim_heap.clear(true);
        subsumption_queue.clear(true);

        use_simplification    = false;
        remove_satisfied      = true;
        ca.extra_clause_field = false;
        max_simp_var          = nVars();

        // Force full cleanup (this is safe and desirable since it only happens once):
        rebuildOrderHeap();
        garbageCollect();
    }else{
        // Cheaper cleanup:
        cleanUpClauses();
        checkGarbage();
    }

    if (verbosity >= 1 && elimclauses.size() > 0)
        printf("|  Eliminated clauses:     %10.2f Mb                                      |\n",
               double(elimclauses.size() * sizeof(uint32_t)) / (1024*1024));

    return ok;
}

void SimpSolver::cleanUpClauses()
{
    occurs.cleanAll();
    int i,j;
    for (i = j = 0; i < clauses.size(); i++)
        if (ca[clauses[i]].mark() == 0)
            clauses[j++] = clauses[i];
    clauses.shrink(i - j);
}

//=================================================================================================
// Garbage Collection methods:


void SimpSolver::relocAll(ClauseAllocator& to)
{
    if (!use_simplification) return;

#ifdef CRAIG_INTERPOLATION
    const auto craig_reloc = [&](CRef cr_from, CRef cr_to) {
        assert(ca[cr_from].size() > 1);
        assert(craig_clause_data.has(cr_from));
        if (reloc_craig_clause_data.has(cr_to)) return;
        reloc_craig_clause_data.insert(cr_to, craig_clause_data[cr_from]);
    };
#endif /* CRAIG_INTERPOLATION */

    // All occurs lists:
    //
    for (int i = 0; i < nVars(); i++){
        occurs.clean(i);
        vec<CRef>& cs = occurs[i];
        for (int j = 0; j < cs.size(); j++) {
#ifdef CRAIG_INTERPOLATION
            CRef cr = cs[j];
            ca.reloc(cs[j], to);
            craig_reloc(cr, cs[j]);
#else /* CRAIG_INTERPOLATION */
            ca.reloc(cs[j], to);
#endif /* CRAIG_INTERPOLATION */
        }
    }

    // Subsumption queue:
    //
    for (int i = subsumption_queue.size(); i > 0; i--){
        CRef cr = subsumption_queue.peek(); subsumption_queue.pop();
        if (ca[cr].mark()) continue;
#ifdef CRAIG_INTERPOLATION
        CRef cr_old = cr;
        ca.reloc(cr, to);
        craig_reloc(cr_old, cr);
#else /* CRAIG_INTERPOLATION */
        ca.reloc(cr, to);
#endif /* CRAIG_INTERPOLATION */
        subsumption_queue.insert(cr);
    }

    // Temporary clause:
    //
    ca.reloc(bwdsub_tmpunit, to);
}


void SimpSolver::garbageCollect()
{
    // Initialize the next region to a size corresponding to the estimated utilization degree. This
    // is not precise but should avoid some unnecessary reallocations for the new region:
    ClauseAllocator to(ca.size() - ca.wasted());

    to.extra_clause_field = ca.extra_clause_field; // NOTE: this is important to keep (or lose) the extra fields.
    relocAll(to);
    Solver::relocAll(to);
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
