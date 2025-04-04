// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include "bmc-ncip.hpp"
#include "bmc-ncip-backend.hpp"

#include <type_traits>
#include <iostream>
#include <fstream>

#ifdef INCLUDE_PREFIXED
#include <minicraig-2.2.0/craig/CraigSolver.h>
#include <minicraig-2.2.0/simp/SimpSolver.h>
#include <minicraig-2.2.0/preproc/Preprocessor.h>
#else /* INCLUDE_PREFIXED */
#include <craig/CraigSolver.h>
#include <simp/SimpSolver.h>
#include <preproc/Preprocessor.h>
#endif /* INCLUDE_PREFIXED */

namespace Ncip {
namespace Backend {

class MiniCraigVariableMapInterface {
public:
	using InterfaceType = MiniCraig::Lit;
	using InternalType = MiniCraig::Var;

	static InterfaceType InternalToInterface(const InternalType& internal, bool negated) {
		return MiniCraig::mkLit(internal, negated);
	}
	static InternalType InterfaceToInternal(const InterfaceType& interface) {
		return MiniCraig::var(interface);
	}
	static bool InterfaceIsNegated(const InterfaceType& interface) {
		return MiniCraig::sign(interface);
	}
	static size_t InternalHash(const InternalType& internal) {
		return std::hash<int>()(MiniCraig::toInt(internal));
	}
};

template<typename SolverTag> struct MiniCraigSolverBase { };
template<> struct MiniCraigSolverBase<PreSolverTag> { using Solver = MiniCraig::Preprocessor; };
template<> struct MiniCraigSolverBase<FpcSolverTag> { using Solver = MiniCraig::SimpSolver; };
template<> struct MiniCraigSolverBase<CraigSolverTag> { using Solver = MiniCraig::CraigSimpSolver; };

template<typename ImplTag, typename SolverTag>
class MiniCraigSolverInterface: public SolverVariableMap<ImplTag, SolverTag>, MiniCraigSolverBase<SolverTag>::Solver {
public:
	static constexpr const bool is_preprocessor = std::is_same_v<SolverTag, PreSolverTag>;
	static constexpr const bool is_fpc = std::is_same_v<SolverTag, FpcSolverTag>;
	static constexpr const bool is_craig = std::is_same_v<SolverTag, CraigSolverTag>;

	template<bool Enable = is_craig>
	std::enable_if_t<Enable, MiniCraig::CraigVarType> MapVariableType(BackendVariableType type) {
		switch (type) {
			case BackendVariableType::GLOBAL: return MiniCraig::CraigVarType::GLOBAL;
			case BackendVariableType::A_LOCAL: return MiniCraig::CraigVarType::A_LOCAL;
			case BackendVariableType::B_LOCAL: return MiniCraig::CraigVarType::B_LOCAL;
			case BackendVariableType::A_PROTECTED: return MiniCraig::CraigVarType::A_LOCAL;
			case BackendVariableType::B_PROTECTED: return MiniCraig::CraigVarType::B_LOCAL;
			default: assert(false);  __builtin_unreachable();
		}
	}

	template<bool Enable = is_craig>
	std::enable_if_t<Enable, MiniCraig::CraigClauseType> MapClauseType(BackendClauseType type) {
		switch (type) {
			case BackendClauseType::A_CLAUSE: return MiniCraig::CraigClauseType::A_CLAUSE;
			case BackendClauseType::B_CLAUSE: return MiniCraig::CraigClauseType::B_CLAUSE;
			default: assert(false);  __builtin_unreachable();
		}
	}

	MiniCraig::Var CreateVariable(BmcVariable variable, BackendVariableType type, bool trace=false) {
		MiniCraig::Var result;

		if constexpr (is_craig) {
			result = this->newVar(MapVariableType(type));
		} else {
			result = this->newVar();
		}

		auto isGlobal { (type == BackendVariableType::GLOBAL) };
		auto isProtected { (type == BackendVariableType::A_PROTECTED || type == BackendVariableType::B_PROTECTED) };
		if (isGlobal || isProtected) { this->setFrozen(result, true); }

		if (trace) {
			std::cerr << "    Add Variable " << to_string(variable)
				<< " <=> Mapped " << MiniCraig::toInt(MiniCraig::mkLit(result, false))
				<< " " << to_string(type) << std::endl;
		}

		return result;
	}

	template<typename Func>
	bool UnprotectVariable(BmcLiteral variable, BackendVariableType type, Func createVariable, bool trace=false) {
		auto const& mappings = this->GetForwardMappings();
		if (auto it = mappings.find(BmcVariable(variable)); it != mappings.end()) {
			auto mapped = this->InternalToInterface(it->second, variable.IsNegated());
			if (trace) {
				std::cerr << "    Unprotecting Variable " << to_string(variable)
					<< " <=> Mapped " << MiniCraig::toInt(mapped)
					<< " " << to_string(type) << std::endl;
			}

			this->setFrozen(MiniCraig::var(mapped), false);
		} else {
			if (trace) {
				std::cerr << "    Unprotecting Variable " << to_string(variable)
					<< " <=> Not Mapped " << " " << to_string(type) << std::endl;
			}
		}

		return true;
	}

