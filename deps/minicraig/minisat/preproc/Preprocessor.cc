#include "../preproc/Preprocessor.h"

#include <iostream>

namespace MiniCraig {

//=================================================================================================
// Options:


static const char* _cat = "PREP";

static BoolOption    opt_use_bce (_cat, "bce",  "Perform blocked clause elimination", false);
static BoolOption    opt_use_upla(_cat, "upla", "Perform unit propagation lookahead to detect equivalences", false);
static BoolOption    opt_use_impl(_cat, "impl", "Check if clause is already implied when eliminating variables", false);

Preprocessor::Preprocessor() :
    SimpSolver()
  , use_bce(opt_use_bce)
  , use_upla(opt_use_upla)
  , use_impl(opt_use_impl)
  , dont_touch()
{
    remove_satisfied = false;
}

Preprocessor::Preprocessor(bool asym, bool impl_check, bool bce, bool upla, int res_length):
    SimpSolver()
  , dont_touch()
{
    remove_satisfied = false;
    setUseAsymm(asym);
    setUseImpl(impl_check);
    setUseBce(bce);
    setUseUpla(upla);
    setResLength(res_length);
}

bool Preprocessor::preprocess(const vec<Lit>& dontouch) {
    budgetOff();

    // my dont_touch stuff
    dont_touch.clear();
    dont_touch.growTo(nVars(), false);

    // mark dont_touch variables
    for( int i = 0; i < dontouch.size() ; ++i){
        dont_touch[var(dontouch[i])] = true;
    }
    dontouch.copyTo(assumptions);

    return solve_(true, true) == l_True;
}

lbool Preprocessor::solve_(bool do_simp, bool turn_off_simp) {
    vec<Var> extra_frozen;
    lbool    result = l_True;

    do_simp &= use_simplification;

#ifdef PREPROCESSOR_DEBUG
    //---------------------------------------------------------//
    //            Print the not simplified clauses             //
    //---------------------------------------------------------//

    std::cerr << "Preprocessor: The NOT simplified clauses look like:" << std::endl;
    for (unsigned int t = 0; t < clauses.size(); ++t){
        if( clauses[t] != CRef_Undef ){
            Clause& c = ca[clauses[t]];
            std::cerr << "    Clause (";
            for (unsigned int i = 0; i < c.size(); ++i) {
                if (i != 0) std::cerr << ", ";
                std::cerr << toInt(c[i]);
            }
            std::cerr << ")" << std::endl;
        }
    }
    for(unsigned int t = 0; t < trail.size(); ++t){
        std::cerr << "    Unit (" << toInt(trail[t]) << ")" << std::endl;
    }
    for(unsigned int t = 0; t < assumptions.size(); ++t){
        std::cerr << "    Frozen (" << toInt(assumptions[t]) << ")" << std::endl;
    }
#endif /* PREPROCESSOR_DEBUG */

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
            }
        }
        // start preprocess
        result = lbool(eliminate_(turn_off_simp));
    }

    if (do_simp) {
        // Unfreeze the assumptions that were frozen:
        for (int i = 0; i < extra_frozen.size(); i++){
            setFrozen(extra_frozen[i], false);
        }
    }

#ifdef PREPROCESSOR_DEBUG
    //---------------------------------------------------------//
    //             Print the simplified clauses                //
    //---------------------------------------------------------//

    std::cerr << "Preprocessor: The simplified clauses look like:" << std::endl;
    for (unsigned int t = 0; t < clauses.size(); ++t){
        if( clauses[t] != CRef_Undef ){
            Clause& c = ca[clauses[t]];
            std::cerr << "    Clause (";
            for (unsigned int i = 0; i < c.size(); ++i) {
                if (i != 0) std::cerr << ", ";
                std::cerr << toInt(c[i]);
            }
            std::cerr << ")" << std::endl;
        }
    }
    for(unsigned int t = 0; t < trail.size(); ++t){
        std::cerr << "    Unit (" << toInt(trail[t]) << ")" << std::endl;
    }
    for(unsigned int t = 0; t < assumptions.size(); ++t){
        std::cerr << "    Frozen (" << toInt(assumptions[t]) << ")" << std::endl;
    }
#endif /* PREPROCESSOR_DEBUG */

    return result;
}

