#ifndef _kittentracer_hpp_INCLUDED
#define _kittentracer_hpp_INCLUDED

#ifdef INCLUDE_PREFIXED
# include <kissat/kitten.h>
#else /* INCLUDE_PREFIXED */
# include <kitten.h>
#endif /* INCLUDE_PREFIXED */

#include <string>
#include <iostream>
#include <vector>
#include <map>
#include <set>

namespace KittenCraig {

class Aig;
class CraigData;

enum class CraigCnfType : uint8_t {
  NONE,
  CONSTANT0,
  CONSTANT1,
  NORMAL
};

enum class CraigConstruction : uint8_t {
  NONE = 0,
  SYMMETRIC = 1,
  ASYMMETRIC = 2,
  DUAL_SYMMETRIC = 4,
  DUAL_ASYMMETRIC = 8,
  ALL = 15
};

enum class CraigInterpolant : uint8_t {
  NONE,
  SYMMETRIC,
  ASYMMETRIC,
  DUAL_SYMMETRIC,
  DUAL_ASYMMETRIC,
  INTERSECTION,
  UNION,
  SMALLEST,
  LARGEST
};

CraigConstruction operator|(const CraigConstruction& first, const CraigConstruction& second);

enum class CraigVarType : uint8_t {
  A_LOCAL,
  B_LOCAL,
  GLOBAL
};

std::string to_string(const CraigVarType& var_type);
std::ostream& operator<<(std::ostream& out, const CraigVarType& var_type);

enum class CraigClauseType : uint8_t {
  A_CLAUSE,
  B_CLAUSE,
  L_CLAUSE
};

std::string to_string(const CraigClauseType& clause_type);
std::ostream& operator<<(std::ostream&, const CraigClauseType& clause_type);

class KittenTracer {
public:
  KittenTracer();
  virtual ~KittenTracer();

  // ====== BEGIN CRAIG INTERFACE ============================================

  // Add variable of A, B or G type. This has to be called before
  // adding clauses using the variables when Craig interpolation is enabled.
  // - A_LOCAL
  // - B_LOCAL
  // - GLOBAL
  //
  //   require (VALID)
  //
  void label_variable (int id, CraigVarType variable_type);

  // Add clause type of A or B. This has to be called right
  // before adding the respective clause that this type applies to.
  // - A_CLAUSE
  // - B_CLAUSE
  //
  //   require (VALID)
  //
  void label_clause (int id, CraigClauseType clause_type);

  // A bit field that configures the Craig interpolant bases to be built.
  // The following interpolant bases can be built:
  // - SYMMETRIC
  // - ASYMMETRIC
  // - DUAL_SYMMETRIC
  // - DUAL_ASYMMETRIC
  //
  //   require (CONFIGURING)
  //   ensure (CONFIGURING)
  //
  void set_craig_construction (CraigConstruction craig_construction);

  // Builds the Craig interpolant specified and writes the result
  // to the output vector. Required Tseitin variables for CNF creation
  // will start from the tseitin_offset provided.
  // The following interpolants are available:
  // - NONE
  // - SYMMETRIC (requires base SYMMETRIC)
  // - ASYMMETRIC (requires base ASYMMETRIC)
  // - DUAL_SYMMETRIC (requires base DUAL_SYMMETRIC)
  // - DUAL_ASYMMETRIC (requires base DUAL_ASYMMETRIC)
  // - INTERSECTION (of selected interpolant bases)
  // - UNION (of selected interpolant bases)
  // - SMALLEST (of selected interpolant bases)
  // - LARGEST (of selected interpolant bases)
  //
  // Returns the resulting CNF type.
  // The result can be NONE when either no Interpolant was requested
  // or if the construction of the craig interpolant is not enabled.
  // The NORMAL type CNF contains a unit clause with the trigger
  // for the Craig interpolant as the last clause.
  // The following CNF types can be returned by the function:
  // - NONE
  // - CONST0 (CNF is constant false)
  // - CONST1 (CNF is constant true)
  // - NORMAL (CNF is not constant)
  //
  //   require (UNSATISFIED)
  //
  CraigCnfType create_craig_interpolant (CraigInterpolant interpolant,
                                         std::vector<std::vector<int>>& cnf,
                                         int& tseitin_offset);

  // ====== END CRAIG INTERFACE ============================================

  void add_assumption(int lit);
  void reset_assumptions();
  void conclude_unsat(kitten* kitten);

private:
  friend void kitten_trace(void* state, unsigned clause_id, unsigned external_id, bool learned,
                         size_t literalCount, const unsigned *literals,
                         size_t antecedentCount, const unsigned *antecedents);

  CraigData* create_interpolant_for_assumption (int literal);
  CraigData* create_interpolant_for_clause (const std::vector<int>& c, CraigClauseType t);
  void extend_interpolant_with_resolution (CraigData& result, int literal, const CraigData& craig_data);
  bool is_construction_enabled (CraigConstruction construction);
  void clear_craig_interpolant ();
  bool has_craig_interpolant ();

  uint8_t mark_literal (int literal);
  uint8_t is_marked (int literal);
  void unmark_all ();

  std::vector<int> marked_history;
  std::map<int, uint8_t> marked_lits;

  std::set<int> assumptions;
  std::map<int, CraigVarType> craig_var_labels;
  std::map<int, CraigClauseType> craig_clause_labels;

  std::map<uint64_t, std::vector<int>> craig_clauses;
  std::map<uint64_t, CraigData*> craig_interpolants;

  int craig_clause_last_id;
  CraigConstruction craig_construction;
  size_t craig_id;
  CraigData* craig_interpolant;

  Aig* craig_aig_sym;
  Aig* craig_aig_asym;
  Aig* craig_aig_dual_sym;
  Aig* craig_aig_dual_asym;

};

} // namespace KittenCraig

#endif
