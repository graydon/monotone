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
#include <cstring>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/convenience.hpp>

#include "sanity.hh"
#include "platform.hh"

std::string get_current_working_dir()
{
  char buffer[4096];
  E(getcwd(buffer, 4096),
    F("cannot get working directory: %s") % std::strerror(errno));
  return std::string(buffer);
}
  
void change_current_working_dir(any_path const & to)
{
  E(!chdir(to.as_external().c_str()),
    F("cannot change to directory %s: %s") % to % std::strerror(errno));
}

// FIXME: BUG: this probably mangles character sets
// (as in, we're treating system-provided data as utf8, but it's probably in
// the filesystem charset)
utf8
get_homedir()
{
  char * home = getenv("HOME");
  if (home != NULL)
    return std::string(home);

  struct passwd * pw = getpwuid(getuid());  
  N(pw != NULL, F("could not find home directory for uid %d") % getuid());
  return std::string(pw->pw_dir);
}

utf8 tilde_expand(utf8 const & in)
{
  if (in().empty() || in()[0] != '~')
    return in;
  fs::path tmp(in(), fs::native);
  fs::path::iterator i = tmp.begin();
  if (i != tmp.end())
    {
      fs::path res;
      if (*i == "~")
        {
          res /= get_homedir()();
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
get_path_status(any_path const & path)
{
  struct stat buf;
  int res;
  res = stat(path.as_external().c_str(), &buf);
  if (res < 0)
    {
      if (errno == ENOENT)
        return path::nonexistent;
      else
        E(false, F("error accessing file %s: %s") % path % std::strerror(errno));
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
