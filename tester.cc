
extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include "lua.hh"
#include "tester.h"
#include "paths.hh"

#include <cstdio>

#include <exception>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/lexical_cast.hpp>

using std::string;
using boost::lexical_cast;


#ifdef WIN32
#include <io.h>
inline int dup2(int x, int y) {return _dup2(x,y);}
inline int dup(int x) {return _dup(x);}
inline int close(int x) {return _close(x);}
#else
#include <unistd.h>
#endif

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
  set_redirect(lua_State * L)
  {
    char const * infile = luaL_checkstring(L, -3);
    char const * outfile = luaL_checkstring(L, -2);
    char const * errfile = luaL_checkstring(L, -1);
    
    int infd = set_redirect(0, infile, "r");
    int outfd = set_redirect(1, outfile, "w");
    int errfd = set_redirect(2, errfile, "w");
    
    lua_pushnumber(L, infd);
    lua_pushnumber(L, outfd);
    lua_pushnumber(L, errfd);
    return 3;
  }

  static int
  clear_redirect(lua_State * L)
  {
    int infd = (int)luaL_checknumber(L, -3);
    int outfd = (int)luaL_checknumber(L, -2);
    int errfd = (int)luaL_checknumber(L, -1);
    
    clear_redirect(0, infd);
    clear_redirect(1, outfd);
    clear_redirect(2, errfd);
    
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
      fs::path file(argv[1], fs::native);
      testfile = fs::complete(file).native_file_string();
      save_initial_path();
      source_dir = fs::complete(file.branch_path());
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
