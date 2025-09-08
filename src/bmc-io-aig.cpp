// SPDX-License-Identifier: MIT OR Apache-2.0

#include "bmc-io-aig.hpp"

#include <aiger.h>

namespace Ncip {

static int aig_generic_read(std::istream* state) {
	return state->get();
}

static int aig_generic_write(char c, std::ostream* state) {
	return (state->put(c).fail() ? EOF : 0);
}

std::tuple<AigProblem, BmcProblem> ParseAigerProblem(std::istream& input) {
	if (input.bad()) {
		throw AigerIoException("Bad AIGER problem input stream");
	}

	auto graph = aiger_init();
	if (auto error = aiger_read_generic(graph, &input,
			reinterpret_cast<aiger_get>(aig_generic_read)); error != nullptr) {
		throw AigerIoException("Could not read Aiger file: \"" + std::string(error) + "\"");
	}
	if (auto error = aiger_check(graph); error != nullptr) {
		throw AigerIoException("Aiger graph has invalid structure: \"" + std::string(error) + "\"");
	}

	AigProblemBuilder builder;
	for (ssize_t variable { 0 }; variable <= graph->maxvar; variable++) {
		auto literal = variable * 2;
		if (aiger_is_constant(literal)) {
			continue; // Constant is already created by the builder
		} else if (auto symbol = aiger_is_input(graph, literal); symbol != nullptr) {
			builder.AddInput(symbol->lit);
		} else if (auto symbol = aiger_is_latch(graph, literal); symbol != nullptr) {
			builder.AddLatch(symbol->lit, symbol->next, symbol->reset);
		} else if (auto symbol = aiger_is_and(graph, literal); symbol != nullptr) {
			builder.AddAnd(symbol->lhs, symbol->rhs0, symbol->rhs1);
		}
	}
	// The outputs are not really used but we declare them for the BMC anyway.
	for (ssize_t index { 0 }; index < graph->num_outputs; index++) {
		builder.AddOutput(graph->outputs[index].lit);
	}
	for (ssize_t index { 0 }; index < graph->num_bad; index++) {
		builder.AddBad(graph->bad[index].lit);
	}
	for (ssize_t index { 0 }; index < graph->num_constraints; index++) {
		builder.AddConstraint(graph->constraints[index].lit);
	}
	for (char** comment { graph->comments }; *comment; comment++) {
		builder.AddComment(*comment);
	}
	aiger_reset(graph);

	try {
		return builder.Build();
	} catch (const AigProblemException& exception) {
		throw AigerIoException(std::string("Invalid AIGER problem: ") + exception.what());
	}
}

void ExportAigerProblem(std::ostream& output, const AigProblem& problem) {
	auto graph = aiger_init();
	for (auto& node : problem.GetNodes()) {
		if (node.type == AigNodeType::Input) {
			aiger_add_input(graph, node.nodeId, nullptr);
		} else if (node.type == AigNodeType::Latch) {
			aiger_add_latch(graph, node.nodeId, node.leftEdgeId, nullptr);
			aiger_add_reset(graph, node.nodeId, node.rightEdgeId);
		} else if (node.type == AigNodeType::And) {
			aiger_add_and(graph, node.nodeId, node.leftEdgeId, node.rightEdgeId);
		}
	}
	for (auto& output : problem.GetOutputs()) {
		aiger_add_output(graph, output, nullptr);
	}
	for (auto& bad : problem.GetBads()) {
		aiger_add_bad(graph, bad, nullptr);
	}
	for (auto& constraint : problem.GetConstraints()) {
		aiger_add_constraint(graph, constraint, nullptr);
	}
	for (auto& comment : problem.GetComments()) {
		aiger_add_comment(graph, comment.c_str());
	}

	if (auto error = aiger_check(graph); error != nullptr) {
		throw AigerIoException("Aiger graph has invalid structure: \"" + std::string(error) + "\"");
	}
	if (!aiger_write_generic(graph, aiger_mode::aiger_ascii_mode, &output,
			reinterpret_cast<aiger_put>(aig_generic_write))) {
		throw AigerIoException("Could not write Aiger file\"");
	}

	aiger_reset(graph);

	if (output.bad()) {
		throw AigerIoException("Bad AIGER problem output stream");
	}
}

void ExportAigerModel(std::ostream& output, const AigProblem& problem, const BmcModel& model) {
	// Use custom encoding here since X has to be transformed to x.
	auto const to_aigsim = [] (auto const& result) {
		switch (result) {
			case BmcAssignment::Positive: return "1"; break;
			case BmcAssignment::Negative: return "0"; break;
			case BmcAssignment::DontCare: return "x"; break;
			default: __builtin_unreachable();
		}
	};

	auto const& bads = (problem.GetBadCount() > 0u)
		? problem.GetBads()
		: problem.GetOutputs();

	// Header
	output << "1" << std::endl;
	for (size_t bad { 0u }; bad < bads.size(); bad++) {
		bool satisfied = false;
		for (size_t depth { 0u }; depth < model.GetTimeframes().size(); depth++) {
			auto result { model.GetTimeframe(depth)[bads[bad] / 2u] };
			satisfied |= ((result ^ (bads[bad] & 1u)) == BmcAssignment::Positive);
			if (satisfied) { break; }
		}
		if (satisfied) { output << "b" << bad; }
	}
	output << std::endl;

	// Initial state
	for (size_t index { 0u }; index < problem.GetLatchCount(); index++) {
		auto result { model.GetTimeframe(0)[problem.GetLatches()[index] / 2] };
		output << to_aigsim(result);
	}
	output << std::endl;

	// Input vectors
	for (size_t depth { 0u }; depth < model.GetTimeframes().size(); depth++) {
		for (size_t index { 0u }; index < problem.GetInputCount(); index++) {
			auto result { model.GetTimeframe(depth)[problem.GetInputs()[index] / 2] };
			output << to_aigsim(result);
		}
		output << std::endl;
	}
	output << "." << std::endl;

	if (output.bad()) {
		throw AigerIoException("Bad AIGER model output stream");
	}
}

void ExportAigerCertificate(std::ostream& output, const AigProblem& problem, const BmcCertificate& certificate) {
	try {
		ExportAigerProblem(output, AigCertificateBuilder {}.Build(problem, certificate));
	} catch (const AigerIoException& exception) {
		throw AigerIoException("Bad AIGER certificate output stream");
	}
}

};
