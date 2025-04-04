// SPDX-License-Identifier: MIT OR Apache-2.0

#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <fstream>
#include <sstream>
#include <regex>
#include <csignal>
#include <functional>
#include <tuple>
#include <memory>
#include <variant>

#include <aiger.h>

#include "bmc-format-aig.hpp"
#include "bmc-format-cip.hpp"
#include "bmc-format-dimspec.hpp"
#include "bmc-problem.hpp"
#include "bmc-ncip.hpp"
#include "bmc-ncip-portfolio.hpp"

#if defined(NCIP_SOLVER_MINICRAIG)
	using BmcSolver = Ncip::MiniCraigBmcSolver;
#elif defined(NCIP_SOLVER_MINICRAIG_DEBUG)
	using BmcSolver = Ncip::MiniCraigDebugBmcSolver;
#elif defined(NCIP_SOLVER_CADICRAIG)
	using BmcSolver = Ncip::CadiCraigBmcSolver;
#elif defined(NCIP_SOLVER_CADICRAIG_DEBUG)
	using BmcSolver = Ncip::CadiCraigDebugBmcSolver;
#elif defined(NCIP_SOLVER_KITTENCRAIG)
	using BmcSolver = Ncip::KittenCraigBmcSolver;
#elif defined(NCIP_SOLVER_PORTFOLIO)
	template<typename Solver>
	static Solver ConstructSolverForPortfilio(const Ncip::BmcProblem& problem, Ncip::BmcConfiguration configuration) {
		if constexpr (std::is_same_v<Solver, Ncip::CadiCraigBmcSolver> || std::is_same_v<Solver, Ncip::KittenCraigBmcSolver>) {
			configuration.SetPreprocessCraig(Ncip::PreprocessLevel::None);
			configuration.SetPreprocessInit(Ncip::PreprocessLevel::None);
			configuration.SetPreprocessTrans(Ncip::PreprocessLevel::None);
			configuration.SetPreprocessTarget(Ncip::PreprocessLevel::None);
		}
		return Solver(problem, configuration);
	};

	template<typename V, typename... Solvers>
	class MakePortfolioBmcSolver:
		public Ncip::PortfolioBmcSolver<Solvers...> {
	public:
		MakePortfolioBmcSolver(Ncip::BmcProblem problem, Ncip::BmcConfiguration configuration):
			Ncip::PortfolioBmcSolver<Solvers...>(ConstructSolverForPortfilio<Solvers>(problem, configuration)...)
		{}
	};

	using BmcSolver = MakePortfolioBmcSolver<
		void
		#ifdef NCIP_BACKEND_MINICRAIG
			, Ncip::MiniCraigBmcSolver
		#endif
		#ifdef NCIP_BACKEND_CADICRAIG
			, Ncip::CadiCraigBmcSolver
		#endif
		#ifdef NCIP_BACKEND_KITTENCRAIG
			, Ncip::KittenCraigBmcSolver
		#endif
	>;
#else
	#error "No backend selected"
#endif

inline std::string trim(const std::string &s) {
   auto wsfront = std::find_if_not(s.begin(), s.end(), [](int c){ return std::isspace(c); });
   auto wsback = std::find_if_not(s.rbegin(), s.rend(), [](int c){ return std::isspace(c); }).base();
   return (wsback <= wsfront ? std::string() : std::string(wsfront, wsback));
}

enum class InputFormat {
	NONE,
	CIP,
	AIGER,
	DIMSPEC
};
using Problem = std::variant<Ncip::CipProblem, Ncip::AigProblem, Ncip::DimspecProblem>;

template<class... Ts> struct overload : Ts... { using Ts::operator()...; };
template<class... Ts> overload(Ts...) -> overload<Ts...>; // line not needed in C++20...

static std::tuple<Ncip::AigProblem, Ncip::BmcProblem> ParseAigerProblem(std::istream& input);
static void ExportAigerProblem(std::ostream& output, const Ncip::AigProblem& problem);
static void ExportAigerModel(std::ostream& output, const Ncip::AigProblem& problem, const Ncip::BmcModel& model);
static void ExportAigerCertificate(std::ostream& output, const Ncip::AigProblem& problem, const Ncip::BmcCertificate& certificate);

