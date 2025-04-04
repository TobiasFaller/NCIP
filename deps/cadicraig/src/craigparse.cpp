#include "craigparse.hpp"

#include <cstdint>
#include <climits>
#include <cinttypes>
#include <cstring>

#include "cadical.hpp"
#include "format.hpp"
#include "craigfile.hpp"
#include "terminal.hpp"
#include "util.hpp"

/*------------------------------------------------------------------------*/

using namespace CaDiCaL;

namespace CaDiCraig {

/*------------------------------------------------------------------------*/

// Parse error.

#define PER(...) \
  do { \
    CaDiCaL::Format format; \
    format.init ( \
        "%s:%" PRIu64 ": parse error: ", file->name (), \
        (uint64_t) file->lineno ()); \
    return format.append (__VA_ARGS__); \
  } while (0)

/*------------------------------------------------------------------------*/

// Parsing utilities.

inline int Parser::parse_char () { return file->get (); }

// Return an non zero error string if a parse error occurred.

inline const char *Parser::parse_string (const char *str, char prev) {
  for (const char *p = str; *p; p++)
    if (parse_char () == *p)
      prev = *p;
    else if (*p == ' ')
      PER ("expected space after '%c'", prev);
    else
      PER ("expected '%c' after '%c'", *p, prev);
  return 0;
}

inline const char *Parser::parse_positive_int (int &ch, int &res,
                                               const char *name) {
  assert (isdigit (ch));
  res = ch - '0';
  while (isdigit (ch = parse_char ())) {
    int digit = ch - '0';
    if (INT_MAX / 10 < res || INT_MAX - digit < 10 * res)
      PER ("too large '%s' in header", name);
    res = 10 * res + digit;
  }
  return 0;
}

static const char *cube_token = "unexpected 'a' in CNF";

inline const char *Parser::parse_lit (int &ch, int &lit, int &vars,
                                      int strict) {
  if (ch == 'a')
    return cube_token;
  int sign = 0;
  if (ch == '-') {
    if (!isdigit (ch = parse_char ()))
      PER ("expected digit after '-'");
    sign = -1;
  } else if (!isdigit (ch))
    PER ("expected digit or '-'");
  else
    sign = 1;
  lit = ch - '0';
  while (isdigit (ch = parse_char ())) {
    int digit = ch - '0';
    if (INT_MAX / 10 < lit || INT_MAX - digit < 10 * lit)
      PER ("literal too large");
    lit = 10 * lit + digit;
  }
  if (ch == '\r')
    ch = parse_char ();
  if (ch != 'c' && ch != ' ' && ch != '\t' && ch != '\n' && ch != EOF)
    PER ("expected white space after '%d'", sign * lit);
  if (lit > vars) {
    if (strict != FORCED)
      PER ("literal %d exceeds maximum variable %d", sign * lit, vars);
    else
      vars = lit;
  }
  lit *= sign;
  return 0;
}

/*------------------------------------------------------------------------*/

// Parsing CNF in DIMACS format.

const char *Parser::parse_dimacs (int &vars, int strict) {
  bool found_cnf_header = false;
  bool found_inccnf_header = false;
  bool found_craigcnf_header = false;
  int ch, clauses = 0;
  const char* cnf_type = 0;
  vars = 0;

  // First read comments before header with possibly embedded options.
  //
  for (;;) {
    ch = parse_char ();
    if (strict != STRICT)
      if (ch == ' ' || ch == '\n' || ch == '\t' || ch == '\r')
        continue;
    if (ch != 'c')
      break;
    string buf;
    while ((ch = parse_char ()) != '\n')
      if (ch == EOF)
        PER ("unexpected end-of-file in header comment");
      else if (ch != '\r')
        buf.push_back (ch);
    const char *o = buf.c_str ();
    if (!(*o == ' ' || *o == '\n' || *o == '\t' || *o == '\r'))
      continue;
    o++;
    if (strict != STRICT)
      while (*o && (*o == ' ' || *o == '\n' || *o == '\t' || *o == '\r'))
        o++;
    if (*o != '-')
      continue;
    solver->set_long_option (o);
  }

  if (ch != 'p')
    PER ("expected 'c' or 'p'");

  ch = parse_char ();
  if (strict == STRICT) {
    if (ch != ' ')
      PER ("expected space after 'p'");
    ch = parse_char ();
  } else if (ch != ' ' && ch != '\t')
    PER ("expected white space after 'p'");
  else {
    do
      ch = parse_char ();
    while (ch == ' ' || ch == '\t');
  }

  CraigTracer* craig = 0;
  if (parse_inccnf_too)
    *parse_inccnf_too = false;
  if (parse_craig_too)
    *parse_craig_too = 0;

  // Now read 'p cnf <var> <clauses>' header of DIMACS file
  // or 'p inccnf' of incremental 'INCCNF' file
  // or 'p craigcnf' of Craig 'CRAIGCNF' file.
  if (ch == 'c') {
    ch = parse_char();
    if (ch == 'n') {
      if (parse_char () != 'f')
        PER ("expected 'f' after 'p cn'");

      found_cnf_header = true;
      cnf_type = "cnf";
    } else if (!parse_craig_too) {
      PER ("expected 'n' after 'p c'");
    } else {
      if (ch != 'r')
        PER ("expected 'n' or 'r' after 'p c'");
      const char *err = parse_string ("aigcnf", ch);
      if (err)
        return err;

      craig = new CraigTracer();
      solver->connect_proof_tracer(craig, true);

      found_craigcnf_header = true;
      *parse_craig_too = craig;
      cnf_type = "craigcnf";

      int interp = options->get ("lratcraig");
      if (interp == 0) craig->set_craig_construction (CraigConstruction::NONE);
      if (interp == 1) craig->set_craig_construction (CraigConstruction::SYMMETRIC);
      if (interp == 2) craig->set_craig_construction (CraigConstruction::ASYMMETRIC);
      if (interp == 3) craig->set_craig_construction (CraigConstruction::DUAL_SYMMETRIC);
      if (interp == 4) craig->set_craig_construction (CraigConstruction::DUAL_ASYMMETRIC);
      if (interp == 5) craig->set_craig_construction (CraigConstruction::ALL);
      if (interp == 6) craig->set_craig_construction (CraigConstruction::ALL);
      if (interp == 7) craig->set_craig_construction (CraigConstruction::ALL);
      if (interp == 8) craig->set_craig_construction (CraigConstruction::ALL);
    }
  } else if (!parse_inccnf_too) {
    PER ("expected 'c' after 'p '");
  } else {
    if (ch != 'i')
      PER ("expected 'c' or 'i' after 'p '");
    const char *err = parse_string ("nccnf", ch);
    if (err)
      return err;

    found_inccnf_header = true;
    // Only set *parse_inccnf_too = true if at least one
    // cube has been found in the input file.
    cnf_type = "inccnf";
    strict = FORCED;
  }

  if (found_cnf_header || found_craigcnf_header) {
    ch = parse_char ();
    if (strict == STRICT) {
      if (ch != ' ')
        PER ("expected space after 'p %s'", cnf_type);
      ch = parse_char ();
    } else if (!isspace (ch))
      PER ("expected white space after 'p %s'", cnf_type);
    else {
      do {
        ch = parse_char ();
      } while (isspace (ch));
    }

    if (!isdigit (ch))
      PER ("expected digit after 'p %s '", cnf_type);
    const char* err = parse_positive_int (ch, vars, "<max-var>");
    if (err)
      return err;

    if (strict == STRICT) {
      if (ch != ' ')
        PER ("expected space after 'p %s %d'", cnf_type, vars);
      ch = parse_char ();
    } else if (!isspace (ch))
      PER ("expected white space after 'p %s %d'", cnf_type, vars);
    else {
      do {
        ch = parse_char ();
      } while (isspace (ch));
    }

    if (!isdigit (ch))
      PER ("expected digit after 'p %s %d '", cnf_type, vars);
    err = parse_positive_int (ch, clauses, "<num-clauses>");
    if (err)
      return err;

    if (strict == STRICT) {
      if (ch != '\n')
        PER ("expected new-line after 'p %s %d %d'", cnf_type, vars, clauses);
    } else {
      while (ch != '\n') {
        if (ch != '\r' && !isspace (ch))
          PER ("expected new-line after 'p %s %d %d'", cnf_type, vars, clauses);
        ch = parse_char ();
      }
    }

    message->message ("found %s'p %s %d %d'%s header", tout.green_code (), cnf_type, vars, clauses, tout.normal_code ());
  } else if (found_inccnf_header) {
    ch = parse_char();
    if (strict == STRICT) {
      if (ch != '\n')
        PER ("expected new-line after 'p %s'", cnf_type);
    } else {
      while (ch != '\n') {
        if (ch != '\r' && !isspace (ch))
          PER ("expected new-line after 'p %s'", cnf_type);
        ch = parse_char ();
      }
    }
  
    message->message ("found %s'p %s'%s header", tout.green_code (), cnf_type, tout.normal_code ());
  }

  if (strict != FORCED && !found_inccnf_header)
    solver->reserve (vars);

  // Now read body of DIMACS part.
  //
  int lit = 0, parsed = 0;
  int label = 0;
  for (; ch != EOF; ch = parse_char ()) {
    if (ch == '\r' || ch == '\n')
      continue;

    if (ch == 'c') {
      while ((ch = parse_char ()) != '\n')
        if (ch == EOF)
          PER ("unexpected end-of-file in comment");
      continue;
    }

    if (found_inccnf_header) {
      if (ch == 'a')
        break;
    } else if (found_craigcnf_header) {
      if (ch == 'a' || ch == 'b' || ch == 'g' || ch == 'A' || ch == 'B' || ch == 'C' || ch == 'D' || ch == 'f') {
        if (lit && label != ch && strict == STRICT)
          PER("found label '%c' while previous label was not terminated with 0 literal", ch);
        label = ch;

        ch = parse_char();
        if (strict == STRICT) {
          if (ch != ' ')
            PER ("expected space after '%c'", label);
          ch = parse_char ();
        } else if (!isspace (ch))
          PER ("expected white space after '%c'", label);
        else {
          do {
            ch = parse_char ();
          } while (isspace (ch));
        }
      }
    }

    const char *err = parse_lit (ch, lit, vars, strict);
    if (err)
      return err;

    if (found_cnf_header) {
      solver->add (lit);
      if (!lit && parsed++ >= clauses && strict != FORCED)
        PER ("too many clauses");
    } else if (found_inccnf_header)
      solver->add (lit);
    else if (found_craigcnf_header) {
      if (label == 'a' || label == 'b' || label == 'g') {
        if (lit) {
          if (label == 'a') craig->label_variable (lit, CraigVarType::A_LOCAL);
          if (label == 'b') craig->label_variable (lit, CraigVarType::B_LOCAL);
          if (label == 'g') craig->label_variable (lit, CraigVarType::GLOBAL);

          if (label == 'g')
            solver->freeze(lit);
        }
      } else if (label == 'A' || label == 'B' || label == 'C' || label == 'D') {
        if (!lit) {
          if (label == 'A') craig->label_clause (parsed + 1, CraigClauseType::A_CLAUSE);
          if (label == 'B') craig->label_clause (parsed + 1, CraigClauseType::B_CLAUSE);
          if (label == 'C') craig->label_constraint (CraigClauseType::A_CLAUSE);
          if (label == 'D') craig->label_constraint (CraigClauseType::B_CLAUSE);

          if ((label == 'A' || label == 'B') && parsed++ >= clauses && strict != FORCED)
            PER ("too many clauses");
        }
        if (label == 'A' || label == 'B') solver->add (lit);
        if (label == 'C' || label == 'D') solver->constrain (lit);
      } else if (label == 'f') {
        if (lit)
          solver->assume(lit);
      } else
        PER("literal '%d' without label", lit);
      
      if (!lit && strict == STRICT)
        label = 0;
    }
  }

  if (lit)
    PER ("last clause without terminating '0'");

  if (found_cnf_header || found_craigcnf_header) {
    if (parsed < clauses && strict != FORCED)
      PER ("clause missing");
  }

  if (found_inccnf_header) {
#ifndef QUIET
    size_t num_cubes = 0;
#endif
    while (ch != EOF) {
      if (ch == ' ' || ch == '\n' || ch == '\t' || ch == '\r') {
        ch = parse_char ();
        continue;
      }
      if (ch == 'c') {
        while ((ch = parse_char ()) != '\n' && ch != EOF)
          ;
        if (ch == EOF)
          break;
        ch = parse_char ();
        continue;
      }

      if (!lit) {
        if (ch != 'a')
          PER ("expected 'a' or end-of-file after zero");
        lit = INT_MIN;
        ch = parse_char ();
        *parse_inccnf_too = true;
        continue;
      }

      const char *err = parse_lit (ch, lit, vars, strict);
      if (err == cube_token)
        PER ("two 'a' in a row");
      else if (err)
        return err;
      if (cubes)
        cubes->push_back (lit);
#ifndef QUIET
      if (!lit)
        num_cubes++;
#endif
    }
    if (lit)
      PER ("last cube without terminating '0'");
  }

  return 0;
}

/*------------------------------------------------------------------------*/

// Parsing solution in competition output format.

const char *Parser::parse_solution (signed char **solution) {
  int max_var = solver->vars ();

  *solution = new signed char[max_var + 1u];
  if (!solution)
    PER ("could not allocate memory for solution");

  clear_n (*solution, max_var + 1u);
  int ch;
  for (;;) {
    ch = parse_char ();
    if (ch == EOF)
      PER ("missing 's' line");
    else if (ch == 'c') {
      while ((ch = parse_char ()) != '\n')
        if (ch == EOF)
          PER ("unexpected end-of-file in comment");
    } else if (ch == 's')
      break;
    else
      PER ("expected 'c' or 's'");
  }
  const char *err = parse_string (" SATISFIABLE", 's');
  if (err)
    return err;
  if ((ch = parse_char ()) == '\r')
    ch = parse_char ();
  if (ch != '\n')
    PER ("expected new-line after 's SATISFIABLE'");
#ifndef QUIET
  int count = 0;
#endif
  for (;;) {
    ch = parse_char ();
    if (ch != 'v')
      PER ("expected 'v' at start-of-line");
    if ((ch = parse_char ()) != ' ')
      PER ("expected ' ' after 'v'");
    int lit = 0;
    ch = parse_char ();
    do {
      if (ch == ' ' || ch == '\t') {
        ch = parse_char ();
        continue;
      }
      err = parse_lit (ch, lit, max_var, false);
      if (err)
        return err;
      if (ch == 'c')
        PER ("unexpected comment");
      if (!lit)
        break;
      if (*solution[abs (lit)])
        PER ("variable %d occurs twice", std::abs (lit));
      message->message ("solution %d", lit);
      *solution[std::abs (lit)] = sign (lit);
#ifndef QUIET
      count++;
#endif
      if (ch == '\r')
        ch = parse_char ();
    } while (ch != '\n');
    if (!lit)
      break;
  }
  return 0;
}

} // namespace CaDiCraig
