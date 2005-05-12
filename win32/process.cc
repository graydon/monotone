// copyright (C) 2005 Jon Bright <jon@siliconcircus.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>
#include <sstream>
#include <windows.h>

#include "sanity.hh"
#include "platform.hh"

int existsonpath(const char *exe)
{
  if (SearchPath(NULL, exe, ".exe", 0, NULL, NULL)==0)
    return -1;
  return 0;
}

bool is_executable(const char *path)
{
  return false; /* Basically meaningless on win32 */
}

int make_executable(const char *path)
{
  return 0; /* Basically meaningless on win32 */
}

pid_t process_spawn(const char * const argv[])
{
  int i;
  char *realexe,*filepart;
  int realexelen;
  std::string cmd,tmp1,tmp2;
  std::string::iterator it;
  STARTUPINFO si;
  PROCESS_INFORMATION pi;

  realexelen = strlen(argv[0])+1+MAX_PATH;
  realexe = (char*)malloc(realexelen);
  if (realexe==NULL) return 0;
  L(F("searching for exe: %s\n") % argv[0]);
  if (SearchPath(NULL, argv[0], ".exe", realexelen, realexe, &filepart)==0)
    {
      L(F("SearchPath failed, err=%d\n") % GetLastError());
      free(realexe);
      return -1;
    }

  std::ostringstream cmdline_ss;
  L(F("building command line\n"));
  cmdline_ss << realexe;
  for (const char *const *i = argv+1; *i; ++i)
    {
      if (i)
        cmdline_ss << ", ";
      cmdline_ss << "'" << *i << "'";
    }
  L(F("spawning command: %s\n") % cmdline_ss.str());

  cmd = "";
  for (i=0; argv[i]; i++)
    {
      cmd += "\"";
      tmp1 = argv[i];
      tmp2 = "";
      for (it=tmp1.begin(); it!=tmp1.end(); it++)
      {
        if (*it == '\\')
          tmp2.append("\\\\");
        else if (*it == '\"')
          tmp2.append("\\\"");
        else
          tmp2.append(1, *it);
      }
      cmd += tmp2;
      cmd += "\" ";
    }
  memset(&si, 0, sizeof(si));
  si.cb = sizeof(STARTUPINFO);
  /* We don't need to set any of the STARTUPINFO members */
  if (CreateProcess(realexe, (char*)cmd.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)==0)
    {
      L(F("CreateProcess failed, err=%d\n") % GetLastError());
      free(realexe);
      return -1;
    }
  free(realexe);
  CloseHandle(pi.hThread);
  return (pid_t)pi.hProcess;
}

int process_wait(pid_t pid, int *res)
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

int process_kill(pid_t pid, int signal)
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

pid_t get_process_id()
{
  return GetCurrentProcessId();
}

