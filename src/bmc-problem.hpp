// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <cassert>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>

namespace Ncip {

class BmcLiteral {
public:
	constexpr static BmcLiteral FromLiteral(size_t literal, ssize_t timeframe = 0) {
		return BmcLiteral(literal, timeframe);
	}
	constexpr static BmcLiteral FromVariable(size_t variable, bool inverted, ssize_t timeframe = 0) {
		return BmcLiteral((variable << 1u) | (inverted ? 1u : 0u), timeframe);
	}

	constexpr size_t GetLiteral() const { return literal; }
	constexpr size_t GetVariable() const { return literal >> 1u; }
	constexpr ssize_t GetTimeframe() const { return timeframe; }
	constexpr bool IsNegated() const { return literal & 1u; }

	constexpr BmcLiteral ToZeroTimeframe() const { return BmcLiteral(literal, 0); }
	constexpr BmcLiteral ToPositive() const { return BmcLiteral(literal & ~1, timeframe); }
	constexpr BmcLiteral ToNegative() const { return BmcLiteral(literal |  1, timeframe); }

	constexpr BmcLiteral operator-() const { return BmcLiteral(literal ^ 1u, timeframe); }
	constexpr BmcLiteral operator^(bool value) const { return value ? BmcLiteral(literal ^ 1u, timeframe) : *this; }
	constexpr BmcLiteral operator<<(ssize_t amount) const { return BmcLiteral(literal, timeframe - amount); }
	constexpr BmcLiteral operator>>(ssize_t amount) const { return BmcLiteral(literal, timeframe + amount); }

	constexpr bool operator==(const BmcLiteral& other) const { return (literal == other.literal) && (timeframe == other.timeframe); }
	constexpr bool operator!=(const BmcLiteral& other) const { return (literal != other.literal) || (timeframe != other.timeframe); }

	struct Hash {
		std::size_t operator()(const Ncip::BmcLiteral& literal) const noexcept {
			return (std::hash<ssize_t>()(literal.GetTimeframe()) << 32u)
				| std::hash<size_t>()(literal.GetLiteral());
		}
	};

private:
	constexpr BmcLiteral(size_t literal, ssize_t timeframe = 0):
		literal(literal),
		timeframe(timeframe)
	{}

	size_t literal;
	ssize_t timeframe;
};

constexpr auto INVALID_LITERAL = BmcLiteral::FromLiteral(std::numeric_limits<size_t>::max() / 2u, 0u);

using BmcVariableId = size_t;
using BmcClauseId = size_t;
using BmcClause = std::vector<BmcLiteral>;
using BmcClauses = std::vector<BmcClause>;

std::string to_string(const BmcLiteral& literal, ssize_t shift = 0);
std::string to_string(const BmcClause& clause, ssize_t shift = 0);

class BmcProblemError:
	public std::runtime_error {
public:
	BmcProblemError(const std::string& message):
		std::runtime_error(message)
	{}

};

class BmcProblem {
public:
	BmcProblem(
		size_t variables,
		BmcClauses initClauses,
		BmcClauses transClauses,
		BmcClauses targetClauses
	):
		variables(variables),
		initClauses(initClauses),
		transClauses(transClauses),
		targetClauses(targetClauses)
	{}

	const size_t& GetVariables() const { return variables; }
	const BmcClauses& GetInit() const { return initClauses; }
	const BmcClauses& GetTrans() const { return transClauses; }
	const BmcClauses& GetTarget() const { return targetClauses; }

	void CheckProblem() const;

private:
	size_t variables;
	BmcClauses initClauses;
	BmcClauses transClauses;
	BmcClauses targetClauses;

};

enum class BmcAssignment {
	Negative,
	Positive,
	DontCare
};

inline BmcAssignment operator-(const BmcAssignment& assignment) {
	switch (assignment) {
		case BmcAssignment::Negative: return BmcAssignment::Positive;
		case BmcAssignment::Positive: return BmcAssignment::Negative;
		case BmcAssignment::DontCare: return BmcAssignment::DontCare;
		default: __builtin_unreachable();
	}
}

inline BmcAssignment operator^(const BmcAssignment& assignment, bool invert) {
	return invert ? -assignment : assignment;
}

std::string to_string(const BmcAssignment& assignment);

using BmcTimeframe = std::vector<BmcAssignment>;
using BmcTimeframes = std::vector<BmcTimeframe>;

std::string to_string(const BmcTimeframe& timeframe);

class BmcModel {
public:
	BmcModel():
		timeframes()
	{}
	BmcModel(BmcTimeframes timeframes):
		timeframes(timeframes)
	{}

