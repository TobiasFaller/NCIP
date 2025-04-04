#include "kittentracer.hpp"

#include <algorithm>
#include <cassert>
#include <map>
#include <stack>
#include <limits>
#include <iostream>
#include <tuple>
#include <unordered_map>

namespace KittenCraig {

// ----------------------------------------------------------------------------
// Minimal AIG implementation used for building Craig interpolants
// ----------------------------------------------------------------------------

class AigEdge {
public:
  AigEdge() : index(0) {}
  AigEdge(const AigEdge& other) : index(other.index) {}

  AigEdge& operator=(const AigEdge& other) { index = other.index; return *this; }
  AigEdge operator!() const { return AigEdge(index ^ 1); }

  bool operator==(const AigEdge& other) const { return index == other.index; }
  bool operator<(const AigEdge& other) const { return index < other.index; }
  bool operator>(const AigEdge& other) const { return index > other.index; }

  bool is_negated() const { return index & 1; }
  bool is_constant() const { return (index >> 1u) == 0; }

  friend class AigNode;
  friend class Aig;

private:
  explicit AigEdge(int index) : index(index) {}
  size_t get_node_index() const { return (index >> 1u) - 1; }

  int index;
};

class AigNode
{
public:
  bool isAnd() const { return edge2.index != 0; }
  bool isVariable() const { return edge2.index == 0; }

  int get_variable() const { return edge1.index; }
  const AigEdge& get_edge1() const { return edge1; }
  const AigEdge& get_edge2() const { return edge2; }

  friend class Aig;

private:
  explicit AigNode(int _variable) : edge1(_variable), edge2(0) {}
  explicit AigNode(AigEdge _edge1, AigEdge _edge2) : edge1(_edge1), edge2(_edge2) {}

  AigEdge edge1;
  AigEdge edge2;
};


class Aig
{
public:
  Aig(): nodes(), varHashMap(), andHashMap() {}

  static AigEdge get_true() { return AigEdge(0); }
  static AigEdge get_false() { return AigEdge(1); }

  void clear() { nodes.clear(); varHashMap.clear(); andHashMap.clear(); }
  AigEdge create_literal(int literal);
  AigEdge create_and(const AigEdge& edge1, const AigEdge& edge2);
  AigEdge create_or(const AigEdge& edge1, const AigEdge& edge2);
  AigEdge create_and(std::vector<AigEdge> edges);
  AigEdge create_or(std::vector<AigEdge> edges);

  CraigCnfType create_cnf(const AigEdge& root, std::vector<std::vector<int>>& cnf, int& nextFreeIndex) const;

private:
  AigEdge create_var(int variable);

  struct EdgePairHash {
    int operator()(const std::tuple<AigEdge, AigEdge>& edges) const {
      return (std::get<0>(edges).index << 16u) | std::get<1>(edges).index << 0u;
    }
  };
  struct VarHash {
    int operator()(const int& variable) const {
      return variable;
    }
  };

