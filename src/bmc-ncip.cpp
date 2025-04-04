// SPDX-License-Identifier: MIT OR Apache-2.0

#include "./bmc-ncip.hpp"
#include "./bmc-ncip-impl.hpp"
#include "./bmc-ncip-backend.hpp"

#ifdef NCIP_BACKEND_MINICRAIG
# include "./bmc-ncip-backend-minicraig.hpp"
# ifndef NDEBUG
#  include "./bmc-ncip-backend-minicraig-debug.hpp"
# endif
#endif

#ifdef NCIP_BACKEND_CADICRAIG
# include "./bmc-ncip-backend-cadicraig.hpp"
# ifndef NDEBUG
#  include "./bmc-ncip-backend-cadicraig-debug.hpp"
# endif
#endif

#ifdef NCIP_BACKEND_KITTENCRAIG
# include "./bmc-ncip-backend-kittencraig.hpp"
#endif

#include <map>
#include <type_traits>
#include <unordered_map>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <iostream>

using namespace Ncip::Impl;
using namespace Ncip::Backend;

// Helper macro to create a cancellation point.
// This is a location in the code where we check for interrupts
// and exit the solve method gracefully with interrupted result.
#define EXIT_SOLVE_ON_INTERRPUT(depth) \
	if (interrupted.load(std::memory_order_acquire)) { \
		return BmcResult::ForUserInterrupt(depth); \
	}

