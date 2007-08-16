// Tester-specific platform interface glue, Unix version.

#include "base.hh"
#include "sanity.hh"
#include "platform.hh"
#include "tester-plaf.hh"

#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

using std::string;

void make_accessible(string const & name)
{
  struct stat st;
  if (stat(name.c_str(), &st) != 0)
    {
      const int err = errno;
      E(false, F("stat(%s) failed: %s") % name % os_strerror(err));
    }

  mode_t new_mode = st.st_mode;
  if (S_ISDIR(st.st_mode))
    new_mode |= S_IEXEC;
  new_mode |= S_IREAD | S_IWRITE;

  if (chmod(name.c_str(), new_mode) != 0)
    {
      const int err = errno;
      E(false, F("chmod(%s) failed: %s") % name % os_strerror(err));
    }
}

time_t get_last_write_time(char const * name)
{
  struct stat st;
  if (stat(name, &st) != 0)
    {
      const int err = errno;
      E(false, F("stat(%s) failed: %s") % name % os_strerror(err));
    }

  return st.st_mtime;
}

void do_copy_file(string const & from, string const & to)
{
  char buf[32768];
  int ifd, ofd;
  ifd = open(from.c_str(), O_RDONLY);
  const int err = errno;
  E(ifd >= 0, F("open %s: %s") % from % os_strerror(err));
  struct stat st;
  st.st_mode = 0666;  // sane default if fstat fails
  fstat(ifd, &st);
  ofd = open(to.c_str(), O_WRONLY|O_CREAT|O_EXCL, st.st_mode);
  if (ofd < 0)
    {
      const int err = errno;
      close(ifd);
      E(false, F("open %s: %s") % to % os_strerror(err));
    }

  ssize_t nread, nwrite;
  int ndead;
  for (;;)
    {
      nread = read(ifd, buf, 32768);
      if (nread < 0)
        goto read_error;
      if (nread == 0)
        break;

      nwrite = 0;
      ndead = 0;
      do
        {
          ssize_t nw = write(ofd, buf + nwrite, nread - nwrite);
          if (nw < 0)
            goto write_error;
          if (nw == 0)
            ndead++;
          if (ndead == 4)
            goto spinning;
          nwrite += nw;
        }
      while (nwrite < nread);
    }
  close(ifd);
  close(ofd);
  return;

 read_error:
  {
    int err = errno;
    close(ifd);
    close(ofd);
    E(false, F("read error copying %s to %s: %s")
      % from % to % os_strerror(err));
  }
 write_error:  {
    int err = errno;
    close(ifd);
    close(ofd);
    E(false, F("write error copying %s to %s: %s")
      % from % to % os_strerror(err));
  }
 spinning:
  {
    close(ifd);
    close(ofd);
    E(false, F("abandoning copy of %s to %s after four zero-length writes")
      % from % to);
  }
}

void set_env(char const * var, char const * val)
{
#if defined HAVE_SETENV
  setenv(var, val, 1);
#elif defined HAVE_PUTENV
  // note: this leaks memory, but the tester is short lived so it probably
  // doesn't matter much.
  string * tempstr = new string(var);
  tempstr->append("=");
  tempstr->append(val);
  putenv(const_cast<char *>(tempstr->c_str()));
#else
#error set_env needs to be ported to this platform
#endif
}

void unset_env(char const * var)
{
#if defined HAVE_UNSETENV
  unsetenv(var);
#else
#error unset_env needs to be ported to this platform
#endif
}

// This function cannot fail, but the Windows version of this function
// always returns -1 to indicate no system support for the operation.
// Therefore the argument and return value are signed.
int do_umask(int mask)
{
  return umask(mask);
}

char * make_temp_dir()
{
  char const * parent;
  parent = getenv("TMPDIR");
  if (parent == 0)
    parent = getenv("TEMP");
  if (parent == 0)
    parent = getenv("TMP");
  if (parent == 0)
    parent = "/tmp";

  char * templ = new char[strlen(parent) + sizeof "/mtXXXXXX"];
  strcpy(templ, parent);
  strcat(templ, "/mtXXXXXX");

  char * result;

  // mkdtemp is not available on all systems.
#ifdef HAVE_MKDTEMP

  result = mkdtemp(templ);
  I(result == templ);
  return templ;

#else

  // Typical use of mktemp() risks the file being created by someone else in
  // between when the name is chosen and the file is opened.  However, use
  // of mktemp() to pick a *directory* name is safe, because mkdir() will
  // not create a directory if anything already exists by that name - even a
  // dangling symlink.  Thus we can simply loop until we find a suitable
  // name.  There IS a very small risk that we loop endlessly, but that's
  // under extreme conditions, and the problem is likely to really be
  // elsewhere... as a backstop, we limit iterations to the smaller of
  // 10000 and TMP_MAX.

  unsigned int cycles = 0, limit = 10000;
#ifdef TMP_MAX
  if (TMP_MAX > 0 && TMP_MAX < limit)
    limit = TMP_MAX;
#endif

  char * tmpdir = new char[strlen(templ) + 1];
  for (;;)
    {
      strcpy(tmpdir, templ);
      result = mktemp(tmpdir);
      E(result, F("mktemp(%s) failed: %s") % tmpdir % os_strerror(errno));
      I(result == tmpdir);

      if (mkdir(tmpdir, 0700) == 0)
        {
          strcpy(templ, tmpdir);
          delete [] tmpdir;
          return templ;
        }
        
      E(errno == EEXIST,
        F("mkdir(%s) failed: %s") % tmpdir % os_strerror(errno));

      cycles++;
      E(cycles < limit,
        F("%d temporary names are all in use") % limit);
    }

#endif
}


bool running_as_root()
{
  return !geteuid();
}

// General note: the magic numbers in this function are meaningful to
// testlib.lua.  They indicate a number of failure scenarios in which
// more detailed diagnostics are not possible.

void run_tests_in_children(test_enumerator const & next_test,
                           test_invoker const & invoke,
                           test_cleaner const & cleanup,
                           std::string const & run_dir,
                           std::string const & /*runner*/,
                           std::string const & /*testfile*/,
                           std::string const & /*firstdir*/)
{
  test_to_run t;
  string testdir;
  while (next_test(t))
    {
      // This must be done before we try to redirect stdout/err to a file
      // within testdir.  If we did it in the child, we would have to do it
      // before it was safe to issue diagnostics.
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

      // Make sure there is no pending buffered output before forking, or it
      // may be doubled.
      fflush(0);
      pid_t child = fork();

      if (child != 0) // parent
        {
          int status;
          if (child == -1)
            status = 122; // spawn failure
          else
            process_wait(child, &status);

          if (cleanup(t, status))
            do_remove_recursive(testdir);
        }
      else // child
        {
          // From this point on we are in the child process.  Until we have
          // entered the test directory and re-opened fds 0-2 it is not safe
          // to throw exceptions or call any of the diagnostic routines.
          // Hence we use bare OS primitives and call _exit(), if any of them
          // fail.  It is safe to assume that close() will not fail.
          if (chdir(testdir.c_str()) != 0)
            _exit(123);

          close(0);
          if (open("/dev/null", O_RDONLY) != 0)
            _exit(124);

          close(1);
          if (open("tester.log", O_WRONLY|O_CREAT|O_EXCL, 0666) != 1)
            _exit(125);
          if (dup2(1, 2) == -1)
            _exit(126);

          invoke(t.name);
          // If invoke() returns something has gone terribly wrong.
          _exit(127);
        }
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
