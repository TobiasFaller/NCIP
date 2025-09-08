// SPDX-License-Identifier: MIT OR Apache-2.0

#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <fstream>
#include <sstream>
#include <csignal>
#include <functional>
#include <tuple>
#include <memory>
#include <variant>

#include "bmc-io-aig.hpp"
#include "bmc-io-cip.hpp"
#include "bmc-io-dimspec.hpp"
#include "bmc-problem.hpp"
#include "bmc-ncip.hpp"
#include "bmc-ncip-portfolio.hpp"

#if defined(NCIP_SOLVER_MINICRAIG)
	static auto CreateSolver(Ncip::BmcProblem problem, Ncip::BmcConfiguration configuration) -> auto {
		return Ncip::MiniCraigBmcSolver(problem, configuration);
	}
#elif defined(NCIP_SOLVER_MINICRAIG_DEBUG)
	static auto CreateSolver(Ncip::BmcProblem problem, Ncip::BmcConfiguration configuration) -> auto {
		return Ncip::MiniCraigDebugBmcSolver(problem, configuration);
	}
#elif defined(NCIP_SOLVER_CADICRAIG)
	static auto CreateSolver(Ncip::BmcProblem problem, Ncip::BmcConfiguration configuration) -> auto {
		return Ncip::CadiCraigBmcSolver(problem, configuration);
	}
#elif defined(NCIP_SOLVER_CADICRAIG_DEBUG)
	static auto CreateSolver(Ncip::BmcProblem problem, Ncip::BmcConfiguration configuration) -> auto {
		return Ncip::CadiCraigDebugBmcSolver(problem, configuration);
	}
#elif defined(NCIP_SOLVER_KITTENCRAIG)
	static auto CreateSolver(Ncip::BmcProblem problem, Ncip::BmcConfiguration configuration) -> auto {
		return Ncip::KittenCraigBmcSolver(problem, configuration);
	}
#elif defined(NCIP_SOLVER_PORTFOLIO)
	#ifdef NCIP_BACKEND_MINICRAIG
		static auto CreateMiniCraig(const Ncip::BmcProblem& problem, Ncip::BmcConfiguration configuration, Ncip::PreprocessLevel preprocessing, bool craig) -> auto {
			configuration.SetEnableCraigInterpolation(craig);
			configuration.SetPreprocessInit(preprocessing);
			configuration.SetPreprocessTrans(preprocessing);
			configuration.SetPreprocessTarget(preprocessing);
			configuration.SetPreprocessCraig((preprocessing >= Ncip::PreprocessLevel::Simple)
				? Ncip::PreprocessLevel::Simple : Ncip::PreprocessLevel::None);
			return Ncip::MiniCraigBmcSolver(problem, configuration);
		}
	#endif

	#ifdef NCIP_BACKEND_CADICRAIG
		static auto CreateCadiCraig(const Ncip::BmcProblem& problem, Ncip::BmcConfiguration configuration, Ncip::PreprocessLevel preprocessing, bool craig) -> auto {
			configuration.SetEnableCraigInterpolation(craig);
			configuration.SetPreprocessInit(preprocessing);
			configuration.SetPreprocessTrans(preprocessing);
			configuration.SetPreprocessTarget(preprocessing);
			configuration.SetPreprocessCraig((preprocessing >= Ncip::PreprocessLevel::Simple)
				? Ncip::PreprocessLevel::Simple : Ncip::PreprocessLevel::None);
			return Ncip::CadiCraigBmcSolver(problem, configuration);
		}
	#endif

	#ifdef NCIP_BACKEND_KITTENCRAIG
		static auto CreateKittenCraig(const Ncip::BmcProblem& problem, Ncip::BmcConfiguration configuration, Ncip::PreprocessLevel preprocessing, bool craig) -> auto {
			configuration.SetEnableCraigInterpolation(craig);
			configuration.SetPreprocessInit(preprocessing);
			configuration.SetPreprocessTrans(preprocessing);
			configuration.SetPreprocessTarget(preprocessing);
			configuration.SetPreprocessCraig((preprocessing >= Ncip::PreprocessLevel::Simple)
				? Ncip::PreprocessLevel::Simple : Ncip::PreprocessLevel::None);
			return Ncip::KittenCraigBmcSolver(problem, configuration);
		}
	#endif

	template<typename... Solver>
	static auto CreatePortfolioSolver(bool ignored, Solver... solver) -> auto {
		return Ncip::PortfolioBmcSolver(std::move(solver)...);
	}
	static auto CreateSolver(Ncip::BmcProblem problem, Ncip::BmcConfiguration configuration) -> auto {
		return CreatePortfolioSolver(
			false // Ignore me
			#ifdef NCIP_BACKEND_MINICRAIG
				, CreateMiniCraig(problem, configuration, Ncip::PreprocessLevel::None, true)
				, CreateMiniCraig(problem, configuration, Ncip::PreprocessLevel::Simple, true)
				, CreateMiniCraig(problem, configuration, Ncip::PreprocessLevel::Expensive, true)
				, CreateMiniCraig(problem, configuration, Ncip::PreprocessLevel::None, false)
				, CreateMiniCraig(problem, configuration, Ncip::PreprocessLevel::Simple, false)
				, CreateMiniCraig(problem, configuration, Ncip::PreprocessLevel::Expensive, false)
			#endif
			#ifdef NCIP_BACKEND_CADICRAIG
				, CreateCadiCraig(problem, configuration, Ncip::PreprocessLevel::None, true)
				, CreateCadiCraig(problem, configuration, Ncip::PreprocessLevel::Simple, true)
				, CreateCadiCraig(problem, configuration, Ncip::PreprocessLevel::Expensive, true)
				, CreateCadiCraig(problem, configuration, Ncip::PreprocessLevel::None, false)
				, CreateCadiCraig(problem, configuration, Ncip::PreprocessLevel::Simple, false)
				, CreateCadiCraig(problem, configuration, Ncip::PreprocessLevel::Expensive, false)
			#endif
			#ifdef NCIP_BACKEND_KITTENCRAIG
				, CreateKittenCraig(problem, configuration, Ncip::PreprocessLevel::None, true)
				, CreateKittenCraig(problem, configuration, Ncip::PreprocessLevel::Simple, true)
				, CreateKittenCraig(problem, configuration, Ncip::PreprocessLevel::Expensive, true)
				, CreateKittenCraig(problem, configuration, Ncip::PreprocessLevel::None, false)
				, CreateKittenCraig(problem, configuration, Ncip::PreprocessLevel::Simple, false)
				, CreateKittenCraig(problem, configuration, Ncip::PreprocessLevel::Expensive, false)
			#endif
		);
	}
