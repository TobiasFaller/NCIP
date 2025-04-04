// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include "bmc-ncip.hpp"
#include "bmc-ncip-backend.hpp"

#include <atomic>
#include <cmath>
#include <iostream>
#include <type_traits>

#ifdef INCLUDE_PREFIXED
#include <cadical/cadical.hpp>
#include <cadicraig/craigtracer.hpp>
#else /* INCLUDE_PREFIXED */
#include <cadical.hpp>
#include <craigtracer.hpp>
#endif /* INCLUDE_PREFIXED */

namespace Ncip {
namespace Backend {

class CadiCraigVariableMapInterface {
public:
	using InterfaceType = int;
	using InternalType = int;

	static InterfaceType InternalToInterface(const InternalType& internal, bool negated) {
		return internal * (negated ? -1 : 1);
	}
	static InternalType InterfaceToInternal(const InterfaceType& interface) {
		return std::abs(interface);
	}
	static bool InterfaceIsNegated(const InterfaceType& interface) {
		return interface < 0;
	}
	static size_t InternalHash(const InternalType& internal) {
		return std::hash<int>()(internal);
	}
};

template<typename ImplTag, typename SolverTag>
class CadiCraigSolverInterface: public SolverVariableMap<ImplTag, SolverTag>, CaDiCaL::Solver, CaDiCaL::Terminator {
private:
	template<typename Func> class ClauseAdder;

	int nextVar;
	int nextClause;
	std::atomic<bool> interrupted;
	CaDiCraig::CraigTracer* craigTracer;

public:
	static constexpr const bool is_preprocessor = std::is_same_v<SolverTag, PreSolverTag>;
	static constexpr const bool is_fpc = std::is_same_v<SolverTag, FpcSolverTag>;
	static constexpr const bool is_craig = std::is_same_v<SolverTag, CraigSolverTag>;

	CadiCraigSolverInterface(): nextVar(1), nextClause(1), interrupted(false), craigTracer(nullptr) {
		this->connect_terminator(this);

		if constexpr (is_craig) {
			craigTracer = new CaDiCraig::CraigTracer();
			this->connect_proof_tracer(craigTracer, true);
		}
	}

	virtual ~CadiCraigSolverInterface() {
		this->disconnect_terminator();

		if constexpr (is_craig) {
			this->disconnect_proof_tracer(craigTracer);
			delete craigTracer;
		}
	}

	template<bool Enable = is_craig>
	std::enable_if_t<Enable, CaDiCraig::CraigVarType> MapVariableType(BackendVariableType type) {
		switch (type) {
			case BackendVariableType::GLOBAL: return CaDiCraig::CraigVarType::GLOBAL;
			case BackendVariableType::A_LOCAL: return CaDiCraig::CraigVarType::A_LOCAL;
			case BackendVariableType::B_LOCAL: return CaDiCraig::CraigVarType::B_LOCAL;
			case BackendVariableType::A_PROTECTED: return CaDiCraig::CraigVarType::A_LOCAL;
			case BackendVariableType::B_PROTECTED: return CaDiCraig::CraigVarType::B_LOCAL;
			default: assert(false); __builtin_unreachable();
		}
	}

	template<bool Enable = is_craig>
	std::enable_if_t<Enable, CaDiCraig::CraigClauseType> MapClauseType(BackendClauseType type) {
		switch (type) {
			case BackendClauseType::A_CLAUSE: return CaDiCraig::CraigClauseType::A_CLAUSE;
			case BackendClauseType::B_CLAUSE: return CaDiCraig::CraigClauseType::B_CLAUSE;
			default: assert(false); __builtin_unreachable();
		}
	}

	int CreateVariable(BmcVariable variable, BackendVariableType type, bool trace=false) {
		int mapped { nextVar++ };

		if constexpr (is_craig) {
			craigTracer->label_variable(mapped, MapVariableType(type));
		}

		// CaDiCal doesn't require freezing, clauses and variables will be restored automatically.
		if (trace) {
			std::cerr << "    Add Variable " << to_string(variable)
				<< " <=> Mapped " << std::to_string(mapped)
				<< " " << to_string(type) << std::endl;
		}

		return mapped;
	}

