// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <iostream>
#include <stdexcept>

#include "bmc-format-aig.hpp"

namespace Ncip {

std::tuple<Ncip::AigProblem, Ncip::BmcProblem> ParseAigerProblem(std::istream& input);
void ExportAigerProblem(std::ostream& output, const Ncip::AigProblem& problem);
void ExportAigerModel(std::ostream& output, const Ncip::AigProblem& problem, const Ncip::BmcModel& model);
void ExportAigerCertificate(std::ostream& output, const Ncip::AigProblem& problem, const Ncip::BmcCertificate& certificate);

class AigerIoException:
	public std::runtime_error {
public:
	AigerIoException(const std::string& message):
		std::runtime_error(message)
	{}

};

};
