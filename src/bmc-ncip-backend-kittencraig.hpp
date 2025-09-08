// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include "bmc-ncip.hpp"
#include "bmc-ncip-backend.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iostream>
#include <type_traits>
#include <memory>

#if __has_include(<kissat/kitten.h>)
#include <kissat/kitten.h>
#include <kittencraig/kittentracer.hpp>
#include <kittencraig/kittenerror.hpp>
#else
#include <kitten.h>
#include <kittentracer.hpp>
#include <kittenerror.hpp>
#endif

namespace Ncip {
namespace Backend {

class KittenCraigVariableMapInterface {
public:
	using InterfaceType = unsigned;
	using InternalType = unsigned;

	static InterfaceType InternalToInterface(const InternalType& internal, bool negated) {
		return (internal << 1) | (negated ? 1 : 0);
	}
	static InternalType InterfaceToInternal(const InterfaceType& interface) {
		return interface >> 1;
	}
	static bool InterfaceIsNegated(const InterfaceType& interface) {
		return interface & 1;
	}
	static size_t InternalHash(const InternalType& internal) {
		return std::hash<unsigned>()(internal);
	}
};

template<typename ImplTag, typename SolverTag>
class KittenCraigSolverInterface: public SolverVariableMap<ImplTag, SolverTag> {
private:
	unsigned nextVar;
	unsigned nextClause;
	std::atomic<bool> interrupted;
	std::unique_ptr<kitten, void(*)(kitten *)> solver;
	std::unique_ptr<KittenCraig::KittenTracer> craigTracer;

public:
	static constexpr const bool is_preprocessor = std::is_same_v<SolverTag, PreSolverTag>;
	static constexpr const bool is_fpc = std::is_same_v<SolverTag, FpcSolverTag>;
	static constexpr const bool is_craig = std::is_same_v<SolverTag, CraigSolverTag>;

	KittenCraigSolverInterface():
			nextVar(1),
			nextClause(1),
			interrupted(false),
			solver({ kitten_init(), kitten_release }),
			craigTracer(nullptr)
	{
		kitten_track_antecedents(solver.get());
		kitten_set_terminator(solver.get(), static_cast<void*>(this), [](void* data) -> int {
			auto solver { static_cast<KittenCraigSolverInterface<ImplTag, SolverTag>*>(data) };
			return solver->interrupted.load(std::memory_order_acquire);
		});

		if constexpr (is_craig) {
			craigTracer = std::make_unique<KittenCraig::KittenTracer>();
		}
	}
	virtual ~KittenCraigSolverInterface() = default;

	template<bool Enable = is_craig>
	std::enable_if_t<Enable, KittenCraig::CraigVarType> MapVariableType(BackendVariableType type) {
		switch (type) {
			case BackendVariableType::GLOBAL: return KittenCraig::CraigVarType::GLOBAL;
			case BackendVariableType::A_LOCAL: return KittenCraig::CraigVarType::A_LOCAL;
			case BackendVariableType::B_LOCAL: return KittenCraig::CraigVarType::B_LOCAL;
			case BackendVariableType::A_PROTECTED: return KittenCraig::CraigVarType::A_LOCAL;
			case BackendVariableType::B_PROTECTED: return KittenCraig::CraigVarType::B_LOCAL;
			default: assert(false); __builtin_unreachable();
		}
	}

	template<bool Enable = is_craig>
	std::enable_if_t<Enable, KittenCraig::CraigClauseType> MapClauseType(BackendClauseType type) {
		switch (type) {
			case BackendClauseType::A_CLAUSE: return KittenCraig::CraigClauseType::A_CLAUSE;
			case BackendClauseType::B_CLAUSE: return KittenCraig::CraigClauseType::B_CLAUSE;
			default: assert(false); __builtin_unreachable();
		}
	}

	unsigned CreateVariable(BmcVariable variable, BackendVariableType type, bool trace=false) {
		unsigned mapped { (nextVar - 1) << 1 };

		if constexpr (is_craig) {
			craigTracer->label_variable(nextVar, MapVariableType(type));
		}

		// Kitten doesn't do variable eliminiation.
		if (trace) {
			std::cerr << "    Add Variable " << to_string(variable)
				<< " <=> Mapped " << std::to_string(mapped)
				<< " " << to_string(type) << std::endl;
		}

		return (nextVar++ - 1);
	}