	template<typename Func>
	bool UnprotectVariable(BmcLiteral variable, BackendVariableType type, Func createVariable, bool trace=false) {
		auto const& mappings = this->GetForwardMappings();
		if (auto it = mappings.find(BmcVariable(variable)); it != mappings.end()) {
			auto mapped = this->InternalToInterface(it->second, variable.IsNegated());
			// CaDiCal doesn't require freezing, clauses and variables will be restored automatically.
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
			std::vector<int> solverClause;
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

			if constexpr (is_craig) {
				craigTracer->label_clause(nextClause++, MapClauseType(type));
			}
			for (auto const& literal : solverClause) {
				this->add(literal);
			}
			this->add(0);
		}

		return true;
	}

	template<typename Func>
	bool AddTrigger(const BmcLiteral& trigger, BackendClauseType type, Func createVariable, bool trace=false) {
		auto mapped { this->MapLiteralForward(trigger, createVariable) };

		// CaDiCal doesn't require freezing, clauses and variables will be restored automatically.
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

		// CaDiCal doesn't require freezing, clauses and variables will be restored automatically.
		if constexpr (is_craig) {
			craigTracer->label_clause(nextClause++, MapClauseType(type));
		}

		this->add(-mapped);
		this->add(0);
		return true;
	}

	template<typename Func, bool Enable = !is_preprocessor>
	std::enable_if_t<Enable, bool> SolveWithAssumptions(const BmcClause& assumptions, Func createVariable, bool trace=false) {
		std::vector<int> solverAssumptions;
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
			this->assume(assumption);
			if constexpr (is_craig) {
				craigTracer->add_assumption(assumption);
			}
		}

		auto result { this->solve() };
		if (trace) { std::cerr << "    Result is " << ((result == 10) ? "SAT" : ((result == 20) ? "UNSAT" : "TIMEOUT")) << std::endl; }

		if constexpr (is_craig) {
			if (result != 0) { this->conclude(); }
		}

		return (result == 10);
	}

	template<typename Func>
	BmcAssignment GetSolvedLiteral(const BmcLiteral& literal, ssize_t shift, Func createVariable, bool trace=false) {
		auto mapped { this->MapLiteralForward(literal >> shift, createVariable) };
		auto assignment { this->val(std::abs(mapped)) };

		if (assignment == 0) { return BmcAssignment::DontCare; }
		return ((assignment > 0) ? BmcAssignment::Positive : BmcAssignment::Negative) ^ literal.IsNegated();
	}

	void Interrupt() {
		interrupted = true;
	}

	bool terminate() override {
		return interrupted;
	}

	template<bool Enable = is_craig>
	std::enable_if_t<Enable, void> ConfigureCraigInterpolant(CraigInterpolant interpolant, bool enableCraig) {
		if (!enableCraig) {
			craigTracer->set_craig_construction(CaDiCraig::CraigConstruction::NONE);
			return;
		}

		switch (interpolant) {
			case CraigInterpolant::Symmetric:
				craigTracer->set_craig_construction(CaDiCraig::CraigConstruction::SYMMETRIC);
				break;
			case CraigInterpolant::Asymmetric:
				craigTracer->set_craig_construction(CaDiCraig::CraigConstruction::ASYMMETRIC);
				break;
			case CraigInterpolant::DualSymmetric:
				craigTracer->set_craig_construction(CaDiCraig::CraigConstruction::DUAL_SYMMETRIC);
				break;
			case CraigInterpolant::DualAsymmetric:
				craigTracer->set_craig_construction(CaDiCraig::CraigConstruction::DUAL_ASYMMETRIC);
				break;
			case CraigInterpolant::Intersection:
				craigTracer->set_craig_construction(CaDiCraig::CraigConstruction::ALL);
				break;
			case CraigInterpolant::Union:
				craigTracer->set_craig_construction(CaDiCraig::CraigConstruction::ALL);
				break;
			case CraigInterpolant::Smallest:
				craigTracer->set_craig_construction(CaDiCraig::CraigConstruction::ALL);
				break;
			case CraigInterpolant::Largest:
				craigTracer->set_craig_construction(CaDiCraig::CraigConstruction::ALL);
				break;
			default: __builtin_unreachable();
		}
	}

