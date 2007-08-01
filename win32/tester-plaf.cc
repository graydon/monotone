// Tester-specific platform interface glue, Windows version.

#define WIN32_LEAN_AND_MEAN // we don't need the GUI interfaces

#include "base.hh"
#include "tester-plaf.hh"
#include "sanity.hh"

#include <windows.h>

void make_accessible(string const & name)
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

void do_copy_file(string const & from, string const & to)
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


pid_t run_one_test_in_child(string const & testname,
                            string const & testdir,
                            lua_State * /* st */,
                            string const & argv0,
                            string const & testfile,
                            string const & firstdir)
{
  // The bulk of the work is done in main(), -r case, q.v.
  char const * argv[6];
  argv[0] = argv0.c_str();
  argv[1] = "-r";
  argv[2] = testfile.c_str();
  argv[3] = firstdir.c_str();
  argv[4] = testname.c_str();
  argv[5] = 0;

  change_current_working_dir(testdir);
  pid_t child = process_spawn_redirected("NUL:",
                                         "tester.log",
                                         "tester.log",
                                         argv);
  change_current_working_dir(run_dir);
  return child;
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
