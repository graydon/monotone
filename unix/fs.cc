// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <stdio.h>
#include <cstring>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/convenience.hpp>

#include "sanity.hh"
#include "platform.hh"

namespace fs = boost::filesystem;

std::string
get_current_working_dir()
{
  char buffer[4096];
  E(getcwd(buffer, 4096),
    F("cannot get working directory: %s") % os_strerror(errno));
  return std::string(buffer);
}

void
change_current_working_dir(std::string const & to)
{
  E(!chdir(to.c_str()),
    F("cannot change to directory %s: %s") % to % os_strerror(errno));
}

std::string
get_default_confdir()
{
  return get_homedir() + "/.monotone";
}

// FIXME: BUG: this probably mangles character sets
// (as in, we're treating system-provided data as utf8, but it's probably in
// the filesystem charset)
std::string
get_homedir()
{
  char * home = getenv("HOME");
  if (home != NULL)
    return std::string(home);

  struct passwd * pw = getpwuid(getuid());
  N(pw != NULL, F("could not find home directory for uid %d") % getuid());
  return std::string(pw->pw_dir);
}

std::string
tilde_expand(std::string const & in)
{
  if (in.empty() || in[0] != '~')
    return in;
  fs::path tmp(in, fs::native);
  fs::path::iterator i = tmp.begin();
  if (i != tmp.end())
    {
      fs::path res;
      if (*i == "~")
        {
          fs::path restmp(get_homedir(), fs::native);
          res /= restmp;
          ++i;
        }
      else if (i->size() > 1 && i->at(0) == '~')
        {
          struct passwd * pw;
          // FIXME: BUG: this probably mangles character sets (as in, we're
          // treating system-provided data as utf8, but it's probably in the
          // filesystem charset)
          pw = getpwnam(i->substr(1).c_str());
          N(pw != NULL,
            F("could not find home directory for user %s") % i->substr(1));
          res /= std::string(pw->pw_dir);
          ++i;
        }
      while (i != tmp.end())
        res /= *i++;
      return res.string();
    }

  return tmp.string();
}

path::status
get_path_status(std::string const & path)
{
  struct stat buf;
  int res;
  res = stat(path.c_str(), &buf);
  if (res < 0)
    {
      if (errno == ENOENT)
        return path::nonexistent;
      else
        E(false, F("error accessing file %s: %s") % path % os_strerror(errno));
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

void
rename_clobberingly(std::string const & from, std::string const & to)
{
  E(!rename(from.c_str(), to.c_str()),
    F("renaming '%s' to '%s' failed: %s") % from % to % os_strerror(errno));
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