	template<typename Func, bool Enable = is_craig>
	std::enable_if_t<Enable, std::tuple<BmcClauses, BmcLiteral>> GetCraigInterpolant(CraigInterpolant interpolant, Func createVariable, bool trace=false) {
		CaDiCraig::CraigInterpolant mapped { 0 };
		switch (interpolant) {
			case CraigInterpolant::Symmetric: mapped = CaDiCraig::CraigInterpolant::SYMMETRIC; break;
			case CraigInterpolant::Asymmetric: mapped = CaDiCraig::CraigInterpolant::ASYMMETRIC; break;
			case CraigInterpolant::DualSymmetric: mapped = CaDiCraig::CraigInterpolant::DUAL_SYMMETRIC; break;
			case CraigInterpolant::DualAsymmetric: mapped = CaDiCraig::CraigInterpolant::DUAL_ASYMMETRIC; break;
			case CraigInterpolant::Intersection: mapped = CaDiCraig::CraigInterpolant::INTERSECTION; break;
			case CraigInterpolant::Union: mapped = CaDiCraig::CraigInterpolant::UNION; break;
			case CraigInterpolant::Smallest: mapped = CaDiCraig::CraigInterpolant::SMALLEST; break;
			case CraigInterpolant::Largest: mapped = CaDiCraig::CraigInterpolant::LARGEST; break;
			default: __builtin_unreachable();
		}

		int nextVar { this->nextVar };
		std::vector<std::vector<int>> craigCnf { };
		int cnfRoot = 0;

		auto cnfType { craigTracer->create_craig_interpolant(mapped, craigCnf, nextVar) };
		switch (cnfType) {
			case CaDiCraig::CraigCnfType::CONSTANT0:
			case CaDiCraig::CraigCnfType::CONSTANT1:
				cnfRoot = nextVar++;
				craigCnf.clear();
				craigCnf.push_back({ (cnfType == CaDiCraig::CraigCnfType::CONSTANT0) ? -cnfRoot : cnfRoot  });
				break;

			case CaDiCraig::CraigCnfType::NORMAL:
				cnfRoot = craigCnf.back()[0];
				craigCnf.pop_back();
				break;

			default: __builtin_unreachable();
		}
		while (this->nextVar < nextVar) {
			auto var = this->nextVar++;
			craigTracer->label_variable(var, CaDiCraig::CraigVarType::A_LOCAL);
			this->MapLiteralBackward(var, createVariable);
		}

		if (trace) {
			switch (cnfType) {
				case CaDiCraig::CraigCnfType::CONSTANT0: std::cerr << "    Constant 0" << std::endl; break;
				case CaDiCraig::CraigCnfType::CONSTANT1: std::cerr << "    Constant 1" << std::endl; break;
				case CaDiCraig::CraigCnfType::NORMAL: std::cerr << "    Normal" << std::endl; break;
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
				clause.push_back(this->MapLiteralBackward(literal, createVariable));
			}

			if (trace) {
				std::cerr << "    Clause " << to_string(clause, 0u) << " <=> Mapped (";
				for (size_t index { 0u }; index < craigCnf[clauseIndex].size(); index++) {
					if (index != 0u) std::cerr << ", ";
					std::cerr << craigCnf[clauseIndex][index];
				}
				std::cerr << ")" << std::endl;
			}

			craig.push_back(clause);
		}

		return { craig, this->MapLiteralBackward(cnfRoot, createVariable) };
	}

