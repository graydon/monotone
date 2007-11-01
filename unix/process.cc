// copyright (C) 2005 Jon Bright <jon@siliconcircus.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "base.hh"
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include <iostream>
#include <sstream>

#include "sanity.hh"
#include "platform.hh"

int existsonpath(const char *exe)
{
  L(FL("checking for program '%s'\n") % exe);
  // this is horribly ugly, but at least it is rather portable
  std::string cmd_str = (FL("command -v '%s' >/dev/null 2>&1") % exe).str();
  const char * const args[] = {"sh", "-c", cmd_str.c_str(), NULL};
  int pid;
  int res;
  pid = process_spawn(args);
  if (pid==-1)
    {
      L(FL("error in process_spawn\n"));
      return -1;
    }
  if (process_wait(pid, &res))
    {
      L(FL("error in process_wait\n"));
      return -1;
    }
  if (res==0)
    {
      L(FL("successful return; %s exists\n") % exe);
      return 0;
    }
  L(FL("failure; %s does not exist\n") % exe);
  return -1;
}

bool is_executable(const char *path)
{
  struct stat s;

  int rc = stat(path, &s);
  if (rc == -1)
    {
      const int err = errno;
      N(false, F("error getting status of file %s: %s") % path % os_strerror(err));
    }

  return (s.st_mode & S_IXUSR) && !(s.st_mode & S_IFDIR);
}

// copied from libc info page
static mode_t
read_umask()
{
  mode_t mask = umask(0);
  umask(mask);
  return mask;
}

int make_executable(const char *path)
{
  mode_t mode;
  struct stat s;
  int fd = open(path, O_RDONLY);
  if (fd == -1)
    {
      const int err = errno;
      N(false, F("error opening file %s: %s") % path % os_strerror(err));
    }
  if (fstat(fd, &s))
    return -1;
  mode = s.st_mode;
  mode |= ((S_IXUSR|S_IXGRP|S_IXOTH) & ~read_umask());
  int ret = fchmod(fd, mode);
  if (close(fd) != 0)
    {
      const int err = errno;
      N(false, F("error closing file %s: %s") % path % os_strerror(err));
    }
  return ret;
}

pid_t process_spawn(const char * const argv[])
{
  {
    std::ostringstream cmdline_ss;
    for (const char *const *i = argv; *i; ++i)
      {
        if (i != argv)
          cmdline_ss << ", ";
        cmdline_ss << "'" << *i << "'";
      }
    L(FL("spawning command: %s\n") % cmdline_ss.str());
  }
  std::cout.flush();
  pid_t pid = fork();
  switch (pid)
    {
    case -1: /* Error */
      return -1;
    case 0: /* Child */
      execvp(argv[0], (char * const *)argv);
      raise(SIGKILL);
    default: /* Parent */
      return pid;
    }
}

struct redir
{
  struct bad_redir {};
  int savedfd;
  int fd;
  redir(int which, char const * file);
  ~redir();
};
redir::redir(int which, char const * file)
  : savedfd(-1), fd(which)
{
  if (!file || *file == '\0')
    return;
  int tempfd = open(file, (which==0?O_RDONLY:O_WRONLY|O_CREAT|O_TRUNC), 0664);
  if (tempfd == -1)
    {
      throw redir::bad_redir();
    }
  int oldfd = dup(which);
  if (oldfd == -1)
    {
      close(tempfd);
      throw redir::bad_redir();
    }
  close(which);
  while (dup2(tempfd, which) == -1 && errno == EINTR) ;
  close(tempfd);
  fd = which;
  savedfd = oldfd;
}
redir::~redir()
{
  if (savedfd != -1)
    {
      close(fd);
      dup2(savedfd, fd);
      close(savedfd);
    }
}

pid_t process_spawn_redirected(char const * in,
                               char const * out,
                               char const * err,
                               char const * const argv[])
{
  try
    {
      redir i(0, in);
      redir o(1, out);
      redir e(2, err);
      return process_spawn(argv);
    }
  catch (redir::bad_redir & r)
    {
      return -1;
    }
}

pid_t process_spawn_pipe(char const * const argv[], FILE** in, FILE** out)
{
  int infds[2];
  int outfds[2];
  pid_t pid;
  
  if (pipe(infds) < 0)
    return -1;
  if (pipe(outfds) < 0)
    {
      close(infds[0]);
      close(infds[1]);
      return -1;
    }
  
  switch(pid = vfork())
    {
      case -1:
        close(infds[0]);
        close(infds[1]);
        close(outfds[0]);
        close(outfds[1]);
        return -1;
      case 0:
        {
          if (infds[0] != STDIN_FILENO)
            {
              dup2(infds[0], STDIN_FILENO);
              close(infds[0]);
            }
          close(infds[1]);
          if (outfds[1] != STDOUT_FILENO)
            {
              dup2(outfds[1], STDOUT_FILENO);
              close(outfds[1]);
            }
          close(outfds[0]);
          
          execvp(argv[0], (char * const *)argv);
          raise(SIGKILL);
        }
    }
  close(infds[0]);
  close(outfds[1]);
  *in = fdopen(infds[1], "w");
  *out = fdopen(outfds[0], "r");
  
  return pid;
}

int process_wait(pid_t pid, int *res, int timeout)
{
  int status;
  int flags = 0;
  if (timeout == -1)
    timeout = 0;
  else
    flags |= WNOHANG;
  int r;
  for (r = 0; r == 0 && timeout >= 0; --timeout)
    {
      r = waitpid(pid, &status, flags);
      if (r == -1)
        {
          *res = errno;
          if (errno == EINTR)
            {
              timeout++;
              r = 0;
              continue;
            }
          else
            return -1;
        }
      if (r == 0 && timeout > 0)
        process_sleep(1);
    }
  if (r == 0)
    {
      *res = 0;
      return -1;
    }
  if (WIFEXITED(status))    
    *res = WEXITSTATUS(status);
  else
    *res = -WTERMSIG(status);
  return 0;
}

int process_kill(pid_t pid, int signal)
{
  return kill(pid, signal);
}

int process_sleep(unsigned int seconds)
{
  return sleep(seconds);
}

pid_t get_process_id()
{
  return getpid();
}

void ignore_sigpipe()
{
  signal(SIGPIPE, SIG_IGN);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
