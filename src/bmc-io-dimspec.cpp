// SPDX-License-Identifier: MIT OR Apache-2.0

#include "bmc-io-dimspec.hpp"

#include <algorithm>
#include <sstream>

namespace Ncip {

static std::string trim(const std::string &s) {
   auto wsfront = std::find_if_not(s.begin(), s.end(), [](int c){ return std::isspace(c); });
   auto wsback = std::find_if_not(s.rbegin(), s.rend(), [](int c){ return std::isspace(c); }).base();
   return (wsback <= wsfront ? std::string() : std::string(wsfront, wsback));
}

std::tuple<DimspecProblem, BmcProblem> ParseDimspecProblem(std::istream& input) {
	if (input.bad()) {
		throw DimspecIoException("Bad DIMSPEC problem input stream");
	}

	DimspecProblemBuilder builder { };
	auto parse_clause = [&](const std::string& line, size_t variableCount) -> BmcClause {
		BmcClause clause { };

		ssize_t signedLiteral;
		std::stringstream stream { line };
		while (stream.good() && !stream.eof()) {
			stream >> signedLiteral;
			if (signedLiteral == 0) { break; }

			size_t literal   { (std::abs(signedLiteral) - 1u) % variableCount };
			size_t timeframe { (std::abs(signedLiteral) - 1u) / variableCount };
			clause.push_back(BmcLiteral::FromVariable(literal, signedLiteral < 0, timeframe));
		}

		return clause;
	};

	bool variablesDeclared = false;
	std::string line;
	while (input.good() && !input.eof()) {
		std::getline(input, line);
		line = trim(line);
		if (line.empty()) {
			continue;
		} else if (line.rfind("c", 0u) == 0u) { // Ignore comments
			continue;
		} else if (line.rfind("u", 0u) == 0u
				|| line.rfind("i", 0u) == 0u
				|| line.rfind("g", 0u) == 0u
				|| line.rfind("t", 0u) == 0u) {
			DimspecClauseType clauseType;
			if (line.rfind("u", 0u) == 0u) {
				clauseType = DimspecClauseType::Universal;
			} else if (line.rfind("i", 0u) == 0u) {
				clauseType = DimspecClauseType::Initial;
			} else if (line.rfind("g", 0u) == 0u) {
				clauseType = DimspecClauseType::Goal;
			} else if (line.rfind("t", 0u) == 0u) {
				clauseType = DimspecClauseType::Transition;
			}

			std::stringstream stream(line);
			std::string type, cnf;
			size_t variables;
			size_t clauses;
			stream >> type;
			stream >> cnf;
			assert(cnf == "cnf");
			stream >> variables;
			stream >> clauses;

			if (clauseType == DimspecClauseType::Transition) {
				variables /= 2u;
			}
			if (variablesDeclared && builder.GetVariables() != variables) {
				throw DimspecIoException("Variable count of " + std::to_string(variables)
					+ " doesn't match previous declared " + std::to_string(builder.GetVariables())
					+ " variables");
			}
			variablesDeclared = true;
			builder.SetVariables(variables);

			size_t clauseCounter{ 0u };
			while(!input.eof() && input.good() && clauseCounter < clauses) {
				std::getline(input, line);
				line = trim(line);
				if (line.empty()) {
					continue;
				}
				if (line.rfind("c", 0u) == 0u) {
					// Ignore comments
					continue;
				}

				auto clause { parse_clause(line, variables) };
				builder.AddClause(clauseType, clause);
				clauseCounter++;
			}
		} else {
			throw DimspecIoException("Unknown line \"" + line + "\"");
		}
	}

	try {
		return builder.Build();
	} catch (const DimspecProblemException& exception) {
		throw DimspecIoException(std::string("Invalid DIMSPEC problem: ") + exception.what());
	}
}

void ExportDimspecProblem(std::ostream& output, const DimspecProblem& problem) {
	const auto print_clause = [&](auto& clause) {
		size_t index { 0u };
		for (auto& literal : clause) {
			output << static_cast<ssize_t>(literal.GetVariable() + literal.GetTimeframe() * problem.GetVariables() + 1u)
				* (literal.IsNegated() ? -1 : 1) << " ";
		}
		output << "0" << std::endl;
	};

	output << "u cnf " << problem.GetVariables() << " " << problem.GetUniversal().size() << std::endl;
	for (auto& clause : problem.GetUniversal()) {
		print_clause(clause);
	}
	output << "i cnf " << problem.GetVariables() << " " << problem.GetInit().size() << std::endl;
	for (auto& clause : problem.GetInit()) {
		print_clause(clause);
	}
	output << "g cnf " << problem.GetVariables() << " " << problem.GetGoal().size() << std::endl;
	for (auto& clause : problem.GetGoal()) {
		print_clause(clause);
	}
	output << "t cnf " << (2u * problem.GetVariables()) << " " << problem.GetTrans().size() << std::endl;
	for (auto& clause : problem.GetTrans()) {
		print_clause(clause);
	}

	if (output.bad()) {
		throw DimspecIoException("Bad DIMSPEC problem output stream");
	}
}

void ExportDimspecModel(std::ostream& output, const DimspecProblem& problem, const BmcModel& model) {
	for (size_t depth { 0u }; depth < model.GetTimeframes().size(); depth++) {
		output << "v" << depth;
		auto const& timeframe { model.GetTimeframe(depth) };
		for (size_t variable { 0u }; variable < timeframe.size(); variable++) {
			switch (timeframe[variable]) {
				case BmcAssignment::DontCare: break;
				case BmcAssignment::Positive: output << " " << (variable + 1); break;
				case BmcAssignment::Negative: output << " -" << (variable + 1); break;
				default: __builtin_unreachable();
			}
		}
		output << " 0" << std::endl;
	}

	if (output.bad()) {
		throw DimspecIoException("Bad DIMSPEC model output stream");
	}
}

void ExportDimspecCertificate(std::ostream& output, const DimspecProblem& problem, const BmcCertificate& certificate) {
	try {
		ExportDimspecProblem(output, DimspecCertificateBuilder {}.Build(problem, certificate));
	} catch (const DimspecIoException& exception) {
		throw DimspecIoException("Bad DIMSPEC ceritifcate output stream");
	}
}

};
