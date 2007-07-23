// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details


#define WIN32_LEAN_AND_MEAN
#include "base.hh"
#include <io.h>
#include <errno.h>
#include <windows.h>
#include <shlobj.h>
#include <direct.h>

#include "sanity.hh"
#include "platform.hh"

std::string
get_current_working_dir()
{
  char buffer[4096];
  E(getcwd(buffer, 4096),
    F("cannot get working directory: %s") % strerror(errno));
  std::string str(buffer);
  if (str[str.size() - 1] == '\\')
    str = str.substr(0, str.size() - 1);
  return str;
}

void
change_current_working_dir(std::string const & to)
{
  E(!chdir(to.c_str()),
    F("cannot change to directory %s: %s") % to % strerror(errno));
}

std::string
get_default_confdir()
{
  std::string base;
  char * appdata;
  appdata = getenv("APPDATA");
  if (appdata != NULL)
    base = appdata;
  else
    {
      TCHAR szPath[MAX_PATH];
      if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, 0, szPath)))
        base = szPath;
    }
  N(!base.empty(), F("could not determine configuration path"));
  return base + "\\monotone";
}

// FIXME: BUG: this probably mangles character sets
// (as in, we're treating system-provided data as utf8, but it's probably in
// the filesystem charset)
std::string
get_homedir()
{
  // Windows is fun!
  // There has been much discussion about the correct way to do this, and a
  // couple of methods have been tried (look at previous versions of this
  // file for the discussion).  For consistency, we now calculate the user's
  // home path using the same technique that Qt's QDir::homePath() uses on
  // Windows.
  char * home;
  home = getenv("HOME");
  if (home != NULL)
    {
      L(FL("Home directory from HOME\n"));
      return std::string(home);
    }
  // Otherwise, try USERPROFILE.  We could also use SHGetFolderPath() to get
  // at USERPROFILE without requiring it to be set as an environment
  // variable, but Qt doesn't, so we won't either.
  char * userprofile = getenv("USERPROFILE");
  if (userprofile != NULL)
    {
      L(FL("Home directory from USERPROFILE\n"));
      return std::string(userprofile);
    }
  // Try concatenating HOMEDRIVE and HOMEPATH
  char * homedrive = getenv("HOMEDRIVE");
  char * homepath = getenv("HOMEPATH");
  if (homedrive != NULL && homepath != NULL)
    {
      L(FL("Home directory from HOMEDRIVE+HOMEPATH\n"));
      return std::string(homedrive) + std::string(homepath);
    }
  char * systemdrive = getenv("SystemDrive");
  if (systemdrive != NULL)
    {
      L(FL("Home directory from SystemDrive\n"));
      return std::string(systemdrive);
    }
  return std::string("C:");
}

std::string
tilde_expand(std::string const & in)
{
  if (in.empty() || in[0] != '~')
    return in;

  // just ~
  if (in.size() == 1)
    return get_homedir();

  // ~/foo, ~\foo
  if (in[1] == '/' || in[1] == '\\')
    return get_homedir() + in.substr(1);

  // We don't support ~name on Windows.
  return in;
}

path::status
get_path_status(std::string const & path)
{
  DWORD attrs = GetFileAttributesA(path.c_str());

  if (attrs == INVALID_FILE_ATTRIBUTES)
    {
      DWORD err = GetLastError();
      // this list of errors that mean the path doesn't exist borrowed from
      // boost 1.33.1, with unnecessary parenthesis removal by zack
      if(err == ERROR_FILE_NOT_FOUND
         || err == ERROR_INVALID_PARAMETER
         || err == ERROR_NOT_READY
         || err == ERROR_PATH_NOT_FOUND
         || err == ERROR_INVALID_NAME
         || err == ERROR_BAD_NETPATH)
        return path::nonexistent;

      E(false, F("%s: GetFileAttributes error: %s") % path % os_strerror(err));
    }
  else if (attrs & FILE_ATTRIBUTE_DIRECTORY)
    return path::directory;
  else
    return path::file;
}

namespace
{
  // RAII wrapper for FindFirstFile/FindNextFile
  struct dirhandle
  {
    dirhandle(std::string const & path) : first(true), last(false)
    {
      std::string p = path;
      // Win98 requires this little dance
      if (p.size() > 0 && p[p.size()-1] != '/' && p[p.size()-1] != '\\')
        p += "/*";
      else
        p += "*";

      h = FindFirstFile(p.c_str(), &firstdata);
      if (h == INVALID_HANDLE_VALUE)
        {
          if (GetLastError() == ERROR_FILE_NOT_FOUND) // zero files in dir
            last = true;
          else
            E(false, F("could not open directory '%s': %s")
              % path % os_strerror(GetLastError()));
        }
    }
    ~dirhandle()
    {
      if (h != INVALID_HANDLE_VALUE)
        FindClose(h);
    }
    bool next(WIN32_FIND_DATA * data)
    {
      if (last)
        return false;
      if (first)
        {
          *data = firstdata;
          first = false;
          return true;
        }
      if (FindNextFile(h, data))
        return true;
      E(GetLastError() == ERROR_NO_MORE_FILES,
        F("error while reading directory: %s") % os_strerror(errno));
      last = true;
      return false;
    }

