// SPDX-License-Identifier: MIT OR Apache-2.0

#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <unordered_map>
#include <memory>
#include <tuple>
#include <vector>
#include <mutex>
#include <iostream>
#include <variant>

#include "./bmc-problem.hpp"
#include "./bmc-ncip-impl.hpp"

namespace Ncip {

enum class BmcStatus {
	Sat,
	Unsat,
	DepthLimitReached,
	CraigLimitReached,
	MemoryLimitReached,
	Interrupted
};

enum class LogLevel {
	None =  0u,
	Competition = 1u,
	Minimal = 2u,
	Info = 3u,
	Debug = 4u,
	Trace = 5u,
	ExtendedTrace = 6u,
	FullTrace = 7u
};

enum class CraigInterpolant {
	Symmetric,
	Asymmetric,
	DualSymmetric,
	DualAsymmetric,
	Intersection,
	Union,
	Smallest,
	Largest,
};

enum class PreprocessLevel {
	None = 0u,
	Simple = 1u,
	Expensive = 2u
};

class BmcConfiguration {
public:
	BmcConfiguration():
		maximumDepth(0u),
		maximumCraigSize(0u),
		logLevel(LogLevel::Info),
#ifndef NDEBUG
		clauseExport(false),
		bmcExport(false),
		debugPath("./output"),
#endif
		craigInterpolant(CraigInterpolant::Asymmetric),
		enableCraigInterpolation(true),
		enableFixPointCheck(true),
		enableSanityChecks(false),
		totalTransitionRelation(false),
		simplifyInit(PreprocessLevel::Simple),
		simplifyTrans(PreprocessLevel::Simple),
		simplifyTarget(PreprocessLevel::Simple),
		simplifyCraig(PreprocessLevel::Simple)
	{}

	BmcConfiguration& SetMaximumDepth(size_t depth) { maximumDepth = depth; return *this; }
	BmcConfiguration& SetMaximumCraigSize(size_t size) { maximumCraigSize = size; return *this; }
	BmcConfiguration& SetCraigInterpolant(CraigInterpolant interpolant) { craigInterpolant = interpolant; return *this; }
	BmcConfiguration& SetEnableCraigInterpolation(bool enable) { enableCraigInterpolation = enable; return *this; }
	BmcConfiguration& SetEnableFixPointCheck(bool enable) { enableFixPointCheck = enable; return *this; }
	BmcConfiguration& SetEnableSanityChecks(bool enable) { enableSanityChecks = enable; return *this; }
	BmcConfiguration& SetTotalTransitionRelation(bool total) { totalTransitionRelation = total; return *this; }
	BmcConfiguration& SetPreprocessInit(PreprocessLevel level) { simplifyInit = level; return *this; }
	BmcConfiguration& SetPreprocessTrans(PreprocessLevel level) { simplifyTrans = level; return *this; }
	BmcConfiguration& SetPreprocessTarget(PreprocessLevel level) { simplifyTarget = level; return *this; }
	BmcConfiguration& SetPreprocessCraig(PreprocessLevel level) { simplifyCraig = level; return *this; }

	size_t GetMaximumDepth() const { return maximumDepth; }
	size_t GetMaximumCraigSize() const { return maximumCraigSize; }

#ifndef NDEBUG
	BmcConfiguration& SetLogLevel(LogLevel level) { logLevel = level; return *this; }
	BmcConfiguration& SetClauseExport(bool enabled) { clauseExport = enabled; return *this; }
	BmcConfiguration& SetBmcExport(bool enabled) { bmcExport = enabled; return *this; }
	BmcConfiguration& SetDebugPath(std::string path) { debugPath = path; return *this; }
	LogLevel GetLogLevel() const { return logLevel; };
	bool IsLogLevelEnabled(const LogLevel& level) const { return __builtin_expect(logLevel >= level, false); }
	bool IsLogLevelExactly(const LogLevel& level) const { return __builtin_expect(logLevel == level, false); }
	bool IsClauseExportEnabled() const { return clauseExport; }
	bool IsBmcExportEnabled() const { return bmcExport; }
	std::string const& GetDebugPath() const { return debugPath; };
#else
	BmcConfiguration& SetLogLevel(LogLevel level) { logLevel = level; return *this; }
	LogLevel GetLogLevel() const { return (logLevel >= LogLevel::Debug) ? LogLevel::Info : logLevel; };
	bool IsLogLevelEnabled(const LogLevel& level) const { return __builtin_expect((level < LogLevel::Debug) && (logLevel >= level), false); }
	bool IsLogLevelExactly(const LogLevel& level) const { return __builtin_expect((level < LogLevel::Debug) && (logLevel == level), false); }
#endif