	template<typename Func>
	bool UnprotectVariable(BmcLiteral variable, BackendVariableType type, Func createVariable, bool trace=false) {
		auto const& mappings = this->GetForwardMappings();
		if (auto it = mappings.find(BmcVariable(variable)); it != mappings.end()) {
			auto mapped = this->InternalToInterface(it->second, variable.IsNegated());
			// Kitten doesn't require freezing as no inprocessing is performed.
			if (trace) {
				std::cerr << "    Unprotecting Variable " << to_string(variable)
					<< " <=> Mapped " << std::to_string(mapped)
					<< " " << to_string(type) << std::endl;
			}
		} else {
			if (trace) {
				std::cerr << "    Unprotecting Variable " << to_string(variable)
					<< " <=> Not Mapped " << " " << to_string(type) << std::endl;
			}
		}

		return true;
	}

	bool IsEliminated(BmcLiteral literal) {
		return false;
	}

	template<typename Func>
	bool AddClauses(BmcLiteral trigger, const BmcClauses& clauses, ssize_t shift, BackendClauseType type, Func createVariable, bool trace=false) {
		for (const auto& clause : clauses) {
			std::vector<unsigned> solverClause;
			solverClause.reserve(clause.size() + ((trigger != INVALID_LITERAL) ? 1 : 0));

			if (trigger != INVALID_LITERAL) {
				solverClause.push_back(this->MapLiteralForward(-trigger, createVariable));
			}
			for (const auto& literal : clause) {
				solverClause.push_back(this->MapLiteralForward(literal >> shift, createVariable));
			}

			if (trace) {
				std::cerr << "    Clause " << to_string(clause, shift);
				std::cerr << " <=> Mapped " << to_string(type) << " (";
				for (size_t index { 0u }; index < solverClause.size(); index++) {
					if (index != 0u) std::cerr << ", ";
					std::cerr << std::to_string(solverClause[index]);
				}
				std::cerr << ")" << std::endl;
			}

			std::sort(solverClause.begin(), solverClause.end());
			solverClause.erase(std::unique(solverClause.begin(), solverClause.end()), solverClause.end());

			bool tautology = false;
            for (int index { 0 }; index + 1 < solverClause.size(); index++) {
                if ((solverClause[index] ^ solverClause[index + 1]) == 1) {
                    tautology = true;
                    break;
                }
            }
            if (tautology) {
                continue;
            }

			if constexpr (is_craig) {
				craigTracer->label_clause(nextClause, MapClauseType(type));
			}
			kitten_clause_with_id_and_exception(solver.get(), nextClause, solverClause.size(), &(solverClause.front()), UINT_MAX);
			nextClause++;
		}

		return true;
	}

	template<typename Func>
	bool AddTrigger(const BmcLiteral& trigger, BackendClauseType type, Func createVariable, bool trace=false) {
		auto mapped { this->MapLiteralForward(trigger, createVariable) };

		// Kitten doesn't do variable eliminiation.
		if (trace) {
			std::cerr << "    Trigger " << to_string(trigger)
				<< " <=> Mapped " << mapped << std::endl;
		}

		return true;
	}

	template<typename Func>
	bool PermanentlyDisableTrigger(const BmcLiteral& trigger, BackendClauseType type, Func createVariable, bool trace=false) {
		auto mapped { this->MapLiteralForward(trigger, createVariable) };

		if (trace) {
			std::cerr << "    Trigger " << to_string(trigger)
				<< " <=> Mapped " << mapped << std::endl;
		}

		// Kitten doesn't do variable eliminiation.
		if constexpr (is_craig) {
			craigTracer->label_clause(nextClause, MapClauseType(type));
		}

		std::vector<unsigned> clause;
		clause.push_back(mapped ^ 1);
		kitten_clause_with_id_and_exception(solver.get(), nextClause, 1, &clause.front(), UINT_MAX);
		nextClause++;
		return true;
	}