  private:
    bool first;
    bool last;
    HANDLE h;
    WIN32_FIND_DATA firstdata;
  };
}

void
do_read_directory(std::string const & path,
                  dirent_consumer & files,
                  dirent_consumer & dirs,
                  dirent_consumer & /* specials */)
{
  dirhandle dir(path);
  WIN32_FIND_DATA d;

  while (dir.next(&d))
    {
      if (!strcmp(d.cFileName, ".") || !strcmp(d.cFileName, ".."))
        continue;

      if (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        dirs.consume(d.cFileName);
      else
        files.consume(d.cFileName);
    }
}

void
do_remove(std::string const & path)
{
  switch (get_path_status(path))
    {
    case path::directory:
      if (RemoveDirectoryA(path.c_str()))
        return;
      break;
    case path::file:
      if (DeleteFileA(path.c_str()))
        return;
      break;
    case path::nonexistent:
      // conveniently, GetLastError() will report the error code from
      // the GetFileAttributes() call in get_path_status() that told us
      // the path doesn't exist.
      break;
    }
  E(false,
    F("could not remove '%s': %s") % path % os_strerror(GetLastError()));
}

void
do_mkdir(std::string const & path)
{
  E(CreateDirectoryA(path.c_str(), 0) != 0,
    F("could not create directory '%s': %s")
    % path % os_strerror(GetLastError()));
}

static bool
rename_clobberingly_impl(const char * from, const char * to)
{
  // MoveFileEx is only available on NT-based systems.  We will revert to a
  // more compatible DeleteFile/MoveFile pair as a compatibility fall-back.
  typedef BOOL (WINAPI *MoveFileExFun)(LPCTSTR, LPCTSTR, DWORD);
  static MoveFileExFun fnMoveFileEx = 0;

  static enum { UNKNOWN, YES, NO } MoveFileExAvailable = UNKNOWN;

  if (MoveFileExAvailable == UNKNOWN)
    {
      HMODULE hModule = LoadLibrary("kernel32");
      if (hModule)
	fnMoveFileEx = reinterpret_cast<MoveFileExFun>
	  (GetProcAddress(hModule, "MoveFileExA"));
      if (fnMoveFileEx)
	{
	  L(FL("using MoveFileEx for renames"));
	  MoveFileExAvailable = YES;
	}
      else
        {
          L(FL("using DeleteFile/MoveFile fallback for renames"));
          MoveFileExAvailable = NO;
        }
      if (hModule)
        FreeLibrary(hModule);
    }

  if (MoveFileExAvailable == YES)
    {
      if (fnMoveFileEx(from, to, MOVEFILE_REPLACE_EXISTING))
	return true;
      else if (GetLastError() != ERROR_CALL_NOT_IMPLEMENTED)
        return false;
      else
        {
          MoveFileExAvailable = NO;
          L(FL("MoveFileEx failed with CALL_NOT_IMPLEMENTED, using fallback"));
	}
    }
  
  // This is not even remotely atomic, but what can you do?
  DeleteFile(to);
  return MoveFile(from, to);
}

void
rename_clobberingly(std::string const & from, std::string const & to)
{
  static const int renameAttempts = 16;
  DWORD sleepTime = 1;
  DWORD lastError = 0;

  // If a clobbering rename attempt fails, we wait and try again, up to an
  // (arbitrary) maximum of 16 attempts.  This is a gross hack to work
  // around the common problem where another process (e.g. a virus checker)
  // will exclusive open a file you've just touched.
  for (int i = 0; i < renameAttempts; ++i) {
    if (rename_clobberingly_impl(from.c_str(), to.c_str()))
      return;
    lastError = GetLastError();
    L(FL("attempted rename of '%s' to '%s' failed: (%s) %d")
      % from % to % os_strerror(lastError) % lastError);
    Sleep(sleepTime);
    if (sleepTime < 250)
      sleepTime *= 2;
  }
  E(false, F("renaming '%s' to '%s' failed: %s (%d)") % from % to
           % os_strerror(lastError) % lastError);
}

// Create a temporary file in directory DIR, writing its name to NAME and
// returning a read-write file descriptor for it.  If unable to create
// the file, throws an E().
//
// N.B. We could use GetTempFileName but it wouldn't help significantly, as
// we want to do the CreateFile ourselves (eventually we will want to
// specify security attributes). This logic borrowed from libiberty's
// mkstemps(), with uppercase characters removed from 'letters' as Windows
// has a case insensitive file system.

static HANDLE
make_temp_file(std::string const & dir, std::string & name)
{
  static const char letters[]
    = "abcdefghijklmnopqrstuvwxyz0123456789";

  const u32 base = sizeof letters - 1;
  const u32 limit = base*base*base * base*base*base;

  static u32 value;
  std::string tmp = dir + "/mtxxxxxx.tmp";

  value += GetTickCount() ^ GetCurrentProcessId();

  for (u64 i = 0; i < limit; i++)
    {
      u64 v = value;

      tmp.at(tmp.size() - 10) = letters[v % base];
      v /= base;
      tmp.at(tmp.size() -  9) = letters[v % base];
      v /= base;
      tmp.at(tmp.size() -  8) = letters[v % base];
      v /= base;
      tmp.at(tmp.size() -  7) = letters[v % base];
      v /= base;
      tmp.at(tmp.size() -  6) = letters[v % base];
      v /= base;
      tmp.at(tmp.size() -  5) = letters[v % base];
      v /= base;
    
      HANDLE h = CreateFile(tmp.c_str(), GENERIC_READ|GENERIC_WRITE,
                            0, // exclusive access
                            (LPSECURITY_ATTRIBUTES)0, // default security
                            CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                            (HANDLE)0); // no template file
                            
      if (h != INVALID_HANDLE_VALUE)
        {
          name = tmp;
          return h;
        }

      // ERROR_ALREADY_EXISTS means we should go 'round again.  Any other
      // GetLastError() value is a plain error.  (Presumably, just as for
      // Unix, there are values that would represent bugs.)
      E(GetLastError() == ERROR_ALREADY_EXISTS,
        F("cannot create temp file %s: %s")
        % tmp % os_strerror(GetLastError()));

      // This increment is relatively prime to any power of two, therefore
      // 'value' will visit every number in its range.
      value += 7777;
    }
  E(false,
    F("cannot find a temporary file (tried %d possibilities)") % limit);
}


// Write string DAT atomically to file FNAME, using TMP as the location to
// create a file temporarily.  rename(2) from an arbitrary filename in TMP
// to FNAME must work (i.e. they must be on the same filesystem).
// If USER_PRIVATE is true, the file will be potentially accessible only to
// the user, else it will be potentially accessible to everyone.
void
write_data_worker(std::string const & fname,
                  std::string const & dat,
                  std::string const & tmpdir,
                  bool user_private)
{
  // USER_PRIVATE true is not implemented for Windows.  It is a thing that
  // can be done, at least under NT-family Windows - we would need to pass a
  // SECURITY_ATTRIBUTES structure to the CreateFile call, specifying a
  // discretionary ACL that denies access to anyone other than the owner -
  // but from what little sense I can make of the MSDN documentation,
  // constructing such an ACL is quite complicated and I am not confident I
  // would get it right.  Better someone who knows Windows should code it.
  // [ Code at http://groups.google.com/group/comp.protocols.kerberos/
  // browse_thread/thread/9e37e931de022791/c5172d5b8c5aa48e%23c5172d5b8c5aa48e
  // might be recyclable to the purpose. ]

  if (user_private)
    W(F("%s will be accessible to all users of this computer\n") % fname);

  struct auto_closer
  {
    HANDLE h;
    auto_closer(HANDLE h) : h(h) {}
    ~auto_closer() { CloseHandle(h); }
  };

  std::string tmp;
  HANDLE h = make_temp_file(tmpdir, tmp);

  {
    auto_closer guard(h);

    char const * ptr = dat.data();
    DWORD remaining = dat.size();
    int deadcycles = 0;

    L(FL("writing %s via temp %s") % fname % tmp);

    do
      {
        DWORD written;
        E(WriteFile(h, (LPCVOID)ptr, remaining, &written, (LPOVERLAPPED)0),
          F("error writing to temp file %s: %s")
          % tmp % os_strerror(GetLastError()));

        if (written == 0)
          {
            deadcycles++;
            E(deadcycles < 4,
              FP("giving up after four zero-length writes to %s "
                 "(%d byte written, %d left)",
                 "giving up after four zero-length writes to %s "
                 "(%d bytes written, %d left)",
                 ptr - dat.data())
              % tmp % (ptr - dat.data()) % remaining);
          }
        ptr += written;
        remaining -= written;
      }
    while (remaining > 0);
  }
  // fd is now closed

  rename_clobberingly(tmp, fname);

}

std::string
get_locale_dir()
{
  char buffer[4096];
  DWORD result = GetModuleFileName(NULL, buffer, sizeof(buffer));
  I(result != sizeof(buffer)); // ran out of buffer space
  I(result != 0); // some other error
  std::string module(buffer);
  std::string::size_type pos = module.find_last_of('\\');
  I(pos != std::string::npos);
  return module.substr(0, pos) + "/locale";
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