	CraigInterpolant GetCraigInterpolant() const { return craigInterpolant; }
	bool GetEnableCraigInterpolation() const { return enableCraigInterpolation; }
	bool GetEnableFixPointCheck() const { return enableFixPointCheck; }
	bool GetEnableSanityChecks() const { return enableSanityChecks; }
	bool GetTotalTransitionRelation() const { return totalTransitionRelation; }
	PreprocessLevel GetPreprocessInit() const { return simplifyInit; }
	PreprocessLevel GetPreprocessTrans() const { return simplifyTrans; }
	PreprocessLevel GetPreprocessTarget() const { return simplifyTarget; }
	PreprocessLevel GetPreprocessCraig() const { return simplifyCraig; }

private:
	size_t maximumDepth;
	size_t maximumCraigSize;
	LogLevel logLevel;
#ifndef NDEBUG
	bool clauseExport;
	bool bmcExport;
	std::string debugPath;
#endif
	CraigInterpolant craigInterpolant;
	bool enableCraigInterpolation;
	bool enableFixPointCheck;
	bool enableSanityChecks;
	bool totalTransitionRelation;
	PreprocessLevel simplifyInit;
	PreprocessLevel simplifyTrans;
	PreprocessLevel simplifyTarget;
	PreprocessLevel simplifyCraig;

};

class BmcResult {
public:
	static BmcResult ForModel(ssize_t depth, BmcModel model) {
		return BmcResult(BmcStatus::Sat, model, {}, depth);
	}
	static BmcResult ForCertificate(ssize_t depth, BmcCertificate certificate) {
		return BmcResult(BmcStatus::Unsat, {}, certificate, depth);
	}
	static BmcResult ForDepthLimit(ssize_t depth) {
		return BmcResult(BmcStatus::DepthLimitReached, {}, {}, depth);
	}
	static BmcResult ForCraigLimit(ssize_t depth) {
		return BmcResult(BmcStatus::CraigLimitReached, {}, {}, depth);
	}
	static BmcResult ForUserInterrupt(ssize_t depth) {
		return BmcResult(BmcStatus::Interrupted, {}, {}, depth);
	}
	static BmcResult ForMemoryLimit() {
		return BmcResult(BmcStatus::MemoryLimitReached, {}, {}, 0);
	}

	const BmcStatus& GetStatus() const { return status; }
	const BmcModel& GetModel() const { return model; }
	const BmcCertificate& GetCertificate() const { return certificate; }
	const size_t& GetDepth() const { return depth; }

private:
	BmcResult(BmcStatus status_, BmcModel model_, BmcCertificate certificate_, ssize_t depth_):
		status(status_),
		model(model_),
		certificate(certificate_),
		depth((depth_ < 0) ? 0 : static_cast<size_t>(depth_))
	{
	}

	BmcStatus status;
	BmcModel model;
	BmcCertificate certificate;
	size_t depth;

};

template<typename ImplTag>
class BmcSolver {
public:
	BmcSolver(BmcProblem problem, BmcConfiguration configuration);
	BmcSolver(BmcSolver&& other);
	virtual ~BmcSolver();

	BmcResult Solve();
	void Interrupt();
	void ClearInterrupt();
	bool IsInterrupted() const;

	using BackendTag = ImplTag;

private:
	BmcResult SolveImpl();
	void InitializeProtectedGlobalVariables();
	bool UnprotectSolverVariable(const BmcLiteral& variable, ssize_t shift, Impl::SolverClauseType type);
	BmcLiteral CreateSolverVariable(const Impl::SolverVariableType type);
	bool AddClausesToSolver(const BmcLiteral& trigger, const BmcClauses& clauses, ssize_t shift, Impl::SolverClauseType type);
	bool AddTrigger(const BmcLiteral& trigger, Impl::SolverClauseType type);
	bool PermanentlyDisableTrigger(const BmcLiteral& trigger, Impl::SolverClauseType type);
	bool SolveWithAssumptions(const BmcClause& assumptions);
	BmcAssignment GetSolvedLiteral(const BmcLiteral& literal, ssize_t shift);
	BmcClauses PreprocessClauses(const BmcClauses& clauses, const BmcLiteral& root, const PreprocessLevel& level);
	std::tuple<BmcClauses, BmcLiteral> ToInvertable(const BmcClauses& clauses, Impl::SolverVariableType type);
	bool IsLogEnabled(LogLevel log);
	bool IsLogExactly(LogLevel log);

	BmcProblem problem;
	BmcConfiguration configuration;

	std::vector<bool> globalVariables;
	std::vector<bool> protectedVariables;
	Impl::BmcVariable nextVariable;
	std::vector<Impl::SolverVariableType> solverVariables;

	std::recursive_mutex mutex;
	std::atomic<bool> interrupted;
	std::unique_ptr<Backend::Solver<ImplTag, Backend::CraigSolverTag>> bmcSolver;
	std::unique_ptr<Backend::Solver<ImplTag, Backend::PreSolverTag>> preSolver;
	std::unique_ptr<Backend::Solver<ImplTag, Backend::FpcSolverTag>> fpcSolver;

#ifndef NDEBUG
	struct DebugVariable { Impl::BmcVariable variable; std::string backendType; };
	struct DebugDisabledTrigger { BmcLiteral literal; std::string backendType; };
	struct DebugAssumption { BmcLiteral literal; };
	struct DebugClause { BmcClause clause; ssize_t shift; BmcLiteral trigger; std::string type; std::string backendType; };

	void ExportClauses(const BmcClauses& clauses, const BmcClause& assumptions, const std::string& path);
	void ExportBmc(const std::string& path);
	void ExportCraig(const std::string& path, BmcLiteral craigRoot, const BmcClauses& craig);

	size_t debugSolves;
	std::vector<DebugVariable> debugVariables;
	std::vector<DebugDisabledTrigger> debugDisabledTriggers;
	std::vector<DebugAssumption> debugAssumptions;
	std::vector<DebugClause> debugClauses;
#endif

};

#ifdef NCIP_BACKEND_MINICRAIG
using MiniCraigBmcSolver = BmcSolver<Backend::MiniCraigTag>;
# ifndef NDEBUG
using MiniCraigDebugBmcSolver = BmcSolver<Backend::MiniCraigDebugTag>;
# endif
#endif

#ifdef NCIP_BACKEND_CADICRAIG
using CadiCraigBmcSolver = BmcSolver<Backend::CadiCraigTag>;
# ifndef NDEBUG
using CadiCraigDebugBmcSolver = BmcSolver<Backend::CadiCraigDebugTag>;
# endif
#endif

#ifdef NCIP_BACKEND_KITTENCRAIG
using KittenCraigBmcSolver = BmcSolver<Backend::KittenCraigTag>;
#endif

}
