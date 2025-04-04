#include "../mtl/Aig.h"

#ifndef MiniCraig_CraigTypes_h
#define MiniCraig_CraigTypes_h

#ifdef CRAIG_INTERPOLATION

namespace MiniCraig {

enum class CraigConstruction : uint8_t {
    NONE = 0,
    SYMMETRIC = 1,
    ASYMMETRIC = 2,
    DUAL_SYMMETRIC = 4,
    DUAL_ASYMMETRIC = 8,
    ALL = 15
};

enum class CraigInterpolant : uint8_t {
    NONE,
    SYMMETRIC,
    ASYMMETRIC,
    DUAL_SYMMETRIC,
    DUAL_ASYMMETRIC,
    INTERSECTION,
    UNION,
    SMALLEST,
    LARGEST
};

CraigConstruction operator|(const CraigConstruction& first, const CraigConstruction& second);

enum class CraigVarType : uint8_t {
    A_LOCAL,
    B_LOCAL,
    GLOBAL
};

enum class CraigClauseType : uint8_t {
    A_CLAUSE,
    B_CLAUSE,
    L_CLAUSE
};

enum class BmcClauseType: uint8_t {
    INIT_CLAUSE,
    TRANS_CLAUSE,
    TARGET_CLAUSE,
    CRAIG_CLAUSE
};

struct CraigData {
    AigEdge partial_interpolant_sym;
    AigEdge partial_interpolant_asym;
    AigEdge partial_interpolant_dual_sym;
    AigEdge partial_interpolant_dual_asym;
    CraigClauseType craig_type;
    size_t craig_id;

    bool isPure() const { return craig_type != CraigClauseType::L_CLAUSE; }
};

}

#endif /* CRAIG_INTERPOLATION */

#endif