static std::tuple<Ncip::CipProblem, Ncip::BmcProblem> ParseCipProblem(std::istream& input);
static void ExportCipProblem(std::ostream& output, const Ncip::CipProblem& problem);
static void ExportCipModel(std::ostream& output, const Ncip::CipProblem& problem, const Ncip::BmcModel& model);
static void ExportCipCertificate(std::ostream& output, const Ncip::CipProblem& problem, const Ncip::BmcCertificate& certificate);

static std::tuple<Ncip::DimspecProblem, Ncip::BmcProblem> ParseDimspecProblem(std::istream& input);
static void ExportDimspecProblem(std::ostream& output, const Ncip::DimspecProblem& problem);
static void ExportDimspecModel(std::ostream& output, const Ncip::DimspecProblem& problem, const Ncip::BmcModel& model);
static void ExportDimspecCertificate(std::ostream& output, const Ncip::DimspecProblem& problem, const Ncip::BmcCertificate& certificate);

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

#if defined(NCIP_SOLVER_MINICRAIG) || defined(NCIP_SOLVER_MINICRAIG_DEBUG)
	bmcConfiguration.SetPreprocessCraig(Ncip::PreprocessLevel::Simple);
	bmcConfiguration.SetPreprocessInit(Ncip::PreprocessLevel::Simple);
	bmcConfiguration.SetPreprocessTrans(Ncip::PreprocessLevel::Simple);
	bmcConfiguration.SetPreprocessTarget(Ncip::PreprocessLevel::Simple);
#elif defined(NCIP_SOLVER_CADICRAIG) || defined(NCIP_SOLVER_CADICRAIG_DEBUG) \
		|| defined(NCIP_SOLVER_KITTENCRAIG)
	bmcConfiguration.SetPreprocessCraig(Ncip::PreprocessLevel::None);
	bmcConfiguration.SetPreprocessInit(Ncip::PreprocessLevel::None);
	bmcConfiguration.SetPreprocessTrans(Ncip::PreprocessLevel::None);
	bmcConfiguration.SetPreprocessTarget(Ncip::PreprocessLevel::None);
#elif defined(NCIP_SOLVER_PORTFOLIO)
	// Maximum optimization settings for portfolio approach
	bmcConfiguration.SetPreprocessCraig(Ncip::PreprocessLevel::Simple);
	bmcConfiguration.SetPreprocessInit(Ncip::PreprocessLevel::Simple);
	bmcConfiguration.SetPreprocessTrans(Ncip::PreprocessLevel::Simple);
	bmcConfiguration.SetPreprocessTarget(Ncip::PreprocessLevel::Simple);
#endif

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
	if (inputFile != "-") {
		inputFStream = std::ifstream { inputFile };
		if (!inputFStream.good()) {
			std::cerr << "Error: Input file \"" << inputFile << "\" was not found" << std::endl;
			exit(2);
		}
	}
	if (outputFile != "-" && outputFile != "") {
		outputFStream = std::ofstream { outputFile };
		if (!outputFStream.good()) {
			std::cerr << "Error: Output file \"" << outputFile << "\" could not be created" << std::endl;
			exit(2);
		}
	} else if (outputFile == "") {
		// In case of outputFile is an empty string we are writing into a dummy stream.
		// Nothing to do here as the default ofstream object behaves as needed.
	}

	std::istream& input { (inputFile == "-") ? std::cin : inputFStream };
	std::ostream& output { (outputFile == "-") ? std::cout : outputFStream };

	auto const parse_problem = [&format](auto& input) -> std::tuple<Problem, Ncip::BmcProblem> {
		if (format == InputFormat::CIP) { auto [problem, bmcProblem] = ParseCipProblem(input); return { problem, bmcProblem }; }
		if (format == InputFormat::AIGER) { auto [problem, bmcProblem] = ParseAigerProblem(input); return { problem, bmcProblem }; }
		if (format == InputFormat::DIMSPEC) { auto [problem, bmcProblem] = ParseDimspecProblem(input); return { problem, bmcProblem }; }
		__builtin_unreachable();
	};
	auto const export_problem = [](auto& output, auto& problem) {
		std::visit(overload {
			[&](const Ncip::CipProblem& problem) { ExportCipProblem(output, problem); },
			[&](const Ncip::AigProblem& problem) { ExportAigerProblem(output, problem); },
			[&](const Ncip::DimspecProblem& problem) { ExportDimspecProblem(output, problem); },
		}, problem);
	};
	auto const export_model = [](auto& output, auto& problem, auto& model) {
		std::visit(overload {
			[&](const Ncip::CipProblem& problem) { ExportCipModel(output, problem, model); },
			[&](const Ncip::AigProblem& problem) { ExportAigerModel(output, problem, model); },
			[&](const Ncip::DimspecProblem& problem) { ExportDimspecModel(output, problem, model); },
		}, problem);
	};
	auto const export_certificate = [](auto& output, auto& problem, auto& certificate) {
		std::visit(overload {
			[&](const Ncip::CipProblem& problem) { ExportCipCertificate(output, problem, certificate); },
			[&](const Ncip::AigProblem& problem) { ExportAigerCertificate(output, problem, certificate); },
			[&](const Ncip::DimspecProblem& problem) { ExportDimspecCertificate(output, problem, certificate); },
		}, problem);
	};
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

	BmcSolver solver { bmcProblem, bmcConfiguration };
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

