// copyright (C) 2005 Jon Bright <jon@siliconcircus.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>
#include <windows.h>

#include "sanity.hh"
#include "platform.hh"

int existsonpath(const char *exe)
{
	if (SearchPath(NULL, exe, ".exe", 0, NULL, NULL)==0)
		return -1;
	return 0;
}

int make_executable(const char *path)
{
	return 0; /* Basically meaningless on win32 */
}

int process_spawn(char * const argv[])
{
	int i;
	char *realexe,*filepart;
	int realexelen;
	std::string cmd;
	STARTUPINFO si;
	PROCESS_INFORMATION pi;

	realexelen = strlen(argv[0])+1+MAX_PATH;
	realexe = (char*)malloc(realexelen);
	if (realexe==NULL) return 0;
	if (SearchPath(NULL, argv[0], ".exe", realexelen, realexe, &filepart)==0)
	{
		free(realexe);
		return -1;
	}
	cmd = "\"";
	for (i=0; argv[i]; i++)
	{
		cmd += argv[i];
		if (i)
			cmd += " ";
		else
			cmd += "\" ";
	}
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(STARTUPINFO);
	/* We don't need to set any of the STARTUPINFO members */
	if (CreateProcess(realexe, (char*)cmd.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)==0)
	{
		free(realexe);
		return -1;
	}
	free(realexe);
	CloseHandle(pi.hThread);
	return (int)pi.hProcess;
}

int process_wait(int pid, int *res)
{
	HANDLE hProcess = (HANDLE)pid;
	if (WaitForSingleObject(hProcess, INFINITE)==WAIT_FAILED)
	{
		CloseHandle(hProcess); /* May well not work, but won't harm */
		return -1;
	}
	if (GetExitCodeProcess(hProcess, (DWORD*)res)==0)
		*res = -1;
	CloseHandle(hProcess); /* Let the process die */
	return 0;
}

int process_kill(int pid, int signal)
{
	HANDLE hProcess = (HANDLE)pid;
	if (TerminateProcess(hProcess, 1)==0)
		return -1;
	return 0;
}

int process_sleep(unsigned int seconds)
{
	Sleep(seconds*1000);
	return 0;
}
