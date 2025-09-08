// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once
#ifndef NDEBUG

#include "bmc-ncip.hpp"
#include "bmc-ncip-backend.hpp"

#include <atomic>
#include <cmath>
#include <iostream>
#include <type_traits>
#include <csignal>

#if __has_include(<cadical/cadical.hpp>)
#include <cadical/cadical.hpp>
#include <cadicraig/craigtracer.hpp>
#else
#include <cadical.hpp>
#include <craigtracer.hpp>
#endif

namespace Ncip {
namespace Backend {


std::string to_string_trace(const CaDiCraig::CraigCnfType& cnf_type) {
  if (cnf_type == CaDiCraig::CraigCnfType::NONE) return "CaDiCraig::CraigCnfType::NONE";
  if (cnf_type == CaDiCraig::CraigCnfType::CONSTANT0) return "CaDiCraig::CraigCnfType::CONSTANT0";
  if (cnf_type == CaDiCraig::CraigCnfType::CONSTANT1) return "CaDiCraig::CraigCnfType::CONSTANT1";
  if (cnf_type == CaDiCraig::CraigCnfType::NORMAL) return "CaDiCraig::CraigCnfType::NORMAL";
  __builtin_unreachable();
}

std::string to_string_trace(const CaDiCraig::CraigConstruction& construction_type) {
  if (construction_type == CaDiCraig::CraigConstruction::NONE) return "CaDiCraig::CraigConstruction::NONE";
  if (construction_type == CaDiCraig::CraigConstruction::ALL) return "CaDiCraig::CraigConstruction::ALL";
  std::string result;
  if (static_cast<int>(construction_type) & static_cast<int>(CaDiCraig::CraigConstruction::SYMMETRIC)) result += "|CaDiCraig::CraigConstruction::SYMMETRIC";
  if (static_cast<int>(construction_type) & static_cast<int>(CaDiCraig::CraigConstruction::ASYMMETRIC)) result += "|CaDiCraig::CraigConstruction::ASYMMETRIC";
  if (static_cast<int>(construction_type) & static_cast<int>(CaDiCraig::CraigConstruction::DUAL_SYMMETRIC)) result += "|CaDiCraig::CraigConstruction::DUAL_SYMMETRIC";
  if (static_cast<int>(construction_type) & static_cast<int>(CaDiCraig::CraigConstruction::DUAL_ASYMMETRIC)) result += "|CaDiCraig::CraigConstruction::DUAL_ASYMMETRIC";
  return result.substr(1);
}

std::string to_string_trace(const CaDiCraig::CraigInterpolant& interpolant_type) {
  if (interpolant_type == CaDiCraig::CraigInterpolant::NONE) return "CaDiCraig::CraigInterpolant::NONE";
  if (interpolant_type == CaDiCraig::CraigInterpolant::SYMMETRIC) return "CaDiCraig::CraigInterpolant::SYMMETRIC";
  if (interpolant_type == CaDiCraig::CraigInterpolant::ASYMMETRIC) return "CaDiCraig::CraigInterpolant::ASYMMETRIC";
  if (interpolant_type == CaDiCraig::CraigInterpolant::DUAL_SYMMETRIC) return "CaDiCraig::CraigInterpolant::DUAL_SYMMETRIC";
  if (interpolant_type == CaDiCraig::CraigInterpolant::DUAL_ASYMMETRIC) return "CaDiCraig::CraigInterpolant::DUAL_ASYMMETRIC";
  if (interpolant_type == CaDiCraig::CraigInterpolant::INTERSECTION) return "CaDiCraig::CraigInterpolant::INTERSECTION";
  if (interpolant_type == CaDiCraig::CraigInterpolant::UNION) return "CaDiCraig::CraigInterpolant::UNION";
  if (interpolant_type == CaDiCraig::CraigInterpolant::SMALLEST) return "CaDiCraig::CraigInterpolant::SMALLEST";
  if (interpolant_type == CaDiCraig::CraigInterpolant::LARGEST) return "CaDiCraig::CraigInterpolant::LARGEST";
  __builtin_unreachable();
}

std::string to_string_trace(const CaDiCraig::CraigVarType& var_type) {
  if (var_type == CaDiCraig::CraigVarType::A_LOCAL) return "CaDiCraig::CraigVarType::A_LOCAL";
  if (var_type == CaDiCraig::CraigVarType::B_LOCAL) return "CaDiCraig::CraigVarType::B_LOCAL";
  if (var_type == CaDiCraig::CraigVarType::GLOBAL) return "CaDiCraig::CraigVarType::GLOBAL";
  __builtin_unreachable();
}

std::string to_string_trace(const CaDiCraig::CraigClauseType& clause_type) {
  if (clause_type == CaDiCraig::CraigClauseType::A_CLAUSE) return "CaDiCraig::CraigClauseType::A_CLAUSE";
  if (clause_type == CaDiCraig::CraigClauseType::B_CLAUSE) return "CaDiCraig::CraigClauseType::B_CLAUSE";
  if (clause_type == CaDiCraig::CraigClauseType::L_CLAUSE) return "CaDiCraig::CraigClauseType::L_CLAUSE";
  __builtin_unreachable();
}

class CadiCraigDebugVariableMapInterface {
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
class CadiCraigDebugSolverInterface: public SolverVariableMap<ImplTag, SolverTag>, CaDiCaL::Solver, CaDiCaL::Terminator {
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

