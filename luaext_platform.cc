
#include "base.hh"
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

// borrowed from lua/liolib.cc
// Note that making C functions that return FILE* in Lua is tricky
// There is a Lua FAQ entitled:
// "Why does my library-created file segfault on :close() but work otherwise?"

#define topfile(L)	((FILE **)luaL_checkudata(L, 1, LUA_FILEHANDLE))

static int io_fclose (lua_State *L) {
  FILE **p = topfile(L);
  int ok = (fclose(*p) == 0);
  *p = NULL;
  lua_pushboolean(L, ok);
  return 1;
}

static FILE **newfile (lua_State *L) {
  FILE **pf = (FILE **)lua_newuserdata(L, sizeof(FILE *));
  *pf = NULL;  /* file handle is currently `closed' */
  luaL_getmetatable(L, LUA_FILEHANDLE);
  lua_setmetatable(L, -2);

  lua_pushcfunction(L, io_fclose);
  lua_setfield(L, LUA_ENVIRONINDEX, "__close");

  return pf;
}

LUAEXT(spawn_pipe, )
{
  int n = lua_gettop(L);
  char **argv = (char**)malloc((n+1)*sizeof(char*));
  int i;
  pid_t pid;
  if (argv==NULL)
    return 0;
  if (n<1)
    return 0;
  for (i=0; i<n; i++) argv[i] = (char*)luaL_checkstring(L,  i+1);
  argv[i] = NULL;
  
  int infd;
  FILE **inpf = newfile(L);
  int outfd;
  FILE **outpf = newfile(L);

  pid = process_spawn_pipe(argv, inpf, outpf);
  free(argv);

  lua_pushnumber(L, pid);

  return 3;
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

LUAEXT(get_pid, )
{
  pid_t pid = get_process_id();
  lua_pushnumber(L, pid);
  return 1;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

