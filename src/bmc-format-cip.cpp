// SPDX-License-Identifier: MIT OR Apache-2.0

#include "bmc-format-cip.hpp"

namespace Ncip {

template<class... Ts> struct overload : Ts... { using Ts::operator()...; };
template<class... Ts> overload(Ts...) -> overload<Ts...>; // line not needed in C++20...

CipProblem::CipProblem(
	CipVariables variables,
	CipClauses initClauses,
	CipClauses transClauses,
	CipClauses targetClauses
):
	variables(variables),
	initClauses(initClauses),
	transClauses(transClauses),
	targetClauses(targetClauses)
{}

CipProblemBuilder::CipProblemBuilder():
	variables(),
	initClauses(),
	transClauses(),
	targetClauses()
{}

void CipProblemBuilder::Clear() {
	variables.clear();
	initClauses.clear();
	transClauses.clear();
	targetClauses.clear();
}

CipVariableId CipProblemBuilder::AddVariable(CipVariableType type) {
	variables.emplace_back(type);
	return variables.size() - 1u;
}

CipClauseId CipProblemBuilder::AddClause(CipClauseType type, CipClause clause) {
	switch (type) {
		case CipClauseType::Initial: initClauses.emplace_back(clause); return initClauses.size() - 1u;
		case CipClauseType::Transition: transClauses.emplace_back(clause); return transClauses.size() - 1u;
		case CipClauseType::Target: targetClauses.emplace_back(clause); return targetClauses.size() - 1u;
		default: __builtin_unreachable();
	}
}

void CipProblemBuilder::Check() const {
	// Check for literals that span INIT / TRANS / TARGET but are declared Tseitin variables.
	std::vector<bool> inInit(variables.size(), false);
	std::vector<bool> inTrans(variables.size(), false);
	std::vector<bool> inTarget(variables.size(), false);

	for (auto const& clause : initClauses) {
		for (auto const& literal : clause) {
			if (literal.GetVariable() >= variables.size()) {
				throw CipProblemException("Found literal in INIT that refers to non-existing variable " + std::to_string(literal.GetVariable()));
			}

			inInit[literal.GetVariable()] = true;
		}
	}
	for (auto const& clause : transClauses) {
		for (auto const& literal : clause) {
			if (literal.GetVariable() >= variables.size()) {
				throw CipProblemException("Found literal in TRANS that refers to non-existing variable " + std::to_string(literal.GetVariable()));
			}

			inTrans[literal.GetVariable()] = true;
		}
	}
	for (auto const& clause : targetClauses) {
		for (auto const& literal : clause) {
			if (literal.GetVariable() >= variables.size()) {
				throw CipProblemException("Found literal in TARGET that refers to non-existing variable " + std::to_string(literal.GetVariable()));
			}

			inTarget[literal.GetVariable()] = true;
		}
	}

	for (BmcVariableId variable { 0u }; variable < variables.size(); variable++) {
		if (variables[variable] == CipVariableType::Tseitin) {
			if (inInit[variable] && inTrans[variable]) {
				throw CipProblemException("Found Tseiting variable " + std::to_string(variable) + " that occurrs in INIT and TRANS");
			}
			if (inInit[variable] && inTarget[variable]) {
				throw CipProblemException("Found Tseiting variable " + std::to_string(variable) + " that occurrs in INIT and TARGET");
			}
			if (inTrans[variable] && inTarget[variable]) {
				throw CipProblemException("Found Tseiting variable " + std::to_string(variable) + " that occurrs in TRANS and TARGET");
			}
		}
	}

	// Check for literals that are not in timeframe 0 but are not declared latches.
	// Also check for timeframes that are greater than 1.
	for (auto const& clause : initClauses) {
		for (auto const& literal : clause) {
			if (literal.GetTimeframe() != 0) {
				throw CipProblemException("Found literal in INIT that is declared for timeframe " + std::to_string(literal.GetTimeframe()));
			}
		}
	}
	for (auto const& clause : transClauses) {
		for (auto const& literal : clause) {
			if (literal.GetTimeframe() != 0 && variables[literal.GetVariable()] != CipVariableType::Latch) {
				throw CipProblemException("Found literal in TRANS that is declared for timeframe " + std::to_string(literal.GetTimeframe()) + " and not a latch");
			}
			if ((literal.GetTimeframe() < 0) | (literal.GetTimeframe() > 1)) {
				throw CipProblemException("Found literal in TRANS that is declared for timeframe " + std::to_string(literal.GetTimeframe()) + ", which is < 0 or > 1");
			}
		}
	}
	for (auto const& clause : targetClauses) {
		for (auto const& literal : clause) {
			if (literal.GetTimeframe() != 0) {
				throw CipProblemException("Found literal in TARGET that is declared for timeframe " + std::to_string(literal.GetTimeframe()));
			}
		}
	}
}

std::tuple<CipProblem, BmcProblem> CipProblemBuilder::Build() {
	Check();
	CipProblem cipProblem { variables, initClauses, transClauses, targetClauses };
	BmcProblem bmcProblem { variables.size(), std::move(initClauses), std::move(transClauses), std::move(targetClauses) };
	Clear();
	return { cipProblem, bmcProblem };
}

CipCertificateBuilder::CipCertificateBuilder():
	CipProblemBuilder()
{}

CipCertificate CipCertificateBuilder::Build(const CipProblem& problem, const BmcCertificate& certificate) {
	assert (certificate.GetType() != Ncip::BmcCertificate::Type::None);
	// Make all variables available by interpreting them as latches now as we copy INIT to TARGET.
	variables = std::vector<CipVariableType>(problem.variables.size(), CipVariableType::Latch);
	initClauses = problem.initClauses;
	transClauses = problem.transClauses;
	targetClauses = { }; // Don't copy problem.targetClauses as they are overwritten

	// Encode the initial state as condition into the AIG
	Ncip::BmcAigerBuilder builder { certificate };
	std::vector<ssize_t> initial;
	for (auto const& clause : initClauses) {
		initial.push_back(builder.AddOr(builder.AddLiterals(clause)));
	}
	auto const initialState = builder.AddAnd(initial);

	// OR initial state and Craig interpolants
	std::vector<ssize_t> roots { initialState };
	std::copy(certificate.GetRoots().begin(), certificate.GetRoots().end(), std::back_inserter(roots));
	auto const property { builder.AddOr(roots) };
	auto const output { -property };

	auto graph { builder.Build() };
	targetClauses = graph.ToClauses(output);

	Check();
	CipCertificate result {
		variables,
		std::move(initClauses),
		std::move(transClauses),
		std::move(targetClauses)
	};
	Clear();
	return result;
}

}