#include "./Aig.h"

#include <map>
#include <set>
#include <stack>

#include "../core/SolverTypes.h"

using namespace MiniCraig;

AigEdge Aig::createVar(const Var& variable) {
    // Try to check if there is a node for the literal already
    if (auto it = varHashMap.find(toInt(variable)); it != varHashMap.end()) {
        return AigEdge(it->second);
    }

    // Nodes 0 and 1 are constant nodes and reserved
    // and already factored into the index.
    nodes.emplace_back(AigNode(variable));
    varHashMap[variable] = (nodes.size() << 1u);
    return AigEdge(nodes.size() << 1u);
}

AigEdge Aig::createLiteral(const Lit& literal) {
    auto edge = createVar(var(literal));
    return sign(literal) ? !edge : edge;
}

AigEdge Aig::createAnd(const AigEdge& edge1, const AigEdge& edge2) {
    if (edge1 == getFalse() || edge2 == getFalse()) return getFalse();
    if (edge1 == getTrue()) return edge2;
    if (edge2 == getTrue()) return edge1;
    if (edge1 == edge2) return edge1;
    if (edge1 == !edge2) return getFalse();

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

AigEdge Aig::createAnd(std::vector<AigEdge> edges) {
    if (edges.empty()) return getTrue();
    if (edges.size() == 1u) return edges[0u];

    // Tree reduction of edges
    std::vector<AigEdge> tempEdges;
    while (edges.size() > 1u) {
        tempEdges.reserve((edges.size() / 2u) + 1u);
        for (size_t index { 0u }; index + 1u < edges.size(); index += 2u)
        {
            tempEdges.emplace_back(createAnd(edges[index], edges[index + 1u]));
        }
        if (edges.size() & 1) tempEdges.emplace_back(edges.back());

        edges = std::move(tempEdges);
        tempEdges.clear();
    }

    return edges[0u];
}

AigEdge Aig::createOr(const AigEdge& edge1, const AigEdge& edge2) {
    return !createAnd(!edge1, !edge2);
}

AigEdge Aig::createOr(std::vector<AigEdge> edges) {
    for (auto& edge : edges) edge = !edge;
    return !createAnd(edges);
}

CraigCnfType Aig::createCnf(const AigEdge& root, vec<vec<Lit>>& cnf, Var& nextFreeIndex) const {
    // The AIG is constant => Handle this simple case.
    if (root.isConstant()) {
        if (root == getFalse()) { cnf.growTo(1); return CraigCnfType::Constant0; }
        return CraigCnfType::Constant1;
    }

    // A fixed single literal => No Tseitin variables are required
    // and we can take a fast path without building an index.
    if (auto node = nodes[root.getNodeIndex()]; node.isVariable()) {
        auto rootLiteral = mkLit(node.getVariable(), root.isNegated());
        cnf.growTo(1); cnf.last().push(rootLiteral);
        return CraigCnfType::Normal;
    }

    // Create index of pre-existing (external) variables.
    // This index is extended with Tseitin variables are required.
    std::map<size_t, Var> nodeToVariable;
    for (size_t nodeIndex { 0u }; nodeIndex < nodes.size(); nodeIndex++) {
        auto const& node = nodes[nodeIndex];
        if (node.isVariable()) nodeToVariable[nodeIndex] = node.getVariable();
    }

#ifdef WITH_OFFSET_REUSE
    Var max_nextFreeIndex = nextFreeIndex;
#endif /* WITH_OFFSET_REUSE */

    std::stack<size_t> pending { { root.getNodeIndex() } };
    while (!pending.empty()) {
        const auto nodeIndex = pending.top();
        const auto& node = nodes[nodeIndex];

        // Check if node was already converted to Tseitin variable.
        if (auto it = nodeToVariable.find(nodeIndex); it != nodeToVariable.end()) {
            pending.pop();
            continue;
        }

        // Both edges have to be processed first.
        const auto& edge1 = node.getEdge1();
        const auto& edge2 = node.getEdge2();
        const size_t node1Index = edge1.getNodeIndex();
        const size_t node2Index = edge2.getNodeIndex();
        if (auto itNode1 = nodeToVariable.find(node1Index); itNode1 == nodeToVariable.end()) {
            pending.push(node1Index);
        } else if (auto itNode2 = nodeToVariable.find(node2Index); itNode2 == nodeToVariable.end()) {
            pending.push(node2Index);
        } else {
            // Edges have been processed, now do Tseiting transformation.
            // This node is guaranteed to not be a variable as they have been inserted
            // into the mapping at the start of this method.
            pending.pop();


#ifdef WITH_OFFSET_REUSE
            const auto tseitinVar = nextFreeIndex + nodeIndex + 1;
            if (max_nextFreeIndex < tseitinVar) {
                max_nextFreeIndex = tseitinVar;
            }
#else
            const auto tseitinVar = nextFreeIndex++;
#endif /* WITH_OFFSET_REUSE */
            nodeToVariable[nodeIndex] = tseitinVar;

            const auto tseitinLit = mkLit(tseitinVar, false);
            const auto litEdge1 = mkLit(itNode1->second, edge1.isNegated());
            const auto litEdge2 = mkLit(itNode2->second, edge2.isNegated());

            //  x = y * z <-> ( !x + y ) * ( !x + z ) * ( x + !y + !z )
            vec<Lit> c1(2); c1[0] = ~tseitinLit; c1[1] =  litEdge1;                    cnf.push(); c1.moveTo(cnf.last());
            vec<Lit> c2(2); c2[0] = ~tseitinLit; c2[1] =  litEdge2;                    cnf.push(); c2.moveTo(cnf.last());
            vec<Lit> c3(3); c3[0] =  tseitinLit; c3[1] = ~litEdge1; c3[2] = ~litEdge2; cnf.push(); c3.moveTo(cnf.last());
        }
    }

#ifdef WITH_OFFSET_REUSE
    nextFreeIndex = max_nextFreeIndex + 1;
#endif

    // Finally add the root literal to the CNF since the required tree
    // now has been built and the root Tseitin variable is accessible.
    auto rootLiteral = mkLit(nodeToVariable[root.getNodeIndex()], root.isNegated());
    cnf.push(); cnf.last().push(rootLiteral);
    return CraigCnfType::Normal;
}

void Aig::toShortOutput(const AigEdge& root, std::ostream& output) const {
    std::stack<size_t> pending { };
    const auto edge_to_string = [&](auto& edge) -> std::string {
        if (edge.isConstant()) {
            if (edge == getFalse()) return "FALSE";
            if (edge == getTrue()) return "TRUE";
        }

        const auto& nodeIndex { edge.getNodeIndex() };
        const auto& node { nodes[nodeIndex] };
        pending.push(nodeIndex);

        if (node.isVariable()) {
            return (edge.isNegated() ? "-" : "") + std::to_string(toInt(node.getVariable()) + 1);
        } else {
            return (edge.isNegated() ? "!n" : "n") + std::to_string(nodeIndex);
        }
    };

    output << edge_to_string(root);

    std::set<size_t> processed;
    while (!pending.empty()) {
        const auto nodeIndex = pending.top();
        const auto& node = nodes[nodeIndex];
        pending.pop();

        if (node.isVariable()) { continue; }

        // Check if node was already visited.
        if (auto it = processed.find(nodeIndex); it != processed.end()) continue;
        processed.insert(nodeIndex);

        output << ", n" << nodeIndex << " = ("
            << edge_to_string(node.getEdge1())
            << " & "
            << edge_to_string(node.getEdge2())
            << ")";
    }
}

void Aig::toFullOutput(const AigEdge& root, std::ostream& output) const {
    if (root.isConstant()) {
        if (root == getFalse()) output << "root FALSE" << std::endl;
        if (root == getTrue()) output << "root TRUE" << std::endl;
        return;
    }

    output << "root " << (root.isNegated() ? "!" : "") << root.getNodeIndex() << std::endl;

    std::stack<size_t> pending { { root.getNodeIndex() } };
    std::set<size_t> processed;
    while (!pending.empty()) {
        const auto nodeIndex = pending.top();
        const auto& node = nodes[nodeIndex];
        pending.pop();

        // Check if node was already visited.
        if (auto it = processed.find(nodeIndex); it != processed.end()) continue;
        processed.insert(nodeIndex);

        if (node.isVariable()) {
            output << nodeIndex << ": var(" << toInt(node.getVariable()) << ")" << std::endl;
        } else {
            const auto& edge1 = node.getEdge1();
            const auto& edge2 = node.getEdge2();
            const auto node1Index = edge1.getNodeIndex();
            const auto node2Index = edge2.getNodeIndex();
            pending.push(node1Index);
            pending.push(node2Index);

            output << nodeIndex << ": and("
                << (edge1.isNegated() ? "!" : "") << node1Index
                << " "
                << (edge2.isNegated() ? "!" : "") << node2Index
                << ")" << std::endl;
        }
    }
}

void Aig::toDumpOutput(std::ostream& output) const {
    for (size_t nodeIndex { 0u }; nodeIndex < nodes.size(); nodeIndex++) {
        auto& node = nodes[nodeIndex];
        if (nodeIndex != 0) output << ",";
        if (node.isVariable()) {
            output << "var " << (toInt(node.getVariable()) + 1);
        } else {
            output << "and " << node.getEdge1().getIndex()
                << " " << node.getEdge2().getIndex();
        }
    }
}