	bool IsEliminated(BmcLiteral literal) {
		if (const auto it = this->GetForwardMappings().find(BmcVariable(literal)); it != this->GetForwardMappings().end()) {
			auto mapped = this->InternalToInterface(it->second, literal.IsNegated());
			return this->isEliminated(MiniCraig::var(mapped));
		}

		return true;
	}

	template<typename Func>
	bool AddClauses(BmcLiteral trigger, const BmcClauses& clauses, ssize_t shift, BackendClauseType type, Func createVariable, bool trace=false) {
		for (const auto& clause : clauses) {
			MiniCraig::vec<MiniCraig::Lit> solverClause(clause.size() + ((trigger != INVALID_LITERAL) ? 1 : 0));

			int index { 0 };
			if (trigger != INVALID_LITERAL) {
				solverClause[index++] = this->MapLiteralForward(-trigger, createVariable);
			}
			for (const auto& literal : clause) {
				solverClause[index++] = this->MapLiteralForward(literal >> shift, createVariable);
			}

			if (trace) {
				std::cerr << "    Clause " << to_string(clause, shift);
				std::cerr << " <=> Mapped " << to_string(type) << " (";
				for (int index { 0 }; index < solverClause.size(); index++) {
					if (index != 0u) std::cerr << ", ";
					std::cerr << MiniCraig::toInt(solverClause[index]);
				}
				std::cerr << ")" << std::endl;
			}

			if constexpr (is_craig) {
				if (!this->addClause(solverClause, MapClauseType(type))) return false;
			} else {
				if (!this->addClause(solverClause)) return false;
			}
		}

		return true;
	}

	template<typename Func>
	bool AddTrigger(const BmcLiteral& trigger, BackendClauseType type, Func createVariable, bool trace=false) {
		auto mapped { this->MapLiteralForward(trigger, createVariable) };

		if (trace) {
			std::cerr << "    Trigger " << to_string(trigger)
				<< " <=> Mapped " << MiniCraig::toInt(mapped) << std::endl;
		}

		this->setFrozen(MiniCraig::var(mapped), true);
		return true;
	}

	template<typename Func>
	bool PermanentlyDisableTrigger(const BmcLiteral& trigger, BackendClauseType type, Func createVariable, bool trace=false) {
		auto mapped { this->MapLiteralForward(trigger, createVariable) };

		if (trace) {
			std::cerr << "    Trigger " << to_string(trigger)
				<< " <=> Mapped " << MiniCraig::toInt(mapped) << std::endl;
		}

		this->setFrozen(MiniCraig::var(mapped), false);

		MiniCraig::vec<MiniCraig::Lit> clause;
		clause.push(~mapped);
		if constexpr (is_craig) {
			return this->addClause(clause, MapClauseType(type));
		} else {
			return this->addClause(clause);
		}
	}

	template<typename Func, bool Enable = !is_preprocessor>
	std::enable_if_t<Enable, bool> SolveWithAssumptions(const BmcClause& assumptions, Func createVariable, bool trace=false) {
		MiniCraig::vec<MiniCraig::Lit> solverAssumptions;
		solverAssumptions.capacity(assumptions.size());
		for (const auto& literal : assumptions) {
			solverAssumptions.push(this->MapLiteralForward(literal, createVariable));
		}

		if (trace) {
			for (size_t index { 0u }; index < assumptions.size(); index++) {
				std::cerr << "    Assuming " << to_string(assumptions[index], 0u)
					<< " <=> Mapped " << MiniCraig::toInt(solverAssumptions[index]) << std::endl;
			}
		}

		auto result { this->solve(solverAssumptions) };
		if (trace) { std::cerr << "    Result is " << (result ? "SAT" : "UNSAT") << std::endl; }
		return result;
	}

	template<typename Func>
	BmcAssignment GetSolvedLiteral(const BmcLiteral& literal, ssize_t shift, Func createVariable, bool trace=false) {
		auto mapped { this->MapLiteralForward(literal >> shift, createVariable) };
		auto assignment { this->model[MiniCraig::var(mapped)] };

		if (assignment == MiniCraig::l_Undef) { return BmcAssignment::DontCare; }
		return ((assignment == MiniCraig::l_True) ? BmcAssignment::Positive : BmcAssignment::Negative) ^ literal.IsNegated();
	}

	void Interrupt() {
		this->interrupt();
	}

