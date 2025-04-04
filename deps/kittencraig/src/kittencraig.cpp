#include "kitten.h"
#include "kittentracer.hpp"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <fstream>
#include <vector>

#include <zlib.h>

using namespace KittenCraig;

//-------------------------------------------------------------------------------------------------
// A simple buffered character stream class:


class StreamBuffer {
    gzFile         in;
    unsigned char* buf;
    int            pos;
    int            size;

    enum { buffer_size = 64*1024 };

    void assureLookahead() {
        if (pos >= size) {
            pos  = 0;
            size = gzread(in, buf, buffer_size); } }

public:
    explicit StreamBuffer(gzFile i) : in(i), pos(0), size(0){
        buf = (unsigned char*)realloc(NULL, buffer_size);
        assureLookahead();
    }
    ~StreamBuffer() { free(buf); }

    int  operator *  () const { return (pos >= size) ? EOF : buf[pos]; }
    void operator ++ ()       { pos++; assureLookahead(); }
    int  position    () const { return pos; }
};


//-------------------------------------------------------------------------------------------------
// End-of-file detection functions for StreamBuffer and char*:


static inline bool isEof(StreamBuffer& in) { return *in == EOF;  }
static inline bool isEof(const char*   in) { return *in == '\0'; }

//-------------------------------------------------------------------------------------------------
// Generic parse functions parametrized over the input-stream type.


static unsigned mkLit(unsigned variable, bool inverted) {
    return (variable << 1) | (inverted ? 1 : 0);
}

template<class B>
static void skipWhitespace(B& in) {
    while ((*in >= 9 && *in <= 13) || *in == 32)
        ++in; }


template<class B>
static void skipLine(B& in) {
    for (;;){
        if (isEof(in)) return;
        if (*in == '\n') { ++in; return; }
        ++in; } }


template<class B>
static int parseInt(B& in) {
    int     val = 0;
    bool    neg = false;
    skipWhitespace(in);
    if      (*in == '-') neg = true, ++in;
    else if (*in == '+') ++in;
    if (*in < '0' || *in > '9') fprintf(stderr, "PARSE ERROR! Unexpected char: %c\n", *in), exit(3);
    while (*in >= '0' && *in <= '9')
        val = val*10 + (*in - '0'),
        ++in;
    return neg ? -val : val; }


// String matching: in case of a match the input iterator will be advanced the corresponding
// number of characters.
template<class B>
static bool match(B& in, const char* str) {
    int i;
    for (i = 0; str[i] != '\0'; i++)
        if (in[i] != str[i])
            return false;

    in += i;

    return true;
}

// String matching: consumes characters eagerly, but does not require random access iterator.
template<class B>
static bool eagerMatch(B& in, const char* str) {
    for (; *str != '\0'; ++str, ++in)
        if (*str != *in)
            return false;
    return true; }

template<class B>
static void readClause(B& in, kitten* solver, std::vector<unsigned>& lits) {
    lits.clear();
    for (;;){
        int parsed_lit = parseInt(in);
        if (parsed_lit == 0) break;
        lits.push_back(mkLit(abs(parsed_lit) - 1, (parsed_lit < 0)));
    }

    std::sort(lits.begin(), lits.end());
    lits.erase(std::unique(lits.begin(), lits.end()), lits.end());
}

template<class B>
static void parseInput(B& in, kitten* solver, KittenTracer& tracer, std::vector<unsigned>& assumptions, int& outVariables, int& outClauses, bool strictp = false) {
    std::vector<unsigned> lits;
    int vars    = 0;
    int clauses = 0;
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
                outVariables++;
                tracer.label_variable(outVariables, varType);
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
                assumptions.push_back(mkLit(var, parsed_lit < 0));
            }
        }
        else if (*in == 'A' || *in == 'B') {
            CraigClauseType craigType;
            if (*in == 'A') { craigType = CraigClauseType::A_CLAUSE; }
            else if (*in == 'B') { craigType = CraigClauseType::B_CLAUSE; }
            else { printf("PARSE ERROR! Unexpected clause type: %c\n", *in); exit(5); }
            ++in;

            readClause(in, solver, lits);
            bool tautology = false;
            for (int index { 0 }; index + 1 < lits.size(); index++) {
                if ((lits[index] ^ lits[index + 1]) == 1) {
                    tautology = true;
                    break;
                }
            }
            if (tautology) {
                continue;
            }

            outClauses++;
            tracer.label_clause(outClauses, craigType);
            kitten_clause_with_id_and_exception(solver, outClauses, lits.size(), &(lits.front()), UINT_MAX);
        }
        else
            printf("PARSE ERROR! Unexpected line: %c\n", *in), exit(6);
    }
    if (strictp && outClauses != clauses)
        printf("PARSE ERROR! DIMACS header mismatch: wrong number of clauses\n");
    if (strictp && outVariables != vars)
        printf("PARSE ERROR! DIMACS header mismatch: wrong number of variables\n");
}