	template<typename Func, bool Enable = !is_preprocessor>
	std::enable_if_t<Enable, bool> SolveWithAssumptions(const BmcClause& assumptions, Func createVariable, bool trace=false) {
		std::vector<unsigned> solverAssumptions;
		solverAssumptions.reserve(assumptions.size());
		for (const auto& literal : assumptions) {
			solverAssumptions.push_back(this->MapLiteralForward(literal, createVariable));
		}

		if (trace) {
			for (size_t index { 0u }; index < assumptions.size(); index++) {
				std::cerr << "    Assuming " << to_string(assumptions[index], 0u)
					<< " <=> Mapped " << std::to_string(solverAssumptions[index]) << std::endl;
			}
		}

		if constexpr (is_craig) {
			craigTracer->reset_assumptions();
		}
		for (auto const& assumption : solverAssumptions) {
			kitten_assume(solver.get(), assumption);
			if constexpr (is_craig) {
				craigTracer->add_assumption(assumption);
			}
		}

		auto result { kitten_solve(solver.get()) };
		if (trace) { std::cerr << "    Result is " << ((result == 10) ? "SAT" : ((result == 20) ? "UNSAT" : "TIMEOUT")) << std::endl; }

		if constexpr (is_craig) {
			if (result == 20) { craigTracer->conclude_unsat(solver.get()); }
		}

		return (result == 10);
	}

	template<typename Func>
	BmcAssignment GetSolvedLiteral(const BmcLiteral& literal, ssize_t shift, Func createVariable, bool trace=false) {
		auto mapped { this->MapLiteralForward(literal >> shift, createVariable) };
		auto assignment { kitten_value(solver.get(), mapped) };

		if (assignment == 0) { return BmcAssignment::DontCare; }
		return ((assignment > 0) ? BmcAssignment::Positive : BmcAssignment::Negative);
	}

	void Interrupt() {
		interrupted.store(true, std::memory_order_release);
	}

	template<bool Enable = is_craig>
	std::enable_if_t<Enable, void> ConfigureCraigInterpolant(CraigInterpolant interpolant, bool enableCraig) {
		if (!enableCraig) {
			craigTracer->set_craig_construction(KittenCraig::CraigConstruction::NONE);
			return;
		}

		switch (interpolant) {
			case CraigInterpolant::Symmetric:
				craigTracer->set_craig_construction(KittenCraig::CraigConstruction::SYMMETRIC);
				break;
			case CraigInterpolant::Asymmetric:
				craigTracer->set_craig_construction(KittenCraig::CraigConstruction::ASYMMETRIC);
				break;
			case CraigInterpolant::DualSymmetric:
				craigTracer->set_craig_construction(KittenCraig::CraigConstruction::DUAL_SYMMETRIC);
				break;
			case CraigInterpolant::DualAsymmetric:
				craigTracer->set_craig_construction(KittenCraig::CraigConstruction::DUAL_ASYMMETRIC);
				break;
			case CraigInterpolant::Intersection:
				craigTracer->set_craig_construction(KittenCraig::CraigConstruction::ALL);
				break;
			case CraigInterpolant::Union:
				craigTracer->set_craig_construction(KittenCraig::CraigConstruction::ALL);
				break;
			case CraigInterpolant::Smallest:
				craigTracer->set_craig_construction(KittenCraig::CraigConstruction::ALL);
				break;
			case CraigInterpolant::Largest:
				craigTracer->set_craig_construction(KittenCraig::CraigConstruction::ALL);
				break;
			default: __builtin_unreachable();
		}
	}

