# Aiger

## Aiger file format

This prover supports Aiger problems with safety properties that are specified through either:

- An Aiger input format with one or multiple `bad states` (Aiger 1.9 type)
- An Aiger input format with one or multiple `outputs` (before Aiger 1.9 type)

If both are specified then the `bad states` are interpreted as properties and the `outputs` are treated as normal circuit outputs.

The format is documented by [Armin Biere et al.](https://fmv.jku.at/papers/) [1].
Both, the ASCII format and the binary format is supported by NCIP.
However, the certificate is always written by NCIP in ASCII format for simplicity.

Below is a short summary of the documentation for the ASCII Aiger 1.9 format.
The concept is as follows:

```text
aag <M> <I> <L> <O> <A> <B>
<i>
<i>
<l> <n> <r>
<l> <n> <r>
<o>
<o>
<a> <l> <r>
<a> <l> <r>
<a> <l> <r>
<b>
<b>
```

Example from the Aiger 1.9 specification [1]:

```text
aag 5 1 1 0 3 1
2
4 10 0
4
6 5 3
8 4 2
10 9 7
```

Literals are encoded in even-odd format:

- Literal 0 = variable 0 positive (always false)
- Literal 1 = variable 0 negative (always true)
- Literal 2 = variable 1 positive
- Literal 3 = variable 1 negative
- Literal 4 = variable 2 positive

The format starts with a header (`aag <M> <I> <L> <O> <A> <B> <C>`) as first line.
It contains the following:

- `<M>`: The maximum literal number that is used
- `<I>`: The number of inputs
- `<L>`: The number of latches (state)
- `<O>`: The number of outputs
- `<A>`: The number of and-gates
- `<B>`: The number of bad state properties
- `<C>`: The number of constraints

Then follows a list with all the contained elements per line.
First are the inputs (`<i>`) per each line:
- `<i>`: The literal number of the input (always even)
Then the latches (`<l> <n> <r>`) per each line:
- `<l>`: The literal number of the latch (always even)
- `<n>`: The next state of the latch
- `<r>`: The reset state of the latch
Then the outputs (`<o>`) per each line:
- `<o>`: The literal representing the output value
Then the and-gates (`<a> <l> <r>`) per each line:
- `<a>`: The literal number of the and-gate (always even)
- `<l>`: The left edge literal of the gate
- `<r>`: The right edge literal of the gate
Then the bad state properties (`<b>`) per each line:
- `<b>`: The literal representing the output value
Finally the constraint (`<c>`) per each line:
- `<c>`: The literal representing the constraint that has to be held

For the following chapters these short-hands for the input parts are used:

- `A := ⋀ (<a> ⇔ <l> ∧ <r>) ∀ a, l, r ∈ and-gates`: The Tseitin encoding for all and-gates
- `R := A ∧ ⋀ (<l> ⇔ <r>) ∀ l, n, r ∈ latches`: The reset function of the Aiger file
- `C := ⋀ (<c> ⇔ 1) ∀ c ∈ constraints`: The constraints of the Aiger file
- `P := ⋁ (<p> ⇔ 0) ∀ p ∈ safety properties`: The safety property of the Aiger file
- `F := A ∧ ⋀ (<l>' ⇔ <n>) ∀ l, n, r ∈ latches`: The transition function of the Aiger file

## Aiger Models

The model written by NCIP is described in Aiger 1.9 [1] as witness.
The concept is as follows:

```text
1
b<bad>b<bad>
<reset><reset>
<input><input>
<input><input>
.
```

Below is an example file:

```text
1
b0b1
0000
00
01
10
11
.
```

The format starts with the result as first line:
- `0`: The properties are safe (UNSAT)
- `1`: The properties are unsafe (SAT)
- `2`: No conclusion for the properties (UNKNOWN)

Then follows a list of the properties referenced by the status:
- `<bad>` The `bad state` / `output` number that represents the property, starting with 0

Then follows a list of the initial states of all flip-flops with symbols `0`, `1`, `x`.
A list of the inputs with symbols `0`, `1`, `x` follows, each one line per state.
Finally, the format is terminated with a fullstop.

## Aiger Certificates

NCIP encodes the fixed point as property into the Aiger certificate.
E.g.: `P = R ∨ C_1 ∨ ... ∨ C_n` where `R` is the allowed Aiger reset states and `C_1` to `C_n` are the Craig interpolants of each step.
As the property is stored as output it is inverted e.g. `O = !P` before writing.
The certificate can be checked with [Certifaiger](https://github.com/Froleyks/certifaiger) [2-6].

Certificate checking is done in the following way:
Let `M := (R, C, P, F, S)` be the original input problem and `M' := (R', C', P', F', S')` be the certificate.

- `R`, `R'`: The reset function of the Aiger input / certificate
- `C`, `C'`: The constraints of the Aiger input / certificate
- `P`, `P'`: The safety property of the Aiger input / certificate
- `F`, `F'`: The transition function of the Aiger input / certificate
- `S`, `S'`: The latches (state) of the Aiger input / certificate

The following SAT-checks prove the certificate is valid.
We assume a stratified reset definition (the dependency graph between resets is acyclic):

- Reset: `R ∧ C ⇒ R' ∧ C'`
- Transition: `F ∧ C0 ∧ C1 ∧ C0' ⇒ F' ∧ C1'`
- Property: `(C ∧ C') ⇒ (P ⇒ P')`
- Base: `R' ∧ C' ⇒ P'`
- Step: `P0' ∧ F' ∧ C0' ∧ C1' ⇒ P1'`

In case the reset definition is not stratified, a QBF-check is required:

- Reset: `∀S ∃(S'\S). R ∧ C ⇒ (R' ∧ C')`
- Transition: `F ∧ C0 ∧ C1 ∧ C0' ⇒ F' ∧ C1'`
- Property: `(C ∧ C') ⇒ (P ⇒ P')`
- Base: `R' ∧ C' ⇒ P'`
- Step: `P0' ∧ F' ∧ C0' ∧ C1' ⇒ P1'`

## Aiger Encoding

The Aiger problem is internally converted into a CNF problem via Tseitin transformation.
Only parts in the cone-of-influence for latches, outputs, bad states and constraints are encoded.
The three parts of the CNF problem are encoded as follows:

- `Init := R & C`
- `Trans := F & C`
- `Target := !P & C`

## Aiger API

The format can be passed to the NCIP C++ API via the `AigProblemBuilder` and `AigProblem` classes.
The Aiger problem is contained in a library-independent format and can be used without linking against the Aiger library.
The builder is provided to transform the input format into the NCIP's native problem specification.
For ease-of-use and simple handling of literals, the `-` (invert literal), `^` (xor literal with boolean) and `>>` (shift literal timeframe) operators are overloaded.
Below is an example for the usage of the Aiger API constructing the format on-the-fly, combined with a prover portfolio approach to run two NCIP instances in parallel.

```cpp
#include <iostream>

#include <bmc-format-aig.hpp>
#include <bmc-ncip.hpp>
#include <bmc-ncip-portfolio.hpp>

using namespace Ncip;

int main() {
    // Build the Aiger-Problem on-the-fly and convert to native format.
    AigProblemBuilder builder { };
    auto latchLit = 2;
    auto latchLitReset = 0;
    auto latchLitBad = 3;
    builder.AddLatch(latchLit, latchLit, latchReset);
    builder.AddBad(latchBad);
    auto [aigProblem, bmcProblem] = builder.Build();

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
            std::cout << "Model " << to_string(timeframe[latchLit / 2]) << std::endl;
        }
        return 10;

    case BmcStatus::Unsat:
        std::cout << "Unsat" << std::endl;
        auto certificate { AigCertificateBuilder { }
            .Build(aigProblem, result.GetCertificate()) };
        std::cout << "Bad " << certificate.GetBad() << std::endl;
        return 20;

    default:
        std::cout << "Limit Reached" << std::endl;
        return 30;

    }
}
```

A typical Aiger import routine would use the Aiger library copy the Aiger structure into the library-independend format like shown below.

```cpp
static int aig_generic_read(std::istream* stream) {
  return stream->get();
}

static std::tuple<AigProblem, BmcProblem> ReadAiger(std::istream& stream) {
  auto graph = aiger_init();
  if (auto error = aiger_read_generic(graph, &stream,
      reinterpret_cast<aiger_get>(aig_generic_read)); error != nullptr) {
    throw "Aiger read error";
  }
  if (auto error = aiger_check(graph); error != nullptr) {
    throw "Invalid Aiger structure";
  }

  AigProblemBuilder builder;
  for (ssize_t variable { 0 }; variable <= graph->maxvar; variable++) {
    auto literal = variable * 2;
    if (aiger_is_constant(literal)) {
      continue; // Constant is already created by the builder
    } else if (auto symbol = aiger_is_input(graph, literal); symbol != nullptr) {
      builder.AddInput(symbol->lit);
    } else if (auto symbol = aiger_is_latch(graph, literal); symbol != nullptr) {
      builder.AddLatch(symbol->lit, symbol->next, symbol->reset);
    } else if (auto symbol = aiger_is_and(graph, literal); symbol != nullptr) {
      builder.AddAnd(symbol->lhs, symbol->rhs0, symbol->rhs1);
    }
  }
  for (ssize_t index { 0 }; index < graph->num_outputs; index++) {
    builder.AddOutput(graph->outputs[index].lit);
  }
  for (ssize_t index { 0 }; index < graph->num_bad; index++) {
    builder.AddBad(graph->bad[index].lit);
  }
  for (ssize_t index { 0 }; index < graph->num_constraints; index++) {
    builder.AddConstraint(graph->constraints[index].lit);
  }
  aiger_reset(graph);

  try {
    return builder.Build();
  } catch (const AigProblemException& error) {
    throw "Invalid problem";
  }
}
```

The procedure of writing is exactly the opposite to reading the Aiger format and consists of converting the library-independend data structure back to the Aiger library.

```cpp
static int aig_generic_write(char c, std::ostream* stream) {
  return (stream->put(c).fail() ? EOF : 0);
}

static void WriteAiger(std::ostream& stream, const AigProblem& problem) {
  auto graph = aiger_init();
  for (auto& node : problem.GetNodes()) {
    if (node.type == AigNodeType::Input) {
      aiger_add_input(graph, node.nodeId, nullptr);
    } else if (node.type == AigNodeType::Latch) {
      aiger_add_latch(graph, node.nodeId, node.leftEdgeId, nullptr);
      aiger_add_reset(graph, node.nodeId, node.rightEdgeId);
    } else if (node.type == AigNodeType::And) {
      aiger_add_and(graph, node.nodeId, node.leftEdgeId, node.rightEdgeId);
    }
  }
  for (auto& output : problem.GetOutputs()) {
    aiger_add_output(graph, output, nullptr);
  }
  for (auto& bad : problem.GetBads()) {
    aiger_add_bad(graph, bad, nullptr);
  }
  for (auto& constraint : problem.GetConstraints()) {
    aiger_add_constraint(graph, constraint, nullptr);
  }

  if (auto error = aiger_check(graph); error != nullptr) {
    throw "Invalid Aiger structure";
  }
  if (!aiger_write_generic(graph, aiger_mode::aiger_ascii_mode, &stream,
      reinterpret_cast<aiger_put>(aig_generic_write))) {
    throw "Aiger write error";
  }

  aiger_reset(graph);
}
```

A certificate for the Aiger format can be generated via the `AigCertificateBuilder` and `AigCertificate` classes.
The certificate can be written like the library-independent Aiger format.

```cpp
static void WriteAigerCertificate(std::ostream& stream, const AigProblem& problem, const BmcCertificate& certificate) {
  AigCertificateBuilder builder;
  AigCertificate output = builder.Build(problem, certificate);
  WriteAiger(stream, output);
}
```

---

[1] Armin Biere, Keijo Heljanko, and Siert Wieringa, "AIGER 1.9 And Beyond", URL: https://fmv.jku.at/papers/BiereHeljankoWieringa-FMV-TR-11-2.pdf, 2011  
[2] Certifaiger Repository, URL: https://github.com/Froleyks/certifaiger  
[3] Yu, Biere & Heljanko, "Progress in Certifying Hardware Model Checking Results", CAV21  
[4] Yu, Froleyks & Biere et al., "Stratified Certification for K-Induction", FMCAD22  
[5] Yu, Froleyks & Biere et al., "Towards Compositional Hardware Model Checking Certification", FMCAD23  
[6] Froleyks, Yu & Biere et al., "Certifying Phase Abstraction", IJCAR24  
