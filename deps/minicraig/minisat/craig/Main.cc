/*****************************************************************************************[Main.cc]
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

#include <errno.h>
#include <zlib.h>

#include "../utils/System.h"
#include "../utils/ParseUtils.h"
#include "../utils/Options.h"
#include "../craig/CraigSolver.h"
#include "../craig/CraigTypes.h"

using namespace MiniCraig;

#ifdef ENABLE_SIMP
#define SOLVER CraigSimpSolver
#else
#define SOLVER CraigSolver
#endif

//=================================================================================================
static void readClause(StreamBuffer& in, SOLVER& , vec<Lit>& lits) {
    lits.clear();
    for (;;){
        int parsed_lit = parseInt(in);
        if (parsed_lit == 0) break;
        lits.push(mkLit(abs(parsed_lit) - 1, parsed_lit < 0));
    }
}

static void parseInput(gzFile input_stream, SOLVER& solver, vec<Lit>& assumptions, bool strictp = false) {
    StreamBuffer in(input_stream);

    vec<Lit> lits;
    int vars    = 0;
    int clauses = 0;
    int cnt     = 0;
    int vcnt    = 0;
    for (;;){
        skipWhitespace(in);
        if (*in == EOF) break;
        else if (*in == 'p'){
            if (eagerMatch(in, "p craigcnf")){
                vars    = parseInt(in);
                clauses = parseInt(in);
            }else{
                printf("PARSE ERROR! Unexpected char: %c\n", *in), exit(3);
            }
        } else if (*in == 'c')
            skipLine(in);
        else if (*in == 'g' || *in == 'a' || *in == 'b') {
            CraigVarType varType;
            if (*in == 'g') { varType = CraigVarType::GLOBAL; }
            else if (*in == 'a') { varType = CraigVarType::A_LOCAL; }
            else if (*in == 'b') { varType = CraigVarType::B_LOCAL; }
            else { printf("PARSE ERROR! Unexpected variable type: %c\n", *in); exit(4); }
            ++in;
            skipWhitespace(in);

            for (;;) {
                int var = parseInt(in);
                ++in;
                skipWhitespace(in);

                if (var == 0) break;

                vcnt++;
                Var v = solver.newVar(varType);
                if (var != (v + 1)) {
                    printf("PARSE ERROR! Var index does not match: %d vs %d\n", var, v), exit(3);
                }
#ifdef ENABLE_SIMP
                if (varType == CraigVarType::GLOBAL) {
                    solver.setFrozen(v, true);
                }
#endif
            }
        }
        else if (*in == 'f') {
            ++in;
            skipWhitespace(in);

            for (;;) {
                int parsed_lit = parseInt(in);
                ++in;
                skipWhitespace(in);

                if (parsed_lit == 0) break;

                int var = abs(parsed_lit) - 1;
                assumptions.push(mkLit(var, parsed_lit < 0));
            }
        }
        else if (*in == 'A' || *in == 'B') {
            CraigClauseType craigType;
            if (*in == 'A') { craigType = CraigClauseType::A_CLAUSE; }
            else if (*in == 'B') { craigType = CraigClauseType::B_CLAUSE; }
            else { printf("PARSE ERROR! Unexpected clause type: %c\n", *in); exit(5); }
            ++in;

            cnt++;
            readClause(in, solver, lits);
            solver.addClause(lits, craigType); }
        else
            printf("PARSE ERROR! Unexpected line: %c\n", *in), exit(6);
    }
    if (strictp && cnt != clauses)
        printf("PARSE ERROR! DIMACS header mismatch: wrong number of clauses\n");
    if (strictp && vcnt != vars)
        printf("PARSE ERROR! DIMACS header mismatch: wrong number of variables\n");
}

//=================================================================================================


static SOLVER* solver;
// Terminate by notifying the solver and back out gracefully. This is mainly to have a test-case
// for this feature of the Solver as it may take longer than an immediate call to '_exit()'.
static void SIGINT_interrupt(int) { solver->interrupt(); }

// Note that '_exit()' rather than 'exit()' has to be used. The reason is that 'exit()' calls
// destructors and may cause deadlocks if a malloc/free function happens to be running (these
// functions are guarded by locks for multithreaded use).
static void SIGINT_exit(int) {
    printf("\n"); printf("*** INTERRUPTED ***\n");
    if (solver->verbosity > 0){
        solver->printStats();
        printf("\n"); printf("*** INTERRUPTED ***\n"); }
    _exit(1); }


//=================================================================================================
// Main:


int main(int argc, char** argv)
{
    try {
        setUsageHelp("USAGE: %s [options] <input-file> <output-file>\n\n  where input may be either in plain or gzipped DIMACS.\n");
        setX86FPUPrecision();

        // Extra options:
        //
        IntOption    verb   ("MAIN", "verb",    "Verbosity level (0=silent, 1=some, 2=more).", 1, IntRange(0, 2));
        IntOption    cpu_lim("MAIN", "cpu-lim", "Limit on CPU time allowed in seconds", 0, IntRange(0, INT32_MAX));
        IntOption    mem_lim("MAIN", "mem-lim", "Limit on memory usage in megabytes", 0, IntRange(0, INT32_MAX));
        IntOption    interp ("MAIN", "interp",  "Craig interpolant (0=none, 1=sym, 2=asym, 3='sym, 4='asym, 5=inter, 6=union, 7=smallest, 8=largest).", 1, IntRange(0, 8));
        BoolOption   strictp("MAIN", "strict",  "Validate DIMACS header during parsing.", false);

        parseOptions(argc, argv, true);

        SOLVER S;
        double initial_time = cpuTime();

        S.verbosity = verb;

        solver = &S;
        // Use signal handlers that forcibly quit until the solver will be able to respond to
        // interrupts:
        sigTerm(SIGINT_exit);

        // Try to set resource limits:
        if (cpu_lim != 0) limitTime(cpu_lim);
        if (mem_lim != 0) limitMemory(mem_lim);

        if (argc == 1)
            printf("Reading from standard input... Use '--help' for help.\n");

        gzFile in = (argc == 1) ? gzdopen(0, "rb") : gzopen(argv[1], "rb");
        if (in == NULL)
            printf("ERROR! Could not open file: %s\n", argc == 1 ? "<stdin>" : argv[1]), exit(1);

        if (S.verbosity > 0){
            printf("============================[ Problem Statistics ]=============================\n");
            printf("|                                                                             |\n"); }

        // Configurate craig construction before adding clauses.
        CraigInterpolant interpolant;
        CraigConstruction construction;
        if (interp == 0) { construction = CraigConstruction::NONE; }
        if (interp == 1) { interpolant = CraigInterpolant::SYMMETRIC; construction = CraigConstruction::SYMMETRIC; }
        if (interp == 2) { interpolant = CraigInterpolant::ASYMMETRIC; construction = CraigConstruction::ASYMMETRIC; }
        if (interp == 3) { interpolant = CraigInterpolant::DUAL_SYMMETRIC; construction = CraigConstruction::DUAL_SYMMETRIC; }
        if (interp == 4) { interpolant = CraigInterpolant::DUAL_ASYMMETRIC; construction = CraigConstruction::DUAL_ASYMMETRIC; }
        if (interp == 5) { interpolant = CraigInterpolant::INTERSECTION; construction = CraigConstruction::ALL; }
        if (interp == 6) { interpolant = CraigInterpolant::UNION; construction = CraigConstruction::ALL; }
        if (interp == 7) { interpolant = CraigInterpolant::SMALLEST; construction = CraigConstruction::ALL; }
        if (interp == 8) { interpolant = CraigInterpolant::LARGEST; construction = CraigConstruction::ALL; }
        S.setCraigConstruction(construction);

        vec<Lit> assumptions;
        parseInput(in, S, assumptions, strictp);
        gzclose(in);
        FILE* res = (argc >= 3) ? fopen(argv[2], "wb") : NULL;

        if (S.verbosity > 0){
            printf("|  Number of variables:  %12d                                         |\n", S.nVars());
            printf("|  Number of clauses:    %12d                                         |\n", S.nClauses()); }

        double parsed_time = cpuTime();
        if (S.verbosity > 0){
            printf("|  Parse time:           %12.2f s                                       |\n", parsed_time - initial_time);
            printf("|                                                                             |\n"); }

        // Change to signal-handlers that will only notify the solver and allow it to terminate
        // voluntarily:
        sigTerm(SIGINT_interrupt);

        lbool ret = S.solveLimited(assumptions);
        if (S.verbosity > 0){
            S.printStats();
            printf("\n"); }
        printf(ret == l_True ? "SATISFIABLE\n" : ret == l_False ? "UNSATISFIABLE\n" : "INDETERMINATE\n");
        if (res != NULL){
            if (ret == l_True){
                fprintf(res, "SAT\n");
                for (int i = 0; i < S.nVars(); i++)
                    if (S.model[i] != l_Undef)
                        fprintf(res, "%s%d ", (S.model[i]==l_True)?"":"-", i+1);
                fprintf(res, "0\n");
            }else if (ret == l_False) {
                fprintf(res, "UNSAT\n");
                if (construction != CraigConstruction::NONE) {
                    vec<vec<Lit>> craig;
                    int vars = S.nVars();
                    CraigCnfType craigType = S.createCraigCnf(interpolant, craig, vars);
                    if (craigType == CraigCnfType::Constant0) { fprintf(res, "CRAIG ZERO\n"); assert(craig.size() == 1); assert(craig[0].size() == 0); }
                    if (craigType == CraigCnfType::Constant1) { fprintf(res, "CRAIG ONE\n"); assert(craig.size() == 0); }
                    if (craigType == CraigCnfType::Normal) {  fprintf(res, "CRAIG NORMAL\n"); }
                    if (craigType == CraigCnfType::None) { fprintf(res, "CRAIG NONE\n"); }

                    fprintf(res, "p cnf %d %d\n", vars, craig.size());
                    for (size_t index { 0u }; index < craig.size(); index++) {
                        for (size_t lit { 0u }; lit < craig[index].size(); lit++) {
                            auto v = var(craig[index][lit]);
                            fprintf(res, "%d ", (var(craig[index][lit]) + 1) * (sign(craig[index][lit]) ? -1 : 1));
                        }
                        fprintf(res, "0\n");
                    }
                } else
                    fprintf(res, "CRAIG NONE\n");
            } else
                fprintf(res, "INDET\n");
            fclose(res);
        }

#ifdef NDEBUG
        exit(ret == l_True ? 10 : ret == l_False ? 20 : 0);     // (faster than "return", which will invoke the destructor for 'Solver')
#else
        return (ret == l_True ? 10 : ret == l_False ? 20 : 0);
#endif
    } catch (OutOfMemoryException&){
        printf("===============================================================================\n");
        printf("INDETERMINATE\n");
        exit(0);
    }
}
