
extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include "lua.hh"
#include "tester.h"
#include "paths.hh"

#include <cstdio>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/lexical_cast.hpp>

using std::string;
using boost::lexical_cast;

fs::path source_dir;
fs::path run_dir;

struct oops {};

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
    fs::create_directory(testdir);
    go_to_workspace(testdir.native_file_string());
    lua_pushstring(L, testdir.native_file_string().c_str());
    return 1;
  }

  static int
  get_source_dir(lua_State * L)
  {
    lua_pushstring(L, source_dir.native_file_string().c_str());
    return 1;
  }

  /*
  static int
  set_redirect(lua_State * L)
  {
    char const * infile = luaL_checkstring(L, -3);
    char const * outfile = luaL_checkstring(L, -2);
    char const * errfile = luaL_checkstring(L, -1);

    lua_pushnumber(L, infd);
    lua_pushnumber(L, outfd);
    lua_pushnumber(L, errfd);
    return 3;
  }

  static int
  clear_redirect(lua_State * L)
  {
    int infd = luaL_checknumber(L, -3);
    int outfd = luaL_checknumber(L, -2);
    int errfd = luaL_checknumber(L, -1);
    return 0;
  }
  */
}

int main(int argc, char **argv)
{
  string testfile;
  if (argc > 1)
    {
      fs::path file(argv[1], fs::native);
      testfile = argv[1];
      save_initial_path();
      source_dir = fs::complete(file.branch_path());
      run_dir = fs::initial_path() / "tester_dir";
      fs::create_directory(run_dir);
    }
  else
    {
      fprintf(stderr, "Usage: %s test-file [arguments]", argv[0]);
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
//  lua_register(st, "set_redirect", set_redirect);
//  lua_register(st, "clear_redirect", clear_redirect);

  int ret = 2;
  try
    {
      run_string(st, tester_constant, "tester builtin functions");
      printf("Loading test file %s\n", testfile.c_str());
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
  catch (oops &e)
    {
    }

  lua_close(st);
  return ret;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