	const BmcTimeframes& GetTimeframes() const { return timeframes; }
	const BmcTimeframe& GetTimeframe(size_t timeframe) const { return timeframes[timeframe]; }

	BmcAssignment GetAssignment(const BmcLiteral& literal) const {
		auto assignment { timeframes[literal.GetTimeframe()][literal.GetVariable()] };
		return assignment ^ literal.IsNegated();
	}

private:
	BmcTimeframes timeframes;

};

class BmcAigerBuilder;
class BmcAiger {
public:
	struct AigConstant { };
	struct AigLiteral { BmcLiteral literal; };
	struct AigAnd { ssize_t left; ssize_t right; };
	using AigNode = std::variant<AigConstant, AigLiteral, AigAnd>;

	const std::vector<AigNode>& GetNodes() const { return nodes; }
	BmcClauses ToClauses(ssize_t root) const;

protected:
	friend class BmcAigerBuilder;

	BmcAiger(std::vector<AigNode> nodes):
		nodes(nodes)
	{}

	std::vector<AigNode> nodes;

};

class BmcAigerBuilder {
public:
	static const ssize_t CONSTANT_1 = 1;
	static const ssize_t CONSTANT_0 = -1;

	BmcAigerBuilder();
	BmcAigerBuilder(const BmcAiger& toCopy);
	ssize_t AddLiteral(BmcLiteral literal);
	std::vector<ssize_t> AddLiterals(std::vector<BmcLiteral> literals);
	ssize_t AddAnd(ssize_t left, ssize_t right);
	ssize_t AddAnd(std::vector<ssize_t> edges);
	ssize_t AddOr(ssize_t left, ssize_t right);
	ssize_t AddOr(std::vector<ssize_t> edges);
	BmcAiger Build();

protected:
	std::vector<BmcAiger::AigNode> nodes;

	struct NodeHash {
		std::size_t operator()(const std::tuple<ssize_t, ssize_t>& node) const noexcept {
			return std::hash<ssize_t>()(std::get<0>(node)) << 16u
				| std::hash<ssize_t>()(std::get<1>(node));
		}
	};

	std::unordered_map<BmcLiteral, ssize_t, BmcLiteral::Hash> hashesLiterals;
	std::unordered_map<std::tuple<ssize_t, ssize_t>, ssize_t, NodeHash> hashesNodes;

};

class BmcCertificateBuilder;

class BmcCertificate: public BmcAiger {
public:
	enum class Type { None, Init, Trans, Target, InitTrans, TransTarget, Craig };

	BmcCertificate():
		BmcAiger({ }),
		type(Type::None),
		roots()
	{}

	const Type& GetType() const { return type; }
	const std::vector<ssize_t>& GetRoots() const { return roots; }

private:
	friend class BmcCertificateBuilder;

	BmcCertificate(Type type, std::vector<AigNode> nodes, std::vector<ssize_t> roots):
		BmcAiger(nodes),
		type(type),
		roots(roots)
	{}

	Type type;
	std::vector<ssize_t> roots;

};

class BmcCertificateBuilder: private BmcAigerBuilder {
public:
	using BmcAigerBuilder::CONSTANT_1;
	using BmcAigerBuilder::CONSTANT_0;

	BmcCertificateBuilder(BmcCertificate::Type type);
	using BmcAigerBuilder::AddLiteral;
	using BmcAigerBuilder::AddAnd;
	BmcCertificate Build(std::vector<ssize_t> roots);

private:
	BmcCertificate::Type type;

};

}