	template<typename Func1, typename Func2, bool Enable = is_preprocessor>
	std::enable_if_t<Enable, BmcClauses> PreprocessClauses(const BmcClauses& clauses, const std::vector<bool>& globalVars, const BmcLiteral& root, const PreprocessLevel& level, Func1 createSolverVariable, Func2 createBmcVariable, bool trace=false) {
		if (level >= PreprocessLevel::Expensive) {
			this->set("block", 1);
			this->set("condition", 1);
			this->set("cover", 1);
			this->set("vivifyonce", 1);
		}

		std::vector<int> preClause;
		for (auto const& clause : clauses) {
			preClause.reserve(clause.size());
			for (auto const& lit : clause) {
				preClause.push_back(this->MapLiteralForward(lit, createSolverVariable));
			}

			if (trace) {
				std::cerr << "    Input Clause " << to_string(clause, 0u);
				std::cerr << " <=> Mapped (";
				for (int index { 0 }; index < preClause.size(); index++) {
					if (index != 0u) std::cerr << ", ";
					std::cerr << std::to_string(preClause[index]);
				}
				std::cerr << ")" << std::endl;
			}

			for (auto const& lit : preClause) {
				this->add(lit);
			}
			this->add(0);
			preClause.clear();
		}

		// Has to happen after clauses have been added.
		// Relies on GetForwardMappings to contain all mappings.
		if (trace) { std::cerr << "  - Freezing protected variables" << std::endl; }
		for (auto [bmcVar, preVar] : this->GetForwardMappings()) {
			if ((bmcVar.GetId() < globalVars.size()) && globalVars[bmcVar.GetId()]) {
				this->freeze(preVar);
				if (trace) { std::cerr << "    Freezing global " << to_string(bmcVar) << " <=> Mapped " << std::to_string(preVar) << std::endl; }
			}
		}
		if (root != INVALID_LITERAL) {
			auto mapped { this->MapLiteralForward(root, createSolverVariable) };
			if (trace) { std::cerr << "    Freezing root " << to_string(root) << " <=> Mapped " << std::to_string(mapped) << std::endl; }
			this->freeze(mapped);
		} else {
			if (trace) { std::cerr << "    No root to freeze" << std::endl; }
		}

		if (trace) { std::cerr << "  - Preprocessing" << std::endl; }
		auto code { this->simplify((level >= PreprocessLevel::Expensive) ? 3 : 1) };
		bool result { code != 20 };
		if (!result) {
			if (trace) { std::cerr << "    Result Constant 0" << std::endl; }
			return { (root != INVALID_LITERAL) ? BmcClause {-root} : BmcClause {} };
		}

		if (trace) { std::cerr << "  - Extracting resulting clauses" << std::endl; }
		BmcClauses resultClauses;
		resultClauses.reserve(this->irredundant());

		ClauseAdder<Func2> clauseAdder { *this, resultClauses, createBmcVariable, trace };
		this->traverse_clauses(clauseAdder);
		this->traverse_witnesses_backward(clauseAdder);
		return resultClauses;
	}

private:
	template<typename Func>
	class ClauseAdder: public CaDiCaL::ClauseIterator, public CaDiCaL::WitnessIterator {
	private:
		SolverVariableMap<ImplTag, SolverTag>& base;
		BmcClauses& target;
		Func createBmcVariable;
		bool trace;

	public:
		ClauseAdder(SolverVariableMap<ImplTag, SolverTag>& base, BmcClauses& target, Func createBmcVariable, bool trace=false):
			base(base),
			target(target),
			createBmcVariable(createBmcVariable),
			trace(trace)
		{}

		bool clause(const std::vector<int>& clause) override {
			auto& resultClause = target.emplace_back();
			resultClause.reserve(clause.size());
			for (auto& lit : clause) {
				resultClause.push_back(base.MapLiteralBackward(lit, createBmcVariable));
			}

			if (trace) {
				std::cerr << "    Result Clause " << to_string(resultClause, 0u);
				std::cerr << " <=> Mapped (";
				size_t index { 0u };
				for (auto const& lit : clause) {
					if (index++ != 0) { std::cerr << ", "; }
					std::cerr << std::to_string(lit);
				}
				std::cerr << ")" << std::endl;
			}

			return true;
		}
		bool witness(const std::vector<int> &clause, const std::vector<int> &witness, uint64_t id) override {
			auto& resultClause = target.emplace_back();
			resultClause.reserve(clause.size());
			for (auto& lit : clause) {
				resultClause.push_back(base.MapLiteralBackward(lit, createBmcVariable));
			}

			if (trace) {
				std::cerr << "    Result Clause " << to_string(resultClause, 0u);
				std::cerr << " <=> Mapped (";
				size_t index { 0u };
				for (auto const& lit : clause) {
					if (index++ != 0) { std::cerr << ", "; }
					std::cerr << std::to_string(lit);
				}
				std::cerr << ")" << std::endl;
			}

			return true;
		}
	};

};

}
}
