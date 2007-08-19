// Tester-specific platform interface glue, Unix version.

#include "base.hh"
#include "sanity.hh"
#include "platform.hh"
#include "tester-plaf.hh"

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <map>

using std::string;
using std::map;
using std::make_pair;

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

// Parallel test case support.
//
// GNU Make's job server algorithm is described in detail at
// <http://make.paulandlesley.org/jobserver.html>.  This program
// implements only part of the general algorithm: specifically, if
// this program is invoked as if it were a recursive make, it will
// participate in the job server algorithm when parallelizing its own
// subcomponents.  None of those subcomponents are themselves
// recursive make operations.  Therefore, what we do is:
//
// 1. The invoking make has created a pipe, and written N tokens into it.
//    We are entitled to run one job at any time, plus as many of the N
//    as we can get tokens for.
//
//    * A token is just a one-byte character.  (Empirically, GNU make
//      uses plus signs (ASCII 0x2B) for this.)
//    * All tokens are identical.
//
// 2. We know that this is the case because we observe, in the MAKEFLAGS
//    environment variable, a construct similar to:
//
//       --jobserver-fds=R,W -j
//
//    where R and W are integers specifying the read and write ends of
//    the jobserver communication pipe.  If we do not observe any such
//    construct, we run in serial mode (this is actually implemented
//    by creating a pipe ourselves, not writing anything to it, and
//    proceeding as described below).
//
// 2a. If the file descriptors specified in the above construct are not
//     open, this means the invoking Makefile did not properly mark the
//     command running this program as a recursive make.  We print a
//     diagnostic and run in serial mode.
//
// 3. We have a queue of jobs to be run, and a set of currently
//    running jobs (initially none).  Before beginning the main loop,
//    we install a handler for SIGCHLD.  The only thing this handler
//    does is close the duplicate jobserver read end (see below).
//
//    The main loop proceeds as follows:
//
//    a. Remove the next job to be run from the queue.
//
//    b. Create a duplicate of the read side of the jobserver pipe, if
//       we don't already have one.
//
//    c. Call wait() in nonblocking mode until it doesn't report any
//       more dead children.  For each reported child, write a token
//       back to the jobserver pipe, unless it is the last running
//       child.
//
//    d. If the set of currently running jobs is nonempty, read one
//       byte in blocking mode from the duplicate fd.  If this returns
//       1, proceed to step e.  If it returns -1 and errno is either
//       EINTR or EBADF, go back to step b.  Abort on any other return
//       and/or errno value.
//
//    e. We have a token! Fork.  In the child, close both sides of the
//       jobserver pipe, and the duplicate, and then invoke the job.
//
// 4. Once the queue of jobs is exhausted, the main loop terminates.
//    Subsequent code repeats step 3c until there are no more children,
//    doing so in *blocking* mode.
//
// The jiggery-pokery with duplicate read fds in 3b-3d is necessary to
// close a race window.  If we didn't do that and a SIGCHLD arrived
// betwen steps c and d, a token could get lost and we could end up
// hanging forever in the read().  See the above webpage for further
// discussion.

// Sadly, there is no getting around global variables for these.  However,
// the information in question really is process-global (file descriptors,
// signal handlers) so it's not like we could be reentrant here anyway.

static int jobsvr_read = -1;
static int jobsvr_write = -1;
static volatile int jobsvr_read_dup = -1;
static int tokens_held = 0;

static void sigchld(int)
{
  if (jobsvr_read_dup != -1)
    {
      close(jobsvr_read_dup);
      jobsvr_read_dup = -1;
    }
}

// Encapsulation of token acquisition and release.  We get one token for free.
// Note: returns true if we need to go reap children again, not if we have
// successfully acquired a token.
static bool acquire_token()
{
  if (tokens_held == 0)
    {
      tokens_held++;
      return false;
    }

  char dummy;
  int n = read(jobsvr_read_dup, &dummy, 1);
  if (n == 1)
    {
      tokens_held++;
      return false;
    }
  else
    {
      I(n == -1 && (errno == EINTR || errno == EBADF));
      return true;
    }
}

static void release_token()
{
  if (tokens_held > 1)
    write(jobsvr_write, "+", 1);
  I(tokens_held > 0);
  tokens_held--;
}

