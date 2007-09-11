// Tester-specific platform interface glue, Windows version.

#define WIN32_LEAN_AND_MEAN // we don't need the GUI interfaces

#include "base.hh"
#include "tester-plaf.hh"
#include "sanity.hh"
#include "platform.hh"

#include <windows.h>

void make_accessible(std::string const & name)
{
  DWORD attrs = GetFileAttributes(name.c_str());
  E(attrs != INVALID_FILE_ATTRIBUTES,
    F("GetFileAttributes(%s) failed: %s") % name % os_strerror(GetLastError()));

  E(SetFileAttributes(name.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY),
    F("SetFileAttributes(%s) failed: %s") % name % os_strerror(GetLastError()));
}

time_t get_last_write_time(char const * name)
{
  HANDLE h = CreateFile(name, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, 0, NULL);
  E(h != INVALID_HANDLE_VALUE,
    F("CreateFile(%s) failed: %s") % name % os_strerror(GetLastError()));

  FILETIME ft;
  E(GetFileTime(h, NULL, NULL, &ft),
    F("GetFileTime(%s) failed: %s") % name % os_strerror(GetLastError()));

  CloseHandle(h);

  // A FILETIME is a 64-bit quantity (represented as a pair of DWORDs)
  // representing the number of 100-nanosecond intervals elapsed since
  // 12:00 AM, January 1, 1601 UTC.  A time_t is the same as it is for
  // Unix: seconds since 12:00 AM, January 1, 1970 UTC.  The offset is
  // taken verbatim from MSDN.
  LONGLONG ft64 = ((LONGLONG)ft.dwHighDateTime) << 32 + ft.dwLowDateTime;
  return (time_t)((ft64/10000000) - 11644473600LL);
}

void do_copy_file(std::string const & from, std::string const & to)
{
  // For once something is easier with Windows.
  E(CopyFile(from.c_str(), to.c_str(), true),
    F("copy %s to %s: %s") % from % to % os_strerror(GetLastError()));
}


void set_env(char const * var, char const * val)
{
  SetEnvironmentVariable(var, val);
}

void unset_env(char const * var)
{
  SetEnvironmentVariable(var, 0);
}

int do_umask(int /* mask */)
{
  return -1; // not a meaningful operation on Windows
}

char * make_temp_dir()
{
  char dir[PATH_MAX];

  // GetTempFileName wants 14 characters at the end of the path.
  {
    DWORD ret = GetTempPath(PATH_MAX - 14, dir);
    E(ret > 0 && ret <= PATH_MAX - 14,
      F("GetTempPath failed: %s") % os_strerror(GetLastError()));
  }

  // If the third argument to GetTempFileName is zero, it will create a
  // file, which is not what we want.
  UINT base = GetTickCount();
  char * name = new char[PATH_MAX];
  for (UINT i = 0; i < 65535; i++)
    {
      if (base + i == 0)
        continue;

      E(GetTempFileName(dir, "MTN", base + i, name) != 0,
        F("GetTempFileName failed: %s") % os_strerror(GetLastError()));

      if (CreateDirectory(name, NULL))
        return name;

      E(GetLastError() == ERROR_ALREADY_EXISTS,
        F("CreateDirectory(%s) failed: %s") % name % GetLastError());
    }
  E(false, F("All temporary directory names are already in use."));
}

bool running_as_root()
{
  // ??? check for privileges (what the test suite cares about is being able
  // to create files it cannot write to - may not be impossible for any
  // privileged account in Windows)
  return false;
}

// FIXME: I don't know any intrinsic reason why parallel test cases and the
// jobserver protocol could not be supported on Windows (see the lengthy
// explanation of the protocol in unix/tester-plaf.cc) but someone with a
// deep understanding of Win32 would have to implement it to ensure its
// race-free-ness.  (Before bothering, confirm that GNU Make supports the
// jobserver protocol on Windows.)
//
// NOTE TO POTENTIAL FIXERS: If you code this with the fake POSIX layer in
// the C runtime instead of with WaitForMultipleObjects and other kernel
// primitives, you will suffer the curse of the vengeful ghost of Dave Cutler.

void prepare_for_parallel_testcases(int jobs, int, int)
{
  if (jobs != 1)
    W(F("parallel execution of test cases is not supported on Windows."));
}

// General note: the magic numbers in this function are meaningful to
// testlib.lua.  They indicate a number of failure scenarios in which
// more detailed diagnostics are not possible.
// The bulk of the work is done in main(), -r case, q.v.

void run_tests_in_children(test_enumerator const & next_test,
                           test_invoker const & /*invoke*/,
                           test_cleaner const & cleanup,
                           std::string const & run_dir,
                           std::string const & runner,
                           std::string const & testfile,
                           std::string const & firstdir)
{
  char const * argv[6];
  argv[0] = runner.c_str();
  argv[1] = "-r";
  argv[2] = testfile.c_str();
  argv[3] = firstdir.c_str();
  argv[4] = 0;
  argv[5] = 0;

  test_to_run t;
  std::string testdir;
  while (next_test(t))
    {
      // This must be done before we try to redirect stdout/err to a
      // file within testdir.
      try
        {
          testdir = run_dir + "/" + t.name;
          do_remove_recursive(testdir);
          do_mkdir(testdir);
        }
      catch (...)
        {
          cleanup(t, 121);
          continue;
        }

      change_current_working_dir(testdir);
      argv[4] = t.name.c_str();
      pid_t child = process_spawn(argv);
      change_current_working_dir(run_dir);

      int status;
      if (child == -1)
        status = 122;
      else
        process_wait(child, &status);

      if (cleanup(t, status))
        do_remove_recursive(testdir);
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
