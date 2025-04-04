#ifndef MiniCraig_Aig_h
#define MiniCraig_Aig_h

#include <cassert>
#include <iostream>
#include <limits>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "../mtl/Vec.h"

namespace MiniCraig {

typedef int Var;
struct Lit;

enum class CraigCnfType {
    None,
    Constant0,
    Constant1,
    Normal
};

inline std::string to_string(const CraigCnfType& type) {
    switch (type) {
        case CraigCnfType::None: return "None";
        case CraigCnfType::Constant0: return "Constant0";
        case CraigCnfType::Constant1: return "Constant1";
        case CraigCnfType::Normal: return "Normal";
        default: __builtin_unreachable();
    }
}

class AigEdge {
public:
    AigEdge() : index(0u) {}
    AigEdge(const AigEdge& other) : index(other.index) {}

    AigEdge& operator=(const AigEdge& other) { index = other.index; return *this; }
    AigEdge operator!() const { return AigEdge(index ^ 1u); }

    bool operator==(const AigEdge& other) const { return index == other.index; }
    bool operator<(const AigEdge& other) const { return index < other.index; }
    bool operator>(const AigEdge& other) const { return index > other.index; }

    bool isNegated() const { return index & 1u; }
    bool isConstant() const { return (index >> 1u) == 0u; }

    size_t getIndex() const { return index; }

    friend class AigNode;
    friend class Aig;

private:
    explicit AigEdge(size_t index) : index(index) {}
    size_t getNodeIndex() const { return (index >> 1u) - 1u; }

    size_t index;
};

class AigNode
{
public:
    bool isAnd() const { return edge2.index != 0u; }
    bool isVariable() const { return edge2.index == 0u; }

    Var getVariable() const { return Var(edge1.index); }
    const AigEdge& getEdge1() const { return edge1; }
    const AigEdge& getEdge2() const { return edge2; }

    friend class Aig;

private:
    explicit AigNode(Var _variable) : edge1(_variable), edge2(0u) {}
    explicit AigNode(AigEdge _edge1, AigEdge _edge2) : edge1(_edge1), edge2(_edge2) {}

    AigEdge edge1;
    AigEdge edge2;
};


class Aig
{
public:
    Aig(): nodes(), varHashMap(), andHashMap() {}

    static AigEdge getTrue() { return AigEdge(0u); }
    static AigEdge getFalse() { return AigEdge(1u); }

    void clear() { nodes.clear(); varHashMap.clear(); andHashMap.clear(); }
    AigEdge createLiteral(const Lit& literal);
    AigEdge createAnd(const AigEdge& edge1, const AigEdge& edge2);
    AigEdge createOr(const AigEdge& edge1, const AigEdge& edge2);
    AigEdge createAnd(std::vector<AigEdge> edges);
    AigEdge createOr(std::vector<AigEdge> edges);

    CraigCnfType createCnf(const AigEdge& root, vec<vec<Lit>>& cnf, Var& nextFreeIndex) const;
    void toShortOutput(const AigEdge& root, std::ostream& os = std::cout) const;
    void toFullOutput(const AigEdge& root, std::ostream& os = std::cout) const;
    void toDumpOutput(std::ostream& os = std::cout) const;

private:
    AigEdge createVar(const Var& variable);

    struct EdgePairHash {
        size_t operator()(const std::tuple<AigEdge, AigEdge>& edges) const {
            return (std::get<0>(edges).index << 16u) | std::get<1>(edges).index << 0u;
        }
    };
    struct VarHash {
        size_t operator()(const Var& variable) const {
            return variable;
        }
    };

    std::vector<AigNode> nodes;
    std::unordered_map<Var, size_t, VarHash> varHashMap;
    std::unordered_map<std::tuple<AigEdge, AigEdge>, size_t, EdgePairHash> andHashMap;

};

}

#endif
