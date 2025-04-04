// SPDX-License-Identifier: MIT OR Apache-2.0

#include "bmc-format-aig.hpp"

#include <algorithm>
#include <iostream>

namespace Ncip {

template<class... Ts> struct overload : Ts... { using Ts::operator()...; };
template<class... Ts> overload(Ts...) -> overload<Ts...>; // line not needed in C++20...

AigProblem::AigProblem(
	std::vector<AigNode> nodes,
	std::vector<AigEdge> inputs,
	std::vector<AigEdge> latches,
	std::vector<AigEdge> ands,
	std::vector<AigEdge> outputs,
	std::vector<AigEdge> bads,
	std::vector<AigEdge> constraints
):
	nodes(nodes),
	inputs(inputs),
	latches(latches),
	ands(ands),
	outputs(outputs),
	bads(bads),
	constraints(constraints)
{}

AigProblemBuilder::AigProblemBuilder():
	nodes({ AigNode { AigNodeType::Constant, 0, 0 } }),
	inputs(),
	latches(),
	ands(),
	outputs(),
	bads(),
	constraints()
{}

void AigProblemBuilder::Clear() {
	nodes = { AigNode { AigNodeType::Constant, 0, 0 } };
	inputs.clear();
	latches.clear();
	ands.clear();
	outputs.clear();
	bads.clear();
	constraints.clear();
}

void AigProblemBuilder::AddInput(AigEdge input) {
	nodes.push_back(AigNode { AigNodeType::Input, input, 0, 0 });
	inputs.push_back(input);
}

AigEdge AigProblemBuilder::AddInput() {
	AigEdge edge = 2u * nodes.size();
	nodes.push_back(AigNode { AigNodeType::Input, edge, 0, 0 });
	inputs.push_back(edge);
	return edge;
}

void AigProblemBuilder::AddLatch(AigEdge latch, AigEdge nextState, AigEdge resetState) {
	nodes.push_back(AigNode { AigNodeType::Latch, latch, nextState, resetState });
	latches.push_back(latch);
}

AigEdge AigProblemBuilder::AddLatch(AigEdge nextState, AigEdge resetState) {
	AigEdge edge = 2u * nodes.size();
	nodes.push_back(AigNode { AigNodeType::Latch, edge, nextState, resetState });
	latches.push_back(edge);
	return edge;
}

void AigProblemBuilder::AddAnd(AigEdge node, AigEdge leftEdge, AigEdge rightEdge) {
	nodes.push_back(AigNode { AigNodeType::And, node, leftEdge, rightEdge });
	ands.push_back(node);
}

AigEdge AigProblemBuilder::AddAnd(AigEdge leftEdge, AigEdge rightEdge) {
	if (leftEdge == CONSTANT_0 || rightEdge == CONSTANT_0 || leftEdge == -rightEdge) {
		return CONSTANT_0;
	} else if (leftEdge == CONSTANT_1 && rightEdge == CONSTANT_1) {
		return CONSTANT_1;
	} else if (leftEdge == CONSTANT_1 || leftEdge == rightEdge) {
		return rightEdge;
	} else if (rightEdge == CONSTANT_1) {
		return leftEdge;
	}

	AigEdge edge = 2u * nodes.size();
	nodes.push_back(AigNode { AigNodeType::And, edge, leftEdge, rightEdge });
	ands.push_back(edge);
	return edge;
}

AigEdge AigProblemBuilder::AddAnd(std::vector<AigEdge> edges) {
	if (edges.size() == 0u) {
		return 1u;
	}

	while (edges.size() > 1u) {
		for (size_t index { 0u }; index < edges.size(); index += 2u) {
			edges[index / 2u] = (index + 1u < edges.size())
				? AddAnd(edges[index], edges[index + 1])
				: edges[index];
		}
		edges.resize((edges.size() + 1u) / 2u);
	}
	return edges[0];
}

AigEdge AigProblemBuilder::AddOr(AigEdge leftEdge, AigEdge rightEdge) {
	return AddAnd(leftEdge ^ 1u, rightEdge ^ 1u) ^ 1u;
}

AigEdge AigProblemBuilder::AddOr(std::vector<AigEdge> edges) {
	for (auto& edge : edges) {
		edge ^= 1u;
	}
	return AddAnd(edges) ^ 1u;
}

void AigProblemBuilder::AddOutput(AigEdge edge) {
	outputs.push_back(edge);
}

void AigProblemBuilder::AddBad(AigEdge edge) {
	bads.push_back(edge);
}

void AigProblemBuilder::AddConstraint(AigEdge edge) {
	constraints.push_back(edge);
}

void AigProblemBuilder::Check() const {
	if (bads.size() == 0 && outputs.size() == 0) {
		throw AigProblemException("Assuming AIG for safety property (expecting at least one bad or output), got neither bads nor outputs");
	}

	std::vector<AigNodeType> nodeTypes (nodes.size(), AigNodeType::Undefined);
	for (auto index { 0 }; index < nodes.size(); index++) {
		auto& node { nodes[index] };
		if (node.nodeId % 2 != 0) {
			throw AigProblemException("Node ID is not even, got " + std::to_string(node.nodeId));
		}
		if (node.nodeId >= 2 * nodes.size()) {
			throw AigProblemException("Node ID is not in range between 0 and "
				+ std::to_string(2 * nodes.size() - 1) + ", got " + std::to_string(node.nodeId));
		}
		if (node.leftEdgeId >= 2 * nodes.size()) {
			throw AigProblemException("Left edge ID is not in range between 0 and "
				+ std::to_string(2 * nodes.size() - 1) + ", got "
				+ std::to_string(node.leftEdgeId));
		}
		if (node.rightEdgeId >= 2 * nodes.size()) {
			throw AigProblemException("Right edge ID is not in range between 0 and "
				+ std::to_string(2 * nodes.size() - 1) + ", got "
				+ std::to_string(node.rightEdgeId));
		}
		if (nodeTypes[node.nodeId / 2] != AigNodeType::Undefined) {
			throw AigProblemException("Node ID " + std::to_string(node.nodeId) + " was used twice");
		}
		nodeTypes[node.nodeId / 2] = node.type;
	}

	for (auto index { 0 }; index < bads.size(); index++) {
		if (bads[index] >= 2 * nodes.size()) {
			throw AigProblemException("Bad ID is not in range between 0 and "
				+ std::to_string(2 * nodes.size() - 1) + ", got "
				+ std::to_string(bads[index]));
		}
	}
	for (auto index { 0 }; index < outputs.size(); index++) {
		if (outputs[index] >= 2 * nodes.size()) {
			throw AigProblemException("Output ID is not in range between 0 and "
				+ std::to_string(2 * nodes.size() - 1) + ", got "
				+ std::to_string(outputs[index]));
		}
	}
	for (auto index { 0 }; index < constraints.size(); index++) {
		if (constraints[index] >= 2 * nodes.size()) {
			throw AigProblemException("Constraint ID is not in range between 0 and "
				+ std::to_string(2 * nodes.size() - 1) + ", got "
				+ std::to_string(constraints[index]));
		}
	}
}

std::tuple<AigProblem, BmcProblem> AigProblemBuilder::Build() {
	Check();

	// Allow for old output-based property encoding.
	auto &bads = (this->bads.size() == 0)
		? this->outputs
		: this->bads;

	std::vector<AigNodeType> nodeTypes (nodes.size(), AigNodeType::Undefined);
	for (auto index { 0 }; index < nodes.size(); index++) {
		auto& node { nodes[index] };
		nodeTypes[node.nodeId / 2] = node.type;
	}

	// There will be more variables than nodes.size() as we encode the Tseitin variables
	// used for the bad output possibly twice.
	std::vector<AigNodeType> variables;
	variables.reserve(nodes.size());
	std::copy(nodeTypes.begin(), nodeTypes.end(), std::back_inserter(variables));

	// Timeframe meanings:
	// 0: Literal has been allocated and everything is encoded
	// -1: Literal has not been allocated
	// -2: Literal has been allocated but no clauses have been created
	// The Tseitin literals of the target cone are re-encoded to comply with
	// the BMC API as we don't want to encode them as global variables while
	// using them in two clause types (TRANS + TARGET).
	std::vector<BmcLiteral> initLiterals;
	std::vector<BmcLiteral> transLiterals;
	std::vector<BmcLiteral> targetLiterals;
	initLiterals.reserve(nodes.size());
	transLiterals.reserve(nodes.size());
	targetLiterals.reserve(nodes.size());
	for (size_t index { 0u }; index < nodeTypes.size(); index++) {
		bool allocated { nodeTypes[index] != AigNodeType::And };
		initLiterals.push_back(BmcLiteral::FromVariable(index, false, allocated ? 0 : -1));
		transLiterals.push_back(BmcLiteral::FromVariable(index, false, allocated ? 0 : -2));
		targetLiterals.push_back(BmcLiteral::FromVariable(index, false, allocated ? 0 : -1));
	}

	auto tseitin_transform = [&](auto& literals, auto& starts, auto& clauses, auto& encoded) {
		auto const add_tseitin = [&](auto index, auto type) -> BmcLiteral {
			auto literal { BmcLiteral::FromVariable(variables.size(), false, type) };
			variables.push_back(AigNodeType::And);
			literals.push_back(literal);

			auto left { nodes[index].leftEdgeId };
			auto right { nodes[index].rightEdgeId };
			nodes.push_back({ AigNodeType::And, nodes.size() * 2u, left, right });
			return literal;
		};

		std::stack<size_t> queue;
		for (auto& start : starts) {
			queue.push(start / 2);
		}
		while (!queue.empty()) {
			auto lookupIndex = queue.top();
			queue.pop();

			// Don't take references here as we are extending the vectors.
			auto literal = literals[lookupIndex];
			if (literal.GetTimeframe() == 0) { continue; }

			// We need a copy of the Tseitin variable for the INIT / TARGET clauses.
			literal = (literal.GetTimeframe() == -1)
				? add_tseitin(literal.GetVariable(), 0)
				: literal.ToZeroTimeframe();
			literals[lookupIndex] = literal;

			// Don't take references here as we are extending the vectors.
			auto nodeIndex = literal.GetVariable();
			auto leftEdge = nodes[nodeIndex].leftEdgeId;
			auto rightEdge = nodes[nodeIndex].rightEdgeId;
			auto leftIndex = leftEdge / 2;
			auto rightIndex = rightEdge / 2;

			// Schedule encoding of both edges if required
			// and create Tseitin-variables for them if this didn't happen yet.
			if (literals[leftIndex].GetTimeframe() < 0) { queue.push(leftIndex); }
			if (literals[leftIndex].GetTimeframe() == -1) {
				leftIndex = add_tseitin(leftIndex, -2).GetVariable();
				leftEdge = (leftIndex * 2u) | (leftEdge & 1u);
				nodes[nodeIndex].leftEdgeId = leftEdge;
			}

			if (literals[rightIndex].GetTimeframe() < 0) { queue.push(rightIndex); }
			if (literals[rightIndex].GetTimeframe() == -1) {
				rightIndex = add_tseitin(rightIndex, -2).GetVariable();
				rightEdge = (rightIndex * 2u) | (rightEdge & 1u);
				nodes[nodeIndex].rightEdgeId = rightEdge;
			}

			auto leftLiteral = literals[leftIndex].ToZeroTimeframe() ^ (leftEdge & 1);
			auto rightLiteral = literals[rightIndex].ToZeroTimeframe() ^ (rightEdge & 1);
			clauses.push_back({ leftLiteral, -literal });
			clauses.push_back({ rightLiteral, -literal });
			clauses.push_back({ -leftLiteral, -rightLiteral, literal });
			encoded++;
		}
	};

	std::vector<AigEdge> initStates;
	std::vector<AigEdge> nextStates;
	initStates.reserve(latches.size());
	nextStates.reserve(latches.size());
	for (size_t index { 0 }; index < nodes.size(); index++) {
		if (nodes[index].type == AigNodeType::Latch) {
			nextStates.push_back(nodes[index].leftEdgeId);
			initStates.push_back(nodes[index].rightEdgeId);
		}
	}

	std::vector<AigEdge> initEdges;
	initEdges.reserve(initStates.size());
	initEdges.insert(initEdges.end(), initStates.begin(), initStates.end());

	size_t initEncoded { 0 };
	BmcClauses initClauses { { -initLiterals[0] } };
	tseitin_transform(initLiterals, initEdges, initClauses, initEncoded);

	// Re-map the latch resets as they have been re-encoded during the Tseitin transformation.
	for (auto& latch : latches) {
		auto& reset { nodes[latch / 2].rightEdgeId };
		reset = (initLiterals[reset / 2].GetVariable() * 2u) | (reset & 1);
	}

	for (size_t index { 0 }; index < nodes.size(); index++) {
		if (nodes[index].type == AigNodeType::Latch) {
			auto latch { initLiterals[nodes[index].nodeId / 2] };
			auto init { initLiterals[nodes[index].rightEdgeId / 2] ^ (nodes[index].rightEdgeId & 1) };
			assert(latch.GetVariable() <= variables.size());
			assert(init.GetVariable() <= variables.size());
			assert(latch.GetTimeframe() >= 0);
			assert(init.GetTimeframe() >= 0);
			initClauses.push_back({ -init,  latch });
			initClauses.push_back({  init, -latch });
		}
	}

	std::vector<AigEdge> transEdges;
	transEdges.reserve(outputs.size() + nextStates.size() + constraints.size());
	transEdges.insert(transEdges.end(), outputs.begin(), outputs.end());
	transEdges.insert(transEdges.end(), nextStates.begin(), nextStates.end());
	transEdges.insert(transEdges.end(), constraints.begin(), constraints.end());

	size_t transEncoded { 0 };
	BmcClauses transClauses { { -transLiterals[0] } };
	tseitin_transform(transLiterals, transEdges, transClauses, transEncoded);

	for (size_t index { 0 }; index < nodes.size(); index++) {
		if (nodes[index].type == AigNodeType::Latch) {
			auto latch { transLiterals[nodes[index].nodeId / 2] };
			auto next { transLiterals[nodes[index].leftEdgeId / 2] ^ (nodes[index].leftEdgeId & 1) };
			assert(latch.GetVariable() <= variables.size());
			assert(next.GetVariable() <= variables.size());
			assert(next.GetTimeframe() >= 0);
			assert(latch.GetTimeframe() >= 0);
			transClauses.push_back({ -next,  (latch >> 1) });
			transClauses.push_back({  next, -(latch >> 1) });
		}
	}
	for (auto& constraint : constraints) {
		auto literal { transLiterals[constraint / 2] ^ (constraint & 1) };
		assert(literal.GetVariable() <= variables.size());
		assert(literal.GetTimeframe() >= 0);
		transClauses.push_back({ literal });
	}

	std::vector<AigEdge> targetEdges;
	targetEdges.reserve(bads.size() + 1);
	targetEdges.insert(targetEdges.end(), bads.begin(), bads.end());
	targetEdges.insert(targetEdges.end(), constraints.begin(), constraints.end());

	size_t targetEncoded { 0 };
	BmcClauses targetClauses { { -targetLiterals[0] } };
	tseitin_transform(targetLiterals, targetEdges, targetClauses, targetEncoded);

	// Re-map the outputs as they have been re-encoded during the Tseitin transformation.
	for (auto& bad : bads) {
		bad = (targetLiterals[bad / 2].GetVariable() * 2u) | (bad & 1);
	}

	targetClauses.emplace_back();
	for (auto& bad : bads) {
		auto literal { targetLiterals[bad / 2] ^ (bad & 1) };
		assert(literal.GetVariable() <= variables.size());
		assert(literal.GetTimeframe() >= 0);
		targetClauses.back().push_back(literal);
	}
	for (auto& constrain : constraints) {
		auto literal { targetLiterals[constrain / 2] ^ (constrain & 1) };
		assert(literal.GetVariable() <= variables.size());
		assert(literal.GetTimeframe() >= 0);
		targetClauses.push_back({ literal });
	}

	AigProblem aigProblem { std::move(nodes), std::move(inputs), std::move(latches), std::move(ands), std::move(outputs), std::move(bads), std::move(constraints) };
	BmcProblem bmcProblem { variables.size(), std::move(initClauses), std::move(transClauses), std::move(targetClauses) };
	std::tuple<AigProblem, BmcProblem> result { aigProblem, bmcProblem };
	Clear();
	return result;
}

AigCertificateBuilder::AigCertificateBuilder():
	AigProblemBuilder()
{}

AigCertificate AigCertificateBuilder::Build(const AigProblem& problem, const BmcCertificate& certificate) {
	assert (certificate.GetType() != Ncip::BmcCertificate::Type::None);
	nodes = problem.nodes;
	inputs = problem.inputs;
	latches = problem.latches;
	ands = problem.ands;
	constraints = problem.constraints;
	outputs = { }; // Don't copy problem.outputs as they are overwritten
	bads = { };    // Don't copy problem.bads as they are overwritten

	// Encode the initial state as condition into the AIG
	std::vector<size_t> initial;
	for (auto const& latch : latches) {
		auto const& node { nodes[latch / 2] };
		auto const& resetState { node.rightEdgeId };
		if (resetState == 0u || resetState == 1u) {
			// Prefer direct encoding to produce more compact certificates.
			initial.push_back(node.nodeId ^ (resetState ^ 1u));
		} else {
			initial.push_back(AddOr(
				AddAnd(node.nodeId,      resetState     ),
				AddAnd(node.nodeId ^ 1u, resetState ^ 1u)
			));
		}
	}
	auto const initialState = AddAnd(initial);

	// Map the Craig interpolants from the BMC Certificate to AIG literals.
	std::vector<size_t> idMap;
	auto to_literal = [&](ssize_t certEdge) -> size_t {
		auto mapped { idMap[std::abs(certEdge) - 1] };
		return mapped ^ ((certEdge < 0) ? 1u : 0u);
	};
	idMap.reserve(certificate.GetNodes().size());
	for (auto &node : certificate.GetNodes()) {
		idMap.push_back(std::visit(overload {
			[&](const Ncip::BmcCertificate::AigConstant& aigConstant) -> AigEdge {
				return 1u;
			},
			[&](const Ncip::BmcCertificate::AigLiteral& aigLiteral) -> AigEdge {
				return aigLiteral.literal.GetLiteral();
			},
			[&](const Ncip::BmcCertificate::AigAnd& aigAnd) -> AigEdge {
				return AddAnd(to_literal(aigAnd.left), to_literal(aigAnd.right));
			}
		}, node));
	}

	// OR initial state and Craig interpolants
	std::vector<size_t> roots { initialState };
	std::transform(certificate.GetRoots().begin(), certificate.GetRoots().end(), std::back_inserter(roots), to_literal);
	auto property { AddOr(roots) };
	AddOutput(property ^ 1u);

	Check();
	AigCertificate result {
		std::move(nodes),
		std::move(inputs),
		std::move(latches),
		std::move(ands),
		std::move(outputs),
		std::move(bads),
		std::move(constraints)
	};
	Clear();
	return result;
}

}
