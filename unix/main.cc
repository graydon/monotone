// Copyright (C) 2006  Zack Weinberg  <zackw@panix.com>
// Based on code by Graydon Hoare and contributors
// Originally derived from execution_monitor.cpp, a part of boost.
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.


// This file provides the outermost main(), but it is probable that you want
// to look at monotone.cc for cpp_main(), where the real program logic
// begins.  The purpose of this file is to hide all the nastiness involved
// in trapping and responding to operating-system-level hard error reports.
//
// On Unix, what we care about is signals.  Signals come in two varieties:
// those that indicate a catastrophic program error (SIGSEGV etc) and those
// that indicate a user-initiated cancellation of processing (SIGINT etc).
// In a perfect universe, we could simply throw an exception from the signal
// handler and leave error reporting up to catch clauses out in main().
// This does work for some platforms and some subset of the signals we care
// about, but not enough of either to be worth doing.  Also, for signals of
// the first variety, enough program state may already have been mangled
// that running destructors is unsafe.
//
// Furthermore, it is not safe to do anything "complicated" in a signal
// handler.  "Complicated" is a hard thing to define, but as a general rule,
// accessing global variables of type 'volatile sig_atomic_t' is safe, and
// so is making some (but not all) system calls, and that's about it.  It is
// known that write, signal, raise, setrlimit, and _exit [ *not* exit ] are
// safe system calls [ even though some of them are actually libc wrappers ].
// Two things that are definitely *not* safe are allocating memory and using
// stdio or iostreams.  strsignal() should be safe, but it is conceivable it
// would allocate memory; should it cause trouble, out it goes.


#include "base.hh"
#include <signal.h>
#include <time.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

static char const * argv0;

// a convenient wrapper
inline void
write_str_to_stderr(const char *s)
{
  write(2, s, strlen(s));
}

// this message should be kept consistent with ui.cc::fatal and
// win32/main.cc::bug_report_message (it is not exactly the same)
static void
bug_report_message()
{
  write_str_to_stderr("\nthis is almost certainly a bug in monotone."
                      "\nplease send this error message, the output of '");
  write_str_to_stderr(argv0);
  write_str_to_stderr(" version --full',"
                      "\nand a description of what you were doing to "
                      PACKAGE_BUGREPORT "\n");
}

// this handler takes signals which would normally trigger a core
// dump, and prints a slightly more helpful error message first.
static void
bug_signal(int signo)
{
  write_str_to_stderr(argv0);
  write_str_to_stderr(": fatal signal: ");
  write_str_to_stderr(strsignal(signo));
  bug_report_message();
  write_str_to_stderr("do not send a core dump, but if you have one, "
                      "\nplease preserve it in case we ask you for "
                      "information from it.\n");

  raise(signo);
  // The signal has been reset to the default handler by SA_RESETHAND
  // specified in the sigaction() call, but it's also blocked; it will be
  // delivered when this function returns.
}

// User interrupts cause abrupt termination of the process as well, but do
// not represent a bug in the program.  We do intercept the signal in order
// to print a pretty message.  Note that this relies on sqlite's auto-
// recovery feature (see <http://sqlite.org/lockingv3.html>, notably section
// 'The Rollback Journal').
static void
interrupt_signal(int signo)
{
  write_str_to_stderr(argv0);
  write_str_to_stderr(": operation canceled: ");
  write_str_to_stderr(strsignal(signo));
  write_str_to_stderr("\n");
  raise(signo);
  // The signal has been reset to the default handler by SA_RESETHAND
  // specified in the sigaction() call, but it's also blocked; it will be
  // delivered when this function returns.
}

// Signals that we handle can indicate either that there is a real bug
// (bug_signal), or that we should cancel processing in response to an
// external event (interrupt_signal).
static const int bug_signals[] = {
  SIGQUIT, SIGILL, SIGABRT, SIGFPE, SIGSEGV, SIGBUS, SIGSYS, SIGTRAP
};
#define bug_signals_len (sizeof bug_signals / sizeof bug_signals[0])
static const int interrupt_signals[] = {
  SIGHUP, SIGINT, SIGPIPE, SIGTERM
};
#define interrupt_signals_len (sizeof interrupt_signals             \
                               / sizeof interrupt_signals[0])


// This file defines the real main().  It just sets up signal
// handlers, and then calls cpp_main(), which is in monotone.cc.

extern int
cpp_main(int argc, char ** argv);

int
main(int argc, char ** argv)
{
  struct sigaction bug_signal_action;
  struct sigaction interrupt_signal_action;
  size_t i;

  argv0 = argv[0];

  bug_signal_action.sa_flags   = SA_RESETHAND;
  bug_signal_action.sa_handler = &bug_signal;
  sigemptyset(&bug_signal_action.sa_mask);
  for (i = 0; i < bug_signals_len; i++)
    sigaddset(&bug_signal_action.sa_mask, bug_signals[i]);
  for (i = 0; i < bug_signals_len; i++)
    sigaction(bug_signals[i], &bug_signal_action, 0);

  interrupt_signal_action.sa_flags   = SA_RESETHAND;
  interrupt_signal_action.sa_handler = &interrupt_signal;
  sigemptyset(&interrupt_signal_action.sa_mask);
  for (i = 0; i < interrupt_signals_len; i++)
    sigaddset(&interrupt_signal_action.sa_mask, interrupt_signals[i]);
  for (i = 0; i < interrupt_signals_len; i++)
    sigaction(interrupt_signals[i], &interrupt_signal_action, 0);

  return cpp_main(argc, argv);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