  std::vector<AigNode> nodes;
  std::unordered_map<int, int, VarHash> varHashMap;
  std::unordered_map<std::tuple<AigEdge, AigEdge>, int, EdgePairHash> andHashMap;

};

AigEdge Aig::create_var(int variable) {
  // Try to check if there is a node for the literal already
  if (auto it = varHashMap.find(variable); it != varHashMap.end()) {
    return AigEdge(it->second);
  }

  // Nodes 0 and 1 are constant nodes and reserved
  // and already factored into the index.
  nodes.emplace_back(AigNode(variable));
  varHashMap[variable] = (nodes.size() << 1u);
  return AigEdge(nodes.size() << 1u);
}

AigEdge Aig::create_literal(int literal) {
  auto edge = create_var(abs(literal));
  return (literal < 0) ? !edge : edge;
}

AigEdge Aig::create_and(const AigEdge& edge1, const AigEdge& edge2) {
  if (edge1 == get_false() || edge2 == get_false()) return get_false();
  if (edge1 == get_true()) return edge2;
  if (edge2 == get_true()) return edge1;
  if (edge1 == edge2) return edge1;
  if (edge1 == !edge2) return get_false();

  // Order edge indices to increase hit rate
  auto pair = (edge1 > edge2)
    ? std::make_tuple(edge2, edge1)
    : std::make_tuple(edge1, edge2);
  if (auto it = andHashMap.find(pair); it != andHashMap.end()) {
    return AigEdge(it->second);
  }

  // Lookup failed, create new node.
  // Nodes 0 and 1 are constant nodes and reserved
  // and already factored into the index.
  nodes.emplace_back(AigNode(edge1, edge2));
  andHashMap[pair] = (nodes.size() << 1u);
  return AigEdge(nodes.size() << 1u);
}

AigEdge Aig::create_and(std::vector<AigEdge> edges) {
  if (edges.empty()) return get_true();
  if (edges.size() == 1u) return edges[0u];

  // Tree reduction of edges
  std::vector<AigEdge> tempEdges;
  while (edges.size() > 1u) {
    tempEdges.reserve((edges.size() / 2u) + 1u);
    for (size_t index { 0u }; index + 1u < edges.size(); index += 2u) {
      tempEdges.emplace_back(create_and(edges[index], edges[index + 1u]));
    }
    if (edges.size() & 1) tempEdges.emplace_back(edges.back());

    edges = move(tempEdges);
    tempEdges.clear();
  }

  return edges[0u];
}

AigEdge Aig::create_or(const AigEdge& edge1, const AigEdge& edge2) {
  return !create_and(!edge1, !edge2);
}

AigEdge Aig::create_or(std::vector<AigEdge> edges) {
  for (auto& edge : edges) edge = !edge;
  return !create_and(edges);
}

CraigCnfType Aig::create_cnf(const AigEdge& root, std::vector<std::vector<int>>& cnf, int& nextFreeIndex) const {
  // The AIG is constant => Handle this simple case.
  if (root.is_constant()) {
    if (root == get_false()) { cnf.push_back({}); return CraigCnfType::CONSTANT0; }
    return CraigCnfType::CONSTANT1;
  }

  // A fixed single literal => No Tseitin variables are required
  // and we can take a fast path without building an index.
  if (auto node = nodes[root.get_node_index()]; node.isVariable()) {
    auto rootLiteral = node.get_variable() * (root.is_negated() ? -1 : 1);
    cnf.push_back({ rootLiteral });
    return CraigCnfType::NORMAL;
  }

  // Create index of pre-existing (external) variables.
  // This index is extended with Tseitin variables are required.
  std::map<size_t, int> node_to_var;
  for (size_t nodeIndex { 0u }; nodeIndex < nodes.size(); nodeIndex++) {
    auto const& node = nodes[nodeIndex];
    if (node.isVariable()) node_to_var[nodeIndex] = node.get_variable();
  }

  std::stack<size_t> pending { { root.get_node_index() } };
  while (!pending.empty()) {
    const auto nodeIndex = pending.top();
    const auto& node = nodes[nodeIndex];

    // Check if node was already converted to Tseitin variable.
    if (auto it = node_to_var.find(nodeIndex); it != node_to_var.end()) {
      pending.pop();
      continue;
    }

    // Both edges have to be processed first.
    const auto& edge1 = node.get_edge1();
    const auto& edge2 = node.get_edge2();
    const size_t node1Index = edge1.get_node_index();
    const size_t node2Index = edge2.get_node_index();
    if (auto itNode1 = node_to_var.find(node1Index); itNode1 == node_to_var.end()) {
      pending.push(node1Index);
    } else if (auto itNode2 = node_to_var.find(node2Index); itNode2 == node_to_var.end()) {
      pending.push(node2Index);
    } else {
      // Edges have been processed, now do Tseiting transformation.
      // This node is guaranteed to not be a variable as they have been inserted
      // into the mapping at the start of this method.
      pending.pop();

      const auto tseitinVar = nextFreeIndex++;
      node_to_var[nodeIndex] = tseitinVar;

      const auto litEdge1 = itNode1->second * (edge1.is_negated() ? -1 : 1);
      const auto litEdge2 = itNode2->second * (edge2.is_negated() ? -1 : 1);

      //  x = y * z <-> ( !x + y ) * ( !x + z ) * ( x + !y + !z )
      cnf.push_back({-tseitinVar,  litEdge1});
      cnf.push_back({-tseitinVar,  litEdge2});
      cnf.push_back({ tseitinVar, -litEdge1, -litEdge2});
    }
  }

  // Finally add the root literal to the CNF since the required tree
  // now has been built and the root Tseitin variable is accessible.
  cnf.push_back({node_to_var[root.get_node_index()] * (root.is_negated() ? -1 : 1)});
  return CraigCnfType::NORMAL;
}

std::string to_string(const CraigVarType& var_type) {
  if (var_type == CraigVarType::A_LOCAL) return "A";
  if (var_type == CraigVarType::B_LOCAL) return "B";
  if (var_type == CraigVarType::GLOBAL) return "G";
  __builtin_unreachable();
}

std::ostream& operator<<(std::ostream& out, const CraigVarType& var_type) {
  if (var_type == CraigVarType::A_LOCAL) out << "A";
  if (var_type == CraigVarType::B_LOCAL) out << "B";
  if (var_type == CraigVarType::GLOBAL) out << "G";
  return out;
}

std::string to_string(const CraigClauseType& clause_type) {
  if (clause_type == CraigClauseType::A_CLAUSE) return "A";
  if (clause_type == CraigClauseType::B_CLAUSE) return "B";
  if (clause_type == CraigClauseType::L_CLAUSE) return "L";
  __builtin_unreachable();
}

std::ostream& operator<<(std::ostream& out, const CraigClauseType& clause_type) {
  if (clause_type == CraigClauseType::A_CLAUSE) out << "A";
  if (clause_type == CraigClauseType::B_CLAUSE) out << "B";
  if (clause_type == CraigClauseType::L_CLAUSE) out << "L";
  return out;
}

struct CraigData {
  AigEdge partial_interpolant_sym;
  AigEdge partial_interpolant_asym;
  AigEdge partial_interpolant_dual_sym;
  AigEdge partial_interpolant_dual_asym;
  CraigClauseType clause_type;
  size_t craig_id;

