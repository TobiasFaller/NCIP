// SPDX-License-Identifier: MIT OR Apache-2.0

#include <algorithm>
#include <iterator>
#include "bmc-problem.hpp"

namespace Ncip {

template<class... Ts> struct overload : Ts... { using Ts::operator()...; };
template<class... Ts> overload(Ts...) -> overload<Ts...>; // line not needed in C++20...

std::string to_string(const BmcLiteral& literal, ssize_t shift) {
	return (literal.IsNegated() ? "-" : "") + std::to_string(literal.GetVariable())
		+ ":" + std::to_string(literal.GetTimeframe() + shift);
}

std::string to_string(const BmcClause& clause, ssize_t shift) {
	std::string result { };
	for (size_t index { 0u }; index < clause.size(); index++) {
		if (index != 0u) result += ", ";
		result += to_string(clause[index], shift);
	}
	return "(" + result + ")";
}

std::string to_string(const BmcAssignment& assignment) {
	switch (assignment) {
		case BmcAssignment::Negative: return "0";
		case BmcAssignment::Positive: return "1";
		case BmcAssignment::DontCare: return "X";
		default: __builtin_unreachable();
	}
}

std::string to_string(const BmcTimeframe& timeframe) {
	std::string result;
	for (auto const& assignment : timeframe) {
		result += to_string(assignment);
	}
	return result;
}

void BmcProblem::CheckProblem() const {
	for (auto const& clause : initClauses) {
		for (auto const& literal : clause) {
			if (literal.GetVariable() >= variables) {
				throw BmcProblemError("Found literal in INIT that refers to non-existing variable " + std::to_string(literal.GetVariable()));
			}
		}
	}
	for (auto const& clause : transClauses) {
		for (auto const& literal : clause) {
			if (literal.GetVariable() >= variables) {
				throw BmcProblemError("Found literal in TRANS that refers to non-existing variable " + std::to_string(literal.GetVariable()));
			}
		}
	}
	for (auto const& clause : targetClauses) {
		for (auto const& literal : clause) {
			if (literal.GetVariable() >= variables) {
				throw BmcProblemError("Found literal in TARGET that refers to non-existing variable " + std::to_string(literal.GetVariable()));
			}
		}
	}

	// Check for literals that are not in timeframe 0 but are not declared latches.
	// Also check for timeframes that are greater than 1.
	for (auto const& clause : initClauses) {
		for (auto const& literal : clause) {
			if (literal.GetTimeframe() != 0) {
				throw BmcProblemError("Found literal in INIT that is declared for timeframe " + std::to_string(literal.GetTimeframe()));
			}
		}
	}
	for (auto const& clause : transClauses) {
		for (auto const& literal : clause) {
			if ((literal.GetTimeframe() < 0) | (literal.GetTimeframe() > 1)) {
				throw BmcProblemError("Found literal in TRANS that is declared for timeframe " + std::to_string(literal.GetTimeframe()) + ", which is < 0 or > 1");
			}
		}
	}
	for (auto const& clause : targetClauses) {
		for (auto const& literal : clause) {
			if (literal.GetTimeframe() != 0) {
				throw BmcProblemError("Found literal in TARGET that is declared for timeframe " + std::to_string(literal.GetTimeframe()));
			}
		}
	}
}

BmcAigerBuilder::BmcAigerBuilder():
	nodes({ BmcCertificate::AigConstant { } }),
	hashesLiterals(),
	hashesNodes()
{}

BmcAigerBuilder::BmcAigerBuilder(const BmcAiger& toCopy):
	nodes(toCopy.GetNodes()),
	hashesLiterals(),
	hashesNodes()
{
	for (size_t index { 0u }; index < nodes.size(); index++) {
		std::visit(overload {
			[&](const BmcAiger::AigConstant& node) -> void { },
			[&](const BmcAiger::AigLiteral& node) -> void {
				hashesLiterals.insert_or_assign(node.literal, index + 1);
			},
			[&](const BmcAiger::AigAnd& node) -> void {
				auto lookup = (node.left < node.right)
					? std::make_tuple(node.left, node.right)
					: std::make_tuple(node.right, node.left);
				hashesNodes.insert_or_assign(lookup, index + 1);
			}
		}, nodes[index]);
	}
}

enum class BmcAigerSubsumption { None, Self, Other };
enum class BmcAigerSimplify { Normal, Tautology, Empty };

std::tuple<BmcAigerSubsumption, size_t> BmcAigerSubsumes(const BmcClause& clause, const BmcClause& other) {
	if (other.size() < clause.size()) {
		return { BmcAigerSubsumption::None, 0u };
	}

	size_t index { clause.size() };
	for (ssize_t i { 0 }; i < clause.size(); i++) {
		for (ssize_t j { 0 }; j < other.size(); j++) {
			if (clause[i] == other[j]) {
				goto nextLit;
			} else if (index == clause.size() && clause[i] == -other[j]) {
				index = i;
				goto nextLit;
			}
		}

		return { BmcAigerSubsumption::None, 0u };
nextLit:
		continue;
	}

	if (index == clause.size()) {
		return { BmcAigerSubsumption::Other, 0u };
	} else if (index != clause.size() && clause.size() == other.size()) {
		return { BmcAigerSubsumption::Self, index };
	}

	return { BmcAigerSubsumption::None, 0u };
}

BmcClauses BmcAigerSimplifyClauses(BmcClauses clauses) {
	if (clauses.size() == 0) {
		return { };
	}
	for (auto& clause : clauses) {
		if (clause.size() == 0) {
			return { {} };
		}
	}

	std::stable_sort(clauses.begin(), clauses.end(),
		[](auto const& left, auto const& right){
			return left.size() < right.size();
		});
	assert (clauses.size() < 2 || clauses[0].size() <= clauses[1].size());

	for (ssize_t i { 0 }; i < static_cast<ssize_t>(clauses.size()); i++) {
		for (ssize_t j { static_cast<ssize_t>(clauses.size()) - 1 }; j > i; j--) {
			auto [subsumption, index] { BmcAigerSubsumes(clauses[i], clauses[j]) };
			if (subsumption == BmcAigerSubsumption::Other) {
				clauses.erase(clauses.begin() + j);
				continue;
			} else if (subsumption == BmcAigerSubsumption::Self) {
				clauses.erase(clauses.begin() + j);
				clauses[i].erase(clauses[i].begin() + index);
				if (clauses[i].size() == 0) {
					return { {} };
				}
				while (i > 0 && clauses[i - 1].size() > clauses[i].size()) {
					std::swap(clauses[i - 1], clauses[i]);
					i--;
				}
				goto reevaluateClause;
			}
		}
		continue;
	reevaluateClause:
		i--;
		continue;
	}

	return clauses;
}

std::tuple<BmcAigerSimplify, BmcClause> BmcAigerSimplifyClause(BmcClause clause) {
	std::stable_sort(clause.begin(), clause.end(), [](auto const& left, auto const& right) {
		return (left.GetTimeframe() < right.GetTimeframe()
			|| (left.GetTimeframe() == right.GetTimeframe()
				&& left.GetVariable() < right.GetVariable()));
	});
	assert (clause.size() < 2
		|| clause[0].GetTimeframe() <= clause[1].GetTimeframe()
		|| clause[0].GetVariable() <= clause[1].GetVariable());
	clause.erase(std::unique(clause.begin(), clause.end()), clause.end());
	if (clause.size() == 0) {
		return { BmcAigerSimplify::Empty, { } };
	}

	// Find tautologies
	for (size_t i { 0 }; i + 1 < clause.size(); i++) {
		if (clause[i] == -clause[i + 1]) {
			return { BmcAigerSimplify::Tautology, { } };
		}
	}

	return { BmcAigerSimplify::Normal, clause };
}

BmcClauses BmcAigerEdgeToClauses(const BmcAiger& graph, ssize_t edge, bool negated, size_t depth) {
	assert (std::abs(edge) < graph.GetNodes().size() + 1u);
	negated ^= (edge < 0);
	return std::visit(overload {
		[&](const BmcAiger::AigConstant& node) -> BmcClauses {
			if (!negated) return { };
			return { {} };
		},
		[&](const BmcCertificate::AigLiteral& node) -> BmcClauses {
			return { { node.literal ^ negated } };
		},
		[&](const BmcCertificate::AigAnd& node) -> BmcClauses {
			auto left { BmcAigerSimplifyClauses(BmcAigerEdgeToClauses(graph, node.left, negated, depth + 1)) };
			auto right { BmcAigerSimplifyClauses(BmcAigerEdgeToClauses(graph, node.right, negated, depth + 1)) };
			if (!negated) {
				std::copy(right.begin(), right.end(), std::back_inserter(left));
				return BmcAigerSimplifyClauses(left);
			} else {
				BmcClauses result { };
				for (auto const& lclause : left) {
					BmcClauses localClauses { };
					for (auto const& rclause : right) {
						BmcClause sourceClause { };
						sourceClause.reserve(lclause.size() + rclause.size());
						std::copy(lclause.begin(), lclause.end(), std::back_inserter(sourceClause));
						std::copy(rclause.begin(), rclause.end(), std::back_inserter(sourceClause));

						auto [status, simplifiedClause] = BmcAigerSimplifyClause(sourceClause);
						if (status == BmcAigerSimplify::Tautology) {
							continue;
						} else if (status == BmcAigerSimplify::Empty) {
							return { };
						}
						localClauses.push_back(simplifiedClause);
					}
					auto simplified { BmcAigerSimplifyClauses(localClauses) };
					std::copy(simplified.begin(), simplified.end(), std::back_inserter(result));
				}
				return BmcAigerSimplifyClauses(result);
			}
		}
	}, graph.GetNodes()[std::abs(edge) - 1]);
}

BmcClauses BmcAiger::ToClauses(ssize_t root) const {
	return BmcAigerEdgeToClauses(*this, root, false, 0);
}

ssize_t BmcAigerBuilder::AddLiteral(BmcLiteral literal) {
	if (auto it = hashesLiterals.find(literal); it != hashesLiterals.end()) {
		return it->second;
	} else if (auto it = hashesLiterals.find(-literal); it != hashesLiterals.end()) {
		return -it->second;
	}

	nodes.push_back(BmcCertificate::AigLiteral { literal });
	hashesLiterals.insert_or_assign(literal, nodes.size());
	return nodes.size();
}

std::vector<ssize_t> BmcAigerBuilder::AddLiterals(std::vector<BmcLiteral> literals) {
	std::vector<ssize_t> edges;
	edges.reserve(literals.size());
	for (auto& literal : literals) {
		edges.push_back(AddLiteral(literal));
	}
	return edges;
}

ssize_t BmcAigerBuilder::AddAnd(ssize_t left, ssize_t right) {
	if (left == CONSTANT_0 || right == CONSTANT_0 || left == -right) {
		return CONSTANT_0;
	} else if (left == CONSTANT_1 && right == CONSTANT_1) {
		return CONSTANT_1;
	} else if (left == CONSTANT_1 || left == right) {
		return right;
	} else if (right == CONSTANT_1) {
		return left;
	}

	auto lookup = (left < right)
		? std::make_tuple(left, right)
		: std::make_tuple(right, left);
	if (auto it = hashesNodes.find(lookup); it != hashesNodes.end()) {
		return it->second;
	}

	nodes.emplace_back(BmcCertificate::AigAnd { left, right });
	hashesNodes.insert_or_assign(lookup, nodes.size());
	return nodes.size();
}

ssize_t BmcAigerBuilder::AddAnd(std::vector<ssize_t> edges) {
	if (edges.size() == 0) {
		return CONSTANT_1;
	}

	while (edges.size() > 1) {
		for (size_t index { 0u }; index < edges.size(); index += 2) {
			edges[index / 2] = (index + 1 < edges.size())
				? AddAnd(edges[index], edges[index + 1])
				: edges[index];
		}
		edges.resize((edges.size() + 1) / 2);
	}
	return edges[0];
}

ssize_t BmcAigerBuilder::AddOr(ssize_t left, ssize_t right) {
	return -AddAnd(-left, -right);
}

ssize_t BmcAigerBuilder::AddOr(std::vector<ssize_t> edges) {
	for (auto& edge : edges) {
		edge = -edge;
	}
	return -AddAnd(edges);
}

BmcAiger BmcAigerBuilder::Build() {
	BmcAiger result { std::move(nodes) };
	nodes.clear();
	hashesLiterals.clear();
	hashesNodes.clear();
	return result;
}

BmcCertificateBuilder::BmcCertificateBuilder(BmcCertificate::Type type):
	BmcAigerBuilder(),
	type(type)
{}

BmcCertificate BmcCertificateBuilder::Build(std::vector<ssize_t> roots) {
	BmcCertificate result { type, std::move(nodes), roots };
	nodes.clear();
	hashesLiterals.clear();
	hashesNodes.clear();
	return result;
}

}