	CadiCraigDebugSolverInterface(): nextVar(1), nextClause(1), interrupted(false), craigTracer(nullptr) {
		this->connect_terminator(this);

		this->set("quiet", 1);
		this->set("factor", 0); // BVA breaks Craig interpolation

		if constexpr (is_preprocessor) {
			this->set("decompose", 0); // Very expensive
			this->set("congruence", 0); // Very expensive
		} else if constexpr (is_fpc)  {
			; // Nothing
		} else if constexpr (is_craig) {
			this->set("probe", 0); // Increases Craig size
			this->set("ternary", 0); // Increases Craig size
			//this->set("minimize", 0);

			std::cerr << "-T- #include <cassert>" << std::endl;
			std::cerr << "-T- #include <vector>" << std::endl;
			std::cerr << "-T- #include <cadical.hpp>" << std::endl;
			std::cerr << "-T- #include <craigtracer.hpp>" << std::endl;
			std::cerr << "-T- " << std::endl;
			std::cerr << "-T- int main() {" << std::endl;
			std::cerr << "-T-   auto* cadical { new CaDiCaL::Solver() };" << std::endl;
			std::cerr << "-T-   cadical->set(\"factor\", 0);" << std::endl;
			std::cerr << "-T-   cadical->set(\"probe\", 0);" << std::endl;
			std::cerr << "-T-   cadical->set(\"ternary\", 0);" << std::endl;
			std::cerr << "-T-   auto* craigTracer { new CaDiCraig::CraigTracer() };" << std::endl;
			std::cerr << "-T-   cadical->connect_proof_tracer(craigTracer, true);" << std::endl;
			craigTracer = new CaDiCraig::CraigTracer();
			this->connect_proof_tracer(craigTracer, true);
		}
	}

	virtual ~CadiCraigDebugSolverInterface() {
		this->disconnect_terminator();

		if constexpr (is_preprocessor) {
			; // Nothing
		} else if constexpr (is_fpc)  {
			; // Nothing
		} else if constexpr (is_craig) {
			std::cerr << "-T-   cadical->disconnect_proof_tracer(craigTracer);" << std::endl;
			std::cerr << "-T-   delete craigTracer;" << std::endl;
			std::cerr << "-T-   delete cadical;" << std::endl;
			std::cerr << "-T- }" << std::endl;
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
			std::cerr << "-T-   craigTracer->label_variable(" << mapped << ", " << to_string_trace(MapVariableType(type)) << ");" << std::endl;
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
		auto mapped { this->MapLiteralForward(variable, createVariable) };

		// CaDiCal doesn't require freezing, clauses and variables will be restored automatically.
		if (trace) {
			std::cerr << "    Unprotecting Variable " << to_string(variable)
				<< " <=> Mapped " << std::to_string(mapped)
				<< " " << to_string(type) << std::endl;
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
				std::cerr << "-T-   craigTracer->label_clause(" << nextClause << ", " << to_string_trace(MapClauseType(type)) << ");" << std::endl;
				craigTracer->label_clause(nextClause++, MapClauseType(type));
				std::cerr << "-T-   ";
				for (auto const& literal : solverClause) {
					std::cerr << "cadical->add(" << literal << "); ";
				}
				std::cerr << "cadical->add(0);" << std::endl;
			}
			for (auto const& literal : solverClause) {
				this->add(literal);
			}
			this->add(0);
		}

		return true;
	}

