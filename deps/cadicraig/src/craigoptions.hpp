#include "craigconfig.hpp"

#include <string>
#include <cstddef>
#include <cassert>

using namespace std;

#ifndef _options_hpp_INCLUDED
#define _options_hpp_INCLUDED

/*------------------------------------------------------------------------*/

// In order to add a new option, simply add a new line below. Make sure that
// options are sorted correctly (with '!}sort -k 2' in 'vi').  Otherwise
// initializing the options will trigger an internal error.  For the model
// based tester 'mobical' the policy is that options which become redundant
// because another one is disabled (set to zero) should have the name of the
// latter as prefix.  The 'O' column determines the options which are
// target to 'optimize' them ('-O[1-3]').  A zero value in the 'O' column
// means that this option is not optimized.  A value of '1' results in
// optimizing its value exponentially with exponent base '2', and a value
// of '2' uses base '10'.  The 'P' column determines simplification
// options (disabled with '--plain') and 'R' which values can be reset.

// clang-format off

#define OPTIONS \
\
/*      NAME         DEFAULT, LO, HI,O,P,R, USAGE */ \
\
OPTION( lratcraig,         1,  0,  8,0,0,1, "Craig interpolant (0=none, 1=sym, 2=asym, 3='sym, 4='asym, 5=inter, 6=union, 7=smollest, 8=largest)") \

// Note, keep an empty line right before this line because of the last '\'!
// Also keep those single spaces after 'OPTION(' for proper sorting.

// clang-format on

/*------------------------------------------------------------------------*/

// Some of the 'OPTION' macros above should only be included if certain
// compile time options are enabled.  This has the effect, that for instance
// if 'LOGGING' is defined, and thus logging code is included, then also the
// 'log' option is defined.  Otherwise the 'log' option is not included.

#ifdef LOGGING
#define LOGOPT OPTION
#else
#define LOGOPT(...) /**/
#endif

#ifdef QUIET
#define QUTOPT(...) /**/
#else
#define QUTOPT OPTION
#endif

/*------------------------------------------------------------------------*/

namespace CaDiCraig {

struct Message;

/*------------------------------------------------------------------------*/

class Options;

struct Option {
  const char *name;
  int def, lo, hi;
  int optimizable;
  bool preprocessing;
  const char *description;
  int &val (Options *);
};

/*------------------------------------------------------------------------*/

// Produce a compile time constant for the number of options.

static const size_t number_of_options =
#define OPTION(N, V, L, H, O, P, R, D) 1 +
    OPTIONS
#undef OPTION
    + 0;

/*------------------------------------------------------------------------*/

class Options {

  Message *message;

  void set (Option *, int val); // Force to [lo,hi] interval.

  friend struct Option;
  static Option table[];

  static void initialize_from_environment (int &val, const char *name,
                                           const int L, const int H);

  friend Config;

  void reset_default_values ();
  void disable_preprocessing ();

public:
  // For library usage we disable reporting by default while for the stand
  // alone SAT solver we enable it by default.  This default value has to
  // be set before the constructor of 'Options' is called (which in turn is
  // called from the constructor of 'Solver').  If we would simply overwrite
  // its initial value while initializing the stand alone solver,  we will
  // get that change of the default value (from 'false' to 'true') shown
  // during calls to 'print ()', which is confusing to the user.
  //
  static int reportdefault;

  Options (Message *);

  // Makes options directly accessible, e.g., for instance declares the
  // member 'int restart' here.  This will give fast access to option values
  // internally in the solver and thus can also be used in tight loops.
  //
private:
  int __start_of_options__; // Used by 'val' below.
public:
#define OPTION(N, V, L, H, O, P, R, D) \
  int N; // Access option values by name.
  OPTIONS
#undef OPTION

  // It would be more elegant to use an anonymous 'struct' of the actual
  // option values overlayed with an 'int values[number_of_options]' array
  // but that is not proper ISO C++ and produces a warning.  Instead we use
  // the following construction which relies on '__start_of_options__' and
  // that the following options are really allocated directly after it.
  //
  inline int &val (size_t idx) {
    assert (idx < number_of_options);
    return (&__start_of_options__ + 1)[idx];
  }

  // With the following function we can get rather fast access to the option
  // limits, the default value and the description.  The code uses binary
  // search over the sorted option 'table'.  This static data is shared
  // among different instances of the solver.  The actual current option
  // values are here in the 'Options' class.  They can be accessed by the
  // offset of the static options using 'Option::val' if you have an
  // 'Option' or to have even faster access directly by the member function
  // (the 'N' above, e.g., 'restart').
  //
  static Option *has (const char *name);

  bool set (const char *name, int); // Explicit version.
  int get (const char *name);       // Get current value.

  void print (bool verbose=true);        // Print current values in command line form
  static void usage (); // Print usage message for all options.

  void optimize (int val); // increase some limits (val=0..31)

  static bool is_preprocessing_option (const char *name);

  // Parse long option argument
  //
  //   --<name>
  //   --<name>=<val>
  //   --no-<name>
  //
  // where '<val>' is as in 'parse_option_value'.  If parsing succeeds,
  // 'true' is returned and the string will be set to the name of the
  // option.  Additionally the parsed value is set (last argument).
  //
  static bool parse_long_option (const char *, string &, int &);

  // Iterating options.

  typedef Option *iterator;
  typedef const Option *const_iterator;

  static iterator begin () { return table; }
  static iterator end () { return table + number_of_options; }

  void copy (Options &other) const; // Copy 'this' into 'other'.
};

inline int &Option::val (Options *opts) {
  assert (Options::table <= this &&
          this < Options::table + number_of_options);
  return opts->val (this - Options::table);
}

} // namespace CaDiCraig

#endif
