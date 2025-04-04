# DIMSPEC

## DIMSPEC file format

This prover supports DIMSPEC problem inputs [1][2].
This format is derived from the DIMACS specification and contains four CNF sections:

- `i`: The allowed initial states
- `u`: The universal constraints
- `g`: The goal states
- `t`: The transition relation

Below is a short summary of the DIMSPEC format.
Note, that the number of variables (`nvars`) for the transition relation is twice the number of variables for the rest of the parts.

```text
i cnf <nvars> <nclauses>
<clause>
<clause>
u cnf <nvars> <nclauses>
<clause>
<clause>
g cnf <nvars> <nclauses>
<clause>
<clause>
t cnf <2*nvars> <nclauses>
<clause>
<clause>
```

Below is a simple example:

```text
i cnf 1 1
-1 0
u cnf 1 0
g cnf 1 1
1 0
t cnf 2 2
-1 -2 0
1 2 0
```

Literals are encoded in signed format:

- Literal 1 = variable 0 positive
- Literal -1 = variable 0 negative
- Literal 2 = variable 1 positive
- Literal -2 = variable 1 negative
- Literal 3 = variable 2 positive

Clauses are specified per line and end with the literal 0.
The timeframe is specified by shifting the literal by the number of variables (`nvars`).

Assuming clauses are part of `t cnf 5 X`, the literals have the following meaning:

- `-1` = Variable 0 negative, timeframe 0
- `6` = Variable 0 positive, timeframe 1

## DIMSPEC Models

The model written by NCIP uses a format derived from DIMACS models.
The concept is as follows:

```text
v<state-id> <assignment> <assignment> 0
v<state-id> <assignment> <assignment> 0
```

Below is an example file:

```text
v0 -1 2 -3 4 -5 6 7 0
v1 1 -2 3 4 5 -6 -7 0
v2 1 2 -3 -4 -5 6 7 0
v3 1 2 3 4 5 -6 -7 0
```

Each line contians the variable assignment for one state.
The `<state-id>` denotes the state for which the assignment is given, starts at 0 and increments linearly.
The list of the variable assignments are given as signed literals.
The literal 0 ends the list of assignments.

- Literal 1 = variable 0 positive
- Literal -1 = variable 0 negative
- Literal 2 = variable 1 positive
- Literal -2 = variable 1 negative
- Literal 3 = variable 2 positive

## DIMSPEC Certificates

