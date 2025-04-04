# CaDiCraig

This is a hacky frontend for CaDiCaL that attaches a tracer building Craig interpolants.
It can be used like CaDiCaL but enables using an extended DIMACS format for input.
This format supports the followings lines:

- **a** = A Variables
- **b** = B Variables
- **g** = Global Variables
- **f** = Assumptions
- **A** = A Clause
- **B** = B Clause
- **C** = Constraint (A Clause)
- **D** = Constraint (B Clause)

Below is an example with only global variables, one assumption, two clauses and a constraint:

```text
p craigcnf 3 2
g 1 2 3 0
f -1 0
A 1 3 0
B -2 -3 0
C 1 2 0
```

If the result is UNSAT the solver output is augmented with a Craig interpolant CNF:

```text
s UNSATISFIABLE
i CRAIG NORMAL
i p cnf 4 4
i -4 2 0
i -4 -1 0
i 4 -2 1 0
i -4 0
```

The generated Craig interpolant can be configured via the command line option `--lratcraig=N`.

- **0** = none
- **1** = symmetric
- **2** = asymmetric
- **3** = dual symmetric
- **4** = dual asymmetric
- **5** = intersection
- **6** = union
- **7** = smallest
- **8** = largest

## API Usage

The tracer itself is easily usable and only requires labling variables and clauses.
Below is an example how to generate Craig interpolants:

```cpp
auto* craig = new CraigTracer();
solver->connect_proof_tracer(craig, true);

craig->set_craig_construction(CraigConstruction::ASYMMETRIC);
craig->label_variable(1, CraigVarType::GLOBAL);
craig->label_clause(1, CraigClauseType::A_CLAUSE);
craig->label_clause(2, CraigClauseType::B_CLAUSE);

solver->add(-1); solver->add(0);
solver->add(1); solver->add(0);
asser(solver->solve() == 20);

int tseitin_offset { 2 };
vector<vector<int>> inter_cnf { };
auto result { craig->create_craig_interpolant(CraigInterpolant::ASYMMETRIC, inter_cnf, tseitin_offset) };

assert(result == CraigCnfType::NORMAL);
assert(inter_cnf == vector<vector<int>> { { -1 } });
```

Currently the following Craig interpolants are implemented:

- NONE
- SYMMETRIC (requires base SYMMETRIC)
- ASYMMETRIC (requires base ASYMMETRIC)
- DUAL_SYMMETRIC (requires base DUAL_SYMMETRIC)
- DUAL_ASYMMETRIC (requires base DUAL_ASYMMETRIC)
- INTERSECTION (of selected interpolant bases)
- UNION (of selected interpolant bases)
- SMALLEST (of selected interpolant bases)
- LARGEST (of selected interpolant bases)

The interpolants are derived from the constructed interpolant bases.
The following interpolant bases are implemented:

- SYMMETRIC
- ASYMMETRIC
- DUAL_SYMMETRIC
- DUAL_ASYMMETRIC

## Building

Install CaDiCal in a directory named `cadical` next to the cadicraig source directory.
The directory structure should look like this:

```text
parent
+ cadical
+ cadicraig
```

Now use CMake to build CaDiCraig:

```bash
# Build CaDiCaL library and CaDiCraig.
# Use following cmake command options for debugging:
#   -DCMAKE_BUILD_TYPE=Debug
#   -DCADICAL_LOGGING=ON
# Use following cmake command options for realease:
#   -DCADICAL_TIMESTAMP=ON
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCADICAL_LOGGING=ON
make -C build -j

# Run CaDiCraig.
./build/cadicraig --log --lratcraig=1 -w result.txt input.ccnf
```
