// Copyright (C) 2006  Zack Weinberg  <zackw@panix.com>
// Based on code by Graydon Saunders and contributors
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
// It is also responsible for a last-ditch catch(...) clause (which is not
// _that_ different from what std::terminate() would do, but does get our
// bug-report message printed.)
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
//
// note that down in the catch handlers in main(), we could use stdio or
// iostreams; that we don't is mainly for consistency.

#include "config.h"

#include <signal.h>
#include <time.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

#if defined HAVE_CXXABI_H \
  && defined HAVE___CXA_CURRENT_EXCEPTION_TYPE \
  && defined HAVE___CXA_DEMANGLE

#define HAVE_CXXABI_EH
#include <cxxabi.h>
using abi::__cxa_current_exception_type;
using abi::__cxa_demangle;
using std::type_info;

#endif

static char const * argv0;

// a convenient wrapper
#define WRITE_STR_TO_STDERR(s) write(2, s, strlen(s))

// this message should be kept consistent with ui.cc::fatal (it is not
// exactly the same)
static void
bug_report_message()
{
  WRITE_STR_TO_STDERR("\nthis is almost certainly a bug in monotone."
                      "\nplease send this error message, the output of '");
  WRITE_STR_TO_STDERR(argv0);
  WRITE_STR_TO_STDERR(" --full-version',"
                      "\nand a description of what you were doing to "
                      PACKAGE_BUGREPORT "\n");
}

// this handler takes signals which would normally trigger a core
// dump, and prints a slightly more helpful error message first.
static void
bug_signal(int signo)
{
  WRITE_STR_TO_STDERR(argv0);
  WRITE_STR_TO_STDERR(": fatal signal: ");
  WRITE_STR_TO_STDERR(strsignal(signo));
  bug_report_message();
  WRITE_STR_TO_STDERR("do not send a core dump, but if you have one, "
                      "\nplease preserve it in case we ask you for "
                      "information from it.\n");

  raise(signo);
  // The signal has been reset to the default handler by SA_RESETHAND
  // specified in the sigaction() call, but it's also blocked; it will be
  // delivered when this function returns.
}

// this exception is thrown for user interrupts (control-C, etc).
// it is not descended from std::exception because we don't want
// the catch-all handlers in monotone.cc::main to get it.
struct user_interrupt {};
static const struct user_interrupt user_interrupted = {};

// the handler for asynchronous user interrupts just sets these flags; we
// rely on the main code to call Q() at appropriate points.
static volatile sig_atomic_t user_interrupt_time;
static volatile sig_atomic_t user_interrupt_signo;

void
Q(void)
{
  if (user_interrupt_signo)
    throw user_interrupted;
}

static void
interrupt_signal(int signo)
{
  sig_atomic_t prev_time = user_interrupt_time;
  time_t now = time(0);
  
  // It is possible that a bug causes us to get stuck in a loop which
  // does not call Q(), or after the exception is thrown but before
  // the process exits.  Thus, detect having been called twice with at
  // least a 1-second interval (i.e. the user got fed up and hit ^C
  // again; not they held down the key too long and the kernel
  // generated two signals).
  if (prev_time == 0 || prev_time == (sig_atomic_t)now)
    {
      user_interrupt_time = now;
      user_interrupt_signo = signo;
      return;
    }

  // Treat this as SIGQUIT, and furthermore attempt to ensure that a
  // core dump is generated, so we have a chance of finding out where
  // the bad loop is.
  struct rlimit unlimit = { RLIM_INFINITY, RLIM_INFINITY };
  setrlimit(RLIMIT_CORE, &unlimit);
  raise(SIGQUIT);
}

// Signals that we handle can indicate either that there is a real bug
// (bug_signal), or that we should cancel processing in response to an
// external event (interrupt_signal).  NOTE: interrupt_signal returns,
// and therefore it must not be used for any signal that means
// processing cannot continue.
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
  int i;

  argv0 = argv[0];

  bug_signal_action.sa_flags   = SA_RESETHAND;
  bug_signal_action.sa_handler = &bug_signal;
  sigemptyset(&bug_signal_action.sa_mask);
  for (i = 0; i < bug_signals_len; i++)
    sigaddset(&bug_signal_action.sa_mask, bug_signals[i]);
  for (i = 0; i < bug_signals_len; i++)
    sigaction(bug_signals[i], &bug_signal_action, 0);

  // we do not use SA_RESTART here because these signals should 
  // interrupt a process blocked on I/O.
  interrupt_signal_action.sa_flags   = 0;
  interrupt_signal_action.sa_handler = &interrupt_signal;
  sigemptyset(&interrupt_signal_action.sa_mask);
  for (i = 0; i < interrupt_signals_len; i++)
    sigaddset(&interrupt_signal_action.sa_mask, interrupt_signals[i]);
  for (i = 0; i < interrupt_signals_len; i++)
    sigaction(interrupt_signals[i], &interrupt_signal_action, 0);

  try
    {
      return cpp_main(argc, argv);
    }
  catch (user_interrupt)
    {
      int signo = user_interrupt_signo;
      WRITE_STR_TO_STDERR(argv0);
      WRITE_STR_TO_STDERR(": ");
      WRITE_STR_TO_STDERR(strsignal(signo));

      // Produce the proper exit code.
      signal(signo, SIG_DFL);
      raise(signo);
      // should never get here, but just in case...
      return 3;  // this exit code is not used anywhere else.
    }
  catch (...)
    {
      WRITE_STR_TO_STDERR(argv0);
#ifdef HAVE_CXXABI_EH
      // logic borrowed from gnu libstdc++'s __verbose_terminate_handler
      type_info *t = __cxa_current_exception_type();
      if (t)
        {
          // t->name() is the _mangled_ name of the type.
          int status = -1;
          char const * name = t->name();
          char * dem = __cxa_demangle(name, 0, 0, &status);

          WRITE_STR_TO_STDERR(": unexpected exception of type ");
          if (status == 0)
            WRITE_STR_TO_STDERR(dem);
          else
            WRITE_STR_TO_STDERR(name);
        }
      else
#endif
        WRITE_STR_TO_STDERR(": exception of unknown type");

      bug_report_message();

      // map this to abort(), but don't run our SIGABRT handler, 'cos
      // we've already printed the bug-report message once.
      signal(SIGABRT, SIG_DFL);
      raise(SIGABRT);
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