bool Preprocessor::eliminate_(bool turn_off_elim)
{
    if (!simplify())
        return false;
    else if (!use_simplification)
        return true;

    bool orig_rcheck = use_rcheck;
    if (use_bce) applyBce();
    if (use_upla) applyUpla();
    if (use_impl) use_rcheck = true;

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
    use_rcheck = orig_rcheck;

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

void Preprocessor::cleanUpClauses() {
    occurs.cleanAll();

    int i, j;
    for (i = 0, j = 0; i < clauses.size(); i++) {
        if (ca[clauses[i]].mark() == 0) {
            clauses[j++] = clauses[i];
        }
    }

    clauses.shrink(i - j);
}

void Preprocessor::applyBce() {
    if (asynch_interrupt) return;
    blockedClauseElim1();

    if (asynch_interrupt) return;
    blockedClauseElim2();
}

void Preprocessor::applyUpla() {
    if (asynch_interrupt) return;

    vec<int> to_check(2 * nVars() + 1, 0);
    for (int variable = 0 ; variable < nVars(); ++variable) {
        if (asynch_interrupt) return;
        unitPropagationLookAhead(variable, to_check);
    }
}

void Preprocessor::blockedClauseElim1(){
    int merge_size = 0; // used in merge
    bool blocked = true;

    for (int t = 0 ; t < nVars(); ++t) {
        if (asynch_interrupt) return;

        Lit pos_current = toLit(t);
        Lit neg_current = ~toLit(t);
        // not a dont touch variable
        if( !frozen[var( neg_current )] &&
            !isEliminated(var(neg_current ) )
            && value(var(neg_current)) == l_Undef ){

            vec<CRef> v_pos;
            vec<CRef> v_neg;
            const vec<CRef>& cls = occurs.lookup(var( neg_current ));

            // build v_pos and v_neg
            for (int j = 0; j <  cls.size(); ++j) {
                if( ca[cls[j]].mark() == 1 || cls[j] == CRef_Undef || satisfied(ca[cls[j]]) ) {
                    continue;
                }

                if (find(ca[cls[j]], pos_current)) {
                    v_pos.push(cls[j]);
                } else {
                    v_neg.push(cls[j]);
                }
            }

            for (int z = 0 ; z < v_neg.size() ; ++z ) {
                blocked = true;
                auto& c = ca[v_neg[z]];

                if (c.mark() == 1 || v_neg[z] == CRef_Undef) continue;

                for (int s = 0 ; s < v_pos.size() ; ++s) {
                    auto& c2 = ca[v_pos[s]];

                    if(c2.mark() == 1 || v_pos[s] == CRef_Undef) continue;

                    if (merge(c, c2, var(neg_current), merge_size)) {
                        blocked = false;
                        break;
                    }
                }
                if (blocked) {
                    removeClause( v_neg[z] );
                }
            }
        }
    }
}

void Preprocessor::blockedClauseElim2(){
    int merge_size = 0; // used in merge
    bool blocked = true;

    for (int t = 0 ; t < nVars(); ++t) {
        if (asynch_interrupt) return;

        Lit pos_current = toLit(t);
        // not a dont touch variable
        if (!frozen[var( pos_current )] &&
            !isEliminated(var(pos_current))
            && value(var(pos_current)) == l_Undef) {

            // v_pos.clear();
            // v_neg.clear();
            vec<CRef> v_pos;
            vec<CRef> v_neg;
            const vec<CRef>& cls = occurs.lookup(var(pos_current));

            // build v_pos and v_neg
            for (int j = 0; j <  cls.size(); ++j) {
                if (ca[cls[j]].mark() == 1 || cls[j] == CRef_Undef || satisfied(ca[cls[j]])) {
                    continue;
                }

                if (find( ca[cls[j]] , pos_current)) {
                    v_pos.push(cls[j]);
                } else {
                    v_neg.push(cls[j]);
                }
            }

            for (int z = 0; z < v_pos.size(); ++z) {
                blocked = true;
                auto& c = ca[v_pos[z]];

                if (c.mark() == 1 || v_pos[z] == CRef_Undef) continue;

                for (int s = 0; s < v_neg.size(); ++s) {
                    auto& c2 = ca[v_neg[s]];

                    if (c2.mark() == 1 || v_neg[s] == CRef_Undef) continue;

                    if (merge( c , c2 , var(pos_current), merge_size)) {
                        blocked = false;
                        break;
                    }
                }
                if (blocked) {
                    removeClause(v_pos[z]);
                }
            }
        }
    }
}

void Preprocessor::unitPropagationLookAhead(Var v, vec<int>& to_check){
    if (isEliminated(v)) return;
    if (value(v) != l_Undef) return;

    // ---------------------------------------------------------
    // assign false
    // ---------------------------------------------------------

    trail_lim.push(trail.size());
    int start = trail.size();
    uncheckedEnqueue(mkLit(v, false));

    vec<Lit> clause;
    vec<int> to_look;
    if (propagate() != CRef_Undef) {
        // conflict found
        // std::cerr << "Conflict found!!\n";
        clause.push(mkLit(v, true));
    } else {
        // no conflict found
        //std::cerr << "The following has been propagated by assigning variable "
        //          << v << " to TRUE\n";
        for( int t = start; t < trail.size(); ++t){
            to_check[toInt(trail[t])]++;
            to_look.push(toInt(trail[t]));
            //std::cerr << toInt(trail[t]) << "\n";
        }
    }

    cancelUntil(0);
    if (clause.size() > 0) {
        // we learn the conflict clause and terminate
        addClause_(clause);
        return;
    }

    // ---------------------------------------------------------
    // assign true
    // ---------------------------------------------------------

    trail_lim.push(trail.size());
    start = trail.size();
    uncheckedEnqueue(mkLit(v, true));

    vec<Lit> unit_clauses;
    vec<Lit> to_substitute;
    if (propagate() != CRef_Undef) {
        // conflict found
        // std::cerr << "Conflict found!!\n";
        clause.push(mkLit(v, false));
    } else {
        // no conflict found
        // std::cerr << "The following has been propagated by assigning variable "
        //          << v << " to TRUE\n";
        for (int t = start; t < trail.size(); ++t) {
            if (to_check[toInt(trail[t])] == 1) {
                // unit found
                unit_clauses.push(trail[t]);
            }
            if (to_check[(toInt(trail[t])^1)] == 1 && !frozen[var(trail[t])] && v != var(trail[t])) {
                // equivalence found
                to_substitute.push(~trail[t]);
            }
        }
    }

    cancelUntil(0);
    if(clause.size() > 0 ){
        // we learn the conflict clause and terminate
        addClause_(clause);
        // reset to_check
        for(unsigned int z = 0; z < to_look.size(); ++z){
            to_check[to_look[z]] = 0;
        }
        return;
    }

    // add the unit clauses
    for (unsigned int t = 0; t < unit_clauses.size(); ++t) {
        clause.clear();
        clause.push(unit_clauses[t]);
        addClause_(clause);
    }
    for (unsigned int u = 0; u < to_substitute.size(); ++u) {
        if (sign(to_substitute[u])) {
            substitute(var(to_substitute[u]), mkLit(v, true));
        } else {
            substitute(var(to_substitute[u]), mkLit(v, false));
        }
    }

    // reset to_check
    for (unsigned int z = 0; z < to_look.size() ; ++z){
        to_check[to_look[z]] = 0;
    }
}

void Preprocessor::getSimplifiedClauses(vec<vec<Lit>>& output) {
    output.capacity(trail.size() + clauses.size());

    // first the unit clauses
    for (int t = 0; t < trail.size(); ++t) {
        output.push(); output.last().growTo(1, trail[t]);
    }

    // second rest of the simp clauses stored at clauses
    for (int t = 0; t < clauses.size(); ++t) {
        if (clauses[t] != CRef_Undef) {
            auto& c = ca[clauses[t]];

            output.push(); output.last().growTo(c.size());
            for (int i = 0; i < c.size() ; ++i) {
                output.last()[i] = c[i];
            }
        }
    }
}

std::vector<std::vector<int>> Preprocessor::getSimpClauses() {
    std::vector<std::vector<int>> output;
    output.reserve(trail.size() + clauses.size());

    // first the unit clauses
    for (int t = 0; t < trail.size(); ++t) {
        if (dont_touch[var(trail[t])]) {
            output.push_back({toInt(trail[t])});
        }
    }

    // second rest of the simp clauses stored at clauses
    for (int t = 0; t < clauses.size(); ++t) {
        if (clauses[t] != CRef_Undef) {
            auto& c = ca[clauses[t]];

            output.emplace_back().resize(c.size());
            for (int i = 0; i < c.size() ; ++i) {
                output.back()[i] = toInt(c[i]);
            }
        }
    }

    return output;
}

}
