// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <io.h>
#include <errno.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/convenience.hpp>

#include "vocab.hh"
#include "sanity.hh"
#include "platform.hh"

std::string get_current_working_dir()
{
  char buffer[4096];
  E(getcwd(buffer, 4096),
    F("cannot get working directory: %s") % strerror(errno));
  return std::string(buffer);
}

void change_current_working_dir(any_path const & to)
{
  E(!chdir(to.as_external().c_str()),
    F("cannot change to directory %s: %s") % to % strerror(errno));
}

// FIXME: BUG: this probably mangles character sets
// (as in, we're treating system-provided data as utf8, but it's probably in
// the filesystem charset)
utf8
get_homedir()
{
  // Windows is fun!
  // See thread on monotone-devel:
  //   Message-Id: <20050221.182951.104117563.dalcolmo@vh-s.de>
  //   URL: http://lists.gnu.org/archive/html/monotone-devel/2005-02/msg00241.html
  char * home;
  L(F("Searching for home directory\n"));
  // First try MONOTONE_HOME, to give people a way out in case the cruft below
  // doesn't work for them.
  home = getenv("MONOTONE_HOME");
  if (home != NULL)
    {
      L(F("Home directory from MONOTONE_HOME\n"));
      return std::string(home);
    }
  // Try HOME next:
  home = getenv("HOME");
  if (home != NULL)
    {
      L(F("Home directory from HOME\n"));
      return std::string(home);
    }
  // Otherwise, try APPDATA:
  home = getenv("APPDATA");
  if (home != NULL)
    {
      L(F("Home directory from APPDATA\n"));
      return std::string(home);
    }
  // Try a second method to get APPDATA:
  TCHAR szPath[MAX_PATH];
  if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, 0, szPath)))
    {
      L(F("Home directory from APPDATA (via SHGetFolderPath)\n"));
      return std::string(szPath);
    }
  // Finally, if even that doesn't work (old version of Windows, I think?),
  // try the HOMEDRIVE/HOMEPATH combo:
  char * homedrive = getenv("HOMEDRIVE");
  char * homepath = getenv("HOMEPATH");
  if (homedrive != NULL && homepath != NULL)
    {
      L(F("Home directory from HOMEDRIVE+HOMEPATH\n"));
      return std::string(homedrive) + std::string(homepath);
    }
  // And if things _still_ didn't work, give up.
  N(false, F("could not find home directory (tried MONOTONE_HOME, HOME, "
             "APPDATA, HOMEDRIVE/HOMEPATH"));
}

utf8
tilde_expand(utf8 const & in)
{
  if (in().empty() || in()[0] != '~')
    return in;
  fs::path tmp(in(), fs::native);
  fs::path::iterator i = tmp.begin();
  if (i != tmp.end())
    {
      fs::path res;
      if (*i == "~" || i->size() > 1 && i->at(0) == '~')
        {
          fs::path restmp(get_homedir()(), fs::native);
          res /= restmp;
          ++i;
        }
      while (i != tmp.end())
        res /= *i++;
      return res.string();
    }

  return tmp.string();
}

path::status
get_path_status(any_path const & path)
{
  fs::path p(path.as_external(), fs::native);
  if (!fs::exists(p))
    return path::nonexistent;
  else if (fs::is_directory(p))
    return path::directory;
  else
    return path::file;
}

static bool
rename_clobberingly_impl(const char* from, const char* to)
{
  // MoveFileEx is only available on NT-based systems.  We will revert to a
  // more compatible DeleteFile/MoveFile pair as a compatibility fall-back.
  typedef BOOL (*MoveFileExFun)(LPCTSTR, LPCTSTR, DWORD);
  static MoveFileExFun MoveFileEx = 0;
  if (MoveFileEx == 0) {
    HMODULE hModule = LoadLibrary("kernel32");
    MoveFileEx = reinterpret_cast<MoveFileExFun>
      (GetProcAddress(hModule, "MoveFileExA"));
    if (MoveFileEx)
      L(F("using MoveFileEx for renames"));
  }

  if (MoveFileEx) {
    if (MoveFileEx(from, to, MOVEFILE_REPLACE_EXISTING))
      return true;
  } else {
    // This is not even remotely atomic, but what can you do?
    DeleteFile(to);
    if (MoveFile(from, to))
      return true;
  }
  return false;
}

void
rename_clobberingly(any_path const & from, any_path const & to)
{
  const char* szFrom = from.as_external().c_str();
  const char* szTo = to.as_external().c_str();
  static const int renameAttempts = 16;
  DWORD sleepTime = 1;

  // If a clobbering rename attempt fails, we wait and try again, up to an
  // (arbitrary) maximum of 16 attempts.  This is a gross hack to work
  // around the common problem where another process (e.g. a virus checker)
  // will exclusive open a file you've just touched.
  for (int i = 0; i < renameAttempts; ++i) {
    if (rename_clobberingly_impl(szFrom, szTo))
      return;
    L(F("attempted rename of '%s' to '%s' failed: %d")
      % szFrom % szTo % GetLastError());
    Sleep(sleepTime);
    if (sleepTime < 250)
      sleepTime *= 2;
  }
  E(false, F("renaming '%s' to '%s' failed: %d") % from % to);
}
