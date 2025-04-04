// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include "bmc-ncip-impl.hpp"

#include <unordered_map>

using namespace Ncip::Impl;

namespace Ncip {
namespace Backend {

enum class BackendVariableType {
	NORMAL,
	GLOBAL,
	A_LOCAL,
	B_LOCAL,
	A_PROTECTED,
	B_PROTECTED
};
enum class BackendClauseType {
	NORMAL,
	A_CLAUSE,
	B_CLAUSE
};

inline std::string to_string(const BackendVariableType& type) {
	if (type == BackendVariableType::NORMAL) return "NORMAL";
	if (type == BackendVariableType::GLOBAL) return "GLOBAL";
	if (type == BackendVariableType::A_LOCAL) return "A_LOCAL";
	if (type == BackendVariableType::B_LOCAL) return "B_LOCAL";
	if (type == BackendVariableType::A_PROTECTED) return "A_PROTECTED";
	if (type == BackendVariableType::B_PROTECTED) return "B_PROTECTED";
	__builtin_unreachable();
}

inline std::string to_string(const BackendClauseType& type) {
	if (type == BackendClauseType::NORMAL) return "NORMAL";
	if (type == BackendClauseType::A_CLAUSE) return "A_CLAUSE";
	if (type == BackendClauseType::B_CLAUSE) return "B_CLAUSE";
	__builtin_unreachable();
}

template<typename ImplTag, typename SolverTag> class SolverInterface;
template<typename ImplTag, typename SolverTag> class SolverVariableMapInterface;

// This is a helper structure to store the mapping to and from the used underlying solver.
// Relations are stored as variable IDs and are negated as required to return an equivalent
// literal from the SAT solver / NCIP.
// On the NCIP side variables have a timeframe that distinguishes different shifted versions
// of a single variable. Example mapping:
// NCIP    SAT
// 0:0 <-> 0
// 1:0 <-> 1
// 0:1 <-> 2   (same BMC variables but shifted by 1 timeframe)
// 1:1 <-> 3   (same BMC variables but shifted by 1 timeframe)
template<typename ImplTag, typename SolverTag>
class SolverVariableMap: public SolverVariableMapInterface<ImplTag, SolverTag> {
public:
	using InternalType = typename SolverVariableMapInterface<ImplTag, SolverTag>::InternalType;
	using InterfaceType = typename SolverVariableMapInterface<ImplTag, SolverTag>::InterfaceType;

	using SolverVariableMapInterface<ImplTag, SolverTag>::InternalToInterface;
	using SolverVariableMapInterface<ImplTag, SolverTag>::InterfaceToInternal;
	using SolverVariableMapInterface<ImplTag, SolverTag>::InterfaceIsNegated;
	using SolverVariableMapInterface<ImplTag, SolverTag>::InternalHash;

	struct BmcVariableHash {
		size_t operator()(const BmcVariable& key) const {
			return (std::hash<ssize_t>()(key.GetTimeframe()) << 16u) | std::hash<size_t>()(key.GetId());
		}
	};
	struct SolverVariableHash {
		size_t operator()(const InternalType& key) const {
			return InternalHash(key);
		}
	};

	SolverVariableMap() = default;

	template<typename CreateFunction, typename... Params>
	InterfaceType MapLiteralForward(const BmcLiteral& literal, Params... params, CreateFunction func) {
		// Look up if a SAT-equivalent of the BmcVariable has been added to the solver before.
		if (const auto it = forwardVarMapping.find(BmcVariable(literal)); it != forwardVarMapping.end()) {
			return InternalToInterface(it->second, literal.IsNegated());
		}

		BmcVariable bmcVar { literal };
		if constexpr (!std::is_same_v<decltype(func(std::declval<BmcVariable>())), void>) {
			// No equivalent mapping in the SAT solver exists yet.
			InternalType solverVar { func(bmcVar, params...) };
			forwardVarMapping.emplace(std::make_pair(bmcVar, solverVar));
			backwardVarMapping.emplace(std::make_pair(solverVar, bmcVar));
			return InternalToInterface(solverVar, literal.IsNegated());
		} else {
			func(bmcVar, params...);
			throw std::runtime_error("Creating new variables is not allowed");
		}
	}

	template<typename CreateFunction, typename... Params>
	BmcLiteral MapLiteralBackward(const InterfaceType& literal, Params... params, CreateFunction func) {
		// Look up if a BMC-equivalent of the SAT variable has been added to the solver before.
		if (const auto it = backwardVarMapping.find(InterfaceToInternal(literal)); it != backwardVarMapping.end()) {
			return it->second.ToLiteral(InterfaceIsNegated(literal));
		}

		InternalType solverVar { InterfaceToInternal(literal) };
		if constexpr (!std::is_same_v<decltype(func(std::declval<InternalType>())), void>) {
			// No equivalent mapping in the BMC solver exists yet.
			// This might happen when creating the craig interpolant which uses
			// additional tseitin variables that have to be mapped to the BMC side.
			BmcVariable bmcVar { func(solverVar, params...) };
			forwardVarMapping.emplace(std::make_pair(bmcVar, solverVar));
			backwardVarMapping.emplace(std::make_pair(solverVar, bmcVar));
			return bmcVar.ToLiteral(InterfaceIsNegated(literal));
		} else {
			func(solverVar, params...);
			throw std::runtime_error("Creating new variables is not allowed");
		}
	}

	const std::unordered_map<BmcVariable, InternalType, BmcVariableHash>& GetForwardMappings() { return forwardVarMapping; };
	const std::unordered_map<InternalType, BmcVariable, SolverVariableHash>& GetBackwardMappings() { return backwardVarMapping; };

private:
	std::unordered_map<BmcVariable, InternalType, BmcVariableHash> forwardVarMapping;
	std::unordered_map<InternalType, BmcVariable, SolverVariableHash> backwardVarMapping;

};

template<typename ImplTag, typename SolverTag>
class Solver: public SolverInterface<ImplTag, SolverTag> { };

}
}
