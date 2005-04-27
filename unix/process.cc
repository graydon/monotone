// copyright (C) 2005 Jon Bright <jon@siliconcircus.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>

#include <sstream>

#include "sanity.hh"
#include "platform.hh"

int existsonpath(const char *exe)
{
	const char * const args[3] = {"which", exe, NULL};
	int pid;
	int res;
	pid = process_spawn(args);
	if (pid==-1)
		return -1;
	if (process_wait(pid, &res))
		return -1;
	if (res==0)
		return 0;
	return -1;
}

bool is_executable(const char *path)
{
	struct stat s;

	int rc = stat(path, &s);
	N(rc != -1, F("stat() error on file %s)") % path);

	return s.st_mode & S_IXUSR;
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

int process_spawn(const char * const argv[])
{
	{
		std::ostringstream cmdline_ss;
		for (const char *const *i = argv; *i; ++i)
		{
			if (i)
				cmdline_ss << ", ";
			cmdline_ss << "'" << *i << "'";
		}
		L(F("spawning command: %s\n") % cmdline_ss.str());
	}       
	pid_t pid;
	pid = fork();
	switch (pid)
	{
		case -1: /* Error */
			return -1;
		case 0: /* Child */
			execvp(argv[0], (char * const *)argv);
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
	return kill((pid_t)pid, signal);
}

int process_sleep(unsigned int seconds)
{
	return sleep(seconds);
}
