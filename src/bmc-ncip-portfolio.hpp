// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <tuple>
#include <future>

#include "./bmc-ncip.hpp"

namespace Ncip {

template<typename... Solvers>
class PortfolioBmcSolver {
private:
	std::tuple<Solvers...> solvers;

public:
	PortfolioBmcSolver(Solvers... args):
		solvers(std::forward<Solvers>(args)...)
	{ }

	void Interrupt() {
		auto interrupt = [](auto& solver) {
			solver.Interrupt();
		};
		std::apply([&](auto&... solver) {
			(interrupt(solver), ...);
		}, solvers);
	}

	void ClearInterrupt() {
		auto clear_interrupt = [](auto& solver) {
			solver.ClearInterrupt();
		};
		std::apply([&](auto&... solver) {
			(clear_interrupt(solver), ...);
		}, solvers);
	}

	bool IsInterrupted() const {
		bool finalResult = false;
		auto update_result = [&finalResult](auto& result){
			finalResult |= result;
		};
		std::apply([&](auto&... solver) {
			(update_result(solver.IsInterrupted()), ...);
		}, solvers);
		return finalResult;
	}

	BmcResult Solve() {
		auto processes {
			std::apply([&](auto&... solvers) {
				return std::make_tuple(
					// Using a pointer for std::async here as it expects to be able to create a copy.
					std::async(std::launch::async, [&](auto* solver) {
						auto result { solver->Solve() };
						auto status { result.GetStatus() };
						//std::cerr << "Solver " << to_string(typename std::decay_t<decltype(*solver)>::BackendTag {}) << " returned ";
						//switch (status) {
						//case Ncip::BmcStatus::Sat: std::cerr << "Sat" << std::endl; break;
						//case Ncip::BmcStatus::Unsat: std::cerr << "Unsat" << std::endl; break;
						//case Ncip::BmcStatus::DepthLimitReached: std::cerr << "DepthLimitReached" << std::endl; break;
						//case Ncip::BmcStatus::CraigLimitReached: std::cerr << "CraigLimitReached" << std::endl; break;
						//case Ncip::BmcStatus::MemoryLimitReached: std::cerr << "MemoryLimitReached" << std::endl; break;
						//case Ncip::BmcStatus::Interrupted: std::cerr << "Interrupted" << std::endl; break;
						//}
						if (status == Ncip::BmcStatus::Sat || status == Ncip::BmcStatus::Unsat) {
							// Notify the other solvers that a result was found.
							Interrupt();
						}
						return std::make_tuple(result, solver);
					}, &solvers)...
				);
			}, solvers)
		};

		auto finalResult { Ncip::BmcResult::ForUserInterrupt(-1) };
		// Take the result by value since that is the result of the solver and future.get() method.
		auto update_result = [&finalResult](auto result, auto* solver) {
			switch (result.GetStatus()) {
			case Ncip::BmcStatus::Sat:
				assert (finalResult.GetStatus() != Ncip::BmcStatus::Unsat);
				finalResult = result;
				break;

			case Ncip::BmcStatus::Unsat:
				assert (finalResult.GetStatus() != Ncip::BmcStatus::Sat);
				finalResult = result;
				break;

			case Ncip::BmcStatus::DepthLimitReached:
			case Ncip::BmcStatus::CraigLimitReached:
			case Ncip::BmcStatus::MemoryLimitReached:
				if (finalResult.GetStatus() == Ncip::BmcStatus::Interrupted) {
					finalResult = result;
				}
				break;

			case Ncip::BmcStatus::Interrupted:
				break;
			}
		};
		std::apply([&](auto&... processes) {
			(std::apply(update_result, processes.get()), ...);
		}, processes);
		return finalResult;
	}

};

}
