// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once
#ifndef NDEBUG

#include "bmc-ncip.hpp"
#include "bmc-ncip-backend.hpp"

#include <type_traits>
#include <iostream>
#include <fstream>

#if __has_include(<minicraig-2.2.0/craig/CraigSolver.h>)
#include <minicraig-2.2.0/craig/CraigSolver.h>
#include <minicraig-2.2.0/simp/SimpSolver.h>
#include <minicraig-2.2.0/preproc/Preprocessor.h>
#else
#include <craig/CraigSolver.h>
#include <simp/SimpSolver.h>
#include <preproc/Preprocessor.h>
#endif

namespace Ncip {
namespace Backend {

std::string to_string_trace(const MiniCraig::CraigCnfType& cnf_type) {
  if (cnf_type == MiniCraig::CraigCnfType::None) return "MiniCraig::CraigCnfType::None";
  if (cnf_type == MiniCraig::CraigCnfType::Constant0) return "MiniCraig::CraigCnfType::Constant0";
  if (cnf_type == MiniCraig::CraigCnfType::Constant1) return "MiniCraig::CraigCnfType::Constant1";
  if (cnf_type == MiniCraig::CraigCnfType::Normal) return "MiniCraig::CraigCnfType::Normal";
  __builtin_unreachable();
}

std::string to_string_trace(const MiniCraig::CraigConstruction& construction_type) {
  if (construction_type == MiniCraig::CraigConstruction::NONE) return "MiniCraig::CraigConstruction::NONE";
  if (construction_type == MiniCraig::CraigConstruction::ALL) return "MiniCraig::CraigConstruction::ALL";
  std::string result;
  if (static_cast<int>(construction_type) & static_cast<int>(MiniCraig::CraigConstruction::SYMMETRIC)) result += "|MiniCraig::CraigConstruction::SYMMETRIC";
  if (static_cast<int>(construction_type) & static_cast<int>(MiniCraig::CraigConstruction::ASYMMETRIC)) result += "|MiniCraig::CraigConstruction::ASYMMETRIC";
  if (static_cast<int>(construction_type) & static_cast<int>(MiniCraig::CraigConstruction::DUAL_SYMMETRIC)) result += "|MiniCraig::CraigConstruction::DUAL_SYMMETRIC";
  if (static_cast<int>(construction_type) & static_cast<int>(MiniCraig::CraigConstruction::DUAL_ASYMMETRIC)) result += "|MiniCraig::CraigConstruction::DUAL_ASYMMETRIC";
  return result.substr(1);
}

std::string to_string_trace(const MiniCraig::CraigInterpolant& interpolant_type) {
  if (interpolant_type == MiniCraig::CraigInterpolant::NONE) return "MiniCraig::CraigInterpolant::NONE";
  if (interpolant_type == MiniCraig::CraigInterpolant::SYMMETRIC) return "MiniCraig::CraigInterpolant::SYMMETRIC";
  if (interpolant_type == MiniCraig::CraigInterpolant::ASYMMETRIC) return "MiniCraig::CraigInterpolant::ASYMMETRIC";
  if (interpolant_type == MiniCraig::CraigInterpolant::DUAL_SYMMETRIC) return "MiniCraig::CraigInterpolant::DUAL_SYMMETRIC";
  if (interpolant_type == MiniCraig::CraigInterpolant::DUAL_ASYMMETRIC) return "MiniCraig::CraigInterpolant::DUAL_ASYMMETRIC";
  if (interpolant_type == MiniCraig::CraigInterpolant::INTERSECTION) return "MiniCraig::CraigInterpolant::INTERSECTION";
  if (interpolant_type == MiniCraig::CraigInterpolant::UNION) return "MiniCraig::CraigInterpolant::UNION";
  if (interpolant_type == MiniCraig::CraigInterpolant::SMALLEST) return "MiniCraig::CraigInterpolant::SMALLEST";
  if (interpolant_type == MiniCraig::CraigInterpolant::LARGEST) return "MiniCraig::CraigInterpolant::LARGEST";
  __builtin_unreachable();
}

std::string to_string_trace(const MiniCraig::CraigVarType& var_type) {
  if (var_type == MiniCraig::CraigVarType::A_LOCAL) return "MiniCraig::CraigVarType::A_LOCAL";
  if (var_type == MiniCraig::CraigVarType::B_LOCAL) return "MiniCraig::CraigVarType::B_LOCAL";
  if (var_type == MiniCraig::CraigVarType::GLOBAL) return "MiniCraig::CraigVarType::GLOBAL";
  __builtin_unreachable();
}

std::string to_string_problem(const MiniCraig::CraigVarType& var_type) {
  if (var_type == MiniCraig::CraigVarType::A_LOCAL) return "a";
  if (var_type == MiniCraig::CraigVarType::B_LOCAL) return "b";
  if (var_type == MiniCraig::CraigVarType::GLOBAL) return "g";
  __builtin_unreachable();
}

std::string to_string_trace(const MiniCraig::CraigClauseType& clause_type) {
  if (clause_type == MiniCraig::CraigClauseType::A_CLAUSE) return "MiniCraig::CraigClauseType::A_CLAUSE";
  if (clause_type == MiniCraig::CraigClauseType::B_CLAUSE) return "MiniCraig::CraigClauseType::B_CLAUSE";
  if (clause_type == MiniCraig::CraigClauseType::L_CLAUSE) return "MiniCraig::CraigClauseType::L_CLAUSE";
  __builtin_unreachable();
}

std::string to_string_problem(const MiniCraig::CraigClauseType& clause_type) {
  if (clause_type == MiniCraig::CraigClauseType::A_CLAUSE) return "A";
  if (clause_type == MiniCraig::CraigClauseType::B_CLAUSE) return "B";
  __builtin_unreachable();
}

std::string to_string_trace(const MiniCraig::lbool& value) {
  if (value == MiniCraig::l_True) return "MiniCraig::l_True";
  if (value == MiniCraig::l_False) return "MiniCraig::l_False";
  if (value == MiniCraig::l_Undef) return "MiniCraig::l_Undef";
  __builtin_unreachable();
}

ssize_t to_signed(size_t literal) {
	return static_cast<ssize_t>((literal >> 1) + 1) * ((literal & 1) ? -1 : 1);
}

class MiniCraigDebugVariableMapInterface {
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

template<typename SolverTag> struct MiniCraigDebugSolverBase { };
template<> struct MiniCraigDebugSolverBase<PreSolverTag> { using Solver = MiniCraig::Preprocessor; };
template<> struct MiniCraigDebugSolverBase<FpcSolverTag> { using Solver = MiniCraig::SimpSolver; };
template<> struct MiniCraigDebugSolverBase<CraigSolverTag> { using Solver = MiniCraig::CraigSimpSolver; };

template<typename ImplTag, typename SolverTag>
class MiniCraigDebugSolverInterface: public SolverVariableMap<ImplTag, SolverTag>, MiniCraigDebugSolverBase<SolverTag>::Solver {
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