  bool isPure() const { return clause_type != CraigClauseType::L_CLAUSE; }
};

// ----------------------------------------------------------------------------
// Computation of Craig interpolants
// ----------------------------------------------------------------------------

KittenTracer::KittenTracer():
  marked_history(),
  marked_lits(),
  assumptions(),
  craig_clause_last_id(-1),
  craig_var_labels(),
  craig_clause_labels(),
  craig_clauses(),
  craig_interpolants(),
  craig_construction(CraigConstruction::NONE),
  craig_id(0),
  craig_interpolant(0),
  craig_aig_sym(new Aig()),
  craig_aig_asym(new Aig()),
  craig_aig_dual_sym(new Aig()),
  craig_aig_dual_asym(new Aig())
{
}

KittenTracer::~KittenTracer() {
  for (auto& [id, interpolant] : craig_interpolants)
    delete interpolant;
  if (craig_interpolant)
    delete craig_interpolant;

  delete craig_aig_sym;
  delete craig_aig_asym;
  delete craig_aig_dual_sym;
  delete craig_aig_dual_asym;
};

void KittenTracer::set_craig_construction(CraigConstruction craig_construction) {
  assert (craig_clauses.empty());
  this->craig_construction = craig_construction;
}

void KittenTracer::clear_craig_interpolant() {
  craig_interpolant = 0;
}

bool KittenTracer::has_craig_interpolant() {
  return craig_interpolant != 0;
}

void KittenTracer::label_variable(int id, CraigVarType variable_type) {
  assert (id > 0);
  craig_var_labels[id] = variable_type;
  marked_lits[id] = 0;
}

void KittenTracer::label_clause(int id, CraigClauseType clause_type) {
  assert (id > 0);
  craig_clause_labels[id] = clause_type;
}

void KittenTracer::add_assumption (int lit) {
  assumptions.insert(lit);
}

void KittenTracer::reset_assumptions () {
  assumptions.clear();
}

void kitten_trace(void* state, unsigned clause_id, unsigned external_id, bool learned,
                  size_t literalCount, const unsigned *literals,
                  size_t antecedentCount, const unsigned *antecedents) {
  auto *tracer = static_cast<KittenTracer*>(state);
  tracer->craig_clause_last_id = clause_id;

  std::vector<int> c;
  c.reserve(literalCount);
  for (size_t i { 0 }; i < literalCount; i++) {
    int id = (literals[i] / 2) + 1;
    int sign = (literals[i] & 1) ? -1 : 1;
    c.push_back(id * sign);
  }

  std::vector<int> proof_chain;
  proof_chain.reserve(antecedentCount);
  for (size_t i { 0 }; i < antecedentCount; i++) {
    proof_chain.push_back(antecedents[i]);
  }

#if defined(LOGGING)
  std::cerr << "Clause " << clause_id;
  if (!learned) std::cerr << " e" << external_id;
  std::cerr << " (";
  for (size_t i { 0 }; i < literalCount; i++) {
    if (i != 0) std::cerr << " ";
    std::cerr << c[i];
  }
  std::cerr << ")";
  if (learned) {
    std::cerr << " [";
    for (size_t i { 0 }; i < antecedentCount; i++) {
      if (i != 0) std::cerr << " ";
      std::cerr << proof_chain[i];
    }
    std::cerr << "]";
  }
  std::cerr << std::endl;
#endif

  if (learned) {
    assert (proof_chain.size() >= 1);

    for (auto& clause : proof_chain)
      assert (tracer->craig_interpolants.find(clause) != tracer->craig_interpolants.end());

    // Mark literals of conflicting clause.
    for (auto &l : tracer->craig_clauses[proof_chain[0]])
      tracer->mark_literal(l);

    auto *interpolant = new CraigData(*tracer->craig_interpolants[proof_chain[0]]);
    for (int i = 1; i < proof_chain.size(); i++) {
      for (auto &l : tracer->craig_clauses[proof_chain[i]]) {
        // Function mark_literal returns true if inverse literal was marked
        // before and marks literal l for the following resolvent literal
        // checks.
        if (!tracer->mark_literal(l))
          continue;

        tracer->extend_interpolant_with_resolution(
            *interpolant, -l, *tracer->craig_interpolants[proof_chain[i]]);
      }
    }
    tracer->unmark_all();

    tracer->craig_clauses[clause_id] = c;
    tracer->craig_interpolants[clause_id] = interpolant;
  } else {
    assert (tracer->craig_clause_labels.find(external_id) != tracer->craig_clause_labels.end());
    for (auto& l : c) { assert(tracer->craig_var_labels.find(std::abs(l)) != tracer->craig_var_labels.end()); }

    auto clause_label = tracer->craig_clause_labels[external_id];
    auto* interpolant = tracer->create_interpolant_for_clause(c, clause_label);

    tracer->craig_clauses[clause_id] = c;
    tracer->craig_interpolants[clause_id] = interpolant;
  }
}

void KittenTracer::conclude_unsat(kitten* kitten) {
  if (craig_interpolant) {
    delete craig_interpolant;
    craig_interpolant = 0;
  }

  // Failed literals have to be probed before computing the core.
  std::vector<int> failed;
  for (auto assumption : assumptions) {
    auto l = ((std::abs(assumption) - 1) << 1) | (assumption < 0 ? 1 : 0);
    if (kitten_failed(kitten, l)) {
      failed.push_back(assumption);
    }
  }

#if defined(LOGGING)
  std::cerr << "Failed (";
  for (size_t i { 0 }; i < failed.size(); i++) {
    if (i != 0) std::cerr << " ";
    std::cerr << failed[i];
  }
  std::cerr << ")" << std::endl;
#endif

  uint64_t learned = 0;
  uint64_t original = kitten_compute_clausal_core(kitten, &learned);

  assert (kitten_status(kitten) == 21);
  kitten_trace_core(kitten, this, kitten_trace);

  CraigData* interpolant = 0;
  if (learned > 0 || original > 0) {
    // There is a single global conflict, resolve with failing assumptions.
    interpolant = new CraigData(*craig_interpolants[craig_clause_last_id]);

#if defined(LOGGING)
    std::cerr << "Final clause " << craig_clause_last_id << std::endl;
#endif

    for (auto& assumption : failed) {
#if defined(LOGGING)
      std::cerr << "Resolve assumption " << -assumption << std::endl;
#endif
      auto* other = create_interpolant_for_assumption(assumption);
      extend_interpolant_with_resolution(*interpolant, -assumption, *other);
      delete other;
    }
  } else {
    std::cerr << "Special case" << std::endl;
    // Exactly two assumptions are responsible for the conflict.
    interpolant = create_interpolant_for_assumption(-failed[0]);
    auto* other = create_interpolant_for_assumption(-failed[1]);
    extend_interpolant_with_resolution(*interpolant, failed[1], *other);
    delete other;
  }

  craig_interpolant = interpolant;
}

CraigData* KittenTracer::create_interpolant_for_assumption(int literal) {
  assert(craig_var_labels.find(abs(literal)) != craig_var_labels.end());

  CraigVarType varType = craig_var_labels[abs(literal)];
  if (varType == CraigVarType::A_LOCAL) {
    return new CraigData({
      .partial_interpolant_sym = craig_aig_sym->get_false(),
      .partial_interpolant_asym = craig_aig_asym->get_false(),
      .partial_interpolant_dual_sym = craig_aig_dual_sym->get_true(),
      .partial_interpolant_dual_asym = craig_aig_dual_asym->get_false(),
      .clause_type = CraigClauseType::A_CLAUSE,
      .craig_id = craig_id++
    });
  } else if (varType == CraigVarType::B_LOCAL) {
    return new CraigData({
      .partial_interpolant_sym = craig_aig_sym->get_true(),
      .partial_interpolant_asym = craig_aig_asym->get_true(),
      .partial_interpolant_dual_sym = craig_aig_dual_sym->get_false(),
      .partial_interpolant_dual_asym = craig_aig_dual_asym->get_true(),
      .clause_type = CraigClauseType::B_CLAUSE,
      .craig_id = craig_id++
    });
  } else if (varType == CraigVarType::GLOBAL) {
    return new CraigData({
      .partial_interpolant_sym = craig_aig_sym->get_true(),
      .partial_interpolant_asym = craig_aig_asym->get_true(),
      .partial_interpolant_dual_sym = craig_aig_dual_sym->get_false(),
      .partial_interpolant_dual_asym = craig_aig_dual_asym->get_false(),
      .clause_type = CraigClauseType::L_CLAUSE,
      .craig_id = craig_id++
    });
  } else {
    assert(false), "Encountered invalid variable type!";
    __builtin_unreachable();
  }
}

CraigData* KittenTracer::create_interpolant_for_clause(const std::vector<int>& clause, CraigClauseType clause_type) {
  auto result = new CraigData({
    .partial_interpolant_sym = craig_aig_sym->get_true(),
    .partial_interpolant_asym = craig_aig_asym->get_true(),
    .partial_interpolant_dual_sym = craig_aig_dual_sym->get_true(),
    .partial_interpolant_dual_asym = craig_aig_dual_asym->get_true(),
    .clause_type = clause_type,
    .craig_id = craig_id++
  });

  if (is_construction_enabled(CraigConstruction::SYMMETRIC)) {
    if (clause_type == CraigClauseType::A_CLAUSE) {
      result->partial_interpolant_sym = craig_aig_sym->get_false();
    } else if (clause_type == CraigClauseType::B_CLAUSE) {
      result->partial_interpolant_sym = craig_aig_sym->get_true();
    }
  }
  if (is_construction_enabled(CraigConstruction::ASYMMETRIC)) {
    if (clause_type == CraigClauseType::A_CLAUSE) {
      std::vector<AigEdge> literals;
      for (size_t i = 0; i < clause.size(); ++i) {
        if (craig_var_labels[abs(clause[i])] == CraigVarType::GLOBAL) {
          literals.push_back(craig_aig_asym->create_literal(clause[i]));
        }
      }
      result->partial_interpolant_asym = craig_aig_asym->create_or(literals);
    } else if (clause_type == CraigClauseType::B_CLAUSE) {
      result->partial_interpolant_asym = craig_aig_asym->get_true();
    }
  }
  if (is_construction_enabled(CraigConstruction::DUAL_SYMMETRIC)) {
    if (clause_type == CraigClauseType::A_CLAUSE) {
      result->partial_interpolant_dual_sym = craig_aig_dual_sym->get_true();
    } else if (clause_type == CraigClauseType::B_CLAUSE) {
      result->partial_interpolant_dual_sym = craig_aig_dual_sym->get_false();
    }
  }
  if (is_construction_enabled(CraigConstruction::DUAL_ASYMMETRIC)) {
    if (clause_type == CraigClauseType::A_CLAUSE) {
      result->partial_interpolant_dual_asym = craig_aig_dual_asym->get_false();
    } else if (clause_type == CraigClauseType::B_CLAUSE) {
      std::vector<AigEdge> literals;
      for (size_t i = 0; i < clause.size(); ++i) {
        if (craig_var_labels[abs(clause[i])] == CraigVarType::GLOBAL) {
          literals.push_back(craig_aig_dual_asym->create_literal(-clause[i]));
        }
      }
      result->partial_interpolant_dual_asym = craig_aig_dual_asym->create_and(literals);
    }
  }

  return result;
}

void KittenTracer::extend_interpolant_with_resolution(CraigData& result, int literal, const CraigData& craig_data) {
  if (result.clause_type != craig_data.clause_type) {
    result.clause_type = CraigClauseType::L_CLAUSE;
  }

  if (is_construction_enabled(CraigConstruction::SYMMETRIC)) {
    if (craig_var_labels[abs(literal)] == CraigVarType::A_LOCAL) {
      result.partial_interpolant_sym = craig_aig_sym->create_or(
        result.partial_interpolant_sym,
        craig_data.partial_interpolant_sym
      );
    } else if (craig_var_labels[abs(literal)] == CraigVarType::B_LOCAL) {
      result.partial_interpolant_sym = craig_aig_sym->create_and(
        result.partial_interpolant_sym,
        craig_data.partial_interpolant_sym
      );
    } else {
      result.partial_interpolant_sym = craig_aig_sym->create_and(
        craig_aig_sym->create_or(result.partial_interpolant_sym,     craig_aig_sym->create_literal( literal)),
        craig_aig_sym->create_or(craig_data.partial_interpolant_sym, craig_aig_sym->create_literal(-literal))
      );
    }
  }
  if (is_construction_enabled(CraigConstruction::ASYMMETRIC)) {
    if (craig_var_labels[abs(literal)] == CraigVarType::A_LOCAL) {
      result.partial_interpolant_asym = craig_aig_asym->create_or(
        result.partial_interpolant_asym,
        craig_data.partial_interpolant_asym
      );
    } else {
      result.partial_interpolant_asym = craig_aig_asym->create_and(
        result.partial_interpolant_asym,
        craig_data.partial_interpolant_asym
      );
    }
  }
  if (is_construction_enabled(CraigConstruction::DUAL_SYMMETRIC)) {
    if (craig_var_labels[abs(literal)] == CraigVarType::A_LOCAL) {
      result.partial_interpolant_dual_sym = craig_aig_dual_sym->create_and(
        result.partial_interpolant_dual_sym,
        craig_data.partial_interpolant_dual_sym
      );
    } else if (craig_var_labels[abs(literal)] == CraigVarType::B_LOCAL) {
      result.partial_interpolant_dual_sym = craig_aig_dual_sym->create_or(
        result.partial_interpolant_dual_sym,
        craig_data.partial_interpolant_dual_sym
      );
    } else {
      result.partial_interpolant_dual_sym = craig_aig_dual_sym->create_or(
        craig_aig_dual_sym->create_and(result.partial_interpolant_dual_sym,     craig_aig_dual_sym->create_literal(-literal)),
        craig_aig_dual_sym->create_and(craig_data.partial_interpolant_dual_sym, craig_aig_dual_sym->create_literal( literal))
      );
    }
  }
  if (is_construction_enabled(CraigConstruction::DUAL_ASYMMETRIC)) {
    if (craig_var_labels[abs(literal)] == CraigVarType::B_LOCAL) {
      result.partial_interpolant_dual_asym = craig_aig_dual_asym->create_and(
        result.partial_interpolant_dual_asym,
        craig_data.partial_interpolant_dual_asym
      );
    } else {
      result.partial_interpolant_dual_asym = craig_aig_dual_asym->create_or(
        result.partial_interpolant_dual_asym,
        craig_data.partial_interpolant_dual_asym
      );
    }
  }
}

CraigCnfType KittenTracer::create_craig_interpolant(CraigInterpolant interpolant, std::vector<std::vector<int>>& cnf, int& nextFreeVariable) {
  cnf.clear();

  if (!has_craig_interpolant()) {
    return CraigCnfType::NONE;
  }

  bool build_cnf_sym = false;
  bool build_cnf_asym = false;
  bool build_cnf_dual_sym = false;
  bool build_cnf_dual_asym = false;
  switch (interpolant) {
    case CraigInterpolant::NONE:
      break;
    case CraigInterpolant::SYMMETRIC:
      build_cnf_sym = is_construction_enabled(CraigConstruction::SYMMETRIC);
      break;
    case CraigInterpolant::ASYMMETRIC:
      build_cnf_asym = is_construction_enabled(CraigConstruction::ASYMMETRIC);
      break;
    case CraigInterpolant::DUAL_SYMMETRIC:
      build_cnf_dual_sym = is_construction_enabled(CraigConstruction::DUAL_SYMMETRIC);
      break;
    case CraigInterpolant::DUAL_ASYMMETRIC:
      build_cnf_dual_asym = is_construction_enabled(CraigConstruction::DUAL_ASYMMETRIC);
      break;
    case CraigInterpolant::INTERSECTION:
    case CraigInterpolant::UNION:
    case CraigInterpolant::SMALLEST:
    case CraigInterpolant::LARGEST:
      build_cnf_sym = is_construction_enabled(CraigConstruction::SYMMETRIC);
      build_cnf_asym = is_construction_enabled(CraigConstruction::ASYMMETRIC);
      build_cnf_dual_sym = is_construction_enabled(CraigConstruction::DUAL_SYMMETRIC);
      build_cnf_dual_asym = is_construction_enabled(CraigConstruction::DUAL_ASYMMETRIC);
      break;

    default:
      assert(false), "Seleted craig interpolation type not supported!";
      __builtin_unreachable();
  }

  std::vector<std::vector<int>> craig_cnf_sym;
  std::vector<std::vector<int>> craig_cnf_asym;
  std::vector<std::vector<int>> craig_cnf_dual_sym;
  std::vector<std::vector<int>> craig_cnf_dual_asym;
  CraigCnfType craig_cnf_type_sym = CraigCnfType::NONE;
  CraigCnfType craig_cnf_type_asym = CraigCnfType::NONE;
  CraigCnfType craig_cnf_type_dual_sym = CraigCnfType::NONE;
  CraigCnfType craig_cnf_type_dual_asym = CraigCnfType::NONE;

  if (build_cnf_sym)
    craig_cnf_type_sym = craig_aig_sym->create_cnf(craig_interpolant->partial_interpolant_sym, craig_cnf_sym, nextFreeVariable);
  if (build_cnf_asym)
    craig_cnf_type_asym = craig_aig_asym->create_cnf(craig_interpolant->partial_interpolant_asym, craig_cnf_asym, nextFreeVariable);
  if (build_cnf_dual_sym)
    craig_cnf_type_dual_sym = craig_aig_dual_sym->create_cnf(craig_interpolant->partial_interpolant_dual_sym, craig_cnf_dual_sym, nextFreeVariable);
  if (build_cnf_dual_asym)
    craig_cnf_type_dual_asym = craig_aig_dual_asym->create_cnf(craig_interpolant->partial_interpolant_dual_asym, craig_cnf_dual_asym, nextFreeVariable);

  // Dual Craig interpolants have to be inverted.
  // However, the construction rules for the dual asymmetric interpolant already incorporates the negation.
  // So only the dual symmetric interpolant needs to be negated.
  if (craig_cnf_type_dual_sym == CraigCnfType::CONSTANT1) {
    craig_cnf_dual_sym = {{}};
    craig_cnf_type_dual_sym = CraigCnfType::CONSTANT0;
  } else if (craig_cnf_type_dual_sym == CraigCnfType::CONSTANT0) {
    craig_cnf_dual_sym = {};
    craig_cnf_type_dual_sym = CraigCnfType::CONSTANT1;
  } else if (craig_cnf_type_dual_sym == CraigCnfType::NORMAL) {
    craig_cnf_dual_sym.back()[0] = -craig_cnf_dual_sym.back()[0];
  }

  if (interpolant == CraigInterpolant::NONE) {
    cnf = {};
    return CraigCnfType::NONE;
  } else if (interpolant == CraigInterpolant::SYMMETRIC) {
    cnf = std::move(craig_cnf_sym);
    return craig_cnf_type_sym;
  } else if (interpolant == CraigInterpolant::ASYMMETRIC) {
    cnf = std::move(craig_cnf_asym);
    return craig_cnf_type_asym;
  } else if (interpolant == CraigInterpolant::DUAL_SYMMETRIC) {
    cnf = std::move(craig_cnf_dual_sym);
    return craig_cnf_type_dual_sym;
  } else if (interpolant == CraigInterpolant::DUAL_ASYMMETRIC) {
    cnf = std::move(craig_cnf_dual_asym);
    return craig_cnf_type_dual_asym;
  }

  std::vector<std::tuple<std::vector<std::vector<int>>*, CraigCnfType>> craig_cnfs { };
  if (craig_cnf_type_sym != CraigCnfType::NONE) craig_cnfs.push_back({ &craig_cnf_sym, craig_cnf_type_sym });
  if (craig_cnf_type_asym != CraigCnfType::NONE) craig_cnfs.push_back({ &craig_cnf_asym, craig_cnf_type_asym });
  if (craig_cnf_type_dual_sym != CraigCnfType::NONE) craig_cnfs.push_back({ &craig_cnf_dual_sym, craig_cnf_type_dual_sym });
  if (craig_cnf_type_dual_asym != CraigCnfType::NONE) craig_cnfs.push_back({ &craig_cnf_dual_asym, craig_cnf_type_dual_asym });

  if (craig_cnfs.size() == 0) {
    return CraigCnfType::NONE;
  } else if (craig_cnfs.size() == 1) {
    cnf = std::move(*std::get<0>(craig_cnfs[0]));
    return std::get<1>(craig_cnfs[0]);
  }

  // We have at least two Craig interpolants for the following computations.
  if (interpolant == CraigInterpolant::UNION) {
    bool allConstantOne = true;
    for (auto [craigCnf, craigCnfType] : craig_cnfs) {
      if (craigCnfType == CraigCnfType::CONSTANT0) {
        cnf = std::move(*craigCnf);
        return CraigCnfType::CONSTANT0;
      }
      allConstantOne &= (craigCnfType == CraigCnfType::CONSTANT1);
    }
    if (allConstantOne) {
      cnf = {};
      return CraigCnfType::CONSTANT1;
    }

    for (auto [craigCnf, craigCnfType] : craig_cnfs) {
      if (craigCnfType == CraigCnfType::NORMAL) {
        size_t i = 0, j = cnf.size(); cnf.resize(cnf.size() + craigCnf->size() - 1u);
        for (; i < craigCnf->size() - 1u; i++, j++) cnf[j] = move((*craigCnf)[i]);
      }
    }

    // Create trigger (t) that enforces all CNF parts.
    int craig_trigger = nextFreeVariable++;
    std::vector<int> craig_trigger_clause { craig_trigger };
    for (auto [craigCnf, craigCnfType] : craig_cnfs) {
      if (craigCnfType == CraigCnfType::NORMAL) {
        // The positive trigger implies that all CNF parts are enabled: (t -> t_1) = (-t v t_1)
        cnf.push_back(std::vector<int> { -craig_trigger, craigCnf->back()[0] });
        // The negative trigger implies that at least one of the CNF parts is not enabled: (-t -> (-t_1 v ... v -t_n)) = (t v -t_1 v ... -t_n)
        craig_trigger_clause.push_back(-(craigCnf->back()[0]));
      }
    }
    cnf.push_back(craig_trigger_clause);
    cnf.push_back({ craig_trigger });

    return CraigCnfType::NORMAL;
  } else if (interpolant == CraigInterpolant::INTERSECTION) {
    bool allConstantZero = true;
    for (auto [craigCnf, craigCnfType] : craig_cnfs) {
      if (craigCnfType == CraigCnfType::CONSTANT1) {
        cnf = std::move(*craigCnf);
        return CraigCnfType::CONSTANT1;
      }
      allConstantZero &= (craigCnfType == CraigCnfType::CONSTANT0);
    }
    if (allConstantZero) {
      cnf = {{}};
      return CraigCnfType::CONSTANT0;
    }

    for (auto [craigCnf, craigCnfType] : craig_cnfs) {
      if (craigCnfType == CraigCnfType::NORMAL) {
        size_t i = 0, j = cnf.size(); cnf.resize(cnf.size() + craigCnf->size() - 1);
        for (; i < craigCnf->size() - 1u; i++, j++) cnf[j] = std::move((*craigCnf)[i]);
      }
    }

    // Create trigger (t) that enforces all CNF parts.
    int craig_trigger = nextFreeVariable++;
    std::vector<int> craig_trigger_clause { -craig_trigger };
    for (auto [craigCnf, craigCnfType] : craig_cnfs) {
      if (craigCnfType == CraigCnfType::NORMAL) {
        // The positive trigger implies that one of the CNF parts is enabled: (t -> (t_1 v ... v t_n)) = (-t v t_1 v ... t_n)
        craig_trigger_clause.push_back(craigCnf->back()[0]);
        // The negative trigger implies that at all CNF parts are not enabled: (-t -> -t_1) = (t v -t_1)
        cnf.push_back(std::vector<int> {craig_trigger, -(craigCnf->back()[0])});
      }
    }
    cnf.push_back(craig_trigger_clause);
    cnf.push_back({ craig_trigger });

    return CraigCnfType::NORMAL;
  } else if (interpolant == CraigInterpolant::SMALLEST) {
    auto minimum = std::min_element(craig_cnfs.begin(), craig_cnfs.end(), [](auto const& elem1, auto const& elem2) {
      auto const& [elem1Cnf, elem1CnfType] = elem1;
      auto const& [elem2Cnf, elem2CnfType] = elem2;
      return (elem1Cnf->size() < elem2Cnf->size());
    });
    auto [minCnf, minCnfType] = *minimum;
    cnf = std::move(*minCnf);
    return minCnfType;
  } else if (interpolant == CraigInterpolant::LARGEST) {
    auto maximum = std::max_element(craig_cnfs.begin(), craig_cnfs.end(), [](auto const& elem1, auto const& elem2) {
      auto const& [elem1Cnf, elem1CnfType] = elem1;
      auto const& [elem2Cnf, elem2CnfType] = elem2;
      return (elem1Cnf->size() < elem2Cnf->size());
    });
    auto [maxCnf, maxCnfType] = *maximum;
    cnf = std::move(*maxCnf);
    return maxCnfType;
  } else {
    assert(false), "Seleted craig interpolation type not supported!";
    __builtin_unreachable();
  }
}

bool KittenTracer::is_construction_enabled(CraigConstruction construction) {
  return static_cast<uint8_t>(construction) & static_cast<uint8_t>(craig_construction);
}

uint8_t KittenTracer::mark_literal(int literal) {
  int index = std::abs(literal);
  uint8_t mask = (literal < 0) ? 2 : 1;

  uint8_t was_marked = marked_lits[index];
  if (!was_marked) marked_history.push_back(index);
  if (!(was_marked & mask)) marked_lits[index] |= mask;
  return was_marked & ~mask;
}

uint8_t KittenTracer::is_marked(int literal) {
  int index = std::abs(literal);
  uint8_t mask = (literal < 0) ? 2 : 1;
  return marked_lits[index] & mask;
}

void KittenTracer::unmark_all() {
  for (auto& index : marked_history) {
    marked_lits[index] = 0;
  }
  marked_history.clear();
}

} // namespace KittenCraig