#else
	#error "No backend selected"
#endif

template<class... Ts> struct overload : Ts... { using Ts::operator()...; };
template<class... Ts> overload(Ts...) -> overload<Ts...>; // line not needed in C++20...

auto const constexpr catch_io = []<typename... T>(auto func) -> auto {
	return [func](T... params) -> auto {
		try {
			if constexpr (std::is_void_v<std::invoke_result_t<decltype(func), T...>>) {
				func(params...);
			} else {
				return func(params...);
			}
		} catch (const std::bad_alloc& exception) {
			std::cerr << "Error: Out of Memory" << std::endl;
			exit(3);
		} catch (const Ncip::CipIoException& exception) {
			std::cerr << "Error: CIP I/O exception: " << exception.what() << std::endl;
			exit(3);
		} catch (const Ncip::AigerIoException& exception) {
			std::cerr << "Error: AIGER I/O exception: " << exception.what() << std::endl;
			exit(3);
		} catch (const Ncip::DimspecIoException& exception) {
			std::cerr << "Error: DIMSPEC I/O exception: " << exception.what() << std::endl;
			exit(3);
		}
	};
};

enum class InputFormat { NONE, CIP, AIGER, DIMSPEC };

void usage() {
	std::cerr << "Usage: ncip [options] <input-file> (<output-file>)" << std::endl;
	std::cerr << std::endl;
	std::cerr << "Options:" << std::endl;
	std::cerr << "  --help: Shows this usage information" << std::endl;
	std::cerr << "  --log=<level>: Log level (none, competition, info, debug, trace, extended-trace, full-trace); default: minimal" << std::endl;
	std::cerr << "  --format=<format>: Input format (cip, aiger, dimspec); default: aiger" << std::endl;
	std::cerr << "  --check-problem=<b>: Enable (yes) / disable (no) checking for correct CNF specification; default: no" << std::endl;
	std::cerr << "  --solve-problem=<b>: Enable (yes) / disable (no) solving of BMC problem; default: yes" << std::endl;
	std::cerr << "  --max-depth=<n>: Maximum number of timeframes; default: 100" << std::endl;
	std::cerr << "  --interpolant=<i>: Craig interpolant (symmetric, asymmetric, dual-symmetric, dual-asymmetric, intersection, union, smallest, largest); default: smallest" << std::endl;
	std::cerr << "  --craig-interpolation=<b>: Enable (yes) / disable (no) Craig interpolation; default yes" << std::endl;
	std::cerr << "  --fixed-point-check=<b>: Enable (yes) / disable (no) fix-point check; depends on craig interpolation; default yes" << std::endl;
	std::cerr << "Preprocessing / optimizations:" << std::endl;
	std::cerr << "  --preprocess-init=<p>: Disabled (no) / quick (quick) / advanced (expensive) preprocessing of initial state; default: depends on backend" << std::endl;
	std::cerr << "  --preprocess-trans=<p>: Disabled (no) / quick (quick) / advanced (expensive) preprocessing of transition relation; default: depends on backend" << std::endl;
	std::cerr << "  --preprocess-target=<p>: Disabled (no) / quick (quick) / advanced (expensive) preprocessing of target state; default depends: on backend" << std::endl;
	std::cerr << "  --preprocess-craig=<p>: Disabled (no) / quick (quick) / advanced (expensive) preprocessing of Craig interpolant; default depends: on backend" << std::endl;
	std::cerr << "  --total-trans=<b>: Assume (yes) / don't assume (no) a total transition relation; default: no" << std::endl;
	std::cerr << "  --sanity-check-problem=<b>: Enable (yes) / disable (no) sanity checks (unsatisfiability of init, trans, target combinations); default: yes" << std::endl;
	std::cerr << "Export:" << std::endl;
	std::cerr << "  --export-problem=<path>: Export problem to <path> (format depends on input format)" << std::endl;
	std::cerr << "  --export-result=<path>: Export result to <path> (format depends on input format)" << std::endl;
	std::cerr << "  --export-model=<path>: Export model in SAT case to <path> (format depends on input format)" << std::endl;
	std::cerr << "  --export-certificate=<path>: Export certificate in UNSAT case to <path> (format depends on input format)" << std::endl;
#ifndef NDEBUG
	std::cerr << "Debug:" << std::endl;
	std::cerr << "  --debug-path=<p>: Path for writing debug files" << std::endl;
	std::cerr << "  --debug-clause-export=<b>: Enable (yes) / disable (no) export of clauses" << std::endl;
	std::cerr << "  --debug-bmc-export=<b>: Enable (yes) / disable (no) trace of BMC problem at each depth" << std::endl;
#endif
}