// Set up the above static variables appropriately, given the arguments
// to -j and/or --jobserver-fd on the command line and MAKEFLAGS.
// Diagnostics generated here are exactly the same as GNU Make's.
void prepare_for_parallel_testcases(int jobs, int jread, int jwrite)
{
  if ((jread != -1 || jwrite != -1)
      && (fcntl(jread, F_GETFD) == -1 || fcntl(jwrite, F_GETFD) == -1))
    {
      W(F("jobserver unavailable: using -j1.  Add `+' to parent make rule."));
      close(jread);
      close(jwrite);
      jread = jwrite = -1;
    }

  if (jread != -1 && jwrite != -1 && jobs >= 2)
    {
      W(F("-jN forced in submake: disabling jobserver mode."));
      close(jread);
      close(jwrite);
      jread = jwrite = -1;
    }

  if (jread == -1 && jwrite == -1)
    {
      int jp[2];
      E(pipe(jp) == 0,
        F("creating jobs pipe: %s") % os_strerror(errno));
      jread = jp[0];
      jwrite = jp[1];

      if (jobs == -1)
        jobs = 11;  // infinity goes to 11, but no higher.

      // can ignore errors; the worst case is we don't parallelize as much
      // as was requested.
      for (int i = 0; i < jobs-1; i++)
        write(jwrite, "+", 1);
    }

  I(jread != -1 && jwrite != -1);
  jobsvr_read = jread;
  jobsvr_write = jwrite;
}

// Child side of the fork.  The magic numbers in this function and
// run_tests_in_children are meaningful to testlib.lua.  They indicate a
// number of failure scenarios in which more detailed diagnostics are not
// possible.

// gcc 4.1 doesn't like attributes on the function definition
static NORETURN(void child(test_invoker const &,
                           string const &, string const &));

// Note: to avoid horrible headaches, we do not touch fds 0-2 nor the stdio
// streams.  Child operations are expected to be coded not to do *anything*
// with those streams.  The use of _exit is intentional.
static void child(test_invoker const & invoke, string const & tdir,
                  string const & tname)
{
  close(jobsvr_read);
  close(jobsvr_write);
  close(jobsvr_read_dup);

  if (chdir(tdir.c_str()) != 0)
    _exit(123);

  _exit(invoke(tname));
}

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
  map<pid_t, test_to_run> children;

  if (jobsvr_read_dup != -1)
    {
      close(jobsvr_read_dup);
      jobsvr_read_dup = -1;
    }

  struct sigaction sa, osa;
  sigemptyset(&sa.sa_mask);
  sa.sa_handler = sigchld;
  sa.sa_flags = SA_NOCLDSTOP; // deliberate non-use of SA_RESTART

  E(sigaction(SIGCHLD, &sa, &osa) == 0,
    F("setting SIGCHLD handler: %s") % os_strerror(errno));
  
  while (next_test(t))
    {
      do
        {
          if (jobsvr_read_dup == -1)
            jobsvr_read_dup = dup(jobsvr_read);

          for (;;)
            {
              int status;
              pid_t pid = waitpid(-1, &status, WNOHANG);
              if (pid == 0)
                break;
              if (pid == -1)
                {
                  if (errno == ECHILD)
                    break;
                  if (errno == EINTR)
                    continue;
                  E(false, F("waitpid failed: %s") % os_strerror(errno));
                }

              map<pid_t, test_to_run>::iterator tfin = children.find(pid);
              I(tfin != children.end());
              if (cleanup(tfin->second, status))
                do_remove_recursive(run_dir + "/" + tfin->second.name);
              children.erase(tfin);
              release_token();
            }
        }
      while (acquire_token());

      
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
          release_token();
          continue;
        }

      // Make sure there is no pending buffered output before forking, or it
      // may be doubled.
      fflush(0);
      pid_t pid = fork();
      if (pid == 0)
        child(invoke, testdir, t.name);
      else if (pid == -1)
        {
          if (cleanup(t, 122))
            do_remove_recursive(testdir);
          release_token();
        }
      else
        children.insert(make_pair(pid, t));
    }

  // Now wait for any unfinished children.
  for (;;)
    {
      int status;
      pid_t pid = waitpid(-1, &status, 0);
      if (pid == 0)
        break;
      if (pid == -1)
        {
          if (errno == ECHILD)
            break;
          if (errno == EINTR)
            continue;
          E(false, F("waitpid failed: %s") % os_strerror(errno));
        }

      map<pid_t, test_to_run>::iterator tfin = children.find(pid);
      I(tfin != children.end());
      if (cleanup(tfin->second, status))
        do_remove_recursive(run_dir + "/" + tfin->second.name);
      children.erase(tfin);
      release_token();
    }

  I(tokens_held == 0);
  I(children.size() == 0);
  close(jobsvr_read_dup);
  jobsvr_read_dup = -1;
  sigaction(SIGCHLD, &osa, 0);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