NCIP encodes the fixed point as property into the DIMSPEC certificate.
E.g.: `P = i ∨ C_1 ∨ ... ∨ C_n` where `i` is the allowed initial states and `C_1` to `C_n` are the Craig interpolants of each step.
As the property is stored as goal, it is inverted e.g. `g = !P` before writing.
The certificate can be checked with [DimCert](https://github.com/Froleyks/dimcert) [3].

Certificate checking is done in the following way:
Let `M := (i, g, t, u, s)` be the original input problem and `M' := (i', g', t', u', s')` be the certificate.

- `i`, `i'`: The initial states of the DIMSPEC input / certificate
- `t`, `t'`: The transition relation of the DIMSPEC input / certificate
- `g`, `g'`: The goal states of the DIMSPEC input / certificate
- `u`, `u'`: The universal constraints of the DIMSPEC input / certificate
- `s`, `s'`: The variables (state) of the DIMSPEC input / certificate

The following QBF-checks prove the certificate is valid:

- Reset: `∀s ∃(s'\s). i ∧ u ⇒ i' ∧ u'`
- Transition: `∀s ∃(s'\s). t ∧ u0 ∧ u1 ∧ u0' ⇒ t' ∧ u1'`
- Property: `∀s ∃(s'\s). (u ∧ u') ⇒ (¬g' ⇒ ¬g)`
- Base: `∀s. i' ∧ u' ⇒ ¬g'`
- Step: `∀s. ¬g0' ∧ t' ∧ u0' ∧ u1' ⇒ ¬g1'`

## DIMSPEC Encoding

The four parts of the DIMSPEC problem are used as follows:

- `Init := i ∧ u`
- `Trans := t ∧ u`
- `Target := g ∧ u`

## DIMSPEC API

The format can be passed to the NCIP C++ API via the `DimspecProblemBuilder` and `DimspecProblem` classes.
The builder is provided to transform the input format into the NCIP's native problem specification.
For ease-of-use and simple handling of literals, the `-` (invert literal), `^` (xor literal with boolean) and `>>` (shift literal timeframe) operators are overloaded.
Below is an example for the usage of the DIMSPEC API constructing the format on-the-fly, combined with a prover portfolio approach to run two NCIP instances in parallel.

```cpp
#include <iostream>

#include <bmc-format-dimspec.hpp>
#include <bmc-ncip.hpp>
#include <bmc-ncip-portfolio.hpp>

using namespace Ncip;

int main() {
    // Build the DIMSPEC-Problem on-the-fly and convert to native format.
    DimspecProblemBuilder builder { };
    auto latchVar { builder.AddVariable() };
    auto latchLit { BmcLiteral::FromVariable(latchVar) };
    builder.AddClause(DimspecClauseType::Initial, { -latchLit });
    builder.AddClause(DimspecClauseType::Trans, {  latchLit, -latchLit >> 1 });
    builder.AddClause(DimspecClauseType::Trans, { -latchLit,  latchLit >> 1 });
    builder.AddClause(DimspecClauseType::Goal, { latchLit });
    auto [dimspecProblem, bmcProblem] = builder.Build();

    // Build the NCIP configuration that holds the NCIP options.
    BmcConfiguration config { };
    config.SetMaximumDepth(10);

    // Build a portfolio with MiniCraig and CaDiCraig as backends.
    // Each prover in the portfolio has a separate problem and configuration.
    PortfolioBmcSolver solver {
        MiniCraigBmcSolver { bmcProblem, config },
        CadiCraigBmcSolver { bmcProblem, config }
    };
    auto result { solver.Solve() };
    switch (result.GetStatus()) {

    case BmcStatus::Sat:
        std::cout << "Sat" << std::endl;
        for (auto const& timeframe : result.GetModel().GetTimeframes()) {
            std::cout << "Model " << to_string(timeframe) << std::endl;
        }
        return 10;

    case BmcStatus::Unsat:
        std::cout << "Unsat" << std::endl;
        auto certificate { CipCertificateBuilder { }
            .Build(dimspecProblem, result.GetCertificate()) };
        for (auto const& clause : certificate.GetInit()) {
            std::cout << "Cert Init " << clause << std::endl;
        }
        for (auto const& clause : certificate.GetTrans()) {
            std::cout << "Cert Trans " << clause << std::endl;
        }
        for (auto const& clause : certificate.GetGoal()) {
            std::cout << "Cert Goal " << clause << std::endl;
        }
        for (auto const& clause : certificate.GetUniversal()) {
            std::cout << "Cert Universal " << clause << std::endl;
        }
        return 20;

    default:
        std::cout << "Limit Reached" << std::endl;
        return 30;

    }
}
```

The format requires custom parsing.
For reference have a look at `ParseDimspecProblem` in `src/bmc-prover.cpp`.

```cpp
static std::tuple<DimspecProblem, BmcProblem> ReadDimspec(std::istream& stream) {
  DimspecProblemBuilder builder { };

  // Custom implementation ...

  try {
    return builder.Build();
  } catch (const DimspecProblemException& error) {
    throw "Invalid problem";
  }
}
```

The procedure of writing is exactly the opposite to reading the DIMSPEC format.
For reference have a look at `ExportDimspecProblem` in `src/bmc-prover.cpp`.

```cpp
static void WriteDimspec(std::ostream& stream, const DimspecProblem& problem) {
  // Custom implementation ...
}
```

A certificate for the DIMSPEC format can be generated via the `DimspecCertificateBuilder` and `DimspecCertificate` classes.
The certificate can be written like the DIMSPEC format.

```cpp
static void WriteDimspecCertificate(std::ostream& stream, const DimspecProblem& problem, const BmcCertificate& certificate) {
  DimspecCertificateBuilder builder;
  DimspecCertificate output = builder.Build(problem, certificate);
  WriteDimspec(stream, output);
}
```

---

[1] Marco Kleine Büning, Tomáš Balyo, Carsten Sinz, "Using DimSpec for Bounded and Unbounded Software Model Checking", 2019, 10.1007/978-3-030-32409-4_2  
[2] Dimspec Reference, URL: https://people.ciirc.cvut.cz/~sudamar2/dimspec.html  
[3] DimCert Repository, URL: https://github.com/Froleyks/dimcert  