	template<bool Enable = is_craig>
	std::enable_if_t<Enable, void> ConfigureCraigInterpolant(CraigInterpolant interpolant, bool enableCraig) {
		if (!enableCraig) {
			this->setCraigConstruction(MiniCraig::CraigConstruction::NONE);
			return;
		}

		switch (interpolant) {
			case CraigInterpolant::Symmetric:
				this->setCraigConstruction(MiniCraig::CraigConstruction::SYMMETRIC);
				break;
			case CraigInterpolant::Asymmetric:
				this->setCraigConstruction(MiniCraig::CraigConstruction::ASYMMETRIC);
				break;
			case CraigInterpolant::DualSymmetric:
				this->setCraigConstruction(MiniCraig::CraigConstruction::DUAL_SYMMETRIC);
				break;
			case CraigInterpolant::DualAsymmetric:
				this->setCraigConstruction(MiniCraig::CraigConstruction::DUAL_ASYMMETRIC);
				break;
			case CraigInterpolant::Intersection:
				this->setCraigConstruction(MiniCraig::CraigConstruction::ALL);
				break;
			case CraigInterpolant::Union:
				this->setCraigConstruction(MiniCraig::CraigConstruction::ALL);
				break;
			case CraigInterpolant::Smallest:
				this->setCraigConstruction(MiniCraig::CraigConstruction::ALL);
				break;
			case CraigInterpolant::Largest:
				this->setCraigConstruction(MiniCraig::CraigConstruction::ALL);
				break;
			default: __builtin_unreachable();
		}
	}

	template<typename Func, bool Enable = is_craig>
	std::enable_if_t<Enable, std::tuple<BmcClauses, BmcLiteral>> GetCraigInterpolant(CraigInterpolant interpolant, Func createVariable, bool trace=false) {
		MiniCraig::CraigInterpolant mapped { };
		switch (interpolant) {
			case CraigInterpolant::Symmetric: mapped = MiniCraig::CraigInterpolant::SYMMETRIC; break;
			case CraigInterpolant::Asymmetric: mapped = MiniCraig::CraigInterpolant::ASYMMETRIC; break;
			case CraigInterpolant::DualSymmetric: mapped = MiniCraig::CraigInterpolant::DUAL_SYMMETRIC; break;
			case CraigInterpolant::DualAsymmetric: mapped = MiniCraig::CraigInterpolant::DUAL_ASYMMETRIC; break;
			case CraigInterpolant::Intersection: mapped = MiniCraig::CraigInterpolant::INTERSECTION; break;
			case CraigInterpolant::Union: mapped = MiniCraig::CraigInterpolant::UNION; break;
			case CraigInterpolant::Smallest: mapped = MiniCraig::CraigInterpolant::SMALLEST; break;
			case CraigInterpolant::Largest: mapped = MiniCraig::CraigInterpolant::LARGEST; break;
			default: __builtin_unreachable();
		}

		MiniCraig::Var nextVar { this->nVars() };
		MiniCraig::vec<MiniCraig::vec<MiniCraig::Lit>> craigCnf { };
		MiniCraig::Lit cnfRoot = MiniCraig::lit_Undef;

		auto cnfType { this->createCraigCnf(mapped, craigCnf, nextVar) };
		switch (cnfType) {
			case MiniCraig::CraigCnfType::Constant0:
			case MiniCraig::CraigCnfType::Constant1:
				cnfRoot = MiniCraig::mkLit(nextVar++, false);
				craigCnf.clear();
				craigCnf.push();
				craigCnf[0].push((cnfType == MiniCraig::CraigCnfType::Constant0) ? ~cnfRoot : cnfRoot);
				break;

			case MiniCraig::CraigCnfType::Normal:
				cnfRoot = craigCnf.last()[0];
				craigCnf.pop();
				break;

			default: __builtin_unreachable();
		}
		while (this->nVars() < nextVar) {
			auto var = this->newVar(MiniCraig::CraigVarType::A_LOCAL);
			this->MapLiteralBackward(MiniCraig::mkLit(var, false), createVariable);
		}

		if (trace) {
			switch (cnfType) {
				case MiniCraig::CraigCnfType::Constant0: std::cerr << "    Constant 0" << std::endl; break;
				case MiniCraig::CraigCnfType::Constant1: std::cerr << "    Constant 1" << std::endl; break;
				case MiniCraig::CraigCnfType::Normal: std::cerr << "    Normal" << std::endl; break;
				default: __builtin_unreachable();
			}
		}

		// Map the craig interpolant back to the BMC literals and create new literals
		// for generated Tseitin variables.
		BmcClauses craig;
		craig.reserve(craigCnf.size());
		for (int clauseIndex { 0 }; clauseIndex < craigCnf.size(); clauseIndex++) {
			BmcClause clause { };
			clause.reserve(craigCnf[clauseIndex].size());
			for (int literalIndex { 0 }; literalIndex < craigCnf[clauseIndex].size(); literalIndex++) {
				const auto& literal { craigCnf[clauseIndex][literalIndex] };
				clause.push_back(this->MapLiteralBackward(literal, createVariable));
			}

			if (trace) {
				std::cerr << "    Clause " << to_string(clause, 0u);
				std::cerr << " <=> Mapped (";
				for (int index { 0 }; index < craigCnf[clauseIndex].size(); index++) {
					if (index != 0u) std::cerr << ", ";
					std::cerr << MiniCraig::toInt(craigCnf[clauseIndex][index]);
				}
				std::cerr << ")" << std::endl;
			}

			craig.push_back(clause);
		}

		return { craig, this->MapLiteralBackward(cnfRoot, createVariable) };
	}

