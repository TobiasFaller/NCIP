// SPDX-License-Identifier: MIT OR Apache-2.0

#include "bmc-io-cip.hpp"

#include <algorithm>
#include <regex>
#include <sstream>

namespace Ncip {

static std::string trim(const std::string &s) {
   auto wsfront = std::find_if_not(s.begin(), s.end(), [](int c){ return std::isspace(c); });
   auto wsback = std::find_if_not(s.rbegin(), s.rend(), [](int c){ return std::isspace(c); }).base();
   return (wsback <= wsfront ? std::string() : std::string(wsfront, wsback));
}

std::tuple<CipProblem, BmcProblem> ParseCipProblem(std::istream& input) {
	if (input.bad()) {
		throw CipIoException("Bad CIP problem input stream");
	}

	const std::regex innerGroups { "\\((.*)\\)" };
	const std::regex specificGroups { "(-?[0-9]+:[0-9]+)" };

	CipProblemBuilder builder { };
	auto parse_clause = [&](const std::string& line) -> BmcClause {
		BmcClause clause { };

		std::smatch match;
		if (!std::regex_match(line, match, innerGroups)) {
			throw CipIoException("Could not parse line \"" + line + "\"");
		}

		std::string fullClause = match[1].str();
		for (auto it = std::sregex_iterator(fullClause.begin(), fullClause.end(), specificGroups);
			it != std::sregex_iterator(); it++)
		{
			int literalId;
			int timeframe;
			match = *it;
			std::stringstream stream(match.str(0));
			std::string token;

			std::getline(stream, token, ':');
			literalId = std::stol(token);

			std::getline(stream, token, ':');
			timeframe = std::stol(token);

			clause.push_back(BmcLiteral::FromVariable(std::abs(literalId) - 1u, (literalId < 0), timeframe));
		}

		return clause;
	};

	std::string line;
	while (input.good() && !input.eof()) {
		std::getline(input, line);
		line = trim(line);
		if (line.empty()) {
			continue;
		}

		if (line.rfind("DECL", 0u) == 0u) {
			while(input.good() && !input.eof()) {
				std::getline(input, line);
				line = trim(line);
				if (line.empty()) {
					break;
				}

				std::string variableType;
				size_t variableIndex;

				std::stringstream stream(line);
				stream >> variableType;
				stream >> variableIndex;

				BmcVariableId variableId;
				if (variableType == "AND_VAR"
					|| variableType == "AUX_VAR") {
					variableId = builder.AddVariable(CipVariableType::Tseitin);
				} else if (variableType == "LATCH_VAR") {
					variableId = builder.AddVariable(CipVariableType::Latch);
				} else if (variableType == "INPUT_VAR") {
					variableId = builder.AddVariable(CipVariableType::Input);
				} else if (variableType == "OUTPUT_VAR") {
					variableId = builder.AddVariable(CipVariableType::Output);
				} else {
					throw CipIoException("Unknown variable type \"" + variableType + "\"");
				}

				if (variableId + 1u != variableIndex) {
					throw CipIoException("Inconsistent literal index counters!");
				}
			}
		} else if (line.rfind("INIT", 0u) == 0u) {
			while(input.good() && !input.eof()) {
				std::getline(input, line);
				line = trim(line);
				if (line.empty()) {
					break;
				}

				auto clause = parse_clause(line);
				builder.AddClause(CipClauseType::Initial, clause);
			}
		} else if (line.rfind("TRANS", 0u) == 0u) {
			while(input.good() && !input.eof()) {
				std::getline(input, line);
				line = trim(line);
				if (line.empty()) {
					break;
				}

				auto clause = parse_clause(line);
				builder.AddClause(CipClauseType::Transition, clause);
			}
		} else if (line.rfind("TARGET", 0u) == 0u) {
			while(input.good() && !input.eof()) {
				std::getline(input, line);
				line = trim(line);
				if (line.empty()) {
					break;
				}

				auto clause = parse_clause(line);
				builder.AddClause(CipClauseType::Target, clause);
			}
		} else if (line.rfind("--", 0u) == 0u) {
			// Ignore comment lines
			continue;
		} else if (line.rfind("OFFSET: ", 0u) == 0u
			|| line.rfind("USE_PROPERTY: ", 0u) == 0u
			|| line.rfind("SIMPLIFY_INTERPOLANTS: ", 0u) == 0u
			|| line.rfind("TIMEOUT: ", 0u) == 0u
			|| line.rfind("MAXDEPTH: ", 0u) == 0u)
		{
			continue; // Ignore unused property lines
		} else {
			throw CipIoException("Unknown section \"" + line + "\"");
		}
	}

	try {
		return builder.Build();
	} catch (const CipProblemException& exception) {
		throw CipIoException(std::string("Invalid CIP problem: ") + exception.what());
	}
}

void ExportCipProblem(std::ostream& output, const CipProblem& problem) {
	const auto print_clause = [&output](auto& clause) {
		output << "(";
		size_t index { 0u };
		for (auto& literal : clause) {
			if (index++ != 0) { output << ", "; }
			output << "[" << (static_cast<ssize_t>(literal.GetVariable()) + 1) * (literal.IsNegated() ? -1 : 1)
				<< ":" << literal.GetTimeframe() << "]";
		}
		output << ")" << std::endl;
	};

	output << "DECL" << std::endl;
	size_t counter { 1 };
	for (auto& var : problem.GetVariables()) {
		switch (var) {
			case CipVariableType::Input: output << "INPUT_VAR " << counter++ << std::endl; break;
			case CipVariableType::Output: output << "OUTPUT_VAR " << counter++ << std::endl; break;
			case CipVariableType::Latch: output << "LATCH_VAR " << counter++ << std::endl; break;
			case CipVariableType::Tseitin: output << "AUX_VAR " << counter++ << std::endl; break;
		}
	}
	output << std::endl;

	output << "INIT" << std::endl;
	for (auto& clause : problem.GetInit()) {
		print_clause(clause);
	}
	output << std::endl;

	output << "TRANS" << std::endl;
	for (auto& clause : problem.GetTrans()) {
		print_clause(clause);
	}
	output << std::endl;

	output << "TARGET" << std::endl;
	for (auto& clause : problem.GetTarget()) {
		print_clause(clause);
	}
	output << std::endl;

	if (output.bad()) {
		throw CipIoException("Bad CIP problem output stream");
	}
}

void ExportCipModel(std::ostream& output, const CipProblem& problem, const BmcModel& model) {
	for (size_t depth { 0u }; depth < model.GetTimeframes().size(); depth++) {
		output << depth << " = ";
		for (auto& assignment : model.GetTimeframe(depth)) {
			output << to_string(assignment);
		}
		output << std::endl;
	}

	if (output.bad()) {
		throw CipIoException("Bad CIP model output stream");
	}
}

void ExportCipCertificate(std::ostream& output, const CipProblem& problem, const BmcCertificate& certificate) {
	try {
		ExportCipProblem(output, CipCertificateBuilder {}.Build(problem, certificate));
	} catch (const CipIoException& exception) {
		throw CipIoException("Bad CIP ceritifcate output stream");
	}
}

};
