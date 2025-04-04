// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <unordered_map>

#include "./bmc-problem.hpp"

namespace Ncip {
namespace Impl {

enum class SolverClauseType {
	Init,
	Trans,
	Target,
	Craig,
	ASide,
	BSide
};

enum class SolverVariableType {
	Original,

	InitTrigger,
	TransTrigger,
	TargetTrigger,
	CraigTrigger,

	InitTseitin,
	TransTseitin,
	TargetTseitin,
	CraigTseitin,

	FpcTrigger,
	FpcRoot,
	ATrigger,
	BTrigger
};

inline std::string to_string(const SolverClauseType& type) {
	switch (type) {
		case SolverClauseType::Init: return "Init";
		case SolverClauseType::Trans: return "Trans";
		case SolverClauseType::Target: return "Target";
		case SolverClauseType::Craig: return "Craig";
		case SolverClauseType::ASide: return "A Side";
		case SolverClauseType::BSide: return "B Side";
		default: assert(false); __builtin_unreachable();
	}
}

inline std::string to_string(const SolverVariableType& type) {
	switch (type) {
		case SolverVariableType::Original: return "Original";

		case SolverVariableType::InitTrigger: return "Init Trigger";
		case SolverVariableType::TransTrigger: return "Trans Trigger";
		case SolverVariableType::TargetTrigger: return "Target Trigger";
		case SolverVariableType::CraigTrigger: return "Craig Trigger";

		case SolverVariableType::InitTseitin: return "Init Tseitin";
		case SolverVariableType::TransTseitin: return "Trans Tseitin";
		case SolverVariableType::TargetTseitin: return "Target Tseitin";
		case SolverVariableType::CraigTseitin: return "Craig Tseitin";

		case SolverVariableType::FpcTrigger: return "FPC Trigger";
		case SolverVariableType::FpcRoot: return "FPC Root";
		case SolverVariableType::ATrigger: return "A Trigger";
		case SolverVariableType::BTrigger: return "B Trigger";
		default: assert(false); __builtin_unreachable();
	}
}

class BmcVariable {
public:
	constexpr explicit BmcVariable(size_t id_, ssize_t timeframe_ = 0):
		id(id_),
		timeframe(timeframe_)
	{}
	constexpr explicit BmcVariable(BmcLiteral literal):
		id(literal.GetVariable()),
		timeframe(literal.GetTimeframe())
	{}

	constexpr size_t GetId() const { return id; }
	constexpr ssize_t GetTimeframe() const { return timeframe; }
	constexpr BmcLiteral ToLiteral(bool negated = false) const {
		return BmcLiteral::FromVariable(id, negated, timeframe);
	}

	BmcVariable operator++(int) { return BmcVariable(id++, timeframe); }
	constexpr BmcVariable operator<<(ssize_t amount) const { return BmcVariable(id, timeframe - amount); }
	constexpr BmcVariable operator>>(ssize_t amount) const { return BmcVariable(id, timeframe + amount); }

	constexpr bool operator==(const BmcVariable& other) const { return (timeframe == other.timeframe) && (id == other.id); }
	constexpr bool operator<(const BmcVariable& other) const {
		return (timeframe < other.timeframe) || ((timeframe == other.timeframe) && (id < other.id));
	}

private:
	size_t id;
	ssize_t timeframe;
};

inline std::string to_string(const BmcVariable& variable, ssize_t shift = 0) {
	return std::to_string(variable.GetId())
		+ ":" + std::to_string(variable.GetTimeframe() + shift);
};

}

namespace Backend {

struct CraigSolverTag {};
struct PreSolverTag {};
struct FpcSolverTag {};

#ifdef NCIP_BACKEND_MINICRAIG
struct MiniCraigTag {};
inline std::string to_string(const MiniCraigTag& tag) {
	return "MiniCraig";
}
# ifndef NDEBUG
struct MiniCraigDebugTag {};
inline std::string to_string(const MiniCraigDebugTag& tag) {
	return "MiniCraigDebug";
}
# endif
#endif

#ifdef NCIP_BACKEND_CADICRAIG
struct CadiCraigTag {};
inline std::string to_string(const CadiCraigTag& tag) {
	return "CaDiCraig";
}
# ifndef NDEBUG
struct CadiCraigDebugTag {};
inline std::string to_string(const CadiCraigDebugTag& tag) {
	return "CaDiCraigDebug";
}
#  endif
#endif

#ifdef NCIP_BACKEND_KITTENCRAIG
struct KittenCraigTag {};
inline std::string to_string(const KittenCraigTag& tag) {
	return "KittenCraig";
}
#endif

template<typename ImplTag, typename SolverTag> class Solver;

}
}