	template<typename Func1, typename Func2, bool Enable = is_preprocessor>
	std::enable_if_t<Enable, BmcClauses> PreprocessClauses(const BmcClauses& clauses, const std::vector<bool>& globalVars, const BmcLiteral& root, const PreprocessLevel& level, Func1 createSolverVariable, Func2 createBmcVariable, bool trace=false) {
		this->setResLength((clauses.size() > 500000) ? 15 : 20);
		this->setUseAsymm(level >= PreprocessLevel::Expensive);
		this->setUseImpl(level >= PreprocessLevel::Expensive);
		this->setUseUpla(true);
		this->setUseBce(false);
		this->setUseRcheck(false);

		MiniCraig::vec<MiniCraig::Lit> preClause;
		for (auto const& clause : clauses) {
			preClause.capacity(clause.size());
			for (auto const& lit : clause) {
				preClause.push(this->MapLiteralForward(lit, createSolverVariable));
			}

			if (trace) {
				std::cerr << "    Input Clause " << to_string(clause, 0u);
				std::cerr << " <=> Mapped (";
				for (int index { 0 }; index < preClause.size(); index++) {
					if (index != 0u) std::cerr << ", ";
					std::cerr << MiniCraig::toInt(preClause[index]);
				}
				std::cerr << ")" << std::endl;
			}

			this->addClause(preClause);
			preClause.clear();
		}

		// Has to happen after clauses have been added.
		// Relies on GetForwardMappings to contain all mappings.
		if (trace) { std::cerr << "  - Freezing protected variables" << std::endl; }
		MiniCraig::vec<MiniCraig::Lit> frozenVars;
		for (auto [bmcVar, preVar] : this->GetForwardMappings()) {
			if ((bmcVar.GetId() < globalVars.size()) && globalVars[bmcVar.GetId()]) {
				frozenVars.push(MiniCraig::mkLit(preVar, false));
				if (trace) { std::cerr << "    Freezing global " << to_string(bmcVar) << " <=> Mapped " << MiniCraig::toInt(preVar) << std::endl; }
			}
		}
		if (root != INVALID_LITERAL) {
			auto mapped { this->MapLiteralForward(root, createSolverVariable) };
			if (trace) { std::cerr << "    Freezing root " << to_string(root) << " <=> Mapped " << MiniCraig::toInt(mapped) << std::endl; }
			frozenVars.push(mapped);
		} else {
			if (trace) { std::cerr << "    No root to freeze" << std::endl; }
		}

		if (trace) { std::cerr << "  - Preprocessing" << std::endl; }
		bool result { this->preprocess(frozenVars) };
		if (!result) {
			if (trace) { std::cerr << "    Result Constant 0" << std::endl; }
			return { (root != INVALID_LITERAL) ? BmcClause {-root} : BmcClause {} };
		}

		if (trace) { std::cerr << "  - Extracting resulting clauses" << std::endl; }
		MiniCraig::vec<MiniCraig::vec<MiniCraig::Lit>> preClauses;
		this->getSimplifiedClauses(preClauses);

		BmcClauses resultClauses;
		resultClauses.reserve(preClauses.size());
		for (int clauseIndex { 0 }; clauseIndex < preClauses.size(); clauseIndex++) {
			auto& resultClause = resultClauses.emplace_back();
			resultClause.reserve(preClauses[clauseIndex].size());
			for (int index { 0 }; index < preClauses[clauseIndex].size(); index++) {
				resultClause.push_back(this->MapLiteralBackward(preClauses[clauseIndex][index], createBmcVariable));
			}

			if (trace) {
				std::cerr << "    Result Clause " << to_string(resultClause, 0u);
				std::cerr << " <=> Mapped (";
				for (int index { 0 }; index < preClauses[clauseIndex].size(); index++) {
					if (index != 0u) std::cerr << ", ";
					std::cerr << MiniCraig::toInt(preClauses[clauseIndex][index]);
				}
				std::cerr << ")" << std::endl;
			}
		}
		return resultClauses;
	}

};

}
}