namespace Ncip {

static BackendVariableType MapVariableType(BackendClauseType clauseType, bool isGlobal, bool isProtected);
static BackendClauseType MapClauseType(SolverClauseType type, ssize_t shift);
static BmcCertificate CreateCertificate(const BmcClauses& invariant, const BmcLiteral& root);

template<typename ImplTag>
BmcSolver<ImplTag>::BmcSolver(BmcProblem problem_, BmcConfiguration configuration_):
	problem(problem_),
	configuration(configuration_),
	globalVariables(),
	protectedVariables(),
	nextVariable(0u),
	solverVariables(),
	mutex(),
	interrupted(false),
	bmcSolver(),
	preSolver(),
	fpcSolver()
#ifndef NDEBUG
	,
	debugSolves(0),
	debugVariables(),
	debugDisabledTriggers(),
	debugAssumptions(),
	debugClauses()
#endif
{
	InitializeProtectedGlobalVariables();
}

template<typename ImplTag>
BmcSolver<ImplTag>::BmcSolver(BmcSolver&& other):
	problem(std::move(other.problem)),
	configuration(std::move(other.configuration)),
	globalVariables(std::move(other.globalVariables)),
	protectedVariables(std::move(other.protectedVariables)),
	nextVariable(other.nextVariable),
	solverVariables(std::move(other.solverVariables)),
	mutex(),
	interrupted(),
	bmcSolver(std::move(other.bmcSolver)),
	preSolver(std::move(other.preSolver)),
	fpcSolver(std::move(other.fpcSolver))
{}

template<typename ImplTag>
BmcSolver<ImplTag>::~BmcSolver() = default;

template<typename ImplTag>
void BmcSolver<ImplTag>::InitializeProtectedGlobalVariables() {
	globalVariables.resize(problem.GetVariables(), false);
	protectedVariables.resize(problem.GetVariables(), false);

	std::vector<bool> initVariables(problem.GetVariables(), false);
	std::vector<bool> transVariables(problem.GetVariables(), false);
	std::vector<bool> targetVariables(problem.GetVariables(), false);
	for (const auto& clause : problem.GetInit()) {
		for (const auto& literal : clause) {
			initVariables[literal.GetVariable()] = true;
		}
	}
	for (const auto& clause : problem.GetTrans()) {
		for (const auto& literal : clause) {
			transVariables[literal.GetVariable()] = true;
			if (literal.GetTimeframe() != 0) {
				globalVariables[literal.GetVariable()] = true;
			}
		}
	}
	for (const auto& clause : problem.GetTarget()) {
		for (const auto& literal : clause) {
			targetVariables[literal.GetVariable()] = true;
		}
	}

	for (BmcVariableId variable { 0u }; variable < problem.GetVariables(); variable++) {
		if (IsLogEnabled(LogLevel::ExtendedTrace)) {
			std::cerr << "  - Variable " << std::to_string(variable) << " in ";
			bool occurrence = false;
			if (initVariables[variable]) {
				std::cerr << "init";
				occurrence = true;
			}
			if (transVariables[variable]) {
				if (occurrence) std::cerr << ", ";
				std::cerr << "trans";
				occurrence = true;
			}
			if (targetVariables[variable]) {
				if (occurrence) std::cerr << ", ";
				std::cerr << "target";
				occurrence = true;
			}
			if (!occurrence) {
				std::cerr << "none";
			}
			std::cerr << std::endl;
		}
		protectedVariables[variable] = (
			// Variables that are shared between INIT and TARGET (base case)
			(initVariables[variable] && targetVariables[variable])
			// Variables in TRANS that are shared with INIT and TARGET
			|| (transVariables[variable] && (initVariables[variable] || targetVariables[variable]))
			// Variables in TRANS that are latch (TRANS to TRANS transition)
			|| globalVariables[variable]
		);
	}
}

template<typename ImplTag>
BmcResult BmcSolver<ImplTag>::Solve() {
	BmcResult result { BmcResult::ForUserInterrupt(-1) };

	try {
		result = SolveImpl();
	} catch (std::bad_alloc& outOfMemory) {
		result = BmcResult::ForMemoryLimit();
	}
#ifdef NCIP_BACKEND_KITTENCRAIG
	catch (KittenError& error) {
		result = BmcResult::ForMemoryLimit();
	}
#endif

	std::lock_guard guard { mutex };
	bmcSolver = nullptr;
	fpcSolver = nullptr;
	preSolver = nullptr;

	return result;
}

// Solves the BMC problem by using plain BMC and Craig interpolation:
// Depth 0: BMC, Normal  A = I0 & !P0        B = 1                                     | A & B     UNSAT
// Depth 1: BMC, Normal  A = I0 & T0->1      B = !P1                                   | A & B     UNSAT
// Depth 2: FPC          H0 = I0            C1 = [I0 & T0-1]1<-0                       | -H0 & C1  SAT
// Depth 2: BMC, Craig   A = C1 & T0->1      B = !P1                                   |           UNSAT
// Depth 3: FPC          H1 = I0 v C1       C2 = [C0 & T0-1]1<-0                       | -H1 & C2  SAT
// Depth 2: BMC, Craig   A = C2 & T0->1      B = !P1                                   |           UNSAT
// Depth 3: FPC          H2 = I0 v C1 V C2  C3 = [C1 & T0-1]1<-0                       | -H2 & C3  SAT
// Depth 3: BMC, Craig   A = C3 & T0->1      B = !P1                                   |           SAT
// Depth 3: BMC          A = I0 & T0->1      B = !P1 v (T1->2 & (!P2 v (T2->3 & !P3))  |           UNSAT
// Depth 4: FPC          H0 = I0            C4 = [I0 & T0-1]1<-0                       | -H0 & C4  SAT
// Depth 4: BMC, Craig   A = C4 & T0->1      B = !P1 v (T1->2 & (!P2 v (T2->3 & !P3))  |           UNSAT
// Depth 5: FPC          H0 = I0 v C4       C5 = [C4 & T0-1]1<-0                       | -H1 & C5  UNSAT
template<typename ImplTag>
BmcResult BmcSolver<ImplTag>::SolveImpl() {
	if (IsLogEnabled(LogLevel::Info)) { std::cerr << "Adding BMC problem to instance" << std::endl; }
	for (size_t index { 0u }; index < problem.GetVariables(); index++) {
		CreateSolverVariable(SolverVariableType::Original);
	}

	// -------------------------------------------------------------------------

	if (IsLogEnabled(LogLevel::Info)) { std::cerr << "Creating trigger variables" << std::endl; }
	auto const initTrigger { CreateSolverVariable(SolverVariableType::InitTrigger) };
	auto const transTrigger { CreateSolverVariable(SolverVariableType::TransTrigger) };
	auto const targetTrigger { CreateSolverVariable(SolverVariableType::TargetTrigger) };
	auto const craigTrigger { CreateSolverVariable(SolverVariableType::CraigTrigger) };
	auto const aNormalTrigger { CreateSolverVariable(SolverVariableType::ATrigger) };
	auto const aCraigTrigger { CreateSolverVariable(SolverVariableType::ATrigger) };
	auto const bNormalTrigger { CreateSolverVariable(SolverVariableType::BTrigger) };
	auto const bCraigTrigger { CreateSolverVariable(SolverVariableType::BTrigger) };

	auto const& originalInit = problem.GetInit();
	if (IsLogEnabled(LogLevel::Info)) { std::cerr << "Preprocessing " << std::to_string(originalInit.size()) << " Init clauses" << std::endl; }
	auto const initClauses = PreprocessClauses(originalInit, INVALID_LITERAL, configuration.GetPreprocessInit());
	EXIT_SOLVE_ON_INTERRPUT(-1);

	auto const& originalTrans = problem.GetTrans();
	if (IsLogEnabled(LogLevel::Info)) { std::cerr << "Preprocessing " << std::to_string(originalTrans.size()) << " Trans clauses" << std::endl; }
	auto const transClauses = PreprocessClauses(originalTrans, INVALID_LITERAL, configuration.GetPreprocessTrans());
	EXIT_SOLVE_ON_INTERRPUT(-1);

	auto const& originalTarget = problem.GetTarget();
	if (IsLogEnabled(LogLevel::Info)) { std::cerr << "Preprocessing " << std::to_string(originalTarget.size()) << " Target clauses" << std::endl; }
	auto const targetClauses = PreprocessClauses(originalTarget, INVALID_LITERAL, configuration.GetPreprocessTarget());
	EXIT_SOLVE_ON_INTERRPUT(-1);

	// -------------------------------------------------------------------------

	if (IsLogEnabled(LogLevel::Info)) { std::cerr << "Making " << std::to_string(initClauses.size()) << " Init clauses invertable" << std::endl; }
	auto const [invertableInitClauses, invertableInitRoot] = ToInvertable(initClauses, SolverVariableType::InitTseitin);

#ifndef NDEBUG
	if (configuration.IsClauseExportEnabled()) {
		ExportClauses(originalInit, { }, configuration.GetDebugPath() + "/0-original-init.cnf");
		ExportClauses(originalTrans, { }, configuration.GetDebugPath() + "/0-original-trans.cnf");
		ExportClauses(originalTarget, { }, configuration.GetDebugPath() + "/0-original-target.cnf");

		ExportClauses(initClauses, { }, configuration.GetDebugPath() + "/0-preprocessed-init.cnf");
		ExportClauses(transClauses, { }, configuration.GetDebugPath() + "/0-preprocessed-trans.cnf");
		ExportClauses(targetClauses, { }, configuration.GetDebugPath() + "/0-preprocessed-target.cnf");
	}
#endif

	// -------------------------------------------------------------------------

	std::vector<BmcLiteral> assumptions;
	ssize_t depth { 0 };
	ssize_t encodedTransDepth { 0 };
	ssize_t encodedTargetDepth { 0 };

	auto timeStart { std::chrono::high_resolution_clock::now() };
	auto get_elapsed = [&timeStart]() -> float {
		auto timeEnd { std::chrono::high_resolution_clock::now() };
		auto result { std::chrono::duration_cast<std::chrono::duration<float>>(timeEnd - timeStart).count() };
		timeStart = timeEnd;
		return result;
	};

	// -------------------------------------------------------------------------

	if (configuration.GetEnableSanityChecks()) {
		{
			std::lock_guard guard { mutex };
			bmcSolver = std::make_unique<Solver<ImplTag, CraigSolverTag>>();
			bmcSolver->ConfigureCraigInterpolant(CraigInterpolant::Smallest, false);
		}

		AddTrigger(initTrigger >> 0, SolverClauseType::Init);
		AddClausesToSolver(initTrigger >> 0, initClauses, 0, SolverClauseType::Init);
		EXIT_SOLVE_ON_INTERRPUT(-1);

		AddTrigger(transTrigger >> 0, SolverClauseType::Trans);
		AddClausesToSolver(transTrigger >> 0, transClauses, 0, SolverClauseType::Trans);
		EXIT_SOLVE_ON_INTERRPUT(-1);

		AddTrigger(targetTrigger >> 0, SolverClauseType::Target);
		AddClausesToSolver(targetTrigger >> 0, targetClauses, 0, SolverClauseType::Target);
		EXIT_SOLVE_ON_INTERRPUT(-1);

		AddTrigger(targetTrigger >> 1, SolverClauseType::Target);
		AddClausesToSolver(targetTrigger >> 1, targetClauses, 1, SolverClauseType::Target);
		EXIT_SOLVE_ON_INTERRPUT(-1);

		if (!SolveWithAssumptions({ initTrigger >> 0 })) {
			EXIT_SOLVE_ON_INTERRPUT(-1);
			if (IsLogEnabled(LogLevel::Info)) { std::cerr << "Init UNSAT" << std::endl; }
			if (IsLogExactly(LogLevel::Competition)) { std::cerr << "=> UNREACHABLE 0 0.0" << std::endl; }
			return BmcResult::ForCertificate(-1, BmcCertificateBuilder(BmcCertificate::Type::Init).Build({ BmcCertificateBuilder::CONSTANT_0 }));
		}
		if (!SolveWithAssumptions({ targetTrigger >> 0 })) {
			EXIT_SOLVE_ON_INTERRPUT(-1);
			if (IsLogEnabled(LogLevel::Info)) { std::cerr << "Target UNSAT" << std::endl; }
			if (IsLogExactly(LogLevel::Competition)) { std::cerr << "=> UNREACHABLE 0 0.0" << std::endl; }
			return BmcResult::ForCertificate(-1, BmcCertificateBuilder(BmcCertificate::Type::Target).Build({ BmcCertificateBuilder::CONSTANT_1 }));
		}

		if (SolveWithAssumptions({ initTrigger >> 0, targetTrigger >> 0 })) {
			if (IsLogEnabled(LogLevel::Info)) { std::cerr << "Init + Target SAT" << std::endl; }
			if (IsLogExactly(LogLevel::Competition)) { std::cerr << "=> SAT 0 0.0" << std::endl; }
			goto expand_solution;
		}
		EXIT_SOLVE_ON_INTERRPUT(-1);

		//if (!SolveWithAssumptions({ transTrigger >> 0 })) {
		//  EXIT_SOLVE_ON_INTERRPUT(-1);
		//	if (IsLogEnabled(LogLevel::Info)) { std::cerr << "Trans UNSAT" << std::endl; }
		//	if (IsLogExactly(LogLevel::Competition)) { std::cerr << "=> UNREACHABLE 0 0.0" << std::endl; }
		//	return BmcResult::ForCertificate(-1, BmcCertificateBuilder(BmcCertificate::Type::Trans).Build({ BmcCertificateBuilder::CONSTANT_1 }));
		//}
		//if (!SolveWithAssumptions({ initTrigger >> 0, transTrigger >> 0 })) {
		//  EXIT_SOLVE_ON_INTERRPUT(-1);
		//	if (IsLogEnabled(LogLevel::Info)) { std::cerr << "Init + Trans UNSAT" << std::endl; }
		//	if (IsLogExactly(LogLevel::Competition)) { std::cerr << "=> UNREACHABLE 0 0.0" << std::endl; }
		//	return BmcResult::ForCertificate(-1, BmcCertificateBuilder(BmcCertificate::Type::InitTrans).Build({ BmcCertificateBuilder::CONSTANT_1 }));
		//}
		//if (!SolveWithAssumptions({ transTrigger >> 0, targetTrigger >> 1 })) {
		//  EXIT_SOLVE_ON_INTERRPUT(-1);
		//	if (IsLogEnabled(LogLevel::Info)) { std::cerr << "Trans + Target UNSAT" << std::endl; }
		//	if (IsLogExactly(LogLevel::Competition)) { std::cerr << "=> UNREACHABLE 0 0.0" << std::endl; }
		//	return BmcResult::ForCertificate(-1, BmcCertificateBuilder(BmcCertificate::Type::TransTarget).Build({ BmcCertificateBuilder::CONSTANT_1 }));
		//}

#ifndef NDEBUG
		if (configuration.IsBmcExportEnabled()) {
			debugVariables.clear();
			debugAssumptions.clear();
			debugDisabledTriggers.clear();
			debugClauses.clear();
		}
#endif
	}

	{
		std::lock_guard guard { mutex };
		bmcSolver = std::make_unique<Solver<ImplTag, CraigSolverTag>>();
		bmcSolver->ConfigureCraigInterpolant(configuration.GetCraigInterpolant(), configuration.GetEnableCraigInterpolation());
	}

	// -------------------------------------------------------------------------

	for (; depth < static_cast<ssize_t>(configuration.GetMaximumDepth()); depth++) {
		EXIT_SOLVE_ON_INTERRPUT(depth);

		if (IsLogEnabled(LogLevel::Debug)) { std::cerr << "----------------------------------" << std::endl; }
		if (IsLogEnabled(LogLevel::Minimal)) { std::cerr << "Problem depth " << depth << " (BMC)" << std::endl; }

		// --------------------------------------------------------------------
		// BMC using simplified problem
		// --------------------------------------------------------------------

		// Add transition relation from k to k+1 until we reach the
		// current depth. We can not simply just encode the transition
		// relation from depth to depth+1 since the Craig interpolation
		// might have increased the size of the BMC problem without having
		// this main loop being iterated. This means, that the Craig
		// interpolation is able to skip some of the iterations and this
		// code has to account for it.
		if (depth == 0) {
			AddTrigger(initTrigger >> 0, SolverClauseType::Init);
			AddClausesToSolver(initTrigger >> 0, initClauses, 0, SolverClauseType::Init);
			EXIT_SOLVE_ON_INTERRPUT(depth);
		}
		while (encodedTransDepth < depth) {
			AddTrigger(transTrigger >> encodedTransDepth, SolverClauseType::Trans);
			AddClausesToSolver(transTrigger >> encodedTransDepth, transClauses, encodedTransDepth, SolverClauseType::Trans);
			EXIT_SOLVE_ON_INTERRPUT(depth);

			// Remove protection from global variables that are allowed to be eliminated in the future.
			// We want to have the following, where we want to protect the interface (arrows with X) between:
			//   (I/C) -X> (S1) -X> (S2) -> (S3) -X> (!P)
			//
			// - The initial state / Craig interpolant and the next timeframe (S1) as the initial state
			//   and Craig interpolants have to be swapped out from time to time.
			// - The interface between the second state and the next as this will be used to build the
			//   Craig interpolant up on and then shifted once to the left.
			// - The interface between the last timeframe (!P) and the previous timeframe (S3).
			//   The reason is, that we will append a transition relation later on.
			if (encodedTransDepth > 1 && encodedTransDepth + 1 < depth) {
				for (size_t variable { 0 }; variable < protectedVariables.size(); variable++) {
					if (!globalVariables[variable]) {
						auto bmcVariable { BmcLiteral::FromVariable(variable, false) };
						UnprotectSolverVariable(bmcVariable, encodedTransDepth, SolverClauseType::Trans);
					}
				}
			}
			encodedTransDepth++;
		}
		while (encodedTargetDepth <= depth) {
			AddTrigger(targetTrigger >> encodedTargetDepth, SolverClauseType::Target);
			AddClausesToSolver(targetTrigger >> encodedTargetDepth, targetClauses, encodedTargetDepth, SolverClauseType::Target);
			EXIT_SOLVE_ON_INTERRPUT(depth);
			encodedTargetDepth++;
		}

		// Solve original BMC problem of the form (I) -> (S) -> (S) -> ... -> (!P).
		// The Craig interpolation requires that B contains that the property can be reached on any depth.
		// Therefore, the BMC problem has the form (I) -> (S v !P) -> (S v !P) -> ... -> (!P) instead.
		//
		// The problem is split into:
		// 1) Depth = 0 (Base case)
		//    The inital state satisfies !P.
		//      A = I0 & !P0
		//      B = 1
		// 2) Depth > 0 (Step)
		//    There exist a transition from I to !P in 1 to k steps.
		//      A = I0 & T0->1
		//      B = !P1 v (T1->2 & (!P2 v ... (Tn-1->n & !Pn))
		//    The B side is split into two parts: Reaching !P or making steps (Px -> Tx->x+1).
		//      B = (!P1 v !P2 v ... v !Pn) & (P1 -> T1->2) & (P2 -> T2->3) & ... & (Pn-1 -> Tn-1->n)
		BmcClauses aSideClauses;
		BmcClauses bSideClauses;
		if (depth == 0) {
			aSideClauses.push_back({ initTrigger >> 0 });
			aSideClauses.push_back({ targetTrigger >> 0 });
		} else {
			aSideClauses.push_back({ initTrigger >> 0 });
			aSideClauses.push_back({ transTrigger >> 0 });
			aSideClauses.push_back({ -(targetTrigger >> 0) });

			for (ssize_t index { 1 }; index < depth; index++) {
				if (configuration.GetTotalTransitionRelation()) {
					bSideClauses.push_back({ transTrigger >> index });
				} else {
					// When the target is not fulfilled then force a transition.
					bSideClauses.push_back({ targetTrigger >> index, transTrigger >> index });
				}
			}
			BmcClause targetReached;
			for (ssize_t index { 1 }; index <= depth; index++) {
				targetReached.push_back(targetTrigger >> index);
			}
			bSideClauses.push_back(targetReached);
		}

		AddTrigger(aNormalTrigger >> depth, SolverClauseType::ASide);
		AddTrigger(bNormalTrigger >> depth, SolverClauseType::BSide);
		AddClausesToSolver(aNormalTrigger >> depth, aSideClauses, 0, SolverClauseType::ASide);
		AddClausesToSolver(bNormalTrigger >> depth, bSideClauses, 0, SolverClauseType::BSide);

		if (IsLogEnabled(LogLevel::Debug)) { std::cerr << "  - Solving plain BMC problem" << std::endl; }
		auto bmcResult = SolveWithAssumptions({ aNormalTrigger >> depth, bNormalTrigger >> depth });
		if (IsLogEnabled(LogLevel::Info)) { std::cerr << "  - Solving plain BMC result is " << (bmcResult ? "SAT" : "UNSAT") << " after " << std::fixed << std::setprecision(3) << get_elapsed() << "s" << std::endl; }
		if (IsLogExactly(LogLevel::Competition)) { std::cerr << "=> " << (bmcResult ? "SAT" : "UNSAT") << " " << depth << " " << std::fixed << std::setprecision(6) << get_elapsed() << std::endl; }
		if (bmcResult) { goto expand_solution; }
		EXIT_SOLVE_ON_INTERRPUT(depth);

#ifndef NDEBUG
		if (configuration.IsBmcExportEnabled()) {
			std::cerr << "  - Exporting BMC problem to " << std::to_string(debugSolves - 1) << ".0-problem.cnf" << std::endl;
			ExportBmc(configuration.GetDebugPath() + "/" + std::to_string(debugSolves - 1) + ".0-problem.cnf");
		}
#endif

		PermanentlyDisableTrigger(aNormalTrigger >> depth, SolverClauseType::ASide);
		PermanentlyDisableTrigger(bNormalTrigger >> depth, SolverClauseType::BSide);
		EXIT_SOLVE_ON_INTERRPUT(depth);

		// --------------------------------------------------------------------
		// Extend the solution with the craig interpolant (multiple times)
		// --------------------------------------------------------------------

		if (configuration.GetEnableCraigInterpolation() && depth > 0) {
			auto const fpcPositiveTrigger { CreateSolverVariable(SolverVariableType::FpcTrigger) };
			auto const fpcNegativeTrigger { CreateSolverVariable(SolverVariableType::FpcTrigger) };
			auto const fpcProgressTrigger { CreateSolverVariable(SolverVariableType::FpcTrigger) };
			auto const fpcRoot { CreateSolverVariable(SolverVariableType::FpcRoot) };
			BmcClause craigRoots { invertableInitRoot >> 0 };
			BmcClause craigTriggers { initTrigger >> 0 };
			std::vector<BmcClauses> craigInterpolants {};

			const auto createFpcVariable = [&](BmcVariable variable) -> auto {
				auto const variableId { variable.GetId() };
				auto isProtected { variableId < protectedVariables.size() && protectedVariables[variableId] };
				return fpcSolver->CreateVariable(variable, isProtected ? BackendVariableType::GLOBAL : BackendVariableType::NORMAL, IsLogEnabled(LogLevel::FullTrace));
			};

			if (configuration.GetEnableFixPointCheck()) {
				{
					std::lock_guard guard { mutex };
					fpcSolver = std::make_unique<Solver<ImplTag, FpcSolverTag>>();
				}

				fpcSolver->AddTrigger(invertableInitRoot >> 0, BackendClauseType::NORMAL, createFpcVariable, IsLogEnabled(LogLevel::FullTrace));
				fpcSolver->AddClauses(INVALID_LITERAL, invertableInitClauses, 0, BackendClauseType::NORMAL, createFpcVariable, IsLogEnabled(LogLevel::FullTrace));
				EXIT_SOLVE_ON_INTERRPUT(depth);
			}

			// The craig interpolant extends the problem by one timeframe each iteration.
			ssize_t craigIteration { 0 };
			for (; depth + 1 < static_cast<ssize_t>(configuration.GetMaximumDepth()); depth++, craigIteration++) {
				EXIT_SOLVE_ON_INTERRPUT(depth + 1);

				if (IsLogEnabled(LogLevel::Debug)) { std::cerr << "----------------------------------" << std::endl; }
				if (IsLogEnabled(LogLevel::Minimal)) { std::cerr << "Problem depth " << (depth + 1) << " (with Craig)" << std::endl; }

				auto currentFpcPositiveTrigger { fpcPositiveTrigger >> craigIteration };
				auto currentFpcNegativeTrigger { fpcNegativeTrigger >> craigIteration };
				auto currentFpcProgressTrigger { fpcProgressTrigger >> craigIteration };
				if (configuration.GetEnableFixPointCheck()) {
					// --------------------------------------------------------------------
					// Fixpoint check (Part 1 - Constant case)
					// --------------------------------------------------------------------
					// Check if the Craig interpolant can be falsified / satisfied.
					// If not this means that the interpolant is constant 1 / 0.

					// Constant 1 (occurrs when I0 is constrant 1 or the union of interpolants reached constant 1)
					//   Try to solve !(C[0]0<-1 v ... v C[x]0<-1).
					//   If the result is UNSAT then at least one Craig interpolant is always true.
					//   Note, that !(C[0]0<-1 v ... v C[x]0<-1) is equivalent to !C[0]0<-1 & ... & !C[x]0<-1.
					if (IsLogEnabled(LogLevel::Debug)) { std::cerr << "  - Fixed point check adding " << std::to_string(craigRoots.size()) << " root clauses" << std::endl; }
					BmcClauses fpcPositiveClauses;
					for (auto& root : craigRoots) {
						fpcPositiveClauses.push_back({ -root });
					}
					fpcSolver->AddTrigger(currentFpcPositiveTrigger, BackendClauseType::NORMAL, createFpcVariable, IsLogEnabled(LogLevel::FullTrace));
					fpcSolver->AddClauses(currentFpcPositiveTrigger, fpcPositiveClauses, 0, BackendClauseType::NORMAL, createFpcVariable, IsLogEnabled(LogLevel::FullTrace));

					if (IsLogEnabled(LogLevel::Debug)) { std::cerr << "  - Fixed point check solving constant 1 problem" << std::endl; }
					auto const fpcConst1 = fpcSolver->SolveWithAssumptions({ currentFpcPositiveTrigger }, createFpcVariable, IsLogEnabled(LogLevel::FullTrace));
					if (IsLogEnabled(LogLevel::Info)) { std::cerr << "  - Fixed point check result constant 1 is " << (fpcConst1 ? "SAT" : "UNSAT") << std::endl; }
					fpcSolver->PermanentlyDisableTrigger(currentFpcPositiveTrigger, BackendClauseType::NORMAL, createFpcVariable);
					EXIT_SOLVE_ON_INTERRPUT(depth + 1);

					if (!fpcConst1) {
						if (IsLogEnabled(LogLevel::Info)) { std::cerr << "Fixed point constant 1 reached at depth " << (depth + 1) << " after " << std::fixed << std::setprecision(3) << get_elapsed() << "s" << std::endl; }
						if (IsLogExactly(LogLevel::Competition)) { std::cerr << "=> UNREACHABLE " << (depth + 1) << " " << std::fixed << std::setprecision(6) << get_elapsed() << std::endl; }
						auto builder { BmcCertificateBuilder(BmcCertificate::Type::Craig) };
						return BmcResult::ForCertificate(depth + 1, builder.Build({ BmcCertificateBuilder::CONSTANT_1 }));
					}

					// --------------------------------------------------------

					// Constant 0 (occurrs when I0 is constant 0)
					//   Try to solve (C[0]0<-1 v ... v C[x]0<-1).
					//   If the result is UNSAT then all Craig interpolants are always false.
					if (IsLogEnabled(LogLevel::Debug)) { std::cerr << "  - Fixed point check adding " << std::to_string(1) << " root clauses" << std::endl; }
					BmcClauses fpcNegativeClauses;
					fpcNegativeClauses.push_back(craigRoots);
					fpcSolver->AddTrigger(currentFpcNegativeTrigger, BackendClauseType::NORMAL, createFpcVariable, IsLogEnabled(LogLevel::FullTrace));
					fpcSolver->AddClauses(currentFpcNegativeTrigger, fpcNegativeClauses, 0, BackendClauseType::NORMAL, createFpcVariable, IsLogEnabled(LogLevel::FullTrace));

					if (IsLogEnabled(LogLevel::Debug)) { std::cerr << "  - Fixed point check solving constant 0 problem" << std::endl; }
					auto const fpcConst0 = fpcSolver->SolveWithAssumptions({ currentFpcNegativeTrigger }, createFpcVariable, IsLogEnabled(LogLevel::FullTrace));
					if (IsLogEnabled(LogLevel::Info)) { std::cerr << "  - Fixed point check result constant 0 is " << (fpcConst0 ? "SAT" : "UNSAT") << std::endl; }
					fpcSolver->PermanentlyDisableTrigger(currentFpcNegativeTrigger, BackendClauseType::NORMAL, createFpcVariable, IsLogEnabled(LogLevel::FullTrace));
					EXIT_SOLVE_ON_INTERRPUT(depth + 1);

					if (!fpcConst0) {
						if (IsLogEnabled(LogLevel::Info)) { std::cerr << "Fixed point constant 0 reached at depth " << (depth + 1) << " after " << std::fixed << std::setprecision(3) << get_elapsed() << "s" << std::endl; }
						if (IsLogExactly(LogLevel::Competition)) { std::cerr << "=> UNREACHABLE " << (depth + 1) << " " << std::fixed << std::setprecision(6) << get_elapsed() << std::endl; }
						auto builder { BmcCertificateBuilder(BmcCertificate::Type::Craig) };
						return BmcResult::ForCertificate(depth + 1, builder.Build({ BmcCertificateBuilder::CONSTANT_0 }));
					}
				}

				if (IsLogEnabled(LogLevel::Trace)) { std::cerr << "  - Converting Craig interpolant to CNF" << std::endl; }

				// The simplified BMC problem returned UNSAT so we can get
				// a craig interpolant from the conflicts that have been found.
				const auto createBmcVariable = [&](auto variableId) -> auto {
					auto result = this->CreateSolverVariable(SolverVariableType::CraigTseitin);
#ifndef NDEBUG
					if (configuration.IsBmcExportEnabled()) {
						debugVariables.push_back({ BmcVariable(result), to_string(SolverVariableType::CraigTseitin) });
					}
#endif
					return result;
				};
				const auto [originalCraig, craigRoot] = bmcSolver->GetCraigInterpolant(configuration.GetCraigInterpolant(), createBmcVariable, IsLogEnabled(LogLevel::ExtendedTrace));
				EXIT_SOLVE_ON_INTERRPUT(depth + 1);

#ifndef NDEBUG
				if (configuration.IsBmcExportEnabled()) {
					std::cerr << "  - Exporting BMC craig to " << std::to_string(debugSolves - 1) << ".1-craig.cnf" << std::endl;
					ExportCraig(configuration.GetDebugPath() + "/" + std::to_string(debugSolves - 1) + ".1-craig.cnf", craigRoot, originalCraig);
				}
#endif

				if ((configuration.GetMaximumCraigSize() != 0) && (originalCraig.size() > configuration.GetMaximumCraigSize())) {
					if (IsLogEnabled(LogLevel::Info)) { std::cerr << "  - Craig interpolant exceeded size limit" << std::endl; }
					if (IsLogEnabled(LogLevel::Debug)) {
						std::cerr << "  - Craig interpolant size is " << std::to_string(originalCraig.size()) << std::endl;
						std::cerr << "  - Craig interpolant limit is " << std::to_string(configuration.GetMaximumCraigSize()) << std::endl;
					}
					return BmcResult::ForCraigLimit(depth + 1);
				}

				if (IsLogEnabled(LogLevel::Trace)) { std::cerr << "  - Preprocessing " << std::to_string(originalCraig.size()) << " Craig interpolant clauses" << std::endl; }
				const auto craigClauses = PreprocessClauses(originalCraig, craigRoot, configuration.GetPreprocessCraig());
				EXIT_SOLVE_ON_INTERRPUT(depth + 1);

#ifndef NDEBUG
				if (configuration.IsClauseExportEnabled()) {
					ExportClauses(originalCraig, { craigRoot }, configuration.GetDebugPath() + "/" + std::to_string(depth + 1) + "-original-craig.cnf");
					ExportClauses(craigClauses, { craigRoot }, configuration.GetDebugPath() + "/" + std::to_string(depth + 1) + "-preprocessed-craig.cnf");
				}
#endif

				// --------------------------------------------------------------------
				// Fixpoint check (Part 2 - Progress Case)
				// --------------------------------------------------------------------
				if (configuration.GetEnableFixPointCheck()) {
					if (IsLogEnabled(LogLevel::Debug)) { std::cerr << "  - Fixed point check adding " << std::to_string(craigClauses.size()) << " Craig clauses" << std::endl; }
					// Check if the Craig interpolant contains exactly the same states as the union of all previous Craig interpolants.
					// If this is the case then the Craig interpolant state space will not further grow and a fixpoint is reached.
					// If we reached a fixpoint then the whole BMC problem is unsolvable as no new states can ever be reached.
					//   Try to solve !(C[0]0<-1 v ... v C[x]0<-1) & C[x + 1]0<-1.
					//   If the result is UNSAT then the new Craig interpolant contains only states that are
					//   inside the union of the old Craig interpolants.
					fpcSolver->AddTrigger(craigRoot >> -1, BackendClauseType::NORMAL, createFpcVariable, IsLogEnabled(LogLevel::FullTrace));
					fpcSolver->AddClauses(INVALID_LITERAL, craigClauses, -1, BackendClauseType::NORMAL, createFpcVariable, IsLogEnabled(LogLevel::FullTrace));

					BmcClauses fpcProgressClauses;
					for (auto& root : craigRoots) {
						fpcProgressClauses.push_back({ -root });
					}
					fpcProgressClauses.push_back({ craigRoot >> -1 });
					fpcSolver->AddTrigger(currentFpcProgressTrigger, BackendClauseType::NORMAL, createFpcVariable, IsLogEnabled(LogLevel::FullTrace));
					fpcSolver->AddClauses(currentFpcProgressTrigger, fpcProgressClauses, 0, BackendClauseType::NORMAL, createFpcVariable, IsLogEnabled(LogLevel::FullTrace));

					if (IsLogEnabled(LogLevel::Debug)) { std::cerr << "  - Fixed point check solving" << std::endl; }
					auto const fpcResult = fpcSolver->SolveWithAssumptions({ currentFpcProgressTrigger }, createFpcVariable, IsLogEnabled(LogLevel::FullTrace));
					if (IsLogEnabled(LogLevel::Info)) { std::cerr << "  - Fixed point check result " << (fpcResult ? "SAT" : "UNSAT") << std::endl; }
					fpcSolver->PermanentlyDisableTrigger(currentFpcProgressTrigger, BackendClauseType::NORMAL, createFpcVariable, IsLogEnabled(LogLevel::FullTrace));
					EXIT_SOLVE_ON_INTERRPUT(depth + 1);

					if (!fpcResult) {
						if (IsLogEnabled(LogLevel::Info)) { std::cerr << "Fixed point reached at depth " << (depth + 1) << " after " << std::fixed << std::setprecision(3) << get_elapsed() << "s" << std::endl; }
						if (IsLogExactly(LogLevel::Competition)) { std::cerr << "=> UNREACHABLE " << (depth + 1) << " " << std::fixed << std::setprecision(6) << get_elapsed() << std::endl; }

						// Transforming CNF back into an AIG
						auto builder { BmcCertificateBuilder(BmcCertificate::Type::Craig) };
						if (originalCraig == BmcClauses { BmcClause { craigRoot } }) {
							return BmcResult::ForCertificate(depth + 1, builder.Build({BmcCertificateBuilder::CONSTANT_1 }));
						} else if (originalCraig == BmcClauses { BmcClause { -craigRoot } }) {
							return BmcResult::ForCertificate(depth + 1, builder.Build({BmcCertificateBuilder::CONSTANT_0 }));
						} else {
							std::unordered_map<BmcLiteral, ssize_t, BmcLiteral::Hash> literalToNode;
							auto const get_index = [&](const BmcLiteral& literal) -> ssize_t {
								if (auto it = literalToNode.find(literal); it != literalToNode.end()) {
									return it->second;
								} else if (auto it = literalToNode.find(-literal); it != literalToNode.end()) {
									return -(it->second);
								} else {
									assert (literal.GetVariable() < globalVariables.size());
									assert (globalVariables[literal.GetVariable()]);
									auto result { literalToNode.insert_or_assign(literal, builder.AddLiteral(literal)) };
									return (result.first)->second;
								}
							};
							auto const create_and = [&](std::vector<ssize_t> inputs) -> ssize_t {
								if (inputs.size() == 0) {
									return BmcCertificateBuilder::CONSTANT_1;
								}

								auto indices { inputs };
								while (indices.size() > 1) {
									for (size_t index { 0u }; index < indices.size(); index += 2) {
										if (index + 1 < indices.size()) {
											indices[index / 2] = builder.AddAnd(indices[index], indices[index + 1]);
										} else {
											indices[index / 2] = indices[index];
										}
									}
									indices.resize((indices.size() + 1) / 2);
								}
								return indices[0];
							};

							for (auto& craig : craigInterpolants) {
								// Encode a Craig interpolant as AND-gates.
								// The decoding of the CNF is dependent on the AIG => CNF implementation inside the solvers.

								// We can have AND-gates that have more than 2 inputs.
								// These are created when selecting a Craig interpolant that is composed of multiple interpolant parts.
								// The problem here is that the clause denoting the size is encoded last.
								// => Iterate twice, first find sizes by going in reverse and then do the encoding.
								std::vector<size_t> andSizes;
								for (ssize_t index { static_cast<ssize_t>(craig.size()) - 1 }; index >= 0;) {
									index -= andSizes.emplace_back(craig[index].size());
								}
								size_t offset { 0u };
								for (ssize_t index { static_cast<ssize_t>(andSizes.size()) - 1 }; index >= 0; index--) {
									offset += andSizes[index];

									std::vector<ssize_t> inputs;
									inputs.reserve(andSizes[index] - 1);
									for (size_t lit { 1u }; lit < craig[offset - 1].size(); lit++) {
										inputs.push_back(-get_index(craig[offset - 1][lit] >> -1));
									}
									literalToNode.insert_or_assign(craig[offset - 1][0] >> -1, create_and(inputs));
								}
							}

							std::vector<ssize_t> aigCraigRoots;
							aigCraigRoots.reserve(craigRoots.size() - 1u);
							for (size_t index { 1u }; index < craigRoots.size(); index++) {
								aigCraigRoots.push_back(-get_index(craigRoots[index]));
							}
							return BmcResult::ForCertificate(depth + 1, builder.Build({ -create_and(aigCraigRoots) }));
						}
					}
				}

				// --------------------------------------------------------------------
				// Solve BMC problem with current Craig interpolant
				// --------------------------------------------------------------------

				craigRoots.push_back(craigRoot >> -1);
				craigTriggers.push_back(craigTrigger >> depth);
				craigInterpolants.push_back(std::move(originalCraig));

				AddTrigger(craigTrigger >> depth, SolverClauseType::Craig);
				AddClausesToSolver(craigTrigger >> depth, craigClauses, -1, SolverClauseType::Craig);
				AddClausesToSolver(craigTrigger >> depth, { { craigRoot } }, -1, SolverClauseType::Craig);

				// Solve Craig BMC problem of the form (C) -> (S) -> (S) -> ... -> (!P).
				// The Craig interpolation requires that B contains that the property can be reached on any depth.
				// Therefore, the BMC problem has the form (C) -> (S v !P) -> (S v !P) -> ... -> (!P) instead.
				// The encoded depth of the problem is kept constant in each Craig iteration as only the interpolant grows in size.
				// E.g. the A side grows by using Craig interpolants as over-approximation, while the B side stays constant.
				//
				// The problem is split into:
				// 1) Depth > 0 (Step)
				//    There exist a transition from the union of all Craig interpolants (C[0]1<-0 v ... v C[x]1<-0) to !P in 1 to k steps.
				//      A = (C[0]1<-0 v ... v C[x]1<-0) & T0->1
				//      B = !P1 v (T1->2 & (!P2 v ... (Tn-1->n & !Pn))
				//    The B side is split into two parts: Reaching !P or making steps (Px -> Tx->x+1).
				//      B = (!P1 v !P2 v ... v !Pn) & (P1 -> T1->2) & ((P1 & P2) -> T2->3) & ... & ((P1 & ... & Pn-1) -> Tn-1->n)
				BmcClauses aSideClauses;
				aSideClauses.push_back(craigTriggers);
				aSideClauses.push_back({ transTrigger >> 0 });
				aSideClauses.push_back({ -(targetTrigger >> 0) });

				BmcClauses bSideClauses;
				for (ssize_t index { 1 }; index < (depth - craigIteration); index++) {
					if (configuration.GetTotalTransitionRelation()) {
						bSideClauses.push_back({ transTrigger >> index });
					} else {
						// When the target is not fulfilled then force a transition.
						BmcClause clause { transTrigger >> index };
						for (ssize_t target { 1 }; target <= index; target++) {
							clause.push_back(targetTrigger >> target);
						}
						bSideClauses.push_back(clause);
					}
				}
				BmcClause targetReached;
				for (ssize_t index { 1 }; index <= (depth - craigIteration); index++) {
					targetReached.push_back(targetTrigger >> index);
				}
				bSideClauses.push_back(targetReached);

				AddTrigger(aCraigTrigger >> depth, SolverClauseType::ASide);
				AddTrigger(bCraigTrigger >> depth, SolverClauseType::BSide);
				AddClausesToSolver(aCraigTrigger >> depth, aSideClauses, 0, SolverClauseType::ASide);
				AddClausesToSolver(bCraigTrigger >> depth, bSideClauses, 0, SolverClauseType::BSide);

				if (IsLogEnabled(LogLevel::Debug)) { std::cerr << "  - Solving Craig problem" << std::endl; }
				auto craigResult = SolveWithAssumptions({ aCraigTrigger >> depth, bCraigTrigger >> depth });
				if (IsLogEnabled(LogLevel::Info)) { std::cerr << "  - Solving Craig result is " << (craigResult ? "SAT" : "UNSAT") << " after " << std::fixed << std::setprecision(3) << get_elapsed() << "s" << std::endl; }
				EXIT_SOLVE_ON_INTERRPUT(depth + 1);

				if (!craigResult) {
					if (IsLogExactly(LogLevel::Competition)) { std::cerr << "=> UNSAT " << (depth + 1) << " " << std::fixed << std::setprecision(6) << get_elapsed() << std::endl; }
				}

#ifndef NDEBUG
				if (configuration.IsBmcExportEnabled()) {
					std::cerr << "  - Exporting BMC problem to " << std::to_string(debugSolves - 1) << ".0-problem.cnf" << std::endl;
					ExportBmc(configuration.GetDebugPath() + "/" + std::to_string(debugSolves - 1) + ".0-problem.cnf");
				}
#endif

				PermanentlyDisableTrigger(aCraigTrigger >> depth, SolverClauseType::ASide);
				PermanentlyDisableTrigger(bCraigTrigger >> depth, SolverClauseType::BSide);
				EXIT_SOLVE_ON_INTERRPUT(depth);

				if (craigResult) {
					// Re-solve the current depth+1 without the Craig interpolants.
					// We solve depth+1 when continuing the outer loop since the
					// Craig interpolant extended the BMC problem by one timeframe.

					// Disable all Craig interpolants as they are not required anymore.
					// The trigger with index 0 is the initial state and is still required.
					for (ssize_t index { 1 }; index < craigTriggers.size(); index++) {
						PermanentlyDisableTrigger(craigTriggers[index], SolverClauseType::Craig);
					}
					EXIT_SOLVE_ON_INTERRPUT(depth + 1);
					break;
				}

				// The problem is UNSAT so loop again and solve with new Craig interpolant.
				continue;
			}
		}
	}

	// Loop ended without solution being found and maximum depth reached.
	assert(depth == configuration.GetMaximumDepth());
	return BmcResult::ForDepthLimit(depth);

expand_solution:
	// --------------------------------------------------------------------
	// Result expansion
	// --------------------------------------------------------------------

	// Expand the simplified BMC problem to the full BMC problem.
	if (IsLogEnabled(LogLevel::Debug)) { std::cerr << "----------------------------------" << std::endl; }
	if (IsLogEnabled(LogLevel::Minimal)) { std::cerr << "Problem depth " << depth << " (result expansion)" << std::endl; }

	// First extract and fix the values of the global variables.
	// The global variables have been protected so we can assume that
	// the full problem using the global variables can be solved too.
	const auto assume_if_protected = [&](const BmcClauses& clauses, ssize_t shift, const auto& globalVariables) -> void {
		// Timeframe 0 and 1 to only generate assumptions once per variable and timeframe.
		std::vector<std::vector<bool>> assumed {
			std::vector<bool>(globalVariables.size(), false),
			std::vector<bool>(globalVariables.size(), false)
		};

		for (auto& clause : clauses) {
			for (auto& literal : clause) {
				if (bmcSolver->IsEliminated(literal)) {
					continue;
				}

				auto variable { literal.GetVariable() };
				auto timeframe { literal.GetTimeframe() };

				if (variable < globalVariables.size() && globalVariables[variable] && !assumed[timeframe][variable]) {
					assumed[timeframe][variable] = true;

					const auto value { GetSolvedLiteral(literal, shift) };
					if (IsLogEnabled(LogLevel::Trace)) {
						std::cerr << "      Value " << to_string(literal.ToPositive(), shift)
							<< " = " << to_string(value ^ literal.IsNegated()) << std::endl;
					}
					if (value != BmcAssignment::DontCare) {
						assumptions.push_back((literal >> shift) ^ (value == BmcAssignment::Negative));
					}
				}
			}
		}
	};

	if (IsLogEnabled(LogLevel::Debug)) { std::cerr << "  - Fixing global variables" << std::endl; }
	if (IsLogEnabled(LogLevel::Trace)) { std::cerr << "    ------------------------- Init 0" << std::endl; }
	assume_if_protected(initClauses, 0, protectedVariables);
	for (ssize_t index { 0 }; index < depth; index++) {
		if (IsLogEnabled(LogLevel::Trace)) { std::cerr << "    ------------------------- Trans " << index << " -> " << (index + 1) << std::endl; }
		assume_if_protected(transClauses, index, protectedVariables);
	}
	if (IsLogEnabled(LogLevel::Trace)) { std::cerr << "    ------------------------- Target " << depth << std::endl; }
	assume_if_protected(targetClauses, depth, protectedVariables);
	EXIT_SOLVE_ON_INTERRPUT(depth);

	// Add the full BMC problem to the solver that will be solved with the
	// fixed global variables extracted from the simplified BMC problem.
	{
		std::lock_guard guard { mutex };
		bmcSolver = std::make_unique<Solver<ImplTag, CraigSolverTag>>();
	}
	bmcSolver->ConfigureCraigInterpolant(configuration.GetCraigInterpolant(), false);

	AddClausesToSolver(INVALID_LITERAL, originalInit, 0, SolverClauseType::Init);
	for (ssize_t index { 0 }; index < depth; index++) {
		AddClausesToSolver(INVALID_LITERAL, originalTrans, index, SolverClauseType::Trans);
	}
	AddClausesToSolver(INVALID_LITERAL, originalTarget, depth, SolverClauseType::Target);
	EXIT_SOLVE_ON_INTERRPUT(depth);

	// Solve full BMC problem: I0 -> T1 -> ... -> Td -> !P
	// This should work assuming that the preprocessor isn't borked
	// as the global variables have been protected during preprocessing.
	if (IsLogEnabled(LogLevel::Debug)) { std::cerr << "  - Solving expanded problem" << std::endl; }
	auto finalResult = SolveWithAssumptions(assumptions);
	if (IsLogEnabled(LogLevel::Info)) { std::cerr << "  - Solving expanded result is " << (finalResult ? "SAT" : "UNSAT") << " after " << std::fixed << std::setprecision(3) << get_elapsed() << "s" << std::endl; }
	EXIT_SOLVE_ON_INTERRPUT(depth);

	if (!finalResult) {
		std::cerr << "Could not expand solution to full problem. Exiting." << std::endl;
		exit(-1);
	}

	BmcTimeframes timeframes;
	for (ssize_t index { 0 }; index <= depth; index++) {
		BmcTimeframe timeframe;
		timeframe.reserve(problem.GetVariables());
		for (BmcVariableId variable { 0u }; variable < problem.GetVariables(); variable++) {
			auto literal { BmcLiteral::FromVariable(variable, false) };
			auto result { GetSolvedLiteral(literal, index) };
			timeframe.push_back(result);
			if (IsLogEnabled(LogLevel::Trace)) {
				std::cerr << "      Value " << to_string(literal.ToPositive(), index)
							<< " = " << to_string(result) << std::endl;
			}
		}
		timeframes.push_back(timeframe);
	}

	if (IsLogEnabled(LogLevel::Debug)) { std::cerr << "----------------------------------" << std::endl; }

	return BmcResult::ForModel(depth, { timeframes });
}

template<typename ImplTag>
void BmcSolver<ImplTag>::Interrupt() {
	interrupted.store(true, std::memory_order_release);

	std::lock_guard guard { mutex };
	if (bmcSolver) bmcSolver->Interrupt();
	if (fpcSolver) fpcSolver->Interrupt();
	if (preSolver) preSolver->Interrupt();
}

template<typename ImplTag>
void BmcSolver<ImplTag>::ClearInterrupt() {
	interrupted.store(false, std::memory_order_release);
}

template<typename ImplTag>
bool BmcSolver<ImplTag>::IsInterrupted() const {
	return interrupted.load(std::memory_order_acquire);
}

// ----------------------------------------------------------------------------
// Helper methods to make main BMC loop as readable as possible.
// ----------------------------------------------------------------------------

static BackendVariableType MapVariableType(BackendClauseType clauseType, bool isGlobal, bool isProtected) {
	if (isGlobal) { return BackendVariableType::GLOBAL; }
	switch (clauseType) {
		case BackendClauseType::NORMAL: return BackendVariableType::NORMAL;
		case BackendClauseType::A_CLAUSE: return isProtected ? BackendVariableType::A_PROTECTED : BackendVariableType::A_LOCAL;
		case BackendClauseType::B_CLAUSE: return isProtected ? BackendVariableType::B_PROTECTED : BackendVariableType::B_LOCAL;
		default: assert(false); __builtin_unreachable();
	}
}

static BackendClauseType MapClauseType(SolverClauseType type, ssize_t shift) {
	switch (type) {
		case SolverClauseType::Init:
		case SolverClauseType::Craig:
		case SolverClauseType::ASide:
			return BackendClauseType::A_CLAUSE;

		case SolverClauseType::Trans:
			return (shift < 1) ? BackendClauseType::A_CLAUSE : BackendClauseType::B_CLAUSE;

		// The target for the base case (depth 0) is encoded as A-clauses.
		// If we encode the target always as B-clauses then shared variables with the transition relation need to be set as global.
		// However, this requires these variables in both, timeframe 0 and timeframe 1 to be global.
		// Therefore, we use A-clauses instead to only need to set them to global in timeframe 1.
		case SolverClauseType::Target:
			return (shift < 1) ? BackendClauseType::A_CLAUSE : BackendClauseType::B_CLAUSE;

		case SolverClauseType::BSide:
			return BackendClauseType::B_CLAUSE;

		default:
			assert(false); __builtin_unreachable();
	}
}

template<typename ImplTag>
bool BmcSolver<ImplTag>::IsLogEnabled(LogLevel log) {
	return configuration.IsLogLevelEnabled(log);
}

template<typename ImplTag>
bool BmcSolver<ImplTag>::IsLogExactly(LogLevel log) {
	return configuration.IsLogLevelExactly(log);
}

template<typename ImplTag>
BmcLiteral BmcSolver<ImplTag>::CreateSolverVariable(const SolverVariableType type) {
	if (IsLogEnabled(LogLevel::ExtendedTrace)) {
		if (nextVariable.GetId() < globalVariables.size()) {
			std::cerr << "  - Creating " << to_string(type) << " variable " << to_string(nextVariable);
			if (globalVariables[nextVariable.GetId()]) {
				std::cerr << " (Global)";
			} else if (protectedVariables[nextVariable.GetId()]) {
				std::cerr << " (Protected)";
			} else {
				std::cerr << " (Unprotected)";
			}
			std::cerr << std::endl;
		} else {
			std::cerr << "  - Creating " << to_string(type) << " variable " << to_string(nextVariable) << std::endl;
		}
	}

	assert(nextVariable.GetId() == solverVariables.size());
	auto variable { nextVariable++ };
	solverVariables.push_back(type);
	return variable.ToLiteral();
}

// Converts a CNF to a CNF that can be negated by inverting the trigger.
// Input: CNF
//   (A1, A2, ...),
//   (B1, B2, ...)
// Output: CNF, root
//   (-root, A1, A2, ...),
//   (-root, B1, B2, ...),
//   (root, T1, T2, ...),
//   (root, -T1, -A1), (root, -T1, -A2), ...
//   (root, -T2, -B1), (root, -T2, -B2), ...
template<typename ImplTag>
std::tuple<BmcClauses, BmcLiteral> BmcSolver<ImplTag>::ToInvertable(const BmcClauses& clauses, SolverVariableType type) {
	size_t resultingClauses { clauses.size() + 1u };
	for (const auto& clause : clauses) {
		resultingClauses += (clause.size() + 1u);
	}

	BmcClauses result { };
	result.reserve(resultingClauses);

	auto const root { CreateSolverVariable(type) };

	// (-root, A1, A2, ...), (-root, B1, B2, ...), ...
	for (auto& clause : clauses) {
		auto& output { result.emplace_back() };
		output.reserve(clause.size() + 1u);

		output.push_back(-root);
		std::copy(clause.begin(), clause.end(), std::back_inserter(output));
	}

	// (root, T1, T2, ...),
	auto& triggers { result.emplace_back() };
	triggers.reserve(clauses.size() + 1u);
	triggers.push_back(root);

	// (root, -T1, -A1), (root, -T1, -A2), (root, -T2, -B1), ...
	for (auto& clause : clauses) {
		auto trigger { CreateSolverVariable(type) };
		triggers.push_back(trigger);
		for (const auto& literal : clause) {
			result.push_back({ -trigger, -literal });
		}
	}

	return { result, root };
}

template<typename ImplTag>
bool BmcSolver<ImplTag>::AddClausesToSolver(const BmcLiteral& trigger, const BmcClauses& clauses, ssize_t shift, SolverClauseType type) {
	if (IsLogEnabled(LogLevel::Trace)) {
		std::cerr << "  - Adding " << clauses.size() << " " << to_string(type) << " clauses with " << shift << " shift" << std::endl;
	}

	auto const clauseType { MapClauseType(type, shift) };
	auto const createVariable = [&](BmcVariable variable) -> auto {
		auto const isGlobal { ((variable.GetId() < globalVariables.size()) && globalVariables[variable.GetId()]) };
		auto const isProtected { ((variable.GetId() < protectedVariables.size()) && protectedVariables[variable.GetId()]) };
		auto const variableType { MapVariableType(clauseType, isGlobal && (variable.GetTimeframe() == 1), isProtected) };

#ifndef NDEBUG
		if (configuration.IsBmcExportEnabled()) {
			debugVariables.push_back({ variable, to_string(variableType) });
		}
#endif

		return bmcSolver->CreateVariable(variable, variableType, IsLogEnabled(LogLevel::FullTrace));
	};

#ifndef NDEBUG
	if (configuration.IsBmcExportEnabled()) {
		for (auto& clause : clauses) {
			debugClauses.push_back({clause, shift, trigger, to_string(type), to_string(clauseType) });
		}
	}
#endif

	return bmcSolver->AddClauses(trigger, clauses, shift, clauseType, createVariable, IsLogEnabled(LogLevel::ExtendedTrace));
}

template<typename ImplTag>
bool BmcSolver<ImplTag>::UnprotectSolverVariable(const BmcLiteral& variable, ssize_t shift, SolverClauseType type) {
	auto const createVariable = [&](BmcVariable variable) -> auto {
		throw std::runtime_error("Unprotecting " + to_string(type) + " introduced new variable " + to_string(variable));
	};

	auto const clauseType { MapClauseType(type, shift) };
	auto const variableType { MapVariableType(clauseType, false, true) };
	return bmcSolver->UnprotectVariable(variable.ToPositive() >> shift, variableType, createVariable, IsLogEnabled(LogLevel::FullTrace));
}

template<typename ImplTag>
bool BmcSolver<ImplTag>::AddTrigger(const BmcLiteral& trigger, SolverClauseType type) {
	if (IsLogEnabled(LogLevel::Trace)) {
		std::cerr << "  - Adding " << to_string(type) << " trigger " << to_string(trigger) << std::endl;
	}

	auto const clauseType { MapClauseType(type, trigger.GetTimeframe()) };
	auto const variableType { MapVariableType(clauseType, false, true) };
	auto const createVariable = [&](BmcVariable variable) -> auto {
#ifndef NDEBUG
		if (configuration.IsBmcExportEnabled()) {
			debugVariables.push_back({ variable, to_string(variableType) });
		}
#endif

		return bmcSolver->CreateVariable(variable, variableType, IsLogEnabled(LogLevel::FullTrace));
	};
	return bmcSolver->AddTrigger(trigger, clauseType, createVariable, IsLogEnabled(LogLevel::ExtendedTrace));
}

template<typename ImplTag>
bool BmcSolver<ImplTag>::PermanentlyDisableTrigger(const BmcLiteral& trigger, SolverClauseType type) {
	if (IsLogEnabled(LogLevel::Trace)) {
		std::cerr << "  - Permanently disabling " << to_string(type) << " trigger " << to_string(trigger) << std::endl;
	}

	auto const clauseType { MapClauseType(type, trigger.GetTimeframe()) };
	auto const createVariable = [&](BmcVariable variable) -> auto {
		throw std::runtime_error("Disabling trigger introduced new variable");
	};

#ifndef NDEBUG
	if (configuration.IsBmcExportEnabled()) {
		debugDisabledTriggers.push_back({ trigger, to_string(clauseType) });
	}
#endif

	return bmcSolver->PermanentlyDisableTrigger(trigger, clauseType, createVariable, IsLogEnabled(LogLevel::ExtendedTrace));
}

template<typename ImplTag>
bool BmcSolver<ImplTag>::SolveWithAssumptions(const BmcClause& assumptions) {
	if (IsLogEnabled(LogLevel::Trace)) {
		std::cerr << "  - Solving problem with " << assumptions.size() << " assumptions" << std::endl;
		for (auto& assumption : assumptions) {
			std::cerr << "    Assuming " << to_string(assumption)
				<< " (" << to_string(solverVariables[assumption.GetVariable()]) << ")" << std::endl;
		}
	}

	auto const createVariable = [&](BmcVariable variable) -> auto {
		throw std::runtime_error("Assumptions introduced new variable");
	};

#ifndef NDEBUG
	if (configuration.IsBmcExportEnabled()) {
		for (auto& literal : assumptions) {
			debugAssumptions.push_back({ literal });
		}
	}
	debugSolves++;
#endif

	return bmcSolver->SolveWithAssumptions(assumptions, createVariable, IsLogEnabled(LogLevel::ExtendedTrace));
}

template<typename ImplTag>
BmcAssignment BmcSolver<ImplTag>::GetSolvedLiteral(const BmcLiteral& literal, ssize_t shift) {
	// Return false if literal has not been used in either part of the BMC formula.
	// The value does not matter and something like "don't care" could be returned instead.
	auto const& mappings = bmcSolver->GetForwardMappings();
	if (mappings.find(BmcVariable(literal >> shift)) == mappings.end()) {
		return BmcAssignment::DontCare;
	}

	auto const createVariable = [&](BmcVariable variable) -> auto {
		throw std::runtime_error("Variable does not exist in BMC problem");
	};
	return bmcSolver->GetSolvedLiteral(literal, shift, createVariable, IsLogEnabled(LogLevel::ExtendedTrace));
}

// Simplifies the clauses by using the preprocessor.
// Global variables have to be protected in order to not be eliminated during preprocessing.
// The process is as follows: BMC clauses -> mapping to SAT solver -> preprocess -> mapping to BMC solver -> simplified BMC clauses
template<typename ImplTag>
BmcClauses BmcSolver<ImplTag>::PreprocessClauses(const BmcClauses& clauses, const BmcLiteral& root, const PreprocessLevel& level) {
	if (level == PreprocessLevel::None) {
		if (IsLogEnabled(LogLevel::Info)) { std::cerr << "  - Preprocessing is disabled" << std::endl; }
		return clauses;
	}

	{
		std::lock_guard guard { mutex };
		preSolver = std::make_unique<Solver<ImplTag, PreSolverTag>>();
	}

	const auto createPreVariable = [&](auto variable) -> auto {
		return preSolver->CreateVariable(variable, BackendVariableType::NORMAL, IsLogEnabled(LogLevel::FullTrace));
	};
	const auto createBmcVariable = [&](auto variable) -> auto {
		throw std::runtime_error("Preprocessor introduced new variable during preprocessing");
	};

	if (IsLogEnabled(LogLevel::Info)) { std::cerr << "  - Preprocessing started with " << clauses.size() << " clauses" << std::endl; }
	auto result = preSolver->PreprocessClauses(clauses, protectedVariables, root, level,
		createPreVariable, createBmcVariable, IsLogEnabled(LogLevel::ExtendedTrace));
	if (IsLogEnabled(LogLevel::Info)) { std::cerr << "  - Preprocessing finished with " << result.size() << " clauses" << std::endl; }

	{
		std::lock_guard guard { mutex };
		preSolver = nullptr;
	}

	return result;
}

#ifndef NDEBUG
template<typename ImplTag>
void BmcSolver<ImplTag>::ExportClauses(const BmcClauses& clauses, const BmcClause& assumptions, const std::string& path) {
	std::ofstream stream { path };

	std::vector<bool> variables(solverVariables.size(), false);
	for (auto const& clause : clauses) {
		for (auto const& literal : clause) {
			auto id { literal.GetVariable() };
			assert(id < variables.size());
			variables[id] = true;
		}
	}
	for (auto const& literal : assumptions) {
		auto id { literal.GetVariable() };
		assert(id < variables.size());
		variables[id] = true;
	}

	stream << "p cnf " << std::to_string(clauses.size()) << " "
		<< std::to_string(solverVariables.size()) << std::endl;

	size_t variableIndex { 0 };
	for (auto& variable : solverVariables) {
		if (variables[variableIndex]) {
			stream << "c " << std::to_string(variableIndex) << " "
				<< to_string(variable) << std::endl;
		}
		variableIndex++;
	}
	for (auto const& assumption : assumptions) {
		stream << "f " << to_string(assumption) << std::endl;
	}
	for (auto const& clause : clauses) {
		stream << to_string(clause) << std::endl;
	}
}

template<typename ImplTag>
void BmcSolver<ImplTag>::ExportBmc(const std::string& path) {
	std::ofstream stream { path };

	auto const& mappings = bmcSolver->GetForwardMappings();
	auto const lookup_var = [&](auto const& var) {
		auto it = mappings.find(BmcVariable(var));
		assert (it != mappings.end());
		return static_cast<ssize_t>(it->second + 1);
	};
	auto const lookup_lit = [&](auto const& lit) {
		auto it = mappings.find(BmcVariable(lit));
		assert (it != mappings.end());
		return static_cast<ssize_t>(it->second + 1) * (lit.IsNegated() ? -1 : 1);
	};
	std::sort(debugVariables.begin(), debugVariables.end(), [&](auto& first, auto& second) {
		auto firstIt = mappings.find(first.variable);
		auto secondIt = mappings.find(second.variable);
		assert (firstIt != mappings.end());
		assert (secondIt != mappings.end());
		return firstIt->second < secondIt->second;
	});

	stream << "p craigcnf " << std::to_string(debugVariables.size())
		<< " " << debugClauses.size() << std::endl;
	for (auto& variable : debugVariables) {
		if (variable.backendType == "A_LOCAL" || variable.backendType == "A_PROTECTED") {
			stream << "a ";
		} else if (variable.backendType == "B_LOCAL" || variable.backendType == "B_PROTECTED") {
			stream << "b ";
		} else {
			stream << "g ";
		}
		stream << std::to_string(lookup_var(variable.variable)) << " 0" << std::endl;
	}

	for (auto& [literal] : debugAssumptions) {
		stream << "f " << std::to_string(lookup_lit(literal)) << " 0" << std::endl;
	}

	for (auto& [literal, type] : debugDisabledTriggers) {
		stream << "f " << std::to_string(lookup_lit(-literal)) << " 0" << std::endl;
	}

	for (auto& [clause, shift, trigger, origType, type] : debugClauses) {
		if (type == "A_CLAUSE") {
			stream << "A";
		} else if (type == "B_CLAUSE") {
			stream << "B";
		}
		if (trigger != INVALID_LITERAL) {
			stream << " " << std::to_string(lookup_lit(-trigger));
		}
		for (auto& lit : clause) {
			stream << " " << std::to_string(lookup_lit(lit >> shift));
		}
		stream << " 0" << std::endl;
	}

	debugAssumptions.clear();
}


template<typename ImplTag>
void BmcSolver<ImplTag>::ExportCraig(const std::string& path, BmcLiteral craigRoot, const BmcClauses& craig) {
	std::ofstream stream { path };

	auto const& mappings = bmcSolver->GetForwardMappings();
	auto const lookup_var = [&](auto const& var) {
		auto it = mappings.find(BmcVariable(var));
		assert (it != mappings.end());
		return static_cast<ssize_t>(it->second + 1);
	};
	auto const lookup_lit = [&](auto const& lit) {
		auto it = mappings.find(BmcVariable(lit));
		assert (it != mappings.end());
		return static_cast<ssize_t>(it->second + 1) * (lit.IsNegated() ? -1 : 1);
	};
	std::sort(debugVariables.begin(), debugVariables.end(), [&](auto& first, auto& second) {
		auto firstIt = mappings.find(first.variable);
		auto secondIt = mappings.find(second.variable);
		assert (firstIt != mappings.end());
		assert (secondIt != mappings.end());
		return firstIt->second < secondIt->second;
	});

	stream << "p craigcnf " << std::to_string(debugVariables.size()) << " " << craig.size() << std::endl;
	for (auto& variable : debugVariables) {
		if (variable.backendType == "A_LOCAL" || variable.backendType == "A_PROTECTED") {
			stream << "a ";
		} else if (variable.backendType == "B_LOCAL" || variable.backendType == "B_PROTECTED") {
			stream << "b ";
		} else {
			stream << "g ";
		}
		stream << std::to_string(lookup_var(variable.variable)) << " 0" << std::endl;
	}
	for (auto& clause : craig) {
		stream << "A";
		for (auto& lit : clause) {
			stream << " " << std::to_string(lookup_lit(lit));
		}
		stream << " 0" << std::endl;
	}
	stream << "A " << std::to_string(lookup_lit(craigRoot)) << " 0" << std::endl;
}
#endif

namespace Backend {

#ifdef NCIP_BACKEND_MINICRAIG
template<> struct SolverVariableMapInterface<MiniCraigTag, CraigSolverTag>: public MiniCraigVariableMapInterface { };
template<> struct SolverVariableMapInterface<MiniCraigTag, PreSolverTag>: public MiniCraigVariableMapInterface { };
template<> struct SolverVariableMapInterface<MiniCraigTag, FpcSolverTag>: public MiniCraigVariableMapInterface { };
#ifndef NDEBUG
template<> struct SolverVariableMapInterface<MiniCraigDebugTag, CraigSolverTag>: public MiniCraigDebugVariableMapInterface { };
template<> struct SolverVariableMapInterface<MiniCraigDebugTag, PreSolverTag>: public MiniCraigDebugVariableMapInterface { };
template<> struct SolverVariableMapInterface<MiniCraigDebugTag, FpcSolverTag>: public MiniCraigDebugVariableMapInterface { };
#endif

template<> struct SolverInterface<MiniCraigTag, CraigSolverTag>: public MiniCraigSolverInterface<MiniCraigTag, CraigSolverTag> { };
template<> struct SolverInterface<MiniCraigTag, PreSolverTag>: public MiniCraigSolverInterface<MiniCraigTag, PreSolverTag> { };
template<> struct SolverInterface<MiniCraigTag, FpcSolverTag>: public MiniCraigSolverInterface<MiniCraigTag, FpcSolverTag> { };
#ifndef NDEBUG
template<> struct SolverInterface<MiniCraigDebugTag, CraigSolverTag>: public MiniCraigDebugSolverInterface<MiniCraigDebugTag, CraigSolverTag> { };
template<> struct SolverInterface<MiniCraigDebugTag, PreSolverTag>: public MiniCraigDebugSolverInterface<MiniCraigDebugTag, PreSolverTag> { };
template<> struct SolverInterface<MiniCraigDebugTag, FpcSolverTag>: public MiniCraigDebugSolverInterface<MiniCraigDebugTag, FpcSolverTag> { };
#endif
#endif

#ifdef NCIP_BACKEND_CADICRAIG
template<> struct SolverVariableMapInterface<CadiCraigTag, CraigSolverTag>: public CadiCraigVariableMapInterface { };
template<> struct SolverVariableMapInterface<CadiCraigTag, PreSolverTag>: public CadiCraigVariableMapInterface { };
template<> struct SolverVariableMapInterface<CadiCraigTag, FpcSolverTag>: public CadiCraigVariableMapInterface { };
#ifndef NDEBUG
template<> struct SolverVariableMapInterface<CadiCraigDebugTag, CraigSolverTag>: public CadiCraigDebugVariableMapInterface { };
template<> struct SolverVariableMapInterface<CadiCraigDebugTag, PreSolverTag>: public CadiCraigDebugVariableMapInterface { };
template<> struct SolverVariableMapInterface<CadiCraigDebugTag, FpcSolverTag>: public CadiCraigDebugVariableMapInterface { };
#endif

template<> struct SolverInterface<CadiCraigTag, CraigSolverTag>: public CadiCraigSolverInterface<CadiCraigTag, CraigSolverTag> { };
template<> struct SolverInterface<CadiCraigTag, PreSolverTag>: public CadiCraigSolverInterface<CadiCraigTag, PreSolverTag> { };
template<> struct SolverInterface<CadiCraigTag, FpcSolverTag>: public CadiCraigSolverInterface<CadiCraigTag, FpcSolverTag> { };
#ifndef NDEBUG
template<> struct SolverInterface<CadiCraigDebugTag, CraigSolverTag>: public CadiCraigDebugSolverInterface<CadiCraigDebugTag, CraigSolverTag> { };
template<> struct SolverInterface<CadiCraigDebugTag, PreSolverTag>: public CadiCraigDebugSolverInterface<CadiCraigDebugTag, PreSolverTag> { };
template<> struct SolverInterface<CadiCraigDebugTag, FpcSolverTag>: public CadiCraigDebugSolverInterface<CadiCraigDebugTag, FpcSolverTag> { };
#endif
#endif

#ifdef NCIP_BACKEND_KITTENCRAIG
template<> struct SolverVariableMapInterface<KittenCraigTag, CraigSolverTag>: public KittenCraigVariableMapInterface { };
template<> struct SolverVariableMapInterface<KittenCraigTag, PreSolverTag>: public KittenCraigVariableMapInterface { };
template<> struct SolverVariableMapInterface<KittenCraigTag, FpcSolverTag>: public KittenCraigVariableMapInterface { };

template<> struct SolverInterface<KittenCraigTag, CraigSolverTag>: public KittenCraigSolverInterface<KittenCraigTag, CraigSolverTag> { };
template<> struct SolverInterface<KittenCraigTag, PreSolverTag>: public KittenCraigSolverInterface<KittenCraigTag, PreSolverTag> { };
template<> struct SolverInterface<KittenCraigTag, FpcSolverTag>: public KittenCraigSolverInterface<KittenCraigTag, FpcSolverTag> { };
#endif

}

#ifdef NCIP_BACKEND_MINICRAIG
template struct BmcSolver<MiniCraigTag>;
#ifndef NDEBUG
template struct BmcSolver<MiniCraigDebugTag>;
#endif
#endif

#ifdef NCIP_BACKEND_CADICRAIG
template struct BmcSolver<CadiCraigTag>;
#ifndef NDEBUG
template struct BmcSolver<CadiCraigDebugTag>;
#endif
#endif

#ifdef NCIP_BACKEND_KITTENCRAIG
template struct BmcSolver<KittenCraigTag>;
#endif

}
