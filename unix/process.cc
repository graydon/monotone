// copyright (C) 2005 Jon Bright <jon@siliconcircus.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>

#include "sanity.hh"
#include "platform.hh"

int existsonpath(const char *exe)
{
	char args[3];
	int pid;
	int res;
	args[0] = "which";
	args[1] = exe;
	args[2] = NULL;
	pid = process_spawn(args);
	if (pid==-1)
		return -1;
	if (process_wait(pid, &res))
		return -1;
	if (res==0)
		return 0;
	return -1;
}

int make_executable(const char *path)
{
	mode_t mode;
	struct stat s;
	if (stat(path, &s))
		return -1;
	mode = s.st_mode;
	mode |= S_IXUSR;
	return chmod(path, mode);
}

int process_spawn(char **argv)
{
	int i;
	pid_t pid;
	pid = fork();
	switch (pid)
	{
		case -1: /* Error */
			return -1;
		case 0: /* Child */
			execvp(argv[0], argv);
			return -1;
		default: /* Parent */
			return (int)pid;
	}
}

int process_wait(int pid, int *res)
{
	int status;
	pid = waitpid((pid_t)pid, &status, 0);
	if (WIFEXITED(status))	  
		*res = WEXITSTATUS(status);
	else
		*res = -1;
	return 0;
}

int process_kill(int pid, int signal)
{
	return kill((pid_t)pid, sig);
}

int process_sleep(unsigned int seconds)
{
	return sleep(seconds);
}
