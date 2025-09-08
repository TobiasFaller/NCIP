// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <iostream>
#include <stdexcept>

#include "bmc-format-dimspec.hpp"

namespace Ncip {

std::tuple<Ncip::DimspecProblem, Ncip::BmcProblem> ParseDimspecProblem(std::istream& input);
void ExportDimspecProblem(std::ostream& output, const Ncip::DimspecProblem& problem);
void ExportDimspecModel(std::ostream& output, const Ncip::DimspecProblem& problem, const Ncip::BmcModel& model);
void ExportDimspecCertificate(std::ostream& output, const Ncip::DimspecProblem& problem, const Ncip::BmcCertificate& certificate);

class DimspecIoException:
	public std::runtime_error {
public:
	DimspecIoException(const std::string& message):
		std::runtime_error(message)
	{}

};

};
