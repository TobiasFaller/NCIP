# KittenCraig

This is a hacky frontend for Kitten that builds Craig interpolants from the proof.
It uses an extended DIMACS format for input.
This format supports the followings lines:

- **a** = A Variables
- **b** = B Variables
- **g** = Global Variables
- **f** = Assumptions
- **A** = A Clause
- **B** = B Clause

Below is an example with only global variables, one assumption, two clauses and a constraint:

```text
p craigcnf 6 4
a 1 0
g 2 0
a 3 4 0
g 5 6 0
f -4 1 -2 -3 0
B 2 -6 0
A -1 -6 -3 0
A 4 6 0
A 3 -2 0
```

If the result is UNSAT the solver output is augmented with a Craig interpolant CNF:

```text
UNSAT
CRAIG NORMAL
p cnf 8 7
-7 2 0
-7 6 0
7 -2 -6 0
-8 -7 0
-8 6 0
8 7 -6 0
8 0
```

The generated Craig interpolant can be configured via the command line option `--interp=N`.

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
craig->set_craig_construction(CraigConstruction::ASYMMETRIC);
craig->label_variable(1, CraigVarType::GLOBAL);
craig->label_clause(1, CraigClauseType::A_CLAUSE);
craig->label_clause(2, CraigClauseType::B_CLAUSE);

solver->add(-1); solver->add(0);
solver->add(1); solver->add(0);
kitten_track_antecedents(solver);
assert (kitten_solve(solver) == 20);

tracer.conclude_unsat(solver);

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

Install Kissat in a directory named `kissat` next to the KittenCraig source directory.
The directory structure should look like this:

```text
parent
+ kissat
+ kittencraig
```

Now use CMake to build KittenCraig:

```bash
# Build Kitten library and KittenCraig.
# Use following cmake command options for debugging:
#   -DCMAKE_BUILD_TYPE=Debug
#   -DKITTENCRAIG_LOGGING=ON
# Use following cmake command options for realease:
#   None
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DKITTENCRAIG_LOGGING=ON
make -C build -j

# Run KittenCraig.
./build/kittencraig --interp=1 input.ccnf result.txt
```
