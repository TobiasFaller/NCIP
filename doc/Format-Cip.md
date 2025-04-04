# CIP

## CIP file format

This prover supports CIP (Craig Interpolant Prover) problem inputs [1].
Note however, this format is mainly used for development and has otherwise no practical use.
This format contains three CNF sections that directly map to the internal prover problem format:

- `INIT`: The allowed initial states
- `TRANS`: The (non-total) transition relation
- `TARGET`: The allowed target states

Variables have to be declared prior to use in the first section an have different types (see below).

Below is a short summary of the CIP format:

```text
DECL
<var> <var-id>
<var> <var-id>

INIT
<clause>
<clause>

TRANS
<clause>
<clause>

TARGET
<clause>
<clause>
```

Below is a simple example:

```text
DECL
INPUT_VAR 1
LATCH_VAR 2

INIT
([-2:0])

TRANS
([-1:0], [2:1])
([1:0],[-2:1])

TARGET
([2:0])
```

Variables have one of four types and need to be declared in ascending order, starting from 1:

- `INPUT_VAR`: An input of the circuit
- `LATCH_VAR`: A latch of the circuit
- `OUTPUT_VAR`: An output of the circuit
- `AUX_VAR`: A Tseitin / local variable

Literals are encoded in signed format:

- Literal 1 = variable 0 positive
- Literal -1 = variable 0 negative
- Literal 2 = variable 1 positive
- Literal -2 = variable 1 negative
- Literal 3 = variable 2 positive

Clauses are enclosed in brackets and only one clause can be specified per line.
In clauses a timeframe is defined per literal, separated via a colon:
Using a timeframe of 1 allows referencing the next-state of a latch.

- `[-1:0]` = Variable 0 negative, timeframe 0
- `[2:1]` = Variable 2 positive, timeframe 1

## CIP Models

The model written by NCIP uses a custom format.
The concept is as follows:

```text
<state-id> = <assignment>
<state-id> = <assignment>
```

Below is an example file:

```text
0 = 000X001X
1 = 10101X00
2 = 00011010
```

The format contains the assignment of variables for each state.
The `<state-id>` denotes the state for which the assignment is given, starts at 0 and increments linearly.
The list of the variable assignments uses the symbols `0`, `1`, `X`.

## CIP Certificates

NCIP encodes the fixed point as property into the CIP certificate.
E.g.: `P = INIT ∨ C_1 ∨ ... ∨ C_n` where `INIT` is the allowed initial states and `C_1` to `C_n` are the Craig interpolants of each step.
As the property is stored as output it is inverted e.g. `TARGET = !P` before writing.
The certificate can be checked with [CipCert](https://github.com/TobiasFaller/cipcert) [2].

Certificate checking is done in the following way:
Let `M := (INIT, TRANS, TARGET, S)` be the original input problem and `M' := (INIT', TRANS', TARGET', S')` be the certificate.

- `INIT`, `INIT'`: The initial states of the CIP input / certificate
- `TRANS`, `TRANS'`: The transition relation of the CIP input / certificate
- `TARGET`, `TARGET'`: The target states of the CIP input / certificate
- `S`, `S'`: The variables (state) of the CIP input / certificate

The following QBF-checks prove the certificate is valid:

- Reset: `∀S ∃(S'\S). INIT => INIT'`
- Transition: `∀S ∃(S'\S). TRANS ⇒ TRANS'`
- Property: `∀S ∃(S'\S). ¬TARGET' ⇒ ¬TARGET`
- Base: `∀S'. INIT' ⇒ !TARGET'`
- Step: `∀S'. !TARGET0' & TRANS' ⇒ !TARGET1'`

## CIP Encoding

The three parts of the CIP problem are used one to one as follows:

- `Init := INIT`
- `Trans := TRANS`
- `Target := TARGET`

## CIP API

The format can be passed to the NCIP C++ API via the `CipProblemBuilder` and `CipProblem` classes.
The builder is provided to transform the input format into the NCIP's native problem specification.
For ease-of-use and simple handling of literals, the `-` (invert literal), `^` (xor literal with boolean) and `>>` (shift literal timeframe) operators are overloaded.
Below is an example for the usage of the CIP API constructing the format on-the-fly, combined with a prover portfolio approach to run two NCIP instances in parallel.

```cpp
#include <iostream>

#include <bmc-format-cip.hpp>
#include <bmc-ncip.hpp>
#include <bmc-ncip-portfolio.hpp>

using namespace Ncip;

int main() {
    // Build the CIP-Problem on-the-fly and convert to native format.
    CipProblemBuilder builder { };
    auto latchVar { builder.AddVariable(CipVariableType::Latch) };
    auto latchLit { BmcLiteral::FromVariable(latchVar) };
    builder.AddClause(CipClauseType::Initial, { -latchLit });
    builder.AddClause(CipClauseType::Trans, {  latchLit, -latchLit >> 1 });
    builder.AddClause(CipClauseType::Trans, { -latchLit,  latchLit >> 1 });
    builder.AddClause(CipClauseType::Target, { latchLit });
    auto [cipProblem, bmcProblem] = builder.Build();

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
            .Build(cipProblem, result.GetCertificate()) };
        for (auto const& clause : certificate.GetInit()) {
            std::cout << "Cert Init " << clause << std::endl;
        }
        for (auto const& clause : certificate.GetTrans()) {
            std::cout << "Cert Trans " << clause << std::endl;
        }
        for (auto const& clause : certificate.GetTarget()) {
            std::cout << "Cert Target " << clause << std::endl;
        }
        return 20;

    default:
        std::cout << "Limit Reached" << std::endl;
        return 30;

    }
}
```

The format requires custom parsing.
For reference have a look at `ParseCipProblem` in `src/bmc-prover.cpp`.

```cpp
static std::tuple<CipProblem, BmcProblem> ReadCip(std::istream& stream) {
  CipProblemBuilder builder { };

  // Custom implementation ...

  try {
    return builder.Build();
  } catch (const CipProblemException& error) {
    throw "Invalid problem";
  }
}
```

The procedure of writing is exactly the opposite to reading the CIP format.
For reference have a look at `ExportCipProblem` in `src/bmc-prover.cpp`.

```cpp
static void WriteCip(std::ostream& stream, const CipProblem& problem) {
  // Custom implementation ...
}
```

A certificate for the CIP format can be generated via the `CipCertificateBuilder` and `CipCertificate` classes.
The certificate can be written like the CIP format.

```cpp
static void WriteCipCertificate(std::ostream& stream, const CipProblem& problem, const BmcCertificate& certificate) {
  CipCertificateBuilder builder;
  CipCertificate output = builder.Build(problem, certificate);
  WriteCip(stream, output);
}
```

---

[1] Stefan Kupferschmid, "Über Craigsche Interpolation und deren Anwendung in der formalen Modellprüfung", 2013, ISBN: 978-3-86247-411-0  
[2] CipCert Repository, URL: https://github.com/TobiasFaller/cipcert  