static std::function<void(int)> signalHandler;
static std::atomic<bool> interrupted;

int main(int argc, char** argv) {
	std::setbuf(stdout, NULL);
	std::setbuf(stderr, NULL);

	signalHandler = [&](int code) { interrupted.store(true, std::memory_order_release); };
	std::signal(SIGINT, [](int code) { signalHandler(code); });
	std::signal(SIGXCPU, [](int code) { signalHandler(code); });

	if (argc < 2) {
		usage();
		exit(1);
	}

	Ncip::BmcConfiguration bmcConfiguration { };
	bmcConfiguration.SetLogLevel(Ncip::LogLevel::Minimal);
	bmcConfiguration.SetMaximumDepth(0u);
	bmcConfiguration.SetMaximumCraigSize(0u);
#ifndef NDEBUG
	bmcConfiguration.SetClauseExport(false);
	bmcConfiguration.SetBmcExport(false);
#endif
	bmcConfiguration.SetCraigInterpolant(Ncip::CraigInterpolant::Smallest);
	bmcConfiguration.SetEnableCraigInterpolation(true);
	bmcConfiguration.SetEnableFixPointCheck(true);
	bmcConfiguration.SetEnableSanityChecks(true);
	bmcConfiguration.SetPreprocessInit(Ncip::PreprocessLevel::Expensive);
	bmcConfiguration.SetPreprocessTrans(Ncip::PreprocessLevel::Expensive);
	bmcConfiguration.SetPreprocessTarget(Ncip::PreprocessLevel::Expensive);
	bmcConfiguration.SetPreprocessCraig(Ncip::PreprocessLevel::Simple);

	std::vector<std::string> freeArguments;
	InputFormat format { InputFormat::NONE };
	bool checkProblem { false };
	bool solveProblem { true };
	std::string exportProblemPath { "" };
	std::string exportResultPath { "" };
	std::string exportModelPath { "" };
	std::string exportCertificatePath { "" };
	for (int argi { 1 }; argi < argc; argi++) {
		const std::string argument { argv[argi] };

		if (argument.rfind("--help", 0u) == 0u) {
			usage();
			exit(0);
		} else if (argument.rfind("--log=", 0u) == 0u) {
			std::cerr << "Setting log to \"" << argument.substr(6) << "\"" << std::endl;
			if (argument == "--log=none") { bmcConfiguration.SetLogLevel(Ncip::LogLevel::None); }
			else if (argument == "--log=competition") { bmcConfiguration.SetLogLevel(Ncip::LogLevel::Competition); }
			else if (argument == "--log=minimal") { bmcConfiguration.SetLogLevel(Ncip::LogLevel::Minimal); }
			else if (argument == "--log=info") { bmcConfiguration.SetLogLevel(Ncip::LogLevel::Info); }
#ifndef NDEBUG
			else if (argument == "--log=debug") { bmcConfiguration.SetLogLevel(Ncip::LogLevel::Debug); }
			else if (argument == "--log=trace") { bmcConfiguration.SetLogLevel(Ncip::LogLevel::Trace); }
			else if (argument == "--log=extended-trace") { bmcConfiguration.SetLogLevel(Ncip::LogLevel::ExtendedTrace); }
			else if (argument == "--log=full-trace") { bmcConfiguration.SetLogLevel(Ncip::LogLevel::FullTrace); }
#else
			else if (argument == "--log=debug") { std::cerr << "Error: Log level debug is disabled in release mode" << std::endl; exit(2); }
			else if (argument == "--log=trace") { std::cerr << "Error: Log level trace is disabled in release mode" << std::endl; exit(2); }
			else if (argument == "--log=extended-trace") { std::cerr << "Error: Log level extended-trace is disabled in release mode" << std::endl; exit(2); }
			else if (argument == "--log=full-trace") { std::cerr << "Error: Log level full-trace is disabled in release mode" << std::endl; exit(2); }
#endif
			else {
				std::cerr << "Error: Unknown value for log \"" << argument.substr(6) << "\"" << std::endl;
				exit(2);
			}
		} else if (argument.rfind("--format=", 0u) == 0u) {
			std::cerr << "Setting format to \"" << argument.substr(9u) << "\"" << std::endl;
			if (argument == "--format=cip") { format = InputFormat::CIP; }
			else if (argument == "--format=aiger") { format = InputFormat::AIGER; }
			else if (argument == "--format=dimspec") { format = InputFormat::DIMSPEC; }
			else {
				std::cerr << "Error: Unknown value for format \"" << argument.substr(9u) << "\"" << std::endl;
				exit(2);
			}
		} else if (argument.rfind("--max-depth=", 0u) == 0u) {
			std::cerr << "Setting max-depth to \"" << argument.substr(12u) << "\"" << std::endl;
			std::stringstream stream(argument.substr(12u));

			size_t maximumDepth;
			stream >> maximumDepth;
			bmcConfiguration.SetMaximumDepth(maximumDepth);
			if (!stream.eof()) {
				std::cerr << "Error: Unknown value for max-depth \"" << argument.substr(12u) << "\"" << std::endl;
				exit(2);
			}
		} else if (argument.rfind("--export-problem=", 0u) == 0u) {
			std::cerr << "Setting export-problem to \"" << argument.substr(17u) << "\"" << std::endl;
			exportProblemPath = argument.substr(17u);
		} else if (argument.rfind("--export-result=", 0u) == 0u) {
			std::cerr << "Setting export-result to \"" << argument.substr(16u) << "\"" << std::endl;
			exportResultPath = argument.substr(16u);
		} else if (argument.rfind("--export-model=", 0u) == 0u) {
			std::cerr << "Setting export-model to \"" << argument.substr(15u) << "\"" << std::endl;
			exportModelPath = argument.substr(15u);
		} else if (argument.rfind("--export-certificate=", 0u) == 0u) {
			std::cerr << "Setting export-certificate to \"" << argument.substr(21u) << "\"" << std::endl;
			exportCertificatePath = argument.substr(21u);
		} else if (argument.rfind("--interpolant=", 0u) == 0u) {
			std::cerr << "Setting interpolant to \"" << argument.substr(14u) << "\"" << std::endl;
			if (argument == "--interpolant=symmetric") { bmcConfiguration.SetCraigInterpolant(Ncip::CraigInterpolant::Symmetric); }
			else if (argument == "--interpolant=asymmetric") { bmcConfiguration.SetCraigInterpolant(Ncip::CraigInterpolant::Asymmetric); }
			else if (argument == "--interpolant=dual-symmetric") { bmcConfiguration.SetCraigInterpolant(Ncip::CraigInterpolant::DualSymmetric); }
			else if (argument == "--interpolant=dual-asymmetric") { bmcConfiguration.SetCraigInterpolant(Ncip::CraigInterpolant::DualAsymmetric); }
			else if (argument == "--interpolant=intersection") { bmcConfiguration.SetCraigInterpolant(Ncip::CraigInterpolant::Intersection); }
			else if (argument == "--interpolant=union") { bmcConfiguration.SetCraigInterpolant(Ncip::CraigInterpolant::Union); }
			else if (argument == "--interpolant=smallest") { bmcConfiguration.SetCraigInterpolant(Ncip::CraigInterpolant::Smallest); }
			else if (argument == "--interpolant=largest") { bmcConfiguration.SetCraigInterpolant(Ncip::CraigInterpolant::Largest); }
			else {
				std::cerr << "Error: Unknown value for interpolant \"" << argument.substr(14u) << "\"" << std::endl;
				exit(2);
			}
		} else if (argument.rfind("--craig-interpolation=", 0u) == 0u) {
			std::cerr << "Setting craig-interpolation to \"" << argument.substr(22u) << "\"" << std::endl;
			if (argument == "--craig-interpolation=yes") { bmcConfiguration.SetEnableCraigInterpolation(true); }
			else if (argument == "--craig-interpolation=no") { bmcConfiguration.SetEnableCraigInterpolation(false); }
			else {
				std::cerr << "Error: Unknown value for craig-interpolation \"" << argument.substr(22u) << "\"" << std::endl;
				exit(2);
			}
		} else if (argument.rfind("--fixed-point-check=", 0u) == 0u) {
			std::cerr << "Setting fixed-point-check to \"" << argument.substr(20u) << "\"" << std::endl;
			if (argument == "--fixed-point-check=yes") { bmcConfiguration.SetEnableFixPointCheck(true); }
			else if (argument == "--fixed-point-check=no") { bmcConfiguration.SetEnableFixPointCheck(false); }
			else {
				std::cerr << "Error: Unknown value for fixed-point-check \"" << argument.substr(20u) << "\"" << std::endl;
				exit(2);
			}
		} else if (argument.rfind("--sanity-check-problem=", 0u) == 0u) {
			std::cerr << "Setting sanity-check to \"" << argument.substr(23u) << "\"" << std::endl;
			if (argument == "--sanity-check-problem=yes") { bmcConfiguration.SetEnableSanityChecks(true); }
			else if (argument == "--sanity-check-problem=no") { bmcConfiguration.SetEnableSanityChecks(false); }
			else {
				std::cerr << "Error: Unknown value for sanity-check \"" << argument.substr(23u) << "\"" << std::endl;
				exit(2);
			}
		} else if (argument.rfind("--preprocess-init=", 0u) == 0u) {
			std::cerr << "Setting preprocess-init to \"" << argument.substr(18u) << "\"" << std::endl;
			if (argument == "--preprocess-init=expensive") { bmcConfiguration.SetPreprocessInit(Ncip::PreprocessLevel::Expensive); }
			else if (argument == "--preprocess-init=quick") { bmcConfiguration.SetPreprocessInit(Ncip::PreprocessLevel::Simple); }
			else if (argument == "--preprocess-init=no") { bmcConfiguration.SetPreprocessInit(Ncip::PreprocessLevel::None); }
			else {
				std::cerr << "Error: Unknown value for preprocess-init \"" << argument.substr(18u) << "\"" << std::endl;
				exit(2);
			}
		} else if (argument.rfind("--preprocess-trans=", 0u) == 0u) {
			std::cerr << "Setting preprocess-trans to \"" << argument.substr(19u) << "\"" << std::endl;
			if (argument == "--preprocess-trans=expensive") { bmcConfiguration.SetPreprocessTrans(Ncip::PreprocessLevel::Expensive); }
			else if (argument == "--preprocess-trans=quick") { bmcConfiguration.SetPreprocessTrans(Ncip::PreprocessLevel::Simple); }
			else if (argument == "--preprocess-trans=no") { bmcConfiguration.SetPreprocessTrans(Ncip::PreprocessLevel::None); }
			else {
				std::cerr << "Error: Unknown value for preprocess-trans \"" << argument.substr(19u) << "\"" << std::endl;
				exit(2);
			}
		} else if (argument.rfind("--preprocess-target=", 0u) == 0u) {
			std::cerr << "Setting preprocess-target to \"" << argument.substr(20u) << "\"" << std::endl;
			if (argument == "--preprocess-target=expensive") { bmcConfiguration.SetPreprocessTarget(Ncip::PreprocessLevel::Expensive); }
			else if (argument == "--preprocess-target=quick") { bmcConfiguration.SetPreprocessTarget(Ncip::PreprocessLevel::Simple); }
			else if (argument == "--preprocess-target=no") { bmcConfiguration.SetPreprocessTarget(Ncip::PreprocessLevel::None); }
			else {
				std::cerr << "Error: Unknown value for preprocess-target \"" << argument.substr(20u) << "\"" << std::endl;
				exit(2);
			}
		} else if (argument.rfind("--preprocess-craig=", 0u) == 0u) {
			std::cerr << "Setting preprocess-craig to \"" << argument.substr(19u) << "\"" << std::endl;
			if (argument == "--preprocess-craig=expensive") { bmcConfiguration.SetPreprocessCraig(Ncip::PreprocessLevel::Expensive); }
			else if (argument == "--preprocess-craig=quick") { bmcConfiguration.SetPreprocessCraig(Ncip::PreprocessLevel::Simple); }
			else if (argument == "--preprocess-craig=no") { bmcConfiguration.SetPreprocessCraig(Ncip::PreprocessLevel::None); }
			else {
				std::cerr << "Error: Unknown value for preprocess-craig \"" << argument.substr(19u) << "\"" << std::endl;
				exit(2);
			}
		} else if (argument.rfind("--check-problem=", 0u) == 0u) {
			std::cerr << "Setting check-problem to \"" << argument.substr(16u) << "\"" << std::endl;
			if (argument == "--check-problem=yes") { checkProblem = true; }
			else if (argument == "--check-problem=no") { checkProblem = false; }
			else {
				std::cerr << "Error: Unknown value for check-problem \"" << argument.substr(16u) << "\"" << std::endl;
				exit(2);
			}
		} else if (argument.rfind("--solve-problem=", 0u) == 0u) {
			std::cerr << "Setting solve-problem to \"" << argument.substr(16u) << "\"" << std::endl;
			if (argument == "--solve-problem=yes") { solveProblem = true; }
			else if (argument == "--solve-problem=no") { solveProblem = false; }
			else {
				std::cerr << "Error: Unknown value for solve-problem \"" << argument.substr(16u) << "\"" << std::endl;
				exit(2);
			}
		} else if (argument.rfind("--total-trans=", 0u) == 0u) {
			std::cerr << "Setting total-trans to \"" << argument.substr(14u) << "\"" << std::endl;
			if (argument == "--total-trans=yes") { bmcConfiguration.SetTotalTransitionRelation(true); }
			else if (argument == "--total-trans=no") { bmcConfiguration.SetTotalTransitionRelation(false); }
			else {
				std::cerr << "Error: Unknown value for total-trans \"" << argument.substr(14u) << "\"" << std::endl;
				exit(2);
			}
		} else if (argument.rfind("--debug-clause-export=", 0u) == 0u) {
#ifndef NDEBUG
			std::cerr << "Setting clause-export to \"" << argument.substr(22u) << "\"" << std::endl;
			if (argument == "--debug-clause-export=yes") { bmcConfiguration.SetClauseExport(true); }
			else if (argument == "--debug-clause-export=no") { bmcConfiguration.SetClauseExport(false); }
			else {
				std::cerr << "Error: Unknown value for clause-export \"" << argument.substr(22u) << "\"" << std::endl;
				exit(2);
			}
#else
			std::cerr << "Error: Option clause-export is disabled in release mode." << std::endl;
			exit(2);
#endif
		} else if (argument.rfind("--debug-bmc-export=", 0u) == 0u) {
#ifndef NDEBUG
			std::cerr << "Setting bmc-export to \"" << argument.substr(19u) << "\"" << std::endl;
			if (argument == "--debug-bmc-export=yes") { bmcConfiguration.SetBmcExport(true); }
			else if (argument == "--debug-bmc-export=no") { bmcConfiguration.SetBmcExport(false); }
			else {
				std::cerr << "Error: Unknown value for bmc-export \"" << argument.substr(19u) << "\"" << std::endl;
				exit(2);
			}
#else
			std::cerr << "Error: Option bmc-export is disabled in release mode." << std::endl;
			exit(2);
#endif
		} else if (argument.rfind("--debug-path=", 0u) == 0u) {
#ifndef NDEBUG
			std::cerr << "Setting debug-path to \"" << argument.substr(14u) << "\"" << std::endl;
			bmcConfiguration.SetDebugPath(argument.substr(14u));
#else
			std::cerr << "Error: Option debug-path is disabled in release mode." << std::endl;
			exit(2);
#endif
		} else if (argument.rfind("--", 0u) == 0u) {
			std::cerr << "Error: Unknown argument \"" << argument << "\"" << std::endl;
			exit(2);
		} else {
			freeArguments.push_back(argument);
		}
	}

	if (freeArguments.size() == 0 || freeArguments.size() > 2) {
		usage();
		exit(1);
	}

	if (format == InputFormat::NONE) {
		std::cerr << "Warning: No input format specified. Assuming Aiger (.aig / .aag) format." << std::endl;
		format = InputFormat::AIGER;
	}
	if (bmcConfiguration.GetMaximumDepth() == 0u) {
		std::cerr << "Warning: No maximum depth specified. Assuming default of 100." << std::endl;
		bmcConfiguration.SetMaximumDepth(100u);
	}

#ifdef NCIP_SOLVER_PORTFOLIO
	switch (bmcConfiguration.GetLogLevel()) {
		case Ncip::LogLevel::None:
		case Ncip::LogLevel::Competition:
		case Ncip::LogLevel::Minimal:
			break;
		case Ncip::LogLevel::Info:
		case Ncip::LogLevel::Debug:
		case Ncip::LogLevel::Trace:
		case Ncip::LogLevel::ExtendedTrace:
		case Ncip::LogLevel::FullTrace:
			std::cerr << "Warning: Setting log-level to \"minimal\" as current logging mode creates unreadable, interleaved output." << std::endl;
			bmcConfiguration.SetLogLevel(Ncip::LogLevel::Minimal);
			break;
	}
#endif

	const std::string inputFile { freeArguments[0] };
	const std::string outputFile { (freeArguments.size() > 1) ? freeArguments[1] : "" };

	std::ifstream inputFStream;
	std::ofstream outputFStream;
	std::stringstream outputSStream { std::ios_base::out };

	std::istream& input {
		[&](const std::string& path) -> std::istream& {
			if (path == "-") {
				return std::cin;
			} else if (path.empty()) {
				std::cerr << "Error: Input file name is empty" << std::endl;
				exit(2);
			} else {
				inputFStream = std::ifstream { path };
				if (!inputFStream.good()) {
					std::cerr << "Error: Input file \"" << inputFile << "\" was not found" << std::endl;
					exit(2);
				}
				return inputFStream;
			}
		}(inputFile)
	};
	std::ostream& output {
		[&](const std::string& path) -> std::ostream& {
			if (path == "-") {
				return std::cout;
			} else if (path.empty()) {
				// In case of outputFile is an empty string we are writing into a dummy stream.
				return outputSStream;
			} else {
				outputFStream = std::ofstream { outputFile };
				if (!outputFStream.good()) {
					std::cerr << "Error: Output file \"" << outputFile << "\" could not be created" << std::endl;
					exit(2);
				}
				return outputFStream;
			}
		}(outputFile)
	};

	using Problem = std::variant<Ncip::CipProblem, Ncip::AigProblem, Ncip::DimspecProblem>;
	auto parse_problem = catch_io.operator()<std::istream&>(
		[&format](auto& input) -> std::tuple<Problem, Ncip::BmcProblem> {
			if (format == InputFormat::CIP) {
				std::cerr << "Parsing input as CIP format" << std::endl;
				auto [problem, bmcProblem] = Ncip::ParseCipProblem(input);
				return { problem, bmcProblem };
			}
			if (format == InputFormat::AIGER) {
				std::cerr << "Parsing input as AIGER format" << std::endl;
				auto [problem, bmcProblem] = Ncip::ParseAigerProblem(input);
				return { problem, bmcProblem };
			}
			if (format == InputFormat::DIMSPEC) {
				std::cerr << "Parsing input as DIMSPEC format" << std::endl;
				auto [problem, bmcProblem] = Ncip::ParseDimspecProblem(input);
				return { problem, bmcProblem };
			}
			__builtin_unreachable();
		}
	);
	auto const export_problem = catch_io.operator()<std::ostream&, Problem const&>(
		[](auto& output, auto& problem) -> void {
			std::visit(overload {
				[&](const Ncip::CipProblem& problem) { Ncip::ExportCipProblem(output, problem); },
				[&](const Ncip::AigProblem& problem) { Ncip::ExportAigerProblem(output, problem); },
				[&](const Ncip::DimspecProblem& problem) { Ncip::ExportDimspecProblem(output, problem); },
			}, problem);
		}
	);
	auto const export_model = catch_io.operator()<std::ostream&, Problem const&, Ncip::BmcModel const&>(
		[](auto& output, auto& problem, auto& model) -> void {
			std::visit(overload {
				[&](const Ncip::CipProblem& problem) { Ncip::ExportCipModel(output, problem, model); },
				[&](const Ncip::AigProblem& problem) { Ncip::ExportAigerModel(output, problem, model); },
				[&](const Ncip::DimspecProblem& problem) { Ncip::ExportDimspecModel(output, problem, model); },
			}, problem);
		}
	);
	auto const export_certificate = catch_io.operator()<std::ostream&, Problem const&, Ncip::BmcCertificate const&>(
		[](auto& output, auto& problem, auto& certificate) -> void {
			std::visit(overload {
				[&](const Ncip::CipProblem& problem) { Ncip::ExportCipCertificate(output, problem, certificate); },
				[&](const Ncip::AigProblem& problem) { Ncip::ExportAigerCertificate(output, problem, certificate); },
				[&](const Ncip::DimspecProblem& problem) { Ncip::ExportDimspecCertificate(output, problem, certificate); },
			}, problem);
		}
	);
	auto const export_options = [&](auto& output) {
		for (int argi { 1 }; argi < argc; argi++) {
			const std::string argument { argv[argi] };
			output << "Option: " << argument << std::endl;
		}
	};

	auto [problem, bmcProblem] = parse_problem(input);
	if (checkProblem) {
		std::cerr << "Checking problem for inconsistencies" << std::endl;
		try {
			bmcProblem.CheckProblem();
		} catch (const Ncip::BmcProblemError& error) {
			std::cerr << "Error: Found invalid BMC problem: " << error.what() << std::endl;
			exit(3);
		}
	}

	if (!exportProblemPath.empty()) {
		std::cerr << "Exporting problem to file \"" << exportProblemPath << "\"" << std::endl;
		std::ofstream stream { exportProblemPath };
		if (!stream.good()) {
			std::cerr << "Error: Problem output file \"" << exportProblemPath << "\" could not be created" << std::endl;
			exit(2);
		}
		export_problem(stream, problem);
	}

	if (!solveProblem) {
		std::cerr << "Not solving BMC problem" << std::endl;
		exit(0);
	}

	auto solver { CreateSolver(bmcProblem, bmcConfiguration) };
	std::cerr << "Solving BMC problem" << std::endl;

	// Check for interrupt as the user might have sent
	// a signal during problem parsing.
	signalHandler = [&](int code) { solver.Interrupt(); };
	if (interrupted.load(std::memory_order_acquire)) {
		std::cerr << "Result: INTERRUPTED" << std::endl;
		output << "Result: INTERRUPTED" << std::endl;
		output << "Exit: 40" << std::endl;
		output << "Depth: 0" << std::endl;
		output << "Runtime: 0 seconds" << std::endl;

		if (!exportResultPath.empty()) {
			std::cerr << "Exporting result to file \"" << exportResultPath << "\"" << std::endl;
			std::ofstream stream { exportResultPath };
			if (!stream.good()) {
				std::cerr << "Error: Result file \"" << exportResultPath << "\" could not be created" << std::endl;
				exit(2);
			}

			stream << "Result: INTERRUPTED" << std::endl;
			stream << "Exit: 40" << std::endl;
			stream << "Depth: 0" << std::endl;
			stream << "Runtime: 0 seconds" << std::endl;
			export_options(stream);
		}
		return 40;
	}

	auto startTime { std::chrono::steady_clock::now() };
	auto result { solver.Solve() };
	auto endTime { std::chrono::steady_clock::now() };
	auto solveTime { std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime).count() };

	auto& model { result.GetModel() };
	switch(result.GetStatus())
	{
		case Ncip::BmcStatus::Sat:
			std::cerr << "Result: SAT" << std::endl;
			output << "Result: SAT" << std::endl;
			output << "Exit: 10" << std::endl;
			output << "Depth: " << result.GetDepth() << std::endl;
			output << "Runtime: " << solveTime << " seconds" << std::endl;

			if (!exportModelPath.empty()) {
				std::cerr << "Exporting model to file \"" << exportModelPath << "\"" << std::endl;
				std::ofstream stream { exportModelPath };
				if (!stream.good()) {
					std::cerr << "Error: Model file \"" << exportModelPath << "\" could not be created" << std::endl;
					exit(2);
				}
				export_model(stream, problem, result.GetModel());
			}

			if (!exportResultPath.empty()) {
				std::cerr << "Exporting result to file \"" << exportResultPath << "\"" << std::endl;
				std::ofstream stream { exportResultPath };
				if (!stream.good()) {
					std::cerr << "Error: Result file \"" << exportResultPath << "\" could not be created" << std::endl;
					exit(2);
				}

				stream << "Result: SAT" << std::endl;
				stream << "Exit: 10" << std::endl;
				stream << "Depth: " << result.GetDepth() << std::endl;
				stream << "Runtime: " << solveTime << " seconds" << std::endl;
				export_options(stream);

				std::string line;
				std::stringstream buffer;
				export_model(buffer, problem, result.GetModel());
				while (std::getline(buffer, line)) {
					stream << "Model: " << line << std::endl;
				}
			}

			return 10;

		case Ncip::BmcStatus::Unsat:
			std::cerr << "Result: UNSAT" << std::endl;
			output << "Result: UNSAT" << std::endl;
			output << "Exit: 20" << std::endl;
			output << "Depth: " << result.GetDepth() << std::endl;
			output << "Runtime: " << solveTime << " seconds" << std::endl;

			if (!exportCertificatePath.empty()) {
				std::cerr << "Exporting certificate to file \"" << exportCertificatePath << "\"" << std::endl;
				std::ofstream stream { exportCertificatePath };
				if (!stream.good()) {
					std::cerr << "Error: Certificate file \"" << exportCertificatePath << "\" could not be created" << std::endl;
					exit(2);
				}
				export_certificate(stream, problem, result.GetCertificate());
			}

			if (!exportResultPath.empty()) {
				std::cerr << "Exporting result to file \"" << exportResultPath << "\"" << std::endl;
				std::ofstream stream { exportResultPath };
				if (!stream.good()) {
					std::cerr << "Error: Result file \"" << exportResultPath << "\" could not be created" << std::endl;
					exit(2);
				}

				stream << "Result: UNSAT" << std::endl;
				stream << "Exit: 20" << std::endl;
				stream << "Depth: " << result.GetDepth() << std::endl;
				stream << "Runtime: " << solveTime << " seconds" << std::endl;
				export_options(stream);

				std::string line;
				std::stringstream buffer;
				export_certificate(buffer, problem, result.GetCertificate());
				while (std::getline(buffer, line)) {
					stream << "Certificate: " << line << std::endl;
				}
			}

			return 20;

		case Ncip::BmcStatus::DepthLimitReached:
		case Ncip::BmcStatus::CraigLimitReached:
		case Ncip::BmcStatus::MemoryLimitReached:
			if (result.GetStatus() == Ncip::BmcStatus::DepthLimitReached) {
				std::cerr << "Result: DEPTH LIMIT" << std::endl;
				output << "Result: DEPTH LIMIT" << std::endl;
			} else if (result.GetStatus() == Ncip::BmcStatus::CraigLimitReached) {
				std::cerr << "Result: CRAIG LIMIT" << std::endl;
				output << "Result: CRAIG LIMIT" << std::endl;
			} else if (result.GetStatus() == Ncip::BmcStatus::MemoryLimitReached) {
				std::cerr << "Result: MEMORY LIMIT" << std::endl;
				output << "Result: MEMORY LIMIT" << std::endl;
			}
			output << "Exit: 30" << std::endl;
			output << "Depth: " << result.GetDepth() << std::endl;
			output << "Runtime: " << solveTime << " seconds" << std::endl;

			if (!exportResultPath.empty()) {
				std::cerr << "Exporting result to file \"" << exportResultPath << "\"" << std::endl;
				std::ofstream stream { exportResultPath };
				if (!stream.good()) {
					std::cerr << "Error: Result file \"" << exportResultPath << "\" could not be created" << std::endl;
					exit(2);
				}

				if (result.GetStatus() == Ncip::BmcStatus::DepthLimitReached) {
					stream << "Result: DEPTH LIMIT" << std::endl;
				} else if (result.GetStatus() == Ncip::BmcStatus::CraigLimitReached) {
					stream << "Result: CRAIG LIMIT" << std::endl;
				} else if (result.GetStatus() == Ncip::BmcStatus::MemoryLimitReached) {
					stream << "Result: MEMORY LIMIT" << std::endl;
				}
				stream << "Exit: 30" << std::endl;
				stream << "Depth: " << result.GetDepth() << std::endl;
				stream << "Runtime: " << solveTime << " seconds" << std::endl;
				export_options(stream);
			}

			return 30;

		case Ncip::BmcStatus::Interrupted:
			std::cerr << "Result: INTERRUPTED" << std::endl;
			output << "Result: INTERRUPTED" << std::endl;
			output << "Exit: 40" << std::endl;
			output << "Depth: " << result.GetDepth() << std::endl;
			output << "Runtime: " << solveTime << " seconds" << std::endl;

			if (!exportResultPath.empty()) {
				std::cerr << "Exporting result to file \"" << exportResultPath << "\"" << std::endl;
				std::ofstream stream { exportResultPath };
				if (!stream.good()) {
					std::cerr << "Error: Result file \"" << exportResultPath << "\" could not be created" << std::endl;
					exit(2);
				}

				stream << "Result: INTERRUPTED" << std::endl;
				stream << "Exit: 40" << std::endl;
				stream << "Depth: " << result.GetDepth() << std::endl;
				stream << "Runtime: " << solveTime << " seconds" << std::endl;
				export_options(stream);
			}

			return 40;

		default:
			std::cerr << "Error: Unhandled result from solver: "
				<< static_cast<std::underlying_type_t<Ncip::BmcStatus>>(result.GetStatus()) << std::endl;
			std::cerr << "Error: This is either an implementation error or a fault in the cpu or the memory" << std::endl;
			std::cerr << "Error: Please run a cpu and memory stress test to test for faults" << std::endl;
			exit(2);
	}
}
