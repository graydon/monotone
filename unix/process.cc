// copyright (C) 2005 Jon Bright <jon@siliconcircus.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include <sstream>

#include "sanity.hh"
#include "platform.hh"

int existsonpath(const char *exe)
{
  L(FL("checking for program '%s'\n") % exe);
  // this is horribly ugly, but at least it is rather portable
  std::string cmd_str = (F("command -v '%s' >/dev/null 2>&1") % exe).str();
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
        N(rc != -1, F("error getting status of file %s: %s") % path % strerror(errno));

        return (s.st_mode & S_IXUSR) && !(s.st_mode & S_IFDIR);
}

int make_executable(const char *path)
{
        mode_t mode;
        struct stat s;
        int fd = open(path, O_RDONLY);
        N(fd != -1, F("error opening file %s: %s") % path % strerror(errno));
        if (fstat(fd, &s))
          return -1;
        mode = s.st_mode;
        mode |= S_IXUSR|S_IXGRP|S_IXOTH;
        int ret = fchmod(fd, mode);
        N(close(fd) == 0, F("error closing file %s: %s") % path % strerror(errno));
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
        pid_t pid;
        pid = fork();
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

int process_wait(pid_t pid, int *res)
{
        int status;
        pid = waitpid(pid, &status, 0);
        if (WIFEXITED(status))    
                *res = WEXITSTATUS(status);
        else
                *res = -1;
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
