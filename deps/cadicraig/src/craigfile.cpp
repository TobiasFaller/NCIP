#include "craigfile.hpp"
#include "craigmessage.hpp"
#include "util.hpp"

/*------------------------------------------------------------------------*/

// Some more low-level 'C' headers.

extern "C" {
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
}

#include <cinttypes>

using namespace CaDiCaL;

/*------------------------------------------------------------------------*/

namespace CaDiCraig {

/*------------------------------------------------------------------------*/

// Private constructor.

File::File (Message *m, bool w, int c, int p, FILE *f, const char *n)
    :
#ifndef QUIET
      message (m),
#endif
#if !defined(QUIET) || !defined(NDEBUG)
      writing (w),
#endif
      close_file (c), child_pid (p), file (f), _name (n), _lineno (1),
      _bytes (0) {
  (void) m, (void) w;
  assert (f), assert (n);
}

/*------------------------------------------------------------------------*/

bool File::exists (const char *path) {
  struct stat buf;
  if (stat (path, &buf))
    return false;
  if (access (path, R_OK))
    return false;
  return true;
}

bool File::writable (const char *path) {
  int res;
  if (!path)
    res = 1;
  else if (!strcmp (path, "/dev/null"))
    res = 0;
  else {
    if (!*path)
      res = 2;
    else {
      struct stat buf;
      const char *p = strrchr (path, '/');
      if (!p) {
        if (stat (path, &buf))
          res = ((errno == ENOENT) ? 0 : -2);
        else if (S_ISDIR (buf.st_mode))
          res = 3;
        else
          res = (access (path, W_OK) ? 4 : 0);
      } else if (!p[1])
        res = 5;
      else {
        size_t len = p - path;
        char *dirname = new char[len + 1];
        strncpy (dirname, path, len);
        dirname[len] = 0;
        if (stat (dirname, &buf))
          res = 6;
        else if (!S_ISDIR (buf.st_mode))
          res = 7;
        else if (access (dirname, W_OK))
          res = 8;
        else if (stat (path, &buf))
          res = (errno == ENOENT) ? 0 : -3;
        else
          res = access (path, W_OK) ? 9 : 0;
        delete[] dirname;
      }
    }
  }
  return !res;
}

// These are signatures for supported compressed file types.  In 2018 the
// SAT Competition was running on StarExec and used messagely 'bzip2'
// compressed files, but gave them uncompressed to the solver using exactly
// the same path (with '.bz2' suffix).  Then 'CaDiCraig' tried to read that
// actually uncompressed file through 'bzip2', which of course failed.  Now
// we double check and fall back to reading the file as is, if the signature
// does not match after issuing a warning.

static int xzsig[] = {0xFD, 0x37, 0x7A, 0x58, 0x5A, 0x00, 0x00, EOF};
static int bz2sig[] = {0x42, 0x5A, 0x68, EOF};
static int gzsig[] = {0x1F, 0x8B, EOF};
static int sig7z[] = {0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C, EOF};
static int lzmasig[] = {0x5D, EOF};

bool File::match (Message *message, const char *path, const int *sig) {
  assert (path);
  FILE *tmp = fopen (path, "r");
  if (!tmp) {
    WARNING ("failed to open '%s' to check signature", path);
    return false;
  }
  bool res = true;
  for (const int *p = sig; res && (*p != EOF); p++)
    res = (cadical_getc_unlocked (tmp) == *p);
  fclose (tmp);
  if (!res)
    WARNING ("file type signature check for '%s' failed", path);
  return res;
}

size_t File::size (const char *path) {
  struct stat buf;
  if (stat (path, &buf))
    return 0;
  return (size_t) buf.st_size;
}

// Check that 'prg' is in the 'PATH' and thus can be found if executed
// through 'popen' or 'exec'.

char *File::find_program (const char *prg) {
  size_t prglen = strlen (prg);
  const char *c = getenv ("PATH");
  if (!c)
    return 0;
  size_t len = strlen (c);
  char *e = new char[len + 1];
  strcpy (e, c);
  char *res = 0;
  for (char *p = e, *q; !res && p < e + len; p = q) {
    for (q = p; *q && *q != ':'; q++)
      ;
    *q++ = 0;
    size_t pathlen = (q - p) + prglen;
    char *path = new char[pathlen + 1];
    snprintf (path, pathlen + 1, "%s/%s", p, prg);
    assert (strlen (path) == pathlen);
    if (exists (path))
      res = path;
    else
      delete[] path;
  }
  delete[] e;
  return res;
}

/*------------------------------------------------------------------------*/

FILE *File::open_file (Message *message, const char *path,
                       const char *mode) {
  (void) message;
  return fopen (path, mode);
}

FILE *File::read_file (Message *message, const char *path) {
  message->message ("opening file to read '%s'", path);
  return open_file (message, path, "r");
}

FILE *File::write_file (Message *message, const char *path) {
  message->message ("opening file to write '%s'", path);
  return open_file (message, path, "w");
}

/*------------------------------------------------------------------------*/

void File::split_str (const char *command, std::vector<char *> &argv) {
  const char *c = command;
  while (*c && *c == ' ')
    c++;
  while (*c) {
    const char *p = c;
    while (*p && *p != ' ')
      p++;
    const size_t bytes = p - c;
    char *arg = new char[bytes + 1];
    (void) strncpy (arg, c, bytes);
    arg[bytes] = 0;
    argv.push_back (arg);
    while (*p && *p == ' ')
      p++;
    c = p;
  }
}

void File::delete_str_vector (std::vector<char *> &argv) {
  for (auto str : argv)
    delete[] str;
}

FILE *File::open_pipe (Message *message, const char *fmt,
                       const char *path, const char *mode) {
#ifdef QUIET
  (void) message;
#endif
  size_t prglen = 0;
  while (fmt[prglen] && fmt[prglen] != ' ')
    prglen++;
  char *prg = new char[prglen + 1];
  strncpy (prg, fmt, prglen);
  prg[prglen] = 0;
  char *found = find_program (prg);
  if (found)
    message->message ("found '%s' in path for '%s'", found, prg);
  if (!found)
    message->message ("did not find '%s' in path", prg);
  delete[] prg;
  if (!found)
    return 0;
  delete[] found;
  size_t cmd_size = strlen (fmt) + strlen (path);
  char *cmd = new char[cmd_size];
  snprintf (cmd, cmd_size, fmt, path);
  FILE *res = popen (cmd, mode);
  delete[] cmd;
  return res;
}

FILE *File::read_pipe (Message *message, const char *fmt, const int *sig,
                       const char *path) {
  if (!File::exists (path)) {
    message->message ("file '%s' does not exist", path);
    return 0;
  }
  message->message ("file '%s' exists", path);
  if (sig && !File::match (message, path, sig))
    return 0;
  message->message ("file '%s' matches signature for '%s'", path, fmt);
  message->message ("opening pipe to read '%s'", path);
  return open_pipe (message, fmt, path, "r");
}

FILE *File::write_pipe (Message *message, const char *command,
                        const char *path, int &child_pid) {
  assert (command[0] && command[0] != ' ');
  message->message ("writing through command '%s' to '%s'", command, path);
  std::vector<char *> args;
  split_str (command, args);
  assert (!args.empty ());
  args.push_back (0);
  char **argv = args.data ();
  char *absolute_command_path = find_program (argv[0]);
  int pipe_fds[2], out;
  FILE *res = 0;
  if (!absolute_command_path)
    message->message ("could not find '%s' in 'PATH' environment variable", argv[0]);
  else if (pipe (pipe_fds) < 0)
    message->message ("could not generate pipe to '%s' command", command);
  else if ((out = ::open (path, O_CREAT | O_TRUNC | O_WRONLY, 0644)) < 0)
    message->message ("could not open '%s' for writing", path);
  else if ((child_pid = fork ()) < 0) {
    message->message ("could not fork process to execute '%s' command", command);
    ::close (out);
  } else if (child_pid) {
    ::close (pipe_fds[0]);
    res = ::fdopen (pipe_fds[1], "w");
  } else {
    ::close (pipe_fds[1]);
    ::close (0);
    ::close (1);
    if (command[0] == '7') // Surpress '7z' verbose output on 'stderr'.
      ::close (2);
    int in = dup (pipe_fds[0]);
    assert (in == 0), (void) in;
    int tmp = dup2 (out, 1);
    assert (tmp == 1), (void) tmp;
    execv (absolute_command_path, argv);
    _exit (1);
  }
  if (absolute_command_path)
    delete[] absolute_command_path;
  delete_str_vector (args);
  return res;
}

/*------------------------------------------------------------------------*/

File *File::read (Message *message, FILE *f, const char *n) {
  return new File (message, false, 0, 0, f, n);
}

File *File::write (Message *message, FILE *f, const char *n) {
  return new File (message, true, 0, 0, f, n);
}

File *File::read (Message *message, const char *path) {
  FILE *file;
  int close_input = 2;
  if (has_suffix (path, ".xz")) {
    file = read_pipe (message, "xz -c -d %s", xzsig, path);
    if (!file)
      goto READ_FILE;
  } else if (has_suffix (path, ".lzma")) {
    file = read_pipe (message, "lzma -c -d %s", lzmasig, path);
    if (!file)
      goto READ_FILE;
  } else if (has_suffix (path, ".bz2")) {
    file = read_pipe (message, "bzip2 -c -d %s", bz2sig, path);
    if (!file)
      goto READ_FILE;
  } else if (has_suffix (path, ".gz")) {
    file = read_pipe (message, "gzip -c -d %s", gzsig, path);
    if (!file)
      goto READ_FILE;
  } else if (has_suffix (path, ".7z")) {
    file = read_pipe (message, "7z x -so %s 2>/dev/null", sig7z, path);
    if (!file)
      goto READ_FILE;
  } else {
  READ_FILE:
    file = read_file (message, path);
    close_input = 1;
  }

  if (!file)
    return 0;

  return new File (message, false, close_input, 0, file, path);
}

File *File::write (Message *message, const char *path) {
  FILE *file;
  int close_output = 3, child_pid = 0;
  if (has_suffix (path, ".xz"))
    file = write_pipe (message, "xz -c", path, child_pid);
  else if (has_suffix (path, ".bz2"))
    file = write_pipe (message, "bzip2 -c", path, child_pid);
  else if (has_suffix (path, ".gz"))
    file = write_pipe (message, "gzip -c", path, child_pid);
  else if (has_suffix (path, ".7z"))
    file = write_pipe (message, "7z a -an -txz -si -so", path, child_pid);
  else
    file = write_file (message, path), close_output = 1;

  if (!file)
    return 0;

  return new File (message, true, close_output, child_pid, file, path);
}

void File::close () {
  assert (file);
  if (close_file == 0) {
    message->message ("disconnecting from '%s'", name ());
  }
  if (close_file == 1) {
    message->message ("closing file '%s'", name ());
    fclose (file);
  }
  if (close_file == 2) {
    message->message ("closing input pipe to read '%s'", name ());
    pclose (file);
  }
  if (close_file == 3) {
    message->message ("closing output pipe to write '%s'", name ());
    fclose (file);
    waitpid (child_pid, 0, 0);
  }
  file = 0; // mark as closed

  // TODO what about error checking for 'fclose', 'pclose' or 'waitpid'?

#ifndef QUIET
  //if (message->opts.verbose > 1) {
    if (writing) {
      uint64_t written_bytes = bytes ();
      double written_mb = written_bytes / (double) (1 << 20);
      message->message ("after writing %" PRIu64 " bytes %.1f MB", written_bytes,
           written_mb);
      if (close_file == 3) {
        size_t actual_bytes = size (name ());
        if (actual_bytes) {
          double actual_mb = actual_bytes / (double) (1 << 20);
          message->message ("deflated to %zd bytes %.1f MB", actual_bytes, actual_mb);
          message->message ("factor %.2f (%.2f%% compression)",
               relative (written_bytes, actual_bytes),
               percent (actual_bytes, written_bytes));
        } else
          message->message ("but could not determine actual size of written file");
      }
    } else {
      uint64_t read_bytes = bytes ();
      double read_mb = read_bytes / (double) (1 << 20);
      message->message ("after reading %" PRIu64 " bytes %.1f MB", read_bytes, read_mb);
      if (close_file == 2) {
        size_t actual_bytes = size (name ());
        double actual_mb = actual_bytes / (double) (1 << 20);
        message->message ("inflated from %zd bytes %.1f MB", actual_bytes, actual_mb);
        message->message ("factor %.2f (%.2f%% compression)",
             relative (read_bytes, actual_bytes),
             percent (actual_bytes, read_bytes));
      }
    }
  //}
#endif
}

void File::flush () {
  assert (file);
  fflush (file);
}

File::~File () {
  if (file)
    close ();
}

} // namespace CaDiCraig
