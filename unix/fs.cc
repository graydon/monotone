// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details


#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#include "base.hh"
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <pwd.h>
#include <stdio.h>
#include <fcntl.h>
#include <dirent.h>

#include "sanity.hh"
#include "platform.hh"

using std::string;

/* On Linux, AT_SYMLNK_NOFOLLOW is spellt AT_SYMLINK_NOFOLLOW.
   Hoooray for compatibility! */
#if defined AT_SYMLINK_NOFOLLOW && !defined AT_SYMLNK_NOFOLLOW
#define AT_SYMLNK_NOFOLLOW AT_SYMLINK_NOFOLLOW
#endif


string
get_current_working_dir()
{
  char buffer[4096];
  if (!getcwd(buffer, 4096))
    {
      const int err = errno;
      E(false, F("cannot get working directory: %s") % os_strerror(err));
    }
  return string(buffer);
}

void
change_current_working_dir(string const & to)
{
  if (chdir(to.c_str()))
    {
      const int err = errno;
      E(false, F("cannot change to directory %s: %s") % to % os_strerror(err));
    }
}

string
get_default_confdir()
{
  return get_homedir() + "/.monotone";
}

// FIXME: BUG: this probably mangles character sets
// (as in, we're treating system-provided data as utf8, but it's probably in
// the filesystem charset)
string
get_homedir()
{
  char * home = getenv("HOME");
  if (home != NULL)
    return string(home);

  struct passwd * pw = getpwuid(getuid());
  N(pw != NULL, F("could not find home directory for uid %d") % getuid());
  return string(pw->pw_dir);
}

string
tilde_expand(string const & in)
{
  if (in.empty() || in[0] != '~')
    return in;
  if (in.size() == 1) // just ~
    return get_homedir();
  if (in[1] == '/') // ~/...
    return get_homedir() + in.substr(1);

  string user, after;
  string::size_type slashpos = in.find('/');
  if (slashpos == string::npos)
    {
      user = in.substr(1);
      after = "";
    }
  else
    {
      user = in.substr(1, slashpos-1);
      after = in.substr(slashpos);
    }

  struct passwd * pw;
  // FIXME: BUG: this probably mangles character sets (as in, we're
  // treating system-provided data as utf8, but it's probably in the
  // filesystem charset)
  pw = getpwnam(user.c_str());
  N(pw != NULL,
    F("could not find home directory for user %s") % user);

  return string(pw->pw_dir) + after;
}

path::status
get_path_status(string const & path)
{
  struct stat buf;
  int res;
  res = stat(path.c_str(), &buf);
  if (res < 0)
    {
      const int err = errno;
      if (err == ENOENT)
        return path::nonexistent;
      else
        E(false, F("error accessing file %s: %s") % path % os_strerror(err));
    }
  if (S_ISREG(buf.st_mode))
    return path::file;
  else if (S_ISDIR(buf.st_mode))
    return path::directory;
  else
    {
      // fifo or device or who knows what...
      E(false, F("cannot handle special file %s") % path);
    }
}

namespace
{
  // RAII object for DIRs.
  struct dirhandle
  {
    dirhandle(string const & path)
    {
      d = opendir(path.c_str());
      if (!d)
        {
          const int err = errno;
          E(false, F("could not open directory '%s': %s") % path % os_strerror(err));
        }
    }
    // technically closedir can fail, but there's nothing we could do about it.
    ~dirhandle() { closedir(d); }

    // accessors
    struct dirent * next() { return readdir(d); }
#ifdef HAVE_DIRFD
    int fd() { return dirfd(d); }
#endif
    private:
    DIR *d;
  };
}

void
do_read_directory(string const & path,
                  dirent_consumer & files,
                  dirent_consumer & dirs,
                  dirent_consumer & specials)
{
  string p(path);
  if (p == "")
    p = ".";
  
  dirhandle dir(p);
  struct dirent *d;
  struct stat st;
  int st_result;

  while ((d = dir.next()) != 0)
    {
      if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, ".."))
        continue;
#if defined(_DIRENT_HAVE_D_TYPE) || defined(HAVE_STRUCT_DIRENT_D_TYPE)
      switch (d->d_type)
        {
        case DT_REG: // regular file
          files.consume(d->d_name);
          continue;
        case DT_DIR: // directory
          dirs.consume(d->d_name);
          continue;
          
        case DT_UNKNOWN: // unknown type
        case DT_LNK:     // symlink - must find out what's at the other end
        default:
          break;
        }          
#endif

      // the use of stat rather than lstat here is deliberate.
#if defined HAVE_FSTATAT && defined HAVE_DIRFD
      {
        static bool fstatat_works = true;
        if (fstatat_works)
          {
            st_result = fstatat(dir.fd(), d->d_name, &st, 0);
            if (st_result == -1 && errno == ENOSYS)
              fstatat_works = false;
          }
        if (!fstatat_works)
          st_result = stat((p + "/" + d->d_name).c_str(), &st);
      }
#else
      st_result = stat((p + "/" + d->d_name).c_str(), &st);
