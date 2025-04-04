// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include "bmc-problem.hpp"

namespace Ncip {

using DimspecVariableId = BmcVariableId;

enum class DimspecClauseType: size_t {
	Initial,
	Transition,
	Goal,
	Universal
};
using DimspecClause = BmcClause;
using DimspecClauses = BmcClauses;
using DimspecClauseId = BmcClauseId;

class DimspecProblemException:
	public std::runtime_error {
public:
	DimspecProblemException(const std::string& message):
		std::runtime_error(message)
	{}

};

class DimspecProblemBuilder;
class DimspecCertificateBuilder;
class DimspecProblem {
public:
	const size_t& GetVariables() const { return variables; }
	const DimspecClauses& GetInit() const { return initClauses; }
	const DimspecClauses& GetTrans() const { return transClauses; }
	const DimspecClauses& GetGoal() const { return goalClauses; }
	const DimspecClauses& GetUniversal() const { return universalClauses; }

private:
	friend class DimspecProblemBuilder;
	friend class DimspecCertificateBuilder;

	DimspecProblem(
		size_t variables,
		DimspecClauses initClauses,
		DimspecClauses transClauses,
		DimspecClauses goalClauses,
		DimspecClauses universalClauses
	);

	size_t variables;
	DimspecClauses initClauses;
	DimspecClauses transClauses;
	DimspecClauses goalClauses;
	DimspecClauses universalClauses;
};

class DimspecProblemBuilder {
public:
	DimspecProblemBuilder();

	DimspecVariableId AddVariable();
	DimspecClauseId AddClause(DimspecClauseType type, DimspecClause clause);
	void SetVariables(size_t variables);
	std::tuple<DimspecProblem, BmcProblem> Build();
	void Check() const;
	void Clear();

	const size_t& GetVariables() const { return variables; }
	const DimspecClauses& GetInit() const { return initClauses; }
	const DimspecClauses& GetTrans() const { return transClauses; }
	const DimspecClauses& GetGoal() const { return goalClauses; }
	const DimspecClauses& GetUniversal() const { return universalClauses; }

protected:
	size_t variables;
	DimspecClauses initClauses;
	DimspecClauses transClauses;
	DimspecClauses goalClauses;
	DimspecClauses universalClauses;
};

using DimspecCertificate = DimspecProblem;
class DimspecCertificateBuilder: private DimspecProblemBuilder {
public:
	DimspecCertificateBuilder();
	DimspecCertificate Build(const DimspecProblem& problem, const BmcCertificate& certificate);

};

};
