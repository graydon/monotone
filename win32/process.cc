// copyright (C) 2005 Jon Bright <jon@siliconcircus.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>
#include <sstream>
#include <windows.h>

#include "sanity.hh"
#include "platform.hh"

static std::string munge_inner_argument(const char* arg)
{
  std::string result;
  bool has_space = false;
  int quotes = 0;
  bool space_outside_quote = false;
  const char* arg_end = arg + std::strlen(arg) - 1;

  for (const char* c = arg; *c; ++c) {
    switch (*c) {
    case ' ':
      has_space = true;
      if (quotes % 2 == 0 || quotes == 0)
        space_outside_quote = true;
      break;
    case '"':
      quotes++;
      break;
    }
  }

  I(quotes % 2 == 0);

  // quote start of argument if needed
  if (has_space && space_outside_quote)
    result += "\"";

  // copy argument
  if (quotes == 0)
    result += arg;
  else {
    // escape inner quotes
    for (const char* c = arg; *c; ++c) {
      if (*c == '"' && c != arg && '"' && c != arg_end)
        result += '\\';
      result += *c;
    }
  }

  // quote end of argument
  if (has_space && space_outside_quote)
    result += "\"";

  return result;
}

static std::string munge_argument(const char* arg)
{
  std::string result;

  // handle DOS-style '/file:c:\path to\file.txt' by splitting at the colon
  // and handling the last part as a standard argument, then reassembling
  // for the cmdline.
  if (arg[0] == '/') {
    const char* dos_cmd = std::strchr(arg, ':');
    if (dos_cmd != 0) {
      result += std::string(arg, dos_cmd - arg + 1);
      result += munge_inner_argument(dos_cmd + 1);
    } else
      result += arg;
    return result;
  }

  return munge_inner_argument(arg);
}

static std::string munge_argv_into_cmdline(const char* const argv[])
{
  std::string cmdline;

  for (int i = 0; argv[i]; ++i) {
    cmdline += munge_argument(argv[i]);
    cmdline += " ";
  }

  return cmdline;
}

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

  cmd = munge_argv_into_cmdline(argv);
  L(F("spawning command: '%s' '%s'\n") % realexe % cmd);

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

