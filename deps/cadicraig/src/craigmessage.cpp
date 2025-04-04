#include "craigmessage.hpp"

#include <cassert>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <unistd.h>

#include "terminal.hpp"

using namespace CaDiCaL;

namespace CaDiCraig {

/*------------------------------------------------------------------------*/
#ifndef QUIET
/*------------------------------------------------------------------------*/

void Message::print_prefix () { fputs (prefix.c_str (), stdout); }

void Message::message (const char *fmt, ...) {
#ifdef LOGGING
  if (!log)
#endif
  if (quiet)
    return;
  print_prefix ();
  va_list ap;
  va_start (ap, fmt);
  vprintf (fmt, ap);
  va_end (ap);
  fputc ('\n', stdout);
  fflush (stdout);
}

void Message::message () {
#ifdef LOGGING
  if (!log)
#endif
  if (quiet)
    return;
  print_prefix ();
  fputc ('\n', stdout);
  fflush (stdout);
}

/*------------------------------------------------------------------------*/

void Message::verbose (int level, const char *fmt, ...) {
#ifdef LOGGING
  if (!log)
#endif
  if (quiet || level > this->level)
    return;
  print_prefix ();
  va_list ap;
  va_start (ap, fmt);
  vprintf (fmt, ap);
  va_end (ap);
  fputc ('\n', stdout);
  fflush (stdout);
}

void Message::verbose (int level) {
#ifdef LOGGING
  if (!log)
#endif
  if (quiet || level > this->level)
    return;
  print_prefix ();
  fputc ('\n', stdout);
  fflush (stdout);
}

/*------------------------------------------------------------------------*/

void Message::section (const char *title) {
#ifdef LOGGING
  if (!log)
#endif
  if (quiet)
    return;
  print_prefix ();
  tout.blue ();
  fputs ("--- [ ", stdout);
  tout.blue (true);
  fputs (title, stdout);
  tout.blue ();
  fputs (" ] ", stdout);
  for (int i = strlen (title) + strlen (prefix.c_str ()) + 9; i < 78; i++)
    fputc ('-', stdout);
  tout.normal ();
  fputc ('\n', stdout);
  message ();
}

/*------------------------------------------------------------------------*/
#endif // ifndef QUIET
/*------------------------------------------------------------------------*/

void Message::warning (const char *fmt, ...) {
  fflush (stdout);
  terr.bold ();
  fputs ("cadical: ", stderr);
  terr.red (1);
  fputs ("warning:", stderr);
  terr.normal ();
  fputc (' ', stderr);
  va_list ap;
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
}

/*------------------------------------------------------------------------*/

void Message::error_message_start () {
  fflush (stdout);
  terr.bold ();
  fputs ("cadical: ", stderr);
  terr.red (1);
  fputs ("error:", stderr);
  terr.normal ();
  fputc (' ', stderr);
}

void Message::error_message_end () {
  fputc ('\n', stderr);
  fflush (stderr);
  // TODO add possibility to use call back instead.
  exit (1);
}

void Message::error (const char *fmt, ...) {
  error_message_start ();
  va_list ap;
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  error_message_end ();
}

/*------------------------------------------------------------------------*/

void Message::fatal_message_start () {
  fflush (stdout);
  terr.bold ();
  fputs ("cadical: ", stderr);
  terr.red (1);
  fputs ("fatal error:", stderr);
  terr.normal ();
  fputc (' ', stderr);
}

void Message::fatal_message_end () {
  fputc ('\n', stderr);
  fflush (stderr);
  abort ();
}

void Message::fatal (const char *fmt, ...) {
  fatal_message_start ();
  va_list ap;
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fatal_message_end ();
  abort ();
}

} // namespace CaDiCraig
