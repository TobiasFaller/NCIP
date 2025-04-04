#ifndef _craigmessage_h_INCLUDED
#define _craigmessage_h_INCLUDED

#include <string>

#ifndef QUIET
namespace CaDiCraig {

struct Message {

  void print_prefix ();

  void fatal_message_start ();
  void fatal_message_end ();
  void fatal (const char *fmt, ...);
  void error_message_start ();
  void error_message_end ();
  void error ();
  void error (const char *fmt, ...);
  void warning ();
  void warning (const char *fmt, ...);
  void message ();
  void message (const char *fmt, ...);
  void section (const char *title);
  void verbose (int level);
  void verbose (int level, const char *fmt, ...);

  std::string prefix = "c ";
  bool quiet = false;
  bool log = false;
  int level = 0;

};

};

#define WARNING(...) message->warning (__VA_ARGS__)
#else
#define WARNING(...)
#endif

#endif // ifndef _message_h_INCLUDED
