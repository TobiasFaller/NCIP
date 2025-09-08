// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include "bmc-problem.hpp"

#include <cstdint>
#include <stack>
#include <vector>

namespace Ncip {

enum class AigNodeType {
	Undefined,
	Constant,
	Input,
	Latch,
	And
};

using AigEdge = size_t;
struct AigNode {
	AigNodeType type;
	AigEdge nodeId;
	AigEdge leftEdgeId;
	AigEdge rightEdgeId;
};

class AigProblemException: public std::runtime_error {
public:
	AigProblemException(const std::string& message):
		std::runtime_error(message)
	{}

};

class AigProblemBuilder;
class AigCertificateBuilder;
class AigProblem {
public:
	static const AigEdge CONSTANT_1 = 1;
	static const AigEdge CONSTANT_0 = 0;

	ssize_t GetInputCount() const { return inputs.size(); }
	ssize_t GetLatchCount() const { return latches.size(); }
	ssize_t GetAndCount() const { return ands.size(); }
	ssize_t GetOutputCount() const { return outputs.size(); }
	ssize_t GetBadCount() const { return bads.size(); }
	ssize_t GetConstraintCount() const { return constraints.size(); }
	const std::vector<AigNode>& GetNodes() const { return nodes; }
	const std::vector<AigEdge>& GetInputs() const { return inputs; }
	const std::vector<AigEdge>& GetLatches() const { return latches; }
	const std::vector<AigEdge>& GetAnds() const { return ands; }
	const std::vector<AigEdge>& GetOutputs() const { return outputs; }
	const std::vector<AigEdge>& GetBads() const { return bads; }
	const std::vector<AigEdge>& GetConstraints() const { return constraints; }
	const std::vector<std::string>& GetComments() const { return comments; }

private:
	friend class AigProblemBuilder;
	friend class AigCertificateBuilder;

	AigProblem(
		std::vector<AigNode> nodes,
		std::vector<AigEdge> inputs,
		std::vector<AigEdge> latches,
		std::vector<AigEdge> ands,
		std::vector<AigEdge> outputs,
		std::vector<AigEdge> bads,
		std::vector<AigEdge> constraints,
		std::vector<std::string> comments
	);

	std::vector<AigNode> nodes;
	std::vector<AigEdge> inputs;
	std::vector<AigEdge> latches;
	std::vector<AigEdge> ands;
	std::vector<AigEdge> outputs;
	std::vector<AigEdge> bads;
	std::vector<AigEdge> constraints;
	std::vector<std::string> comments;

};
class AigProblemBuilder {
public:
	static const AigEdge CONSTANT_1 = 1;
	static const AigEdge CONSTANT_0 = 0;

	AigProblemBuilder();

	AigEdge AddInput();
	AigEdge AddLatch(AigEdge nextState, AigEdge resetState);
	AigEdge AddAnd(AigEdge leftEdge, AigEdge rightEdge);
	AigEdge AddAnd(std::vector<AigEdge> edges);
	AigEdge AddOr(AigEdge leftEdge, AigEdge rightEdge);
	AigEdge AddOr(std::vector<AigEdge> edges);
	void AddInput(AigEdge input);
	void AddLatch(AigEdge latch, AigEdge nextState, AigEdge resetState);
	void AddAnd(AigEdge node, AigEdge leftEdge, AigEdge rightEdge);
	void AddOutput(AigEdge edge);
	void AddBad(AigEdge edge);
	void AddConstraint(AigEdge edge);
	void AddComment(std::string comment);
	void Check() const;
	std::tuple<AigProblem, BmcProblem> Build(bool encodeOutputs = false);
	void Clear();

	ssize_t GetInputCount() const { return inputs.size(); }
	ssize_t GetLatchCount() const { return latches.size(); }
	ssize_t GetAndCount() const { return ands.size(); }
	ssize_t GetOutputCount() const { return outputs.size(); }
	ssize_t GetBadCount() const { return bads.size(); }
	ssize_t GetConstraintCount() const { return constraints.size(); }
	const std::vector<AigNode>& GetNodes() const { return nodes; }
	const std::vector<AigEdge>& GetInputs() const { return inputs; }
	const std::vector<AigEdge>& GetLatches() const { return latches; }
	const std::vector<AigEdge>& GetAnds() const { return ands; }
	const std::vector<AigEdge>& GetOutputs() const { return outputs; }
	const std::vector<AigEdge>& GetBads() const { return bads; }
	const std::vector<AigEdge>& GetConstraints() const { return constraints; }
	const std::vector<std::string>& GetComments() const { return comments; }

protected:
	std::vector<AigNode> nodes;
	std::vector<AigEdge> inputs;
	std::vector<AigEdge> latches;
	std::vector<AigEdge> ands;
	std::vector<AigEdge> outputs;
	std::vector<AigEdge> bads;
	std::vector<AigEdge> constraints;
	std::vector<std::string> comments;

};

using AigCertificate = AigProblem;
class AigCertificateBuilder: private AigProblemBuilder {
public:
	AigCertificateBuilder();
	AigCertificate Build(const AigProblem& problem, const BmcCertificate& certificate);

};

};
