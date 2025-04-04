#ifndef _craigparse_hpp_INCLUDED
#define _craigparse_hpp_INCLUDED

#include <cassert>
#include <vector>

#include "cadical.hpp"
#include "craigtracer.hpp"
#include "craigmessage.hpp"
#include "craigoptions.hpp"

namespace CaDiCraig {

// Factors out common functions for parsing of DIMACS and solution files.

class File;

class Parser {

  CaDiCaL::Solver *solver;
  CaDiCraig::Options *options;
  Message *message;
  File *file;

  void perr (const char *fmt, ...) CADICAL_ATTRIBUTE_FORMAT (2, 3);
  int parse_char ();

  enum {
    FORCED = 0,  // Force reading even if header is broken.
    RELAXED = 1, // Relaxed white space treatment in header.
    STRICT = 2,  // Strict white space and header compliance.
  };

  const char *parse_string (const char *str, char prev);
  const char *parse_positive_int (int &ch, int &res, const char *name);
  const char *parse_lit (int &ch, int &lit, int &vars, int strict);

  bool *parse_inccnf_too;
  CraigTracer **parse_craig_too;
  vector<int> *cubes;

public:
  // Parse a DIMACS CNF or ICNF file.
  //
  // Return zero if successful. Otherwise parse error.
  Parser (CaDiCaL::Solver *s, CaDiCraig::Options *o, Message *m, File *f, bool *inc, CraigTracer **craig, vector<int> *c)
      : solver (s), options(o), message (m), file (f), parse_inccnf_too (inc), parse_craig_too (craig), cubes (c) {}

  // Parse a DIMACS file.  Return zero if successful. Otherwise a parse
  // error is return. The parsed clauses are added to the solver and the
  // maximum variable index found is returned in the 'vars' argument. The
  // 'strict' argument can be '0' in which case the numbers in the header
  // can be arbitrary, e.g., 'p cnf 0 0' all the time, without producing a
  // parse error.  Only for this setting the parsed literals are not checked
  // to overflow the maximum variable index of the header.  The strictest
  // form of parsing is enforced  for the value '2' of 'strict', in which
  // case the header can not have additional white space, while a value of
  // '1' exactly relaxes this, e.g., 'p cnf \t  1   3  \r\n' becomes legal.
  //
  const char *parse_dimacs (int &vars, int strict);

  // Parse a solution file as used in the SAT competition, e.g., with
  // comment lines 'c ...', a status line 's ...' and value lines 'v ...'.
  // Returns zero if successful. Otherwise a string is returned describing
  // the parse error.  The parsed solution is saved in 'solution' and can be
  // accessed with 'sol (int lit)'.  We use it for checking learned clauses.
  //
  const char *parse_solution (signed char **solution);
};

} // namespace CaDiCraig

#endif
