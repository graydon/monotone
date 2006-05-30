
extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include "lua.hh"
#include "tester.h"
#include "paths.hh"
#include "platform.hh"

#include <cstdio>

#include <exception>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/lexical_cast.hpp>

#include <map>
#include <utility>

using std::string;
using std::map;
using std::make_pair;
using boost::lexical_cast;

namespace redirect
{
  enum what {in, out, err};
}

#ifdef WIN32
#include <windows.h>
namespace redirect {typedef HANDLE savetype;}
HANDLE set_redirect(redirect::what what, string where)
{
  HANDLE file;
  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.lpSecurityDescriptor = 0;
  sa.bInheritHandle = true;
  if (what == redirect::in)
    {
      file = CreateFile(where.c_str(),
                        GENERIC_READ,
                        FILE_SHARE_READ,
                        &sa,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    }
  else
    {
      file = CreateFile(where.c_str(),
                        GENERIC_WRITE,
                        0,
                        &sa,
                        CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);
    }
  HANDLE old;
  switch(what)
  {
  case redirect::in:
    old = GetStdHandle(STD_INPUT_HANDLE);
    SetStdHandle(STD_INPUT_HANDLE, file);
    break;
  case redirect::out:
    old = GetStdHandle(STD_OUTPUT_HANDLE);
    SetStdHandle(STD_OUTPUT_HANDLE, file);
    break;
  case redirect::err:
    old = GetStdHandle(STD_ERROR_HANDLE);
    SetStdHandle(STD_ERROR_HANDLE, file);
    break;
  }
  return old;
}
void clear_redirect(redirect::what what, HANDLE saved)
{
  switch(what)
  {
  case redirect::in:
    CloseHandle(GetStdHandle(STD_INPUT_HANDLE));
    SetStdHandle(STD_INPUT_HANDLE, saved);
    break;
  case redirect::out:
    CloseHandle(GetStdHandle(STD_OUTPUT_HANDLE));
    SetStdHandle(STD_OUTPUT_HANDLE, saved);
    break;
  case redirect::err:
    CloseHandle(GetStdHandle(STD_ERROR_HANDLE));
    SetStdHandle(STD_ERROR_HANDLE, saved);
    break;
  }
}
#else
#include <unistd.h>
int set_redirect(int what, string where, string mode)
{
  int saved = dup(what);
  FILE *f = fopen(where.c_str(), mode.c_str());
  if (!f)
    return -1;
  dup2(fileno(f), what);
  fclose(f);
  return saved;
}
void clear_redirect(int what, int saved)
{
  dup2(saved, what);
  close(saved);
}
#endif
namespace redirect
{
  struct saveblock
  {
    savetype in;
    savetype out;
    savetype err;
  };
}

#include <cstdlib>
map<string, string> orig_env_vars;
void save_env() { orig_env_vars.clear(); }
#ifdef WIN32
void restore_env()
{
  for (map<string,string>::const_iterator i = orig_env_vars.begin();
       i != orig_env_vars.end(); ++i)
    {
      _putenv_s(i->first.c_str(), i->second.c_str());
    }
  orig_env_vars.clear();
}
void set_env(string const &var, string const &val)
{
  char const *old = getenv(var.c_str());
  if (old)
    orig_env_vars.insert(make_pair(var, string(old)));
  else
    orig_env_vars.insert(make_pair(var, ""));
  _putenv_s(var.c_str(), val.c_str());
}
#else
void putenv2(string const &var, string const &val)
{
  char const *s = (var + "=" + val).c_str();
  size_t len = var.size() + val.size() + 2;
  char *cp = new char[len];
  memcpy(cp, s, len);
  putenv(cp);
}
void restore_env()
{
  for (map<string,string>::const_iterator i = orig_env_vars.begin();
       i != orig_env_vars.end(); ++i)
    {
      putenv2(i->first, i->second);
    }
  orig_env_vars.clear();
}
void set_env(string const &var, string const &val)
{
  char const *old = getenv(var.c_str());
  if (old)
    orig_env_vars.insert(make_pair(var, string(old)));
  else
    orig_env_vars.insert(make_pair(var, ""));
  putenv2(var, val);
}
#endif


fs::path source_dir;
fs::path run_dir;

struct oops : public std::exception
{
  oops(string const &s = "") throw() : err(s) {}
  ~oops() throw() {}
  string err;
  char const * what() {return err.c_str();}
};

static int panic_thrower(lua_State * st)
{
  throw oops();
}

extern "C"
{
  static int
  go_to_test_dir(lua_State * L)
  {
    char const * testname = luaL_checkstring(L, -1);
    fs::path tname(testname, fs::native);
    fs::path testdir = run_dir / tname.leaf();
    if (fs::exists(testdir))
      fs::remove_all(testdir);
    fs::create_directory(testdir);
    go_to_workspace(testdir.native_file_string());
    lua_pushstring(L, testdir.native_file_string().c_str());
    lua_pushstring(L, tname.leaf().c_str());
    return 2;
  }

  static int
  clean_test_dir(lua_State *L)
  {
    char const * testname = luaL_checkstring(L, -1);
    fs::path tname(testname, fs::native);
    fs::path testdir = run_dir / tname.leaf();
    go_to_workspace(run_dir.native_file_string());
    fs::remove_all(testdir);
    return 0;
  }

  static int
  remove_recursive(lua_State *L)
  {
    char const * dirname = luaL_checkstring(L, -1);
    fs::path dir(dirname, fs::native);
    fs::remove_all(dir);
    return 0;
  }

  static int
  leave_test_dir(lua_State *L)
  {
    go_to_workspace(run_dir.native_file_string());
    return 0;
  }

  static int
  make_dir(lua_State *L)
  {
    char const * dirname = luaL_checkstring(L, -1);
    fs::path dir(dirname, fs::native);
    fs::create_directory(dir);
    return 0;
  }

  static int
  exists(lua_State *L)
  {
    char const * name = luaL_checkstring(L, -1);
    fs::path p(name, fs::native);
    lua_pushboolean(L, fs::exists(p));
    return 1;
  }

  static int
  get_source_dir(lua_State * L)
  {
    lua_pushstring(L, source_dir.native_file_string().c_str());
    return 1;
  }

  static int
  clear_redirect(lua_State * L)
  {
    typedef redirect::saveblock rsb;
    rsb const *sb = static_cast<rsb const*>(lua_topointer(L, 1));
    clear_redirect(redirect::in, sb->in);
    clear_redirect(redirect::out, sb->out);
    clear_redirect(redirect::err, sb->err);
    return 0;
  }

  static int
  set_redirect(lua_State * L)
  {
    char const * infile = luaL_checkstring(L, -3);
    char const * outfile = luaL_checkstring(L, -2);
    char const * errfile = luaL_checkstring(L, -1);
    
    typedef redirect::saveblock rsb;
    rsb *sb = static_cast<rsb*> (lua_newuserdata(L, sizeof(rsb)));
    sb->in = set_redirect(redirect::in, infile);
    sb->out = set_redirect(redirect::out, outfile);
    sb->err = set_redirect(redirect::err, errfile);
    lua_newtable(L);
    lua_pushstring(L, "restore");
    lua_pushcfunction(L,clear_redirect);
    lua_settable(L, -3);
    lua_setmetatable(L, -2);
    
    return 1;
  }

  static int
  get_ostype(lua_State * L)
  {
    string str;
    get_system_flavour(str);
    lua_pushstring(L, str.c_str());
    return 1;
  }

  static int
  do_save_env(lua_State * L)
  {
    save_env();
    return 0;
  }

  static int
  do_restore_env(lua_State * L)
  {
    restore_env();
    return 0;
  }

  static int
  do_set_env(lua_State * L)
  {
    char const * var = luaL_checkstring(L, -2);
    char const * val = luaL_checkstring(L, -1);
    set_env(var, val);
    return 0;
  }
}

int main(int argc, char **argv)
{
  string testfile;
  bool needhelp = false;
  for (int i = 1; i < argc; ++i)
    if (string(argv[i]) == "--help" || string(argv[i]) == "-h")
      needhelp = true;
  if (argc > 1 && !needhelp)
    {
      fs::path file = fs::complete(fs::path(argv[1], fs::native));
      testfile = file.native_file_string();
      save_initial_path();
      source_dir = file.branch_path();
      run_dir = fs::initial_path() / "tester_dir";
      fs::create_directory(run_dir);
      go_to_workspace(run_dir.native_file_string());
    }
  else
    {
      fprintf(stderr, "Usage: %s test-file [arguments]\n", argv[0]);
      fprintf(stderr, "\t-h         print this message\n");
      fprintf(stderr, "\t-l         print test names only; don't run them\n");
      fprintf(stderr, "\t-d         don't clean the scratch directories\n");
      fprintf(stderr, "\tnum        run a specific test\n");
      fprintf(stderr, "\tnum..num   run tests in a range\n");
      fprintf(stderr, "\t           if num is negative, count back from the end");
      fprintf(stderr, "\tregex      run tests with matching names\n");
      return 1;
    }
  lua_State *st = lua_open();
  lua_atpanic (st, &panic_thrower);
  luaopen_base(st);
  luaopen_io(st);
  luaopen_string(st);
  luaopen_math(st);
  luaopen_table(st);
  luaopen_debug(st);
  add_functions(st);
  lua_register(st, "go_to_test_dir", go_to_test_dir);
  lua_register(st, "get_source_dir", get_source_dir);
  lua_register(st, "set_redirect", set_redirect);
  lua_register(st, "clear_redirect", clear_redirect);
  lua_register(st, "clean_test_dir", clean_test_dir);
  lua_register(st, "leave_test_dir", leave_test_dir);
  lua_register(st, "mkdir", make_dir);
  lua_register(st, "remove_recursive", remove_recursive);
  lua_register(st, "exists", exists);
  lua_register(st, "get_ostype", get_ostype);
  lua_register(st, "save_env", do_save_env);
  lua_register(st, "restore_env", do_restore_env);
  lua_register(st, "set_env", do_set_env);

  int ret = 2;
  try
    {
      run_string(st, tester_constant, "tester builtin functions");
      //printf("Loading test file %s\n", testfile.c_str());
      run_file(st, testfile);
      Lua ll(st);
      ll.func("run_tests");
      ll.push_table();
      for (int i = 2; i < argc; ++i)
        {
          ll.push_int(i-1);
          ll.push_str(argv[i]);
          ll.set_table();
        }
      ll.call(1,1)
        .extract_int(ret);
    }
  catch (std::exception &e)
    {
    }

  lua_close(st);
  return ret;
}
