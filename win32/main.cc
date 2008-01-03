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


#define WIN32_LEAN_AND_MEAN
#include "base.hh"
#include <windows.h>
#include <string.h>

// Disable the C runtime's built-in filename globbing.
int _CRT_glob = 0;

// Microsoft + other compatible compilers such as Intel
#if defined(_MSC_VER)
#define MS_CRT_DEBUG_HOOK
#include <crtdbg.h>
#endif

// Actual error printing goes through here always.
static HANDLE hStderr = INVALID_HANDLE_VALUE;
static void
write_str_to_stderr(char const * s)
{
  if (hStderr == INVALID_HANDLE_VALUE)
    hStderr = GetStdHandle(STD_ERROR_HANDLE);

  DWORD n = strlen(s);
  DWORD dummy;
  WriteFile(hStderr, (LPCVOID)s, n, &dummy, 0);
}

static char const * argv0;

// this message should be kept consistent with ui.cc::fatal and
// unix/main.cc::bug_report_message (it is not exactly the same)
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

static void
report_error(char const * msg)
{
  write_str_to_stderr(argv0);
  write_str_to_stderr(": fatal: ");
  write_str_to_stderr(msg);
  bug_report_message();
}

static LONG WINAPI
seh_reporting_function(LPEXCEPTION_POINTERS ep)
{
  // These are all the exception codes documented at
  // http://msdn.microsoft.com/library/en-us/debug/base/exception_record_str.asp
  // Some of them should never happen, but let's be thorough.
  switch (ep->ExceptionRecord->ExceptionCode)
    {
    case EXCEPTION_ACCESS_VIOLATION:
      report_error("memory access violation");
      break;
      
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
      report_error("array bounds exceeded");
      break;

    case EXCEPTION_BREAKPOINT:
      report_error("breakpoint trap");
      break;

    case EXCEPTION_DATATYPE_MISALIGNMENT:
      report_error("attempt to access misaligned data");
      break;

    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
      report_error("floating point divide by zero");
      break;

    case EXCEPTION_FLT_STACK_CHECK:
      report_error("floating point stack over- or underflow");
      break;

    case EXCEPTION_FLT_DENORMAL_OPERAND:
    case EXCEPTION_FLT_INEXACT_RESULT:
    case EXCEPTION_FLT_INVALID_OPERATION:
    case EXCEPTION_FLT_OVERFLOW:
    case EXCEPTION_FLT_UNDERFLOW:
      report_error("floating point exception");
      break;

    case EXCEPTION_ILLEGAL_INSTRUCTION:
      report_error("attempt to execute invalid instruction");
      break;

    case EXCEPTION_IN_PAGE_ERROR:
      report_error("system unable to load memory page");
      break;

    case EXCEPTION_INT_DIVIDE_BY_ZERO:
      report_error("integer divide by zero");
      break;

    case EXCEPTION_INT_OVERFLOW:
      report_error("integer overflow");
      break;

    case EXCEPTION_INVALID_DISPOSITION:
    case EXCEPTION_NONCONTINUABLE_EXCEPTION:
      report_error("SEH internal error");
      break;

    case EXCEPTION_PRIV_INSTRUCTION:
      report_error("attempt to execute privileged instruction");
      break;

    case EXCEPTION_SINGLE_STEP:
      report_error("single step trap");
      break;

    case EXCEPTION_STACK_OVERFLOW:
      report_error("stack overflow");
      break;

    default:
      report_error("undocumented exception");
    }

  return EXCEPTION_EXECUTE_HANDLER;  // causes process termination
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

extern int
cpp_main(int argc, char ** argv);

int
main(int argc, char ** argv)
{
  // Have to get fully qualified path to mtn.exe into argv0 before anything
  // might try to report an error.
  char name[MAX_PATH];
  int len = 0;
  len = (int)GetModuleFileName(0, name, MAX_PATH);
  if(len != 0) {
    argv0 = strdup(name);
  } else {
    argv0 = argv[0];
  }
  
  SetUnhandledExceptionFilter(&seh_reporting_function);

#ifdef MS_CRT_DEBUG_HOOK
  _CrtSetReportHook(&assert_reporting_function);
#endif

  return cpp_main(argc, argv);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