#endif

      // if we get no entry it might be a broken symlink
      // try again with lstat
      if (st_result < 0 && errno == ENOENT)
        {
#if defined HAVE_FSTATAT && defined HAVE_DIRFD && defined AT_SYMLNK_NOFOLLOW
          static bool fstatat_works = true;
          if (fstatat_works)
            {
              st_result = fstatat(dir.fd(), d->d_name, &st, AT_SYMLNK_NOFOLLOW);
              if (st_result == -1 && errno == ENOSYS)
                fstatat_works = false;
            }
          if (!fstatat_works)
            st_result = lstat((p + "/" + d->d_name).c_str(), &st);
#else
          st_result = lstat((p + "/" + d->d_name).c_str(), &st);
#endif
        }

      int err = errno;

      E(st_result == 0,
        F("error accessing '%s/%s': %s") % p % d->d_name % os_strerror(err));

      if (S_ISREG(st.st_mode))
        files.consume(d->d_name);
      else if (S_ISDIR(st.st_mode))
        dirs.consume(d->d_name);
      else if (S_ISLNK(st.st_mode))
        files.consume(d->d_name); // treat broken links as files
      else
        specials.consume(d->d_name);
    }
  return;
}
  
                  

void
rename_clobberingly(string const & from, string const & to)
{
  if (rename(from.c_str(), to.c_str()))
    {
      const int err = errno;
      E(false, F("renaming '%s' to '%s' failed: %s") % from % to % os_strerror(err));
    }
}

// the C90 remove() function is guaranteed to work for both files and
// directories
void
do_remove(string const & path)
{
  if (remove(path.c_str()))
    {
      const int err = errno;
      E(false, F("could not remove '%s': %s") % path % os_strerror(err));
    }
}

// Create the directory DIR.  It will be world-accessible modulo umask.
// Caller is expected to check for the directory already existing.
void
do_mkdir(string const & path)
{
  if (mkdir(path.c_str(), 0777))
    {
      const int err = errno;
      E(false, F("could not create directory '%s': %s") % path % os_strerror(err));
    }
}

// Create a temporary file in directory DIR, writing its name to NAME and
// returning a read-write file descriptor for it.  If unable to create
// the file, throws an E().
//

// N.B. None of the standard temporary-file creation routines in libc do
// what we want (mkstemp almost does, but it doesn't let us specify the
// mode).  This logic borrowed from libiberty's mkstemps().  To avoid grief
// with case-insensitive file systems (*cough* OSX) we use only lowercase
// letters for the name.  This reduces the number of possible temporary
// files from 62**6 to 36**6, oh noes.

static int
make_temp_file(string const & dir, string & name, mode_t mode)
{
  static const char letters[]
    = "abcdefghijklmnopqrstuvwxyz0123456789";

  const u32 base = sizeof letters - 1;
  const u32 limit = base*base*base * base*base*base;

  static u32 value;
  struct timeval tv;
  string tmp = dir + "/mtxxxxxx.tmp";

  gettimeofday(&tv, 0);
  value += ((u32) tv.tv_usec << 16) ^ tv.tv_sec ^ getpid();
  value %= limit;

  for (u32 i = 0; i < limit; i++)
    {
      u32 v = value;

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
    
      int fd = open(tmp.c_str(), O_RDWR|O_CREAT|O_EXCL, mode);
      int err = errno;

      if (fd >= 0)
        {
          name = tmp;
          return fd;
        }

      // EEXIST means we should go 'round again.  Any other errno value is a
      // plain error.  (ENOTDIR is a bug, and so are some ELOOP and EACCES
      // conditions - caller's responsibility to make sure that 'dir' is in
      // fact a directory to which we can write - but we get better
      // diagnostics from this E() than we would from an I().)

      E(err == EEXIST,
        F("cannot create temp file %s: %s") % tmp % os_strerror(err));

      // This increment is relatively prime to 'limit', therefore 'value'
      // will visit every number in its range.
      value += 7777;
      value %= limit;
    }

  // we really should never get here.
  E(false, F("all %d possible temporary file names are in use") % limit);
}


// Write string DAT atomically to file FNAME, using TMP as the location to
// create a file temporarily.  rename(2) from an arbitrary filename in TMP
// to FNAME must work (i.e. they must be on the same filesystem).
// If USER_PRIVATE is true, the file will be potentially accessible only to
// the user, else it will be potentially accessible to everyone (i.e. open()
// will be passed mode 0600 or 0666 -- the actual permissions are modified
// by umask as usual).
void
write_data_worker(string const & fname,
                  string const & dat,
                  string const & tmpdir,
                  bool user_private)
{
  struct auto_closer
  {
    int fd;
    auto_closer(int fd) : fd(fd) {}
    ~auto_closer() { close(fd); }
  };

  string tmp;
  int fd = make_temp_file(tmpdir, tmp, user_private ? 0600 : 0666);

  {
    auto_closer guard(fd);

    char const * ptr = dat.data();
    size_t remaining = dat.size();
    int deadcycles = 0;

    L(FL("writing %s via temp %s") % fname % tmp);

    do
      {
        ssize_t written = write(fd, ptr, remaining);
        const int err = errno;
        E(written >= 0,
          F("error writing to temp file %s: %s") % tmp % os_strerror(err));
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

string
get_locale_dir()
{
  return string(LOCALEDIR);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