	template<typename Func, bool Enable = is_craig>
	std::enable_if_t<Enable, std::tuple<BmcClauses, BmcLiteral>> GetCraigInterpolant(CraigInterpolant interpolant, Func createVariable, bool trace=false) {
		KittenCraig::CraigInterpolant mapped { 0 };
		switch (interpolant) {
			case CraigInterpolant::Symmetric: mapped = KittenCraig::CraigInterpolant::SYMMETRIC; break;
			case CraigInterpolant::Asymmetric: mapped = KittenCraig::CraigInterpolant::ASYMMETRIC; break;
			case CraigInterpolant::DualSymmetric: mapped = KittenCraig::CraigInterpolant::DUAL_SYMMETRIC; break;
			case CraigInterpolant::DualAsymmetric: mapped = KittenCraig::CraigInterpolant::DUAL_ASYMMETRIC; break;
			case CraigInterpolant::Intersection: mapped = KittenCraig::CraigInterpolant::INTERSECTION; break;
			case CraigInterpolant::Union: mapped = KittenCraig::CraigInterpolant::UNION; break;
			case CraigInterpolant::Smallest: mapped = KittenCraig::CraigInterpolant::SMALLEST; break;
			case CraigInterpolant::Largest: mapped = KittenCraig::CraigInterpolant::LARGEST; break;
			default: __builtin_unreachable();
		}

		int nextVar { static_cast<int>(this->nextVar) };
		std::vector<std::vector<int>> craigCnf { };
		int cnfRoot = 0;

		auto cnfType { craigTracer->create_craig_interpolant(mapped, craigCnf, nextVar) };
		switch (cnfType) {
			case KittenCraig::CraigCnfType::CONSTANT0:
			case KittenCraig::CraigCnfType::CONSTANT1:
				cnfRoot = nextVar++;
				craigCnf.clear();
				craigCnf.push_back({ (cnfType == KittenCraig::CraigCnfType::CONSTANT0) ? -cnfRoot : cnfRoot });
				break;

			case KittenCraig::CraigCnfType::NORMAL:
				cnfRoot = craigCnf.back()[0];
				craigCnf.pop_back();
				break;

			default: __builtin_unreachable();
		}
		while (static_cast<int>(this->nextVar) < nextVar) {
			auto var = this->nextVar++;
			craigTracer->label_variable(var, KittenCraig::CraigVarType::A_LOCAL);
			this->MapLiteralBackward((var - 1) << 1, createVariable);
		}

		if (trace) {
			switch (cnfType) {
				case KittenCraig::CraigCnfType::CONSTANT0: std::cerr << "    Constant 0" << std::endl; break;
				case KittenCraig::CraigCnfType::CONSTANT1: std::cerr << "    Constant 1" << std::endl; break;
				case KittenCraig::CraigCnfType::NORMAL: std::cerr << "    Normal" << std::endl; break;
				default: __builtin_unreachable();
			}
		}

		// Map the craig interpolant back to the BMC literals and create new literals
		// for generated Tseitin variables.
		BmcClauses craig;
		craig.reserve(craigCnf.size());
		for (size_t clauseIndex { 0u }; clauseIndex < craigCnf.size(); clauseIndex++) {
			BmcClause clause { };
			clause.reserve(craigCnf[clauseIndex].size());
			for (size_t literalIndex { 0u }; literalIndex < craigCnf[clauseIndex].size(); literalIndex++) {
				const auto& literal { craigCnf[clauseIndex][literalIndex] };
				clause.push_back(this->MapLiteralBackward(((std::abs(literal) - 1) << 1) | ((literal < 0) ? 1 : 0), createVariable));
			}

			if (trace) {
				std::cerr << "    Clause " << to_string(clause, 0u) << " <=> Mapped (";
				for (size_t index { 0u }; index < craigCnf[clauseIndex].size(); index++) {
					if (index != 0u) std::cerr << ", ";
					const auto& literal { craigCnf[clauseIndex][index] };
					std::cerr << std::to_string(((std::abs(literal) - 1) << 1) | ((literal < 0) ? 1 : 0));
				}
				std::cerr << ")" << std::endl;
			}

			craig.push_back(clause);
		}

		return { craig, this->MapLiteralBackward(((std::abs(cnfRoot) - 1) << 1) | ((cnfRoot < 0) ? 1 : 0), createVariable) };
	}

	template<typename Func1, typename Func2, bool Enable = is_preprocessor>
	std::enable_if_t<Enable, BmcClauses> PreprocessClauses(const BmcClauses& clauses, const std::vector<bool>& globalVars, const BmcLiteral& root, const PreprocessLevel& level, Func1 createSolverVariable, Func2 createBmcVariable, bool trace=false) {
		if (trace) { std::cerr << "  - Preprocessing unsupported" << std::endl; }
		return clauses;
	}

};

}
}
