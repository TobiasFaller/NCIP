// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <iostream>
#include <stdexcept>

#include "bmc-format-cip.hpp"

namespace Ncip {

std::tuple<Ncip::CipProblem, Ncip::BmcProblem> ParseCipProblem(std::istream& input);
void ExportCipProblem(std::ostream& output, const Ncip::CipProblem& problem);
void ExportCipModel(std::ostream& output, const Ncip::CipProblem& problem, const Ncip::BmcModel& model);
void ExportCipCertificate(std::ostream& output, const Ncip::CipProblem& problem, const Ncip::BmcCertificate& certificate);

class CipIoException:
	public std::runtime_error {
public:
	CipIoException(const std::string& message):
		std::runtime_error(message)
	{}

};

};