	template<typename Func>
	bool AddTrigger(const BmcLiteral& newTrigger, BackendClauseType type, Func createVariable, bool trace=false) {
		auto mapped { this->MapLiteralForward(newTrigger, createVariable) };

		// CaDiCal doesn't require freezing, clauses and variables will be restored automatically.
		if (trace) {
			std::cerr << "    Trigger " << to_string(newTrigger)
				<< " <=> Mapped " << mapped << std::endl;
		}

		return true;
	}

	template<typename Func>
	bool PermanentlyDisableTrigger(const BmcLiteral& newTrigger, BackendClauseType type, Func createVariable, bool trace=false) {
		auto mapped { this->MapLiteralForward(newTrigger, createVariable) };

		if (trace) {
			std::cerr << "    Trigger " << to_string(newTrigger)
				<< " <=> Mapped " << mapped << std::endl;
		}

		// CaDiCal doesn't require freezing, clauses and variables will be restored automatically.
		if constexpr (is_craig) {
			std::cerr << "-T-   craigTracer->label_clause(" << nextClause << ", " << to_string_trace(MapClauseType(type)) << ");" << std::endl;
			craigTracer->label_clause(nextClause++, MapClauseType(type));
			std::cerr << "-T-   cadical->add(" << -mapped << "); cadical->add(0);" << std::endl;
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

		for (auto const& assumption : solverAssumptions) {
			if constexpr (is_craig) {
				std::cerr << "-T-   cadical->assume(" << assumption << ");" << std::endl;
			}
			this->assume(assumption);
		}

		auto result { this->solve() };
		if (trace) { std::cerr << "    Result is " << ((result == 10) ? "SAT" : ((result == 20) ? "UNSAT" : "TIMEOUT")) << std::endl; }

		if constexpr (is_craig) {
			std::cerr << "-T-   assert (cadical->solve() == " << result << ");" << std::endl;
			if (result != 0) {
				std::cerr << "-T-   cadical->conclude();" << std::endl;
			}
			if (result != 0) { this->conclude(); }
		}

		return (result == 10);
	}

	template<typename Func>
	BmcAssignment GetSolvedLiteral(const BmcLiteral& literal, ssize_t shift, Func createVariable, bool trace=false) {
		auto mapped { this->MapLiteralForward(literal >> shift, createVariable) };
		auto assignment { this->val(std::abs(mapped)) };

		if constexpr (is_craig) {
			std::cerr << "-T-   assert(cadical->val(" << std::abs(mapped) << ") == " << assignment << ");" << std::endl;
		}
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
			std::cerr << "-T-   craigTracer->set_craig_construction(" << to_string_trace(CaDiCraig::CraigConstruction::NONE) << ");" << std::endl;
			craigTracer->set_craig_construction(CaDiCraig::CraigConstruction::NONE);
			return;
		}

		switch (interpolant) {
			case CraigInterpolant::Symmetric:
				std::cerr << "-T-   craigTracer->set_craig_construction(" << to_string_trace(CaDiCraig::CraigConstruction::SYMMETRIC) << ");" << std::endl;
				craigTracer->set_craig_construction(CaDiCraig::CraigConstruction::SYMMETRIC);
				break;
			case CraigInterpolant::Asymmetric:
				std::cerr << "-T-   craigTracer->set_craig_construction(" << to_string_trace(CaDiCraig::CraigConstruction::ASYMMETRIC) << ");" << std::endl;
				craigTracer->set_craig_construction(CaDiCraig::CraigConstruction::ASYMMETRIC);
				break;
			case CraigInterpolant::DualSymmetric:
				std::cerr << "-T-   craigTracer->set_craig_construction(" << to_string_trace(CaDiCraig::CraigConstruction::DUAL_SYMMETRIC) << ");" << std::endl;
				craigTracer->set_craig_construction(CaDiCraig::CraigConstruction::DUAL_SYMMETRIC);
				break;
			case CraigInterpolant::DualAsymmetric:
				std::cerr << "-T-   craigTracer->set_craig_construction(" << to_string_trace(CaDiCraig::CraigConstruction::DUAL_ASYMMETRIC) << ");" << std::endl;
				craigTracer->set_craig_construction(CaDiCraig::CraigConstruction::DUAL_ASYMMETRIC);
				break;
			case CraigInterpolant::Intersection:
				std::cerr << "-T-   craigTracer->set_craig_construction(" << to_string_trace(CaDiCraig::CraigConstruction::ALL) << ");" << std::endl;
				craigTracer->set_craig_construction(CaDiCraig::CraigConstruction::ALL);
				break;
			case CraigInterpolant::Union:
				std::cerr << "-T-   craigTracer->set_craig_construction(" << to_string_trace(CaDiCraig::CraigConstruction::ALL) << ");" << std::endl;
				craigTracer->set_craig_construction(CaDiCraig::CraigConstruction::ALL);
				break;
			case CraigInterpolant::Smallest:
				std::cerr << "-T-   craigTracer->set_craig_construction(" << to_string_trace(CaDiCraig::CraigConstruction::ALL) << ");" << std::endl;
				craigTracer->set_craig_construction(CaDiCraig::CraigConstruction::ALL);
				break;
			case CraigInterpolant::Largest:
				std::cerr << "-T-   craigTracer->set_craig_construction(" << to_string_trace(CaDiCraig::CraigConstruction::ALL) << ");" << std::endl;
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
		std::cerr << "-T-   {" << std::endl;
		std::cerr << "-T-     std::vector<std::vector<int>> cnf;" << std::endl;
		std::cerr << "-T-     int nextVar { " << this->nextVar << " };" << std::endl;
		std::cerr << "-T-     assert(craigTracer->create_craig_interpolant(" << to_string_trace(mapped) << ", cnf, nextVar) == " << to_string_trace(cnfType) << ");" << std::endl;
		std::cerr << "-T-     assert(cnf == (std::vector<std::vector<int>> {" << std::endl;
		for (auto& clause : craigCnf) {
			std::cerr << "-T-       {";
			size_t count { 0u };
			for (auto& literal : clause) {
				if (count++ != 0) { std::cerr << ", "; }
				std::cerr << literal;
			}
			std::cerr << "}," << std::endl;
		}
		std::cerr << "-T-     }));" << std::endl;
		std::cerr << "-T-     assert(nextVar == " << nextVar << ");" << std::endl;
		std::cerr << "-T-   }" << std::endl;

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
		while (this->nextVar < nextVar) { craigTracer->label_variable(this->nextVar++, CaDiCraig::CraigVarType::A_LOCAL); }

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
		int rounds = 0;
		if (level >= PreprocessLevel::Simple) {
			rounds = 1;
			this->optimize(1);
		} else if (level >= PreprocessLevel::Expensive) {
			rounds = 3;
			this->optimize(2);
		}

		UnitCounter unitCounter;
		this->connect_fixed_listener(&unitCounter);

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
		auto code { this->simplify(rounds) };
		this->disconnect_fixed_listener();
		bool result { code != 20 };
		if (!result) {
			if (trace) { std::cerr << "    Result Constant 0" << std::endl; }
			return { (root != INVALID_LITERAL) ? BmcClause {-root} : BmcClause {} };
		}

		if (trace) { std::cerr << "  - Extracting resulting clauses" << std::endl; }
		BmcClauses resultClauses;
		resultClauses.reserve(this->irredundant() + unitCounter.getNumUnits());

		ClauseAdder<Func2> clauseAdder { *this, resultClauses, createBmcVariable, trace };
		this->traverse_clauses(clauseAdder);
		return resultClauses;
	}

private:
	class UnitCounter: public CaDiCaL::FixedAssignmentListener {
	private:
		size_t units;

	public:
		UnitCounter(void):
			units(0u)
		{}

		void notify_fixed_assignment(int unit) override {
			units++;
		}

		size_t getNumUnits(void) {
			return units;
		}
	};

	template<typename Func>
	class ClauseAdder: public CaDiCaL::ClauseIterator {
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
	};

};

}
}

#endif