void usage() {
	std::cerr << "Usage: kittencraig [options] <input-file> (<output-file>)" << std::endl;
	std::cerr << std::endl;
	std::cerr << "Options:" << std::endl;
	std::cerr << "  --interp=<i>: Craig interpolant (0=none, 1=sym, 2=asym, 3='sym, 4='asym, 5=inter, 6=union, 7=smollest, 8=largest)" << std::endl;
}

int main(int argc, char** argv) {
    int interp = 0;

	std::vector<std::string> freeArguments;
	for (int argi { 1 }; argi < argc; argi++) {
		const std::string argument { argv[argi] };

		if (argument.rfind("--interp=", 0u) == 0u) {
			std::cerr << "Setting interpolant to \"" << argument.substr(9u) << "\"" << std::endl;
			if (argument == "--interp=0") { interp = 0; }
			else if (argument == "--interp=1") { interp = 1; }
			else if (argument == "--interp=2") { interp = 2; }
			else if (argument == "--interp=3") { interp = 3; }
			else if (argument == "--interp=4") { interp = 4; }
			else if (argument == "--interp=5") { interp = 5; }
			else if (argument == "--interp=6") { interp = 6; }
			else if (argument == "--interp=7") { interp = 7; }
			else if (argument == "--interp=8") { interp = 8; }
			else {
				std::cerr << "Unknown value for interpolant \"" << argument.substr(9u) << "\"" << std::endl;
				exit(2);
			}
		} else if (argument.rfind("--", 0u) == 0u) {
			std::cerr << "Unknown argument \"" << argument << "\"" << std::endl;
			exit(2);
		} else {
			freeArguments.push_back(argument);
		}
	}

	if (freeArguments.size() == 0 || freeArguments.size() > 2) {
		usage();
		exit(1);
	}

    auto* solver = kitten_init();

    // Configurate craig construction before adding clauses.
    KittenTracer tracer;
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
    tracer.set_craig_construction(construction);

	const std::string inputFile { freeArguments[0] };
	const std::string outputFile { (freeArguments.size() > 1) ? freeArguments[1] : "" };

	std::ofstream outputFStream;
	if (outputFile != "-" && outputFile != "") {
		outputFStream = std::ofstream { outputFile };
		if (!outputFStream.good()) {
			std::cerr << "Output file \"" << outputFile << "\" could not be created" << std::endl;
			exit(2);
		}
	}

	std::ostream& output { (outputFile == "-") ? std::cout : outputFStream };

    gzFile input = (inputFile == "-") ? gzdopen(0, "rb") : gzopen(inputFile.c_str(), "rb");
    if (input == NULL) {
        std::cerr << "Output file \"" << outputFile << "\" could not be created" << std::endl;
        exit(2);
    }

    int variables = 0;
    int clauses = 0;
    std::vector<unsigned> assumptions;
    StreamBuffer sb(input);
    parseInput(sb, solver, tracer, assumptions, variables, clauses, false);
    gzclose(input);

    for (auto& assumption : assumptions) {
        kitten_assume(solver, assumption);
        tracer.add_assumption((assumption / 2 + 1) * ((assumption & 1) ? -1 : 1));
    }

    kitten_track_antecedents(solver);
    unsigned ret = kitten_solve(solver);
    if (ret == 20) {
        tracer.conclude_unsat(solver);
    }

    if (ret == 0) {
        std::cerr << "INDETERMINATE" << std::endl;

        output << "INDET" << std::endl;
    } else if (ret == 10) {
        std::cerr << "SATISFIABLE" << std::endl;

        output << "SAT" << std::endl;
        for (unsigned variable { 0 }; variable < variables; variable++) {
            auto value = kitten_value(solver, variable << 1);
            output << ((variable + 1) * value) << " ";
        }
        output << "0" << std::endl;
    } else if (ret == 20) {
        std::cerr << "UNSATISFIABLE" << std::endl;

        output << "UNSAT" << std::endl;
        if (construction != CraigConstruction::NONE) {
            std::vector<std::vector<int>> craig;
            int nextVar = variables + 1;
            auto craigType = tracer.create_craig_interpolant(interpolant, craig, nextVar);
            if (craigType == CraigCnfType::CONSTANT0) { output << "CRAIG ZERO" << std::endl; assert(craig.size() == 1); assert(craig[0].size() == 0); }
            if (craigType == CraigCnfType::CONSTANT1) { output << "CRAIG ONE" << std::endl; assert(craig.size() == 0); }
            if (craigType == CraigCnfType::NORMAL) {  output << "CRAIG NORMAL" << std::endl; }
            if (craigType == CraigCnfType::NONE) { output << "CRAIG NONE" << std::endl; }

            output << "p cnf " << (nextVar - 1) << " " << craig.size() << std::endl;
            for (auto& clause : craig) {
                for (auto& literal : clause) {
                    output << literal << " ";
                }
                output << "0" << std::endl;
            }
        } else {
            output << "CRAIG NONE" << std::endl;
        }
    }

    kitten_release(solver);
    exit(ret);
}