	MiniCraigDebugSolverInterface() {
		if constexpr (is_craig) {
			std::cerr << "-T- #include <cassert>" << std::endl;
			std::cerr << "-T- #include <iostream>" << std::endl;
#if __has_include(<minicraig-2.2.0/craig/CraigSolver.h>)
			std::cerr << "-T- #include <minicraig-2.2.0/craig/CraigSolver.h>" << std::endl;
			std::cerr << "-T- #include <minicraig-2.2.0/simp/SimpSolver.h>" << std::endl;
			std::cerr << "-T- #include <minicraig-2.2.0/preproc/Preprocessor.h>" << std::endl;
#else
			std::cerr << "-T- #include <craig/CraigSolver.h>" << std::endl;
			std::cerr << "-T- #include <simp/SimpSolver.h>" << std::endl;
			std::cerr << "-T- #include <preproc/Preprocessor.h>" << std::endl;
#endif
			std::cerr << "-T- " << std::endl;
			std::cerr << "-T- ssize_t to_signed(size_t literal) {" << std::endl;
			std::cerr << "-T-   return static_cast<ssize_t>((literal >> 1) + 1) * ((literal & 1) ? -1 : 1);" << std::endl;
			std::cerr << "-T- }" << std::endl;
			std::cerr << "-T- " << std::endl;
			std::cerr << "-T- int solves = 0;" << std::endl;
			std::cerr << "-T- int id = 0;" << std::endl;
			std::cerr << "-T- int main() {" << std::endl;
			std::cerr << "-T-   auto* solver { new MiniCraig::CraigSimpSolver() };" << std::endl;
		}
	}

