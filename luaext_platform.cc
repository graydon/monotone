
#include "lua.hh"

#include <signal.h>
#include <cstdlib>

#include "platform.hh"

using std::malloc;
using std::free;

LUAEXT(get_ostype, )
{
  std::string str;
  get_system_flavour(str);
  lua_pushstring(L, str.c_str());
  return 1;
}

LUAEXT(existsonpath, )
{
  const char *exe = luaL_checkstring(L, -1);
  lua_pushnumber(L, existsonpath(exe));
  return 1;
}

LUAEXT(is_executable, )
{
  const char *path = luaL_checkstring(L, -1);
  lua_pushboolean(L, is_executable(path));
  return 1;
}

LUAEXT(make_executable, )
{
  const char *path = luaL_checkstring(L, -1);
  lua_pushnumber(L, make_executable(path));
  return 1;
}

LUAEXT(spawn, )
{
  int n = lua_gettop(L);
  const char *path = luaL_checkstring(L, 1);
  char **argv = (char**)malloc((n+1)*sizeof(char*));
  int i;
  pid_t ret;
  if (argv==NULL)
    return 0;
  argv[0] = (char*)path;
  for (i=1; i<n; i++) argv[i] = (char*)luaL_checkstring(L, i+1);
  argv[i] = NULL;
  ret = process_spawn(argv);
  free(argv);
  lua_pushnumber(L, ret);
  return 1;
}

LUAEXT(spawn_redirected, )
{
  int n = lua_gettop(L);
  char const * infile = luaL_checkstring(L, 1);
  char const * outfile = luaL_checkstring(L, 2);
  char const * errfile = luaL_checkstring(L, 3);
  const char *path = luaL_checkstring(L, 4);
  n -= 3;
  char **argv = (char**)malloc((n+1)*sizeof(char*));
  int i;
  pid_t ret;
  if (argv==NULL)
    return 0;
  argv[0] = (char*)path;
  for (i=1; i<n; i++) argv[i] = (char*)luaL_checkstring(L,  i+4);
  argv[i] = NULL;
  ret = process_spawn_redirected(infile, outfile, errfile, argv);
  free(argv);
  lua_pushnumber(L, ret);
  return 1;
}

LUAEXT(wait, )
{
  pid_t pid = static_cast<pid_t>(luaL_checknumber(L, -1));
  int res;
  int ret;
  ret = process_wait(pid, &res);
  lua_pushnumber(L, res);
  lua_pushnumber(L, ret);
  return 2;
}

LUAEXT(kill, )
{
  int n = lua_gettop(L);
  pid_t pid = static_cast<pid_t>(luaL_checknumber(L, -2));
  int sig;
  if (n>1)
    sig = static_cast<int>(luaL_checknumber(L, -1));
  else
    sig = SIGTERM;
  lua_pushnumber(L, process_kill(pid, sig));
  return 1;
}

LUAEXT(sleep, )
{
  int seconds = static_cast<int>(luaL_checknumber(L, -1));
  lua_pushnumber(L, process_sleep(seconds));
  return 1;
}