static int aig_generic_read(std::istream* state) {
	return state->get();
}

static int aig_generic_write(char c, std::ostream* state) {
	return (state->put(c).fail() ? EOF : 0);
}

static std::tuple<Ncip::AigProblem, Ncip::BmcProblem> ParseAigerProblem(std::istream& input) {
	std::cerr << "Parsing input as Aiger format" << std::endl;

	auto graph = aiger_init();
	if (auto error = aiger_read_generic(graph, &input,
			reinterpret_cast<aiger_get>(aig_generic_read)); error != nullptr) {
		std::cerr << "Error: Could not read Aiger file: \"" << std::string(error) << "\"" << std::endl;
		exit(2);
	}
	if (auto error = aiger_check(graph); error != nullptr) {
		std::cerr << "Error: Aiger graph has invalid structure: \"" << std::string(error) << "\"" << std::endl;
		exit(2);
	}

	Ncip::AigProblemBuilder builder;
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
	aiger_reset(graph);

	try {
		return builder.Build();
	} catch (const Ncip::AigProblemException& error) {
		std::cerr << "Error: Found invalid AIGER problem: " << error.what() << std::endl;
		exit(3);
	}
}

static void ExportAigerProblem(std::ostream& output, const Ncip::AigProblem& problem) {
	std::cerr << "Exporting output as Aiger format" << std::endl;

	auto graph = aiger_init();
	for (auto& node : problem.GetNodes()) {
		if (node.type == Ncip::AigNodeType::Input) {
			aiger_add_input(graph, node.nodeId, nullptr);
		} else if (node.type == Ncip::AigNodeType::Latch) {
			aiger_add_latch(graph, node.nodeId, node.leftEdgeId, nullptr);
			aiger_add_reset(graph, node.nodeId, node.rightEdgeId);
		} else if (node.type == Ncip::AigNodeType::And) {
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

	if (auto error = aiger_check(graph); error != nullptr) {
		std::cerr << "Error: Aiger graph has invalid structure: \"" << std::string(error) << "\"" << std::endl;
		exit(2);
	}
	if (!aiger_write_generic(graph, aiger_mode::aiger_ascii_mode, &output,
			reinterpret_cast<aiger_put>(aig_generic_write))) {
		std::cerr << "Error: Could not write Aiger file" << std::endl;
		exit(2);
	}

	aiger_reset(graph);
}

static void ExportAigerModel(std::ostream& output, const Ncip::AigProblem& problem, const Ncip::BmcModel& model) {
	// Use custom encoding here since X has to be transformed to x.
	auto const to_aigsim = [] (auto const& result) {
		switch (result) {
			case Ncip::BmcAssignment::Positive: return "1"; break;
			case Ncip::BmcAssignment::Negative: return "0"; break;
			case Ncip::BmcAssignment::DontCare: return "x"; break;
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
			satisfied |= ((result ^ (bads[bad] & 1u)) == Ncip::BmcAssignment::Positive);
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
}

static void ExportAigerCertificate(std::ostream& output, const Ncip::AigProblem& problem, const Ncip::BmcCertificate& certificate) {
	Ncip::AigCertificateBuilder builder;
	ExportAigerProblem(output, builder.Build(problem, certificate));
}

static std::tuple<Ncip::CipProblem, Ncip::BmcProblem> ParseCipProblem(std::istream& input) {
	const std::regex innerGroups { "\\((.*)\\)" };
	const std::regex specificGroups { "(-?[0-9]+:[0-9]+)" };

	std::cerr << "Parsing input as CIP format" << std::endl;

	Ncip::CipProblemBuilder builder { };
	auto parse_clause = [&](const std::string& line) -> Ncip::BmcClause {
		Ncip::BmcClause clause { };

		std::smatch match;
		if (!std::regex_match(line, match, innerGroups)) {
			std::cerr << "Error: Could not parse line \"" << line << "\"" << std::endl;
			exit(2);
		}

		std::string fullClause = match[1].str();
		for (auto it = std::sregex_iterator(fullClause.begin(), fullClause.end(), specificGroups);
			it != std::sregex_iterator(); it++)
		{
			int literalId;
			int timeframe;
			match = *it;
			std::stringstream stream(match.str(0));
			std::string token;

			std::getline(stream, token, ':');
			literalId = std::stol(token);

			std::getline(stream, token, ':');
			timeframe = std::stol(token);

			clause.push_back(Ncip::BmcLiteral::FromVariable(std::abs(literalId) - 1u, (literalId < 0), timeframe));
		}

		return clause;
	};

	std::string line;
	while (input.good() && !input.eof()) {
		std::getline(input, line);
		line = trim(line);
		if (line.empty()) {
			continue;
		}

		if (line.rfind("DECL", 0u) == 0u) {
			std::cerr << "Parsing Variable Declarations" << std::endl;

			while(input.good() && !input.eof()) {
				std::getline(input, line);
				line = trim(line);
				if (line.empty()) {
					break;
				}

				std::string variableType;
				size_t variableIndex;

				std::stringstream stream(line);
				stream >> variableType;
				stream >> variableIndex;

				Ncip::BmcVariableId variableId;
				if (variableType == "AND_VAR"
					|| variableType == "AUX_VAR") {
					variableId = builder.AddVariable(Ncip::CipVariableType::Tseitin);
				} else if (variableType == "LATCH_VAR") {
					variableId = builder.AddVariable(Ncip::CipVariableType::Latch);
				} else if (variableType == "INPUT_VAR") {
					variableId = builder.AddVariable(Ncip::CipVariableType::Input);
				} else if (variableType == "OUTPUT_VAR") {
					variableId = builder.AddVariable(Ncip::CipVariableType::Output);
				} else {
					std::cerr << "Error: Unknown variable type \"" << variableType << "\"" << std::endl;
					exit(2);
				}

				if (variableId + 1u != variableIndex) {
					std::cerr << "Error: Inconsistent literal index counters!" << std::endl;
					exit(2);
				}
			}
		} else if (line.rfind("INIT", 0u) == 0u) {
			std::cerr << "Parsing Initial Clauses" << std::endl;

			while(input.good() && !input.eof()) {
				std::getline(input, line);
				line = trim(line);
				if (line.empty()) {
					break;
				}

				auto clause = parse_clause(line);
				builder.AddClause(Ncip::CipClauseType::Initial, clause);
			}
		} else if (line.rfind("TRANS", 0u) == 0u) {
			std::cerr << "Parsing Transition Clauses" << std::endl;

			while(input.good() && !input.eof()) {
				std::getline(input, line);
				line = trim(line);
				if (line.empty()) {
					break;
				}

				auto clause = parse_clause(line);
				builder.AddClause(Ncip::CipClauseType::Transition, clause);
			}
		} else if (line.rfind("TARGET", 0u) == 0u) {
			std::cerr << "Parsing Target Clauses" << std::endl;

			while(input.good() && !input.eof()) {
				std::getline(input, line);
				line = trim(line);
				if (line.empty()) {
					break;
				}

				auto clause = parse_clause(line);
				builder.AddClause(Ncip::CipClauseType::Target, clause);
			}
		} else if (line.rfind("--", 0u) == 0u) {
			// Ignore comment lines
			continue;
		} else if (line.rfind("OFFSET: ", 0u) == 0u
			|| line.rfind("USE_PROPERTY: ", 0u) == 0u
			|| line.rfind("SIMPLIFY_INTERPOLANTS: ", 0u) == 0u
			|| line.rfind("TIMEOUT: ", 0u) == 0u
			|| line.rfind("MAXDEPTH: ", 0u) == 0u)
		{
			// Ignore unused property lines
			std::cerr << "Warning: Ignoring property \"" << line << "\" from input file" << std::endl;
			continue;
		} else {
			std::cerr << "Error: Unknown section \"" << line << "\"" << std::endl;
			exit(2);
		}
	}

	try {
		return builder.Build();
	} catch (const Ncip::CipProblemException& error) {
		std::cerr << "Error: Found invalid CIP problem: " << error.what() << std::endl;
		exit(3);
	}
}

static void ExportCipProblem(std::ostream& output, const Ncip::CipProblem& problem) {
	const auto print_clause = [&output](auto& clause) {
		output << "(";
		size_t index { 0u };
		for (auto& literal : clause) {
			if (index++ != 0) { output << ", "; }
			output << "[" << (static_cast<ssize_t>(literal.GetVariable()) + 1) * (literal.IsNegated() ? -1 : 1)
				<< ":" << literal.GetTimeframe() << "]";
		}
		output << ")" << std::endl;
	};

	output << "DECL" << std::endl;
	size_t counter { 1 };
	for (auto& var : problem.GetVariables()) {
		switch (var) {
			case Ncip::CipVariableType::Input: output << "INPUT_VAR " << counter++ << std::endl; break;
			case Ncip::CipVariableType::Output: output << "OUTPUT_VAR " << counter++ << std::endl; break;
			case Ncip::CipVariableType::Latch: output << "LATCH_VAR " << counter++ << std::endl; break;
			case Ncip::CipVariableType::Tseitin: output << "AUX_VAR " << counter++ << std::endl; break;
		}
	}
	output << std::endl;

	output << "INIT" << std::endl;
	for (auto& clause : problem.GetInit()) {
		print_clause(clause);
	}
	output << std::endl;

	output << "TRANS" << std::endl;
	for (auto& clause : problem.GetTrans()) {
		print_clause(clause);
	}
	output << std::endl;

	output << "TARGET" << std::endl;
	for (auto& clause : problem.GetTarget()) {
		print_clause(clause);
	}
	output << std::endl;
}

static void ExportCipModel(std::ostream& output, const Ncip::CipProblem& problem, const Ncip::BmcModel& model) {
	for (size_t depth { 0u }; depth < model.GetTimeframes().size(); depth++) {
		output << depth << " = ";
		for (auto& assignment : model.GetTimeframe(depth)) {
			output << to_string(assignment);
		}
		output << std::endl;
	}
}

static void ExportCipCertificate(std::ostream& output, const Ncip::CipProblem& problem, const Ncip::BmcCertificate& certificate) {
	Ncip::CipCertificateBuilder builder;
	ExportCipProblem(output, builder.Build(problem, certificate));
}

static std::tuple<Ncip::DimspecProblem, Ncip::BmcProblem> ParseDimspecProblem(std::istream& input) {
	std::cerr << "Parsing input as DIMSPEC format" << std::endl;

	Ncip::DimspecProblemBuilder builder { };
	auto parse_clause = [&](const std::string& line, size_t variableCount) -> Ncip::BmcClause {
		Ncip::BmcClause clause { };

		ssize_t signedLiteral;
		std::stringstream stream { line };
		while (stream.good() && !stream.eof()) {
			stream >> signedLiteral;
			if (signedLiteral == 0) { break; }

			size_t literal   { (std::abs(signedLiteral) - 1u) % variableCount };
			size_t timeframe { (std::abs(signedLiteral) - 1u) / variableCount };
			clause.push_back(Ncip::BmcLiteral::FromVariable(literal, signedLiteral < 0, timeframe));
		}

		return clause;
	};

	bool variablesDeclared = false;
	std::string line;
	while (input.good() && !input.eof()) {
		std::getline(input, line);
		line = trim(line);
		if (line.empty()) {
			continue;
		} else if (line.rfind("c", 0u) == 0u) { // Ignore comments
			continue;
		} else if (line.rfind("u", 0u) == 0u
				|| line.rfind("i", 0u) == 0u
				|| line.rfind("g", 0u) == 0u
				|| line.rfind("t", 0u) == 0u) {
			Ncip::DimspecClauseType clauseType;
			if (line.rfind("u", 0u) == 0u) {
				std::cerr << "Parsing UNIVERSAL clauses" << std::endl;
				clauseType = Ncip::DimspecClauseType::Universal;
			} else if (line.rfind("i", 0u) == 0u) {
				std::cerr << "Parsing INIT clauses" << std::endl;
				clauseType = Ncip::DimspecClauseType::Initial;
			} else if (line.rfind("g", 0u) == 0u) {
				std::cerr << "Parsing GOAL clauses" << std::endl;
				clauseType = Ncip::DimspecClauseType::Goal;
			} else if (line.rfind("t", 0u) == 0u) {
				std::cerr << "Parsing TRANS clauses" << std::endl;
				clauseType = Ncip::DimspecClauseType::Transition;
			}

			std::stringstream stream(line);
			std::string type, cnf;
			size_t variables;
			size_t clauses;
			stream >> type;
			stream >> cnf;
			assert(cnf == "cnf");
			stream >> variables;
			stream >> clauses;

			if (clauseType == Ncip::DimspecClauseType::Transition) {
				variables /= 2u;
			}
			if (variablesDeclared && builder.GetVariables() != variables) {
				std::cerr << "Error: Variable count of " << variables << " doesn't match previous declared "
					<< builder.GetVariables() << " variables" << std::endl;
				exit(2);
			}
			variablesDeclared = true;
			builder.SetVariables(variables);

			size_t clauseCounter{ 0u };
			while(!input.eof() && input.good() && clauseCounter < clauses) {
				std::getline(input, line);
				line = trim(line);
				if (line.empty()) {
					continue;
				}
				if (line.rfind("c", 0u) == 0u) {
					// Ignore comments
					continue;
				}

				auto clause { parse_clause(line, variables) };
				builder.AddClause(clauseType, clause);
				clauseCounter++;
			}
		} else {
			std::cerr << "Error: Unknown line \"" << line << "\"" << std::endl;
			exit(2);
		}
	}

	try {
		return builder.Build();
	} catch (const Ncip::DimspecProblemException& exception) {
		std::cerr << "Error: Found invalid DIMSPEC problem: " << exception.what() << std::endl;
		exit(3);
	}
}

static void ExportDimspecProblem(std::ostream& output, const Ncip::DimspecProblem& problem) {
	const auto print_clause = [&](auto& clause) {
		size_t index { 0u };
		for (auto& literal : clause) {
			output << static_cast<ssize_t>(literal.GetVariable() + literal.GetTimeframe() * problem.GetVariables() + 1u)
				* (literal.IsNegated() ? -1 : 1) << " ";
		}
		output << "0" << std::endl;
	};

	output << "u cnf " << problem.GetVariables() << " " << problem.GetUniversal().size() << std::endl;
	for (auto& clause : problem.GetUniversal()) {
		print_clause(clause);
	}
	output << "i cnf " << problem.GetVariables() << " " << problem.GetInit().size() << std::endl;
	for (auto& clause : problem.GetInit()) {
		print_clause(clause);
	}
	output << "g cnf " << problem.GetVariables() << " " << problem.GetGoal().size() << std::endl;
	for (auto& clause : problem.GetGoal()) {
		print_clause(clause);
	}
	output << "t cnf " << (2u * problem.GetVariables()) << " " << problem.GetTrans().size() << std::endl;
	for (auto& clause : problem.GetTrans()) {
		print_clause(clause);
	}
}

static void ExportDimspecModel(std::ostream& output, const Ncip::DimspecProblem& problem, const Ncip::BmcModel& model) {
	for (size_t depth { 0u }; depth < model.GetTimeframes().size(); depth++) {
		output << "v" << depth;
		auto const& timeframe { model.GetTimeframe(depth) };
		for (size_t variable { 0u }; variable < timeframe.size(); variable++) {
			switch (timeframe[variable]) {
				case Ncip::BmcAssignment::DontCare: break;
				case Ncip::BmcAssignment::Positive: output << " " << (variable + 1); break;
				case Ncip::BmcAssignment::Negative: output << " -" << (variable + 1); break;
				default: __builtin_unreachable();
			}
		}
		output << " 0" << std::endl;
	}
}

static void ExportDimspecCertificate(std::ostream& output, const Ncip::DimspecProblem& problem, const Ncip::BmcCertificate& certificate) {
	Ncip::DimspecCertificateBuilder builder;
	ExportDimspecProblem(output, builder.Build(problem, certificate));
}