	virtual ~MiniCraigDebugSolverInterface() {
		if constexpr (is_craig) {
			std::cerr << "-T-   delete solver;" << std::endl;
			std::cerr << "-T- }" << std::endl;
		}
	}

	int solves = 0;

	MiniCraig::Var CreateVariable(BmcVariable variable, BackendVariableType type, bool trace=false) {
		MiniCraig::Var result;

		if constexpr (is_craig) {
			result = this->newVar(MapVariableType(type));
			std::cerr << "-T-   auto var" << std::to_string(MiniCraig::toInt(result)) << " = solver->newVar(" << to_string_trace(MapVariableType(type)) << ");";
			std::cerr << " std::cout << \"P\" << std::to_string(solves) << \" " << to_string_problem(MapVariableType(type))
				<< " \" << std::to_string(id + 1) << \" 0\" << std::endl; id++;" << std::endl;
			std::cerr << "-T-   assert(var" << std::to_string(result) << " == " << std::to_string(MiniCraig::toInt(result)) << ");" << std::endl;
		} else {
			result = this->newVar();
		}

		auto isGlobal { (type == BackendVariableType::GLOBAL) };
		auto isProtected { (type == BackendVariableType::A_PROTECTED || type == BackendVariableType::B_PROTECTED) };
		if (isGlobal || isProtected) { this->setFrozen(result, true); }

		if constexpr (is_craig) {
			if (isGlobal || isProtected) {
				std::cerr << "-T-   solver->setFrozen(var" << std::to_string(MiniCraig::toInt(result)) << ", true);" << std::endl;
			}
		}

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
			if constexpr (is_craig) {
				std::cerr << "-T-   solver->setFrozen(var" << std::to_string(MiniCraig::toInt(MiniCraig::var(mapped))) << ", false);" << std::endl;
			}
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
				std::cerr << "-T-   {";
				std::cerr << "MiniCraig::vec<MiniCraig::Lit> clause(" << solverClause.size() << ");";
				for (int index { 0 }; index < solverClause.size(); index++) {
					std::cerr << " clause[" << std::to_string(index) << "] = MiniCraig::mkLit(var" << std::to_string(MiniCraig::var(solverClause[index]))
						<< ", " << std::to_string(MiniCraig::sign(solverClause[index])) << ");";
				}
				std::cerr << " auto result = solver->addClause(clause, " << to_string_trace(MapClauseType(type)) << ");";
				std::cerr << " std::cout << \"P\" << std::to_string(solves) << \" " << to_string_problem(MapClauseType(type)) << "\";";
				std::cerr << " for (int index { 0 }; index < clause.size(); index++) {";
				std::cerr << "   std::cout << \" \" << std::to_string(to_signed(MiniCraig::toInt(clause[index])));";
				std::cerr << " }";
				std::cerr << " std::cout << \" 0\" << std::endl;";
				std::cerr << "}" << std::endl;

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

		if constexpr (is_craig) {
			std::cerr << "-T-   solver->setFrozen(var" << std::to_string(MiniCraig::toInt(MiniCraig::var(mapped))) << ", true);" << std::endl;
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

		if constexpr (is_craig) {
			std::cerr << "-T-   solver->setFrozen(MiniCraig::var(MiniCraig::toLit(" << std::to_string(MiniCraig::toInt(mapped)) << ")), false);" << std::endl;
		}
		this->setFrozen(MiniCraig::var(mapped), false);

		MiniCraig::vec<MiniCraig::Lit> clause;
		clause.push(~mapped);
		if constexpr (is_craig) {
			auto result = this->addClause(clause, MapClauseType(type));
			std::cerr << "-T-   {" << std::endl;
			std::cerr << "-T-     MiniCraig::vec<MiniCraig::Lit> solverClause(1);" << std::endl;
			std::cerr << "-T-     solverClause[0] = MiniCraig::mkLit(var" << std::to_string(MiniCraig::toInt(MiniCraig::var(mapped))) << ", true);" << std::endl;
			std::cerr << "-T-     assert(solver->addClause(solverClause, " << to_string_trace(MapClauseType(type)) << ") == " << std::to_string(result) << ");" << std::endl;
			std::cerr << "-T-   }" << std::endl;
			return result;
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
		if constexpr (is_craig) {
			std::cerr << "-T-   {";
			std::cerr << " MiniCraig::vec<MiniCraig::Lit> assumptions(" << solverAssumptions.size() << ");";
			for (int index { 0 }; index < solverAssumptions.size(); index++) {
				std::cerr << " assumptions[" << std::to_string(index) << "] = MiniCraig::toLit(" << std::to_string(MiniCraig::toInt(solverAssumptions[index])) << ");";
			}
			std::cerr << " assert(solver->solve(assumptions) == " << std::to_string(result) << ");";
			std::cerr << " std::cout << \"S\" << std::to_string(solves) << \" f\";";
			std::cerr << " for (int index { 0 }; index < assumptions.size(); index++) {";
			std::cerr << "   std::cout << \" \" << std::to_string(to_signed(MiniCraig::toInt(assumptions[index])));";
			std::cerr << " }";
			std::cerr << " std::cout << \" 0\" << std::endl; solves++;";
			std::cerr << "}" << std::endl;
			solves++;
		}
		if (trace) { std::cerr << "    Result is " << (result ? "SAT" : "UNSAT") << std::endl; }
		return result;
	}

	template<typename Func>
	BmcAssignment GetSolvedLiteral(const BmcLiteral& literal, ssize_t shift, Func createVariable, bool trace=false) {
		auto mapped { this->MapLiteralForward(literal >> shift, createVariable) };
		auto assignment { this->model[MiniCraig::var(mapped)] };

		if constexpr (is_craig) {
			//std::cerr << "-T-   assert(solver->model[" << std::to_string(MiniCraig::var(mapped)) << "] == " << to_string_trace(assignment) << ");" << std::endl;
		}

		if (assignment == MiniCraig::l_Undef) { return BmcAssignment::DontCare; }
		return ((assignment == MiniCraig::l_True) ? BmcAssignment::Positive : BmcAssignment::Negative) ^ literal.IsNegated();
	}

	void Interrupt() {
		this->interrupt();
	}

	template<bool Enable = is_craig>
	std::enable_if_t<Enable, void> ConfigureCraigInterpolant(CraigInterpolant interpolant, bool enableCraig) {
		if (!enableCraig) {
			std::cerr << "-T-   solver->setCraigConstruction(" << to_string_trace(MiniCraig::CraigConstruction::NONE) << ");" << std::endl;
			this->setCraigConstruction(MiniCraig::CraigConstruction::NONE);
			return;
		}

		switch (interpolant) {
			case CraigInterpolant::Symmetric:
				std::cerr << "-T-   solver->setCraigConstruction(" << to_string_trace(MiniCraig::CraigConstruction::SYMMETRIC) << ");" << std::endl;
				this->setCraigConstruction(MiniCraig::CraigConstruction::SYMMETRIC);
				break;
			case CraigInterpolant::Asymmetric:
				std::cerr << "-T-   solver->setCraigConstruction(" << to_string_trace(MiniCraig::CraigConstruction::ASYMMETRIC) << ");" << std::endl;
				this->setCraigConstruction(MiniCraig::CraigConstruction::ASYMMETRIC);
				break;
			case CraigInterpolant::DualSymmetric:
				std::cerr << "-T-   solver->setCraigConstruction(" << to_string_trace(MiniCraig::CraigConstruction::DUAL_SYMMETRIC) << ");" << std::endl;
				this->setCraigConstruction(MiniCraig::CraigConstruction::DUAL_SYMMETRIC);
				break;
			case CraigInterpolant::DualAsymmetric:
				std::cerr << "-T-   solver->setCraigConstruction(" << to_string_trace(MiniCraig::CraigConstruction::DUAL_ASYMMETRIC) << ");" << std::endl;
				this->setCraigConstruction(MiniCraig::CraigConstruction::DUAL_ASYMMETRIC);
				break;
			case CraigInterpolant::Intersection:
				std::cerr << "-T-   solver->setCraigConstruction(" << to_string_trace(MiniCraig::CraigConstruction::ALL) << ");" << std::endl;
				this->setCraigConstruction(MiniCraig::CraigConstruction::ALL);
				break;
			case CraigInterpolant::Union:
				std::cerr << "-T-   solver->setCraigConstruction(" << to_string_trace(MiniCraig::CraigConstruction::ALL) << ");" << std::endl;
				this->setCraigConstruction(MiniCraig::CraigConstruction::ALL);
				break;
			case CraigInterpolant::Smallest:
				std::cerr << "-T-   solver->setCraigConstruction(" << to_string_trace(MiniCraig::CraigConstruction::ALL) << ");" << std::endl;
				this->setCraigConstruction(MiniCraig::CraigConstruction::ALL);
				break;
			case CraigInterpolant::Largest:
				std::cerr << "-T-   solver->setCraigConstruction(" << to_string_trace(MiniCraig::CraigConstruction::ALL) << ");" << std::endl;
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
		std::cerr << "-T-   {" << std::endl;
		std::cerr << "-T-     assert(solver->nVars() == " << std::to_string(this->nVars()) << ");" << std::endl;
		std::cerr << "-T-     MiniCraig::Var nextVar { solver->nVars() };" << std::endl;
		std::cerr << "-T-     MiniCraig::vec<MiniCraig::vec<MiniCraig::Lit>> craigCnf { };" << std::endl;
		std::cerr << "-T-     auto cnfType { solver->createCraigCnf(" << to_string_trace(mapped) << ", craigCnf, nextVar) };" << std::endl;
		std::cerr << "-T-     assert(craigCnf.size() == " << craigCnf.size() << ");" << std::endl;
		std::cerr << "-T-     for (int i = 0; i < craigCnf.size(); i++) {" << std::endl;
		std::cerr << "-T-     	std::cout << \"C\" << std::to_string(solves) << \" A\";" << std::endl;
		std::cerr << "-T-     	for (int j = 0; j < craigCnf[i].size(); j++) {" << std::endl;
		std::cerr << "-T-         std::cout << \" \" << std::to_string(to_signed(MiniCraig::toInt(craigCnf[i][j])));" << std::endl;
		std::cerr << "-T-       }" << std::endl;
		std::cerr << "-T-       std::cout << \" 0\" << std::endl;" << std::endl;
		std::cerr << "-T-     }" << std::endl;
		for (int i { 0 }; i < craigCnf.size(); i++) {
			std::cerr << "-T-     assert(craigCnf[" << std::to_string(i) << "].size() == " << craigCnf[i].size() << ");" << std::endl;
			for (int j { 0 }; j < craigCnf[i].size(); j++) {
				std::cerr << "-T-     assert(MiniCraig::toInt(craigCnf[" << std::to_string(i) << "][" << std::to_string(j) << "]) == " << MiniCraig::toInt(craigCnf[i][j]) << ");" << std::endl;
			}
		}
		std::cerr << "-T-     assert(nextVar == " << nextVar << ");" << std::endl;
		std::cerr << "-T-     while (solver->nVars() < nextVar) {" << std::endl;
		std::cerr << "-T-       auto var = solver->newVar(MiniCraig::CraigVarType::A_LOCAL);" << std::endl;
		std::cerr << "-T-       std::cout << \"C\" << std::to_string(solves) << \" g \" << std::to_string(var + 1) << \" 0\" << std::endl;" << std::endl;
		std::cerr << "-T-     }" << std::endl;
		std::cerr << "-T-   }" << std::endl;

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

#endif