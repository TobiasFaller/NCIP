// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include "bmc-problem.hpp"

namespace Ncip {

enum class CipVariableType: size_t {
	Input,
	Output,
	Latch,
	Tseitin
};
using CipVariables = std::vector<CipVariableType>;
using CipVariableId = BmcVariableId;

enum class CipClauseType: size_t {
	Initial,
	Transition,
	Target
};
using CipClause = BmcClause;
using CipClauses = BmcClauses;
using CipClauseId = BmcClauseId;

class CipProblemException:
	public std::runtime_error {
public:
	CipProblemException(const std::string& message):
		std::runtime_error(message)
	{}

};

class CipProblemBuilder;
class CipCertificateBuilder;
class CipProblem {
public:
	const CipVariables& GetVariables() const { return variables; }
	const CipClauses& GetInit() const { return initClauses; }
	const CipClauses& GetTrans() const { return transClauses; }
	const CipClauses& GetTarget() const { return targetClauses; }

private:
	friend class CipProblemBuilder;
	friend class CipCertificateBuilder;

	CipProblem(
		CipVariables variables,
		CipClauses initClauses,
		CipClauses transClauses,
		CipClauses targetClauses
	);

	CipVariables variables;
	CipClauses initClauses;
	CipClauses transClauses;
	CipClauses targetClauses;
};

class CipProblemBuilder {
public:
	CipProblemBuilder();

	CipVariableId AddVariable(CipVariableType type);
	CipClauseId AddClause(CipClauseType type, CipClause clause);
	std::tuple<CipProblem, BmcProblem> Build();
	void Check() const;
	void Clear();

	const CipVariables& GetVariables() const { return variables; }
	const CipClauses& GetInit() const { return initClauses; }
	const CipClauses& GetTrans() const { return transClauses; }
	const CipClauses& GetTarget() const { return targetClauses; }

protected:
	CipVariables variables;
	CipClauses initClauses;
	CipClauses transClauses;
	CipClauses targetClauses;
};

using CipCertificate = CipProblem;
class CipCertificateBuilder: private CipProblemBuilder {
public:
	CipCertificateBuilder();
	CipCertificate Build(const CipProblem& problem, const BmcCertificate& certificate);

};

};
