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
// On Win32, hard error reports come via SEH ("structured exception
// handling", which is, alas, not the same thing as C++ runtime exception
// handling) and possibly via compiler-specific runtime error report hooks.
// Both of these indicate catastrophic program error.  This logic currently
// does not handle user interrupts at all; FIXME.
//
// There are fewer problems with using stdio in this context than there
// are with Unix, but there is still the possibility of trashed global
// data structures, so we use the lowest-level API that appears to be
// available (GetStdHandle()/WriteFile()).

#include "config.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>

// Microsoft + other compatible compilers such as Intel
#if defined(_MSC_VER) || (defined(__MWERKS__) && __MWERKS__ >= 0x3000)
#define MSC_STYLE_SEH
#include <excpt.h>
#include <eh.h>
#if !defined(__MWERKS__)
#define MS_CRT_DEBUG_HOOK
#include <crtdbg.h>
#endif

#elif (defined(__BORLANDC__) && defined(_Windows))
#define BORLAND_STYLE_SEH

#else
// FIXME: We used to have a Mingw block in here, but if you read through the
// maze of ifdefs you found that it did not actually trap SEH errors.  Only
// put Mingw back if you know how to make it do so.  Mingw uses MSVCRT
// internally, so an MSC-style approach should work?  Or we could do
// something with SetUnhandledExceptionFilter maybe.

#error "unsupported compiler - don't know how to interface to SEH"
#endif

// Actual error printing goes through here always.
static HANDLE hStderr = INVALID_HANDLE_VALUE;
static void
WRITE_STR_TO_STDERR(char const * s)
{
  if (hStderr == INVALID_HANDLE_VALUE)
    hStderr = GetStdHandle(STD_ERROR_HANDLE);

  DWORD n = strlen(s);
  DWORD dummy;
  WriteFile(hStderr, (LPCVOID)s, n, &dummy, 0);
}

static char const * argv0;

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

static void
report_error(char const * msg)
{
  WRITE_STR_TO_STDERR(argv0);
  WRITE_STR_TO_STDERR(": fatal: ");
  WRITE_STR_TO_STDERR(msg);
  bug_report_message();
}

static void
report_ms_se_error(unsigned int id)
{
  switch (id)
    {
    case EXCEPTION_ACCESS_VIOLATION:
      report_error("memory access violation");
      break;

    case EXCEPTION_ILLEGAL_INSTRUCTION:
      report_error("illegal instruction");
      break;

    case EXCEPTION_PRIV_INSTRUCTION:
      report_error("privilaged instruction");
      break;

    case EXCEPTION_IN_PAGE_ERROR:
      report_error("memory page error");
      break;

    case EXCEPTION_STACK_OVERFLOW:
      report_error("stack overflow");
      break;

    case EXCEPTION_DATATYPE_MISALIGNMENT:
      report_error("data misalignment");
      break;

    case EXCEPTION_INT_DIVIDE_BY_ZERO:
      report_error("integer divide by zero");
      break;

    case EXCEPTION_INT_OVERFLOW:
      report_error("integer overflow");
      break;

    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
      report_error("array bounds exceeded");
      break;

    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
      report_error("floating point divide by zero");
      break;

    case EXCEPTION_FLT_STACK_CHECK:
      report_error("floating point stack check");
      break;

    case EXCEPTION_FLT_DENORMAL_OPERAND:
    case EXCEPTION_FLT_INEXACT_RESULT:
    case EXCEPTION_FLT_INVALID_OPERATION:
    case EXCEPTION_FLT_OVERFLOW:
    case EXCEPTION_FLT_UNDERFLOW:
      report_error("floating point error");
      break;

    default:
      report_error("unrecognized exception or signal");
    }
}

#ifdef MS_CRT_DEBUG_HOOK
static int
assert_reporting_function(int reportType, char* userMessage, int* retVal)
{
  switch (reportType)
    {
    case _CRT_ASSERT:
    case _CRT_ERROR:
      report_error(userMessage);
      return 1;

    default:
      return 0;
    }
}
#endif

#ifdef MSC_STYLE_SEH
struct
ms_se_exception
{
  unsigned int exception_id;
  explicit ms_se_exception(unsigned int n)
    : exception_id(n)
  {}
};

static void
ms_se_trans_func(unsigned int id, _EXCEPTION_POINTERS*)
{
  throw ms_se_exception(id);
}
#endif

// FIXME: Implement trapping of ^C etc, and make this actually do something.
void Q()
{
}


extern int
cpp_main(int argc, char ** argv);

int
main(int argc, char ** argv)
{
#ifdef MSC_STYLE_SEH
  _set_se_translator(ms_se_trans_func);
#endif

#ifdef MS_CRT_DEBUG_HOOK
  _CrtSetReportHook(&assert_reporting_function);
#endif

  try
    {
      // this works for Borland but not other Win32 compilers (which trap
      // too many cases)
#ifdef BORLAND_STYLE_SEH
      __try {
#endif
        return cpp_main(argc, argv);
#ifdef BORLAND_STYLE_SEH
      }  __except (1) {
        report_ms_se_error(GetExceptionCode());
      }
#endif
    }
#ifdef MSC_STYLE_SEH
  catch (ms_se_exception const & ex)
    {
      report_ms_se_error(ex.exception_id);
    }
#endif
  catch (...)
    {
      report_error("exception of unknown type");
    }
  // If control reaches this point it indicates a catastrophic failure.
  return 3;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
