// SPDX-License-Identifier: MIT OR Apache-2.0

#include "bmc-format-dimspec.hpp"

namespace Ncip {

DimspecProblem::DimspecProblem(
	size_t variables,
	DimspecClauses initClauses,
	DimspecClauses transClauses,
	DimspecClauses goalClauses,
	DimspecClauses universalClauses
):
	variables(variables),
	initClauses(initClauses),
	transClauses(transClauses),
	goalClauses(goalClauses),
	universalClauses(universalClauses)
{}

DimspecProblemBuilder::DimspecProblemBuilder():
	variables(),
	initClauses(),
	transClauses(),
	goalClauses(),
	universalClauses()
{}

void DimspecProblemBuilder::Clear() {
	variables = 0u;
	initClauses.clear();
	transClauses.clear();
	goalClauses.clear();
	universalClauses.clear();
}

DimspecVariableId DimspecProblemBuilder::AddVariable() {
	return variables++;
}

DimspecClauseId DimspecProblemBuilder::AddClause(DimspecClauseType type, DimspecClause clause) {
	switch (type) {
		case DimspecClauseType::Initial: initClauses.emplace_back(clause); return initClauses.size() - 1u;
		case DimspecClauseType::Transition: transClauses.emplace_back(clause); return transClauses.size() - 1u;
		case DimspecClauseType::Goal: goalClauses.emplace_back(clause); return goalClauses.size() - 1u;
		case DimspecClauseType::Universal: universalClauses.emplace_back(clause); return universalClauses.size() - 1u;
		default: __builtin_unreachable();
	}
}

void DimspecProblemBuilder::SetVariables(size_t variables) {
	this->variables = variables;
}

void DimspecProblemBuilder::Check() const {
	for (auto const& clause : initClauses) {
		for (auto const& literal : clause) {
			if (literal.GetVariable() >= variables) {
				throw DimspecProblemException("Found literal in INIT that refers to non-existing variable " + std::to_string(literal.GetVariable()));
			}
		}
	}
	for (auto const& clause : transClauses) {
		for (auto const& literal : clause) {
			if (literal.GetVariable() >= variables) {
				throw DimspecProblemException("Found literal in TRANS that refers to non-existing variable " + std::to_string(literal.GetVariable()));
			}
		}
	}
	for (auto const& clause : goalClauses) {
		for (auto const& literal : clause) {
			if (literal.GetVariable() >= variables) {
				throw DimspecProblemException("Found literal in GOAL that refers to non-existing variable " + std::to_string(literal.GetVariable()));
			}
		}
	}
	for (auto const& clause : universalClauses) {
		for (auto const& literal : clause) {
			if (literal.GetVariable() >= variables) {
				throw DimspecProblemException("Found literal in UNIVERSAL that refers to non-existing variable " + std::to_string(literal.GetVariable()));
			}
		}
	}

	// Check for literals that are not in timeframe 0 but are not declared latches.
	// Also check for timeframes that are greater than 1.
	for (auto const& clause : initClauses) {
		for (auto const& literal : clause) {
			if (literal.GetTimeframe() != 0) {
				throw DimspecProblemException("Found literal in INIT that is declared for timeframe " + std::to_string(literal.GetTimeframe()));
			}
		}
	}
	for (auto const& clause : transClauses) {
		for (auto const& literal : clause) {
			if ((literal.GetTimeframe() < 0) | (literal.GetTimeframe() > 1)) {
				throw DimspecProblemException("Found literal in TRANS that is declared for timeframe " + std::to_string(literal.GetTimeframe()) + ", which is < 0 or > 1");
			}
		}
	}
	for (auto const& clause : goalClauses) {
		for (auto const& literal : clause) {
			if (literal.GetTimeframe() != 0) {
				throw DimspecProblemException("Found literal in GOAL that is declared for timeframe " + std::to_string(literal.GetTimeframe()));
			}
		}
	}
	for (auto const& clause : universalClauses) {
		for (auto const& literal : clause) {
			if (literal.GetTimeframe() != 0) {
				throw DimspecProblemException("Found literal in UNIVERSAL that is declared for timeframe " + std::to_string(literal.GetTimeframe()));
			}
		}
	}
}

std::tuple<DimspecProblem, BmcProblem> DimspecProblemBuilder::Build() {
	Check();
	DimspecProblem dimspecProblem { variables, initClauses, transClauses, goalClauses, universalClauses };
	std::copy(universalClauses.begin(), universalClauses.end(), std::back_inserter(initClauses));
	std::copy(universalClauses.begin(), universalClauses.end(), std::back_inserter(transClauses));
	std::copy(universalClauses.begin(), universalClauses.end(), std::back_inserter(goalClauses));
	BmcProblem bmcProblem { variables, std::move(initClauses), std::move(transClauses), std::move(goalClauses) };
	Clear();
	return { dimspecProblem, bmcProblem };
}

DimspecCertificateBuilder::DimspecCertificateBuilder():
	DimspecProblemBuilder()
{}

DimspecCertificate DimspecCertificateBuilder::Build(const DimspecProblem& problem, const BmcCertificate& certificate) {
	assert (certificate.GetType() != Ncip::BmcCertificate::Type::None);
	variables = problem.variables;
	initClauses = problem.initClauses;
	transClauses = problem.transClauses;
	universalClauses = problem.universalClauses;
	goalClauses = { }; // Don't copy problem.goalClauses as they are overwritten

	// Encode the initial state (under assumption of universal state set)
	// as condition into the AIG
	Ncip::BmcAigerBuilder builder { certificate };
	std::vector<ssize_t> initial;
	for (auto const& clause : initClauses) {
		initial.push_back(builder.AddOr(builder.AddLiterals(clause)));
	}
	for (auto const& clause : universalClauses) {
		initial.push_back(builder.AddOr(builder.AddLiterals(clause)));
	}
	auto const initialState = builder.AddAnd(initial);

	// OR initial state and Craig interpolants
	std::vector<ssize_t> roots { initialState };
	std::copy(certificate.GetRoots().begin(), certificate.GetRoots().end(), std::back_inserter(roots));
	auto const property { builder.AddOr(roots) };
	auto const output { -property };

	auto graph { builder.Build() };
	goalClauses = graph.ToClauses(output);

	Check();
	DimspecCertificate result {
		variables,
		std::move(initClauses),
		std::move(transClauses),
		std::move(goalClauses),
		std::move(universalClauses)
	};
	Clear();
	return result;
}

}