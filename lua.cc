// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "config.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <boost/lexical_cast.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/regex.hpp>

#include <set>
#include <map>
#include <fstream>

#include "app_state.hh"
#include "file_io.hh"
#include "lua.hh"
#include "mkstemp.hh"
#include "sanity.hh"
#include "vocab.hh"
#include "platform.hh"
#include "transforms.hh"
#include "paths.hh"
#include "globish.hh"
#include "basic_io.hh"

// defined in {std,test}_hooks.lua, converted
#include "test_hooks.h"
#include "std_hooks.h"

using namespace std;
using boost::lexical_cast;

// this lets the lua callbacks (monotone_*_for_lua) have access to the
// app_state the're associated with.
// it was added so that the confdir (normally ~/.monotone) can be specified on
// the command line (and so known only to the app_state), and still be
// available to lua
// please *don't* use it for complex things that can throw errors
static std::map<lua_State*, app_state*> map_of_lua_to_app;

static int panic_thrower(lua_State * st)
{
  throw oops("lua panic");
}

// adapted from "programming in lua", section 24.2.3
// http://www.lua.org/pil/24.2.3.html
// output is from bottom (least accessible) to top (most accessible, where
// push and pop happen).
static std::string
dump_stack(lua_State * st)
{
  std::string out;
  int i;
  int top = lua_gettop(st);
  for (i = 1; i <= top; i++) {  /* repeat for each level */
    int t = lua_type(st, i);
    switch (t) {
    case LUA_TSTRING:  /* strings */
      out += (boost::format("`%s'") % std::string(lua_tostring(st, i), lua_strlen(st, i))).str();
      break;
      
    case LUA_TBOOLEAN:  /* booleans */
      out += (lua_toboolean(st, i) ? "true" : "false");
      break;
      
    case LUA_TNUMBER:  /* numbers */
      out += (boost::format("%g") % lua_tonumber(st, i)).str();
      break;
      
    default:  /* other values */
      out += (boost::format("%s") % lua_typename(st, t)).str();
      break;
      
    }
    out += "  ";  /* put a separator */
  }
  return out;
}

// This Lua object represents a single imperative transaction with the lua
// interpreter. if it fails at any point, all further commands in the
// transaction are ignored. it cleans the lua stack up when it is
// destructed, so no need to pop values when you're done.

struct 
Lua
{
  lua_State * st;
  bool failed;
  static std::set<string> missing_functions;

  Lua(lua_State * s) : 
    st(s), failed(false)
  {}
  
  ~Lua()
  {
    lua_settop(st, 0);
  }

  void fail(std::string const & reason)
  {
    L(FL("lua failure: %s; stack = %s\n") % reason % dump_stack(st));
    failed = true;
  }

  bool ok() 
  {
    if (failed) 
      L(FL("Lua::ok(): failed"));
    return !failed; 
  }

  void report_error()
  {
    I(lua_isstring(st, -1));
    string err = string(lua_tostring(st, -1), lua_strlen(st, -1));
    W(i18n_format("%s\n") % err);
    L(FL("lua stack: %s") % dump_stack(st));
    lua_pop(st, 1);
    failed = true;
  }
 
  // getters

  Lua & get(int idx = LUA_GLOBALSINDEX) 
  { 
    if (failed) return *this;
    if (!lua_istable (st, idx)) 
      { 
        fail("istable() in get");
        return *this; 
      }
    if (lua_gettop (st) < 1) 
      { 
        fail("stack top > 0 in get");
        return *this; 
      }
    lua_gettable(st, idx); 
    return *this; 
  }

  Lua & get_fn(int idx = LUA_GLOBALSINDEX) 
  { 
    if (failed) return *this;
    get(idx);
    if (!lua_isfunction (st, -1)) 
      fail("isfunction() in get_fn");
    return *this; 
  }

  Lua & get_tab(int idx = LUA_GLOBALSINDEX) 
  { 
    if (failed) return *this;
    get(idx);
    if (!lua_istable (st, -1)) 
      fail("istable() in get_tab");
    return *this; 
  }

  Lua & get_str(int idx = LUA_GLOBALSINDEX) 
  { 
    if (failed) return *this;
    get(idx);
    if (!lua_isstring (st, -1)) 
      fail("isstring() in get_str");
    return *this; 
  }

  Lua & get_num(int idx = LUA_GLOBALSINDEX) 
  { 
    if (failed) return *this;
    get(idx);
    if (!lua_isnumber (st, -1)) 
      fail("isnumber() in get_num");
    return *this; 
  }

  Lua & get_bool(int idx = LUA_GLOBALSINDEX) 
  { 
    if (failed) return *this;
    get(idx);
    if (!lua_isboolean (st, -1)) 
      fail("isboolean() in get_bool");
    return *this; 
  }

  // extractors

  Lua & extract_str(string & str)
  {
    if (failed) return *this;
    if (!lua_isstring (st, -1)) 
      { 
        fail("isstring() in extract_str");
        return *this;
      }
    str = string(lua_tostring(st, -1), lua_strlen(st, -1));
    L(FL("lua: extracted string = %s") % str);
    return *this;
  }

  Lua & extract_int(int & i)
  {
    if (failed) return *this;
    if (!lua_isnumber (st, -1)) 
      { 
        fail("isnumber() in extract_int");
        return *this;
      }
    i = static_cast<int>(lua_tonumber(st, -1));
    L(FL("lua: extracted int = %i") % i);
    return *this;
  }

  Lua & extract_double(double & i)
  {
    if (failed) return *this;
    if (!lua_isnumber (st, -1)) 
      { 
        fail("isnumber() in extract_double");
        return *this;
      }
    i = lua_tonumber(st, -1);
    L(FL("lua: extracted double = %i") % i);
    return *this;
  }


  Lua & extract_bool(bool & i)
  {
    if (failed) return *this;
    if (!lua_isboolean (st, -1)) 
      { 
        fail("isboolean() in extract_bool");
        return *this;
      }
    i = (lua_toboolean(st, -1) == 1);
    L(FL("lua: extracted bool = %i") % i);
    return *this;
  }


  // table iteration

  Lua & begin()
  {
    if (failed) return *this;
    if (!lua_istable(st, -1)) 
      { 
        fail("istable() in begin");
        return *this;
      }
    I(lua_checkstack (st, 1));
    lua_pushnil(st);    
    return *this;
  }
  
  bool next() 
  {
    if (failed) return false;
    if (!lua_istable(st, -2)) 
      { 
        fail("istable() in next");
        return false;
      }
    I(lua_checkstack (st, 1));
    if (lua_next(st, -2) != 0)
      {
        return true;
      }
    pop();
    return false;
  }

  // pushers

  Lua & push_str(string const & str) 
  { 
    if (failed) return *this;
    I(lua_checkstack (st, 1));
    lua_pushlstring(st, str.c_str(), str.size()); 
    return *this; 
  }

  Lua & push_int(int num) 
  { 
    if (failed) return *this;
    I(lua_checkstack (st, 1));
    lua_pushnumber(st, num); 
    return *this; 
  }

  Lua & push_int(double num) 
  { 
    if (failed) return *this;
    I(lua_checkstack (st, 1));
    lua_pushnumber(st, num); 
    return *this; 
  }

  Lua & push_bool(bool b) 
  { 
    if (failed) return *this;
    I(lua_checkstack (st, 1));
    lua_pushboolean(st, b); 
    return *this; 
  }

  Lua & push_nil() 
  { 
    if (failed) return *this;
    I(lua_checkstack (st, 1));
    lua_pushnil(st); 
    return *this; 
  }

  Lua & push_table() 
  { 
    if (failed) return *this;
    I(lua_checkstack (st, 1));
    lua_newtable(st); 
    return *this; 
  }

  Lua & set_table(int idx = -3) 
  { 
    if (failed) return *this;
    I(lua_checkstack (st, 1));
    lua_settable(st, idx); 
    return *this; 
  }

  Lua & call(int in, int out) 
  { 
    if (failed) return *this;
    I(lua_checkstack (st, out));
    if (lua_pcall(st, in, out, 0) != 0)
      { 
        report_error();
      } 
    return *this; 
  }

  Lua & pop(int count = 1) 
  { 
    if (failed) return *this;
    if (lua_gettop (st) < count) 
      { 
        fail("stack top is not >= count in pop");
        return *this; 
      }
    lua_pop(st, count); 
    return *this; 
  }

  Lua & func(string const & fname)
  {
    L(FL("loading lua hook %s") % fname);
    if (!failed) 
      {
        if (missing_functions.find(fname) != missing_functions.end())
          failed = true;
        else
          {
            push_str(fname);
            get_fn();
            if (failed)
              missing_functions.insert(fname);
          }
      }
    return *this;
  }

  Lua & loadstring(string const & str, string const & identity)
  {
    if (!failed)
      {
        if (luaL_loadbuffer(st, str.c_str(), str.size(), identity.c_str()))
          {
            report_error();
          }
      }
    return *this;
  }

  Lua & loadfile(string const & filename)
  {
    if (!failed)
      {
        if (luaL_loadfile(st, filename.c_str()))
          {
            report_error();
          }
      }
    return *this;
  }
};

std::set<string> Lua::missing_functions;




extern "C"
{
  static int 
  monotone_mkstemp_for_lua(lua_State *L) 
  {
    int fd = -1;
    FILE **pf = NULL;
    char const *filename = lua_tostring (L, -1);
    std::string dup(filename);
    
    fd = monotone_mkstemp(dup);
    
    if (fd == -1)
      return 0;
    
    // this magic constructs a lua object which the lua io library
    // will enjoy working with
    pf = static_cast<FILE **>(lua_newuserdata(L, sizeof(FILE *)));
    *pf = fdopen(fd, "r+");  
    lua_pushstring(L, "FILE*");
    lua_rawget(L, LUA_REGISTRYINDEX);
    lua_setmetatable(L, -2);  
    
    lua_pushstring(L, dup.c_str());
    
    if (*pf == NULL) 
      {
        lua_pushnil(L);
        lua_pushfstring(L, "%s", strerror(errno));
        lua_pushnumber(L, errno);
        return 3;
      }
    else
      return 2;
  }

  static int
  monotone_existsonpath_for_lua(lua_State *L)
  {
    const char *exe = lua_tostring(L, -1);
    lua_pushnumber(L, existsonpath(exe));
    return 1;
  }

  static int
  monotone_is_executable_for_lua(lua_State *L)
  {
    const char *path = lua_tostring(L, -1);
    lua_pushboolean(L, is_executable(path));
    return 1;
  }

  static int
  monotone_make_executable_for_lua(lua_State *L)
  {
    const char *path = lua_tostring(L, -1);
    lua_pushnumber(L, make_executable(path));
    return 1;
  }

  static int
  monotone_spawn_for_lua(lua_State *L)
  {
    int n = lua_gettop(L);
    const char *path = lua_tostring(L, -n);
    char **argv = (char**)malloc((n+1)*sizeof(char*));
    int i;
    pid_t ret;
    if (argv==NULL)
      return 0;
    argv[0] = (char*)path;
    for (i=1; i<n; i++) argv[i] = (char*)lua_tostring(L, -(n - i));
    argv[i] = NULL;
    ret = process_spawn(argv);
    free(argv);
    lua_pushnumber(L, ret);
    return 1;
  }

  static int
  monotone_wait_for_lua(lua_State *L)
  {
    pid_t pid = (pid_t)lua_tonumber(L, -1);
    int res;
    int ret;
    ret = process_wait(pid, &res);
    lua_pushnumber(L, res);
    lua_pushnumber(L, ret);
    return 2;
  }

  static int
  monotone_kill_for_lua(lua_State *L)
  {
    int n = lua_gettop(L);
    pid_t pid = (pid_t)lua_tonumber(L, -2);
    int sig;
    if (n>1)
      sig = (int)lua_tonumber(L, -1);
    else
      sig = SIGTERM;
    lua_pushnumber(L, process_kill(pid, sig));
    return 1;
  }

  static int
  monotone_sleep_for_lua(lua_State *L)
  {
    int seconds = (int)lua_tonumber(L, -1);
    lua_pushnumber(L, process_sleep(seconds));
    return 1;
  }

  static int
  monotone_guess_binary_file_contents_for_lua(lua_State *L)
  {
    const char *path = lua_tostring(L, -1);
    N(path, F("%s called with an invalid parameter") % "guess_binary");

    std::ifstream file(path, ios_base::binary);
    if (!file) 
      {
        lua_pushnil(L);
        return 1;
      }
    const int bufsize = 8192;
    char tmpbuf[bufsize];
    string buf;
    while (file.read(tmpbuf, sizeof tmpbuf))
      {
        I(file.gcount() <= static_cast<int>(sizeof tmpbuf));
        buf.assign(tmpbuf, file.gcount());
        if (guess_binary(buf)) 
          {
            lua_pushboolean(L, true);
            return 1;
          }
      }
    lua_pushboolean(L, false);
    return 1;
  }
  
  static int
  monotone_include_for_lua(lua_State *L)
  {
    const char *path = lua_tostring(L, -1);
    N(path, F("%s called with an invalid parameter") % "Include");
    
    bool res =Lua(L)
    .loadfile(std::string(path, lua_strlen(L, -1)))
    .call(0,1)
    .ok();

    lua_pushboolean(L, res);
    return 1;
  }
  
  static int
  monotone_includedir_for_lua(lua_State *L)
  {
    const char *pathstr = lua_tostring(L, -1);
    N(pathstr, F("%s called with an invalid parameter") % "IncludeDir");

    fs::path locpath(pathstr, fs::native);
    N(fs::exists(locpath), F("Directory '%s' does not exists") % pathstr);
    N(fs::is_directory(locpath), F("'%s' is not a directory") % pathstr);

    // directory, iterate over it, skipping subdirs, taking every filename,
    // sorting them and loading in sorted order
    fs::directory_iterator it(locpath);
    std::vector<fs::path> arr;
    while (it != fs::directory_iterator())
      {
        if (!fs::is_directory(*it))
          arr.push_back(*it);
        ++it;
      }
    std::sort(arr.begin(), arr.end());
    for (std::vector<fs::path>::iterator i= arr.begin(); i != arr.end(); ++i)
      {
        bool res =Lua(L)
        .loadfile(i->string())
        .call(0,1)
        .ok();
        N(res, F("lua error while loading rcfile '%s'") % i->string());
      }

    lua_pushboolean(L, true); 
    return 1;
  }

  static int
  monotone_regex_search_for_lua(lua_State *L)
  {
    const char *re = lua_tostring(L, -2);
    const char *str = lua_tostring(L, -1);
    boost::cmatch what;

    bool result = false;
    try {
      result = boost::regex_search(str, what, boost::regex(re));
    } catch (boost::bad_pattern e) {
      lua_pushstring(L, e.what());
      lua_error(L);
      return 0;
    }
    lua_pushboolean(L, result);
    return 1;
  }

  static int
  monotone_globish_match_for_lua(lua_State *L)
  {
    const char *re = lua_tostring(L, -2);
    const char *str = lua_tostring(L, -1);

    bool result = false;
    try {
      string r(re);
      string n;
      string s(str);
      result = globish_matcher(r, n)(s);
    } catch (informative_failure & e) {
      lua_pushstring(L, e.what.c_str());
      lua_error(L);
      return 0;
    } catch (boost::bad_pattern & e) {
      lua_pushstring(L, e.what());
      lua_error(L);
      return 0;
    } catch (...) {
      lua_pushstring(L, "Unknown error.");
      lua_error(L);
      return 0;
    }
    lua_pushboolean(L, result);
    return 1;
  }

  static int
  monotone_gettext_for_lua(lua_State *L)
  {
    const char *msgid = lua_tostring(L, -1);
    lua_pushstring(L, gettext(msgid));
    return 1;
  }

  static int
  monotone_get_confdir_for_lua(lua_State *L)
  {
    map<lua_State*, app_state*>::iterator i = map_of_lua_to_app.find(L);
    if (i != map_of_lua_to_app.end())
      {
        system_path dir = i->second->get_confdir();
        string confdir = dir.as_external();
        lua_pushstring(L, confdir.c_str());
      }
    else
      lua_pushnil(L);
    return 1;
  }

  static int
  monotone_parse_basic_io_for_lua(lua_State *L)
  {
    vector<pair<string, vector<string> > > res;
    const string str(lua_tostring(L, -1), lua_strlen(L, -1));
    basic_io::input_source in(str, "monotone_parse_basic_io_for_lua");
    basic_io::tokenizer tok(in);
    try
      {
        string got;
        basic_io::token_type tt;
        do
          {
            tt = tok.get_token(got);
            switch (tt)
              {
              case basic_io::TOK_SYMBOL:
                res.push_back(make_pair(got, vector<string>()));
                break;
              case basic_io::TOK_STRING:
              case basic_io::TOK_HEX:
                E(!res.empty(), F("bad input to parse_basic_io"));
                res.back().second.push_back(got);
                break;
              default:
                break;
              }
          }
        while (tt != basic_io::TOK_NONE);
      }
    catch (informative_failure & e)
      {// there was a syntax error in our string
        lua_pushnil(L);
        return 0;
      }
    lua_newtable(L);
    int n = 1;
    for (vector<pair<string, vector<string> > >::const_iterator i = res.begin();
         i != res.end(); ++i)
      {
        lua_pushnumber(L, n++);
        lua_newtable(L);
        lua_pushstring(L, "name");
        lua_pushstring(L, i->first.c_str());
        lua_settable(L, -3);
        lua_pushstring(L, "values");
        lua_newtable(L);
        int m = 1;
        for (vector<string>::const_iterator j = i->second.begin();
             j != i->second.end(); ++j)
          {
            lua_pushnumber(L, m++);
            lua_pushstring(L, j->c_str());
            lua_settable(L, -3);
          }
        lua_settable(L, -3);
        lua_settable(L, -3);
      }
    return 1;
  }
}

static bool 
run_string(lua_State * st, string const &str, string const & identity)
{
  I(st);
  return 
    Lua(st)
    .loadstring(str, identity)
    .call(0,1)
    .ok();
}

static bool 
run_file(lua_State * st, string const &filename)
{
  I(st);
  return 
    Lua(st)
    .loadfile(filename)
    .call(0,1)
    .ok();
}

lua_hooks::lua_hooks()
{
  st = lua_open ();  
  I(st);

  lua_atpanic (st, &panic_thrower);

  luaopen_base(st);
  luaopen_io(st);
  luaopen_string(st);
  luaopen_math(st);
  luaopen_table(st);
  luaopen_debug(st);

  // add monotone-specific functions
  lua_register(st, "mkstemp", monotone_mkstemp_for_lua);
  lua_register(st, "existsonpath", monotone_existsonpath_for_lua);
  lua_register(st, "is_executable", monotone_is_executable_for_lua);
  lua_register(st, "make_executable", monotone_make_executable_for_lua);
  lua_register(st, "spawn", monotone_spawn_for_lua);
  lua_register(st, "wait", monotone_wait_for_lua);
  lua_register(st, "kill", monotone_kill_for_lua);
  lua_register(st, "sleep", monotone_sleep_for_lua);
  lua_register(st, "guess_binary_file_contents", monotone_guess_binary_file_contents_for_lua);
  lua_register(st, "include", monotone_include_for_lua);
  lua_register(st, "includedir", monotone_includedir_for_lua);
  lua_register(st, "gettext", monotone_gettext_for_lua);
  lua_register(st, "get_confdir", monotone_get_confdir_for_lua);
  lua_register(st, "parse_basic_io", monotone_parse_basic_io_for_lua);

  // add regex functions:
  lua_newtable(st);
  lua_pushstring(st, "regex");
  lua_pushvalue(st, -2);
  lua_settable(st, LUA_GLOBALSINDEX);

  lua_pushstring(st, "search");
  lua_pushcfunction(st, monotone_regex_search_for_lua);
  lua_settable(st, -3);

  // add globish functions:
  lua_newtable(st);
  lua_pushstring(st, "globish");
  lua_pushvalue(st, -2);
  lua_settable(st, LUA_GLOBALSINDEX);

  lua_pushstring(st, "match");
  lua_pushcfunction(st, monotone_globish_match_for_lua);
  lua_settable(st, -3);

  lua_pop(st, 1);

  // Disable any functions we don't want. This is easiest
  // to do just by running a lua string.
  if (!run_string(st, 
                  "os.execute = nil "
                  "io.popen = nil ", 
                  string("<disabled dangerous functions>")))
    throw oops("lua error while disabling existing functions");
}

lua_hooks::~lua_hooks()
{
  map<lua_State*, app_state*>::iterator i = map_of_lua_to_app.find(st);
  if (st)
    lua_close (st);
  if (i != map_of_lua_to_app.end())
    map_of_lua_to_app.erase(i);
}

void
lua_hooks::set_app(app_state *_app)
{
  map_of_lua_to_app.insert(make_pair(st, _app));
}



#ifdef BUILD_UNIT_TESTS
void 
lua_hooks::add_test_hooks()
{
  if (!run_string(st, test_hooks_constant, string("<test hooks>")))
    throw oops("lua error while setting up testing hooks");
}
#endif

void 
lua_hooks::add_std_hooks()
{
  if (!run_string(st, std_hooks_constant, string("<std hooks>")))
    throw oops("lua error while setting up standard hooks");
}

void 
lua_hooks::default_rcfilename(system_path & file)
{
  map<lua_State*, app_state*>::iterator i = map_of_lua_to_app.find(st);
  I(i != map_of_lua_to_app.end());
  file = i->second->get_confdir() / "monotonerc";
}

void 
lua_hooks::working_copy_rcfilename(bookkeeping_path & file)
{
  file = bookkeeping_root / "monotonerc";
}


void
lua_hooks::load_rcfile(utf8 const & rc)
{
  I(st);
  if (rc() != "-")
    {
      fs::path locpath(system_path(rc).as_external(), fs::native);
      if (fs::exists(locpath) && fs::is_directory(locpath))
        {
          // directory, iterate over it, skipping subdirs, taking every filename,
          // sorting them and loading in sorted order
          fs::directory_iterator it(locpath);
          std::vector<fs::path> arr;
          while (it != fs::directory_iterator())
            {
              if (!fs::is_directory(*it))
                arr.push_back(*it);
              ++it;
            }
          std::sort(arr.begin(), arr.end());
          for (std::vector<fs::path>::iterator i= arr.begin(); i != arr.end(); ++i)
            {
              load_rcfile(system_path(i->native_directory_string()), true);
            }
          return; // directory read, skip the rest ...
        }
    }
  data dat;
  L(FL("opening rcfile '%s' ...\n") % rc);
  read_data_for_command_line(rc, dat);
  N(run_string(st, dat(), rc().c_str()),
    F("lua error while loading rcfile '%s'") % rc);
  L(FL("'%s' is ok\n") % rc);
}

void 
lua_hooks::load_rcfile(any_path const & rc, bool required)
{
  I(st);  
  if (path_exists(rc))
    {
      L(FL("opening rcfile '%s' ...\n") % rc);
      N(run_file(st, rc.as_external()),
        F("lua error while loading '%s'") % rc);
      L(FL("'%s' is ok\n") % rc);
    }
  else
    {
      N(!required, F("rcfile '%s' does not exist") % rc);
      L(FL("skipping nonexistent rcfile '%s'\n") % rc);
    }
}


// concrete hooks

// nb: if you're hooking lua to return your passphrase, you don't care if we
// keep a couple extra temporaries of your passphrase around.
bool 
lua_hooks::hook_get_passphrase(rsa_keypair_id const & k, string & phrase)
{
  return Lua(st)
    .func("get_passphrase")
    .push_str(k())
    .call(1,1)
    .extract_str(phrase)
    .ok();
}

bool 
lua_hooks::hook_persist_phrase_ok()
{
  bool persist_ok = false;
  bool executed_ok = Lua(st)
    .func("persist_phrase_ok")
    .call(0,1)
    .extract_bool(persist_ok)
    .ok();
  return executed_ok && persist_ok;
}

bool 
lua_hooks::hook_expand_selector(std::string const & sel, 
                                std::string & exp)
{
  return Lua(st)
    .func("expand_selector")
    .push_str(sel)
    .call(1,1)
    .extract_str(exp)
    .ok();
}

bool 
lua_hooks::hook_expand_date(std::string const & sel, 
                            std::string & exp)
{
  exp.clear();
  bool res= Lua(st)
    .func("expand_date")
    .push_str(sel)
    .call(1,1)
    .extract_str(exp)
    .ok();
  return res && exp.size();
}

bool 
lua_hooks::hook_get_branch_key(cert_value const & branchname, 
                               rsa_keypair_id & k)
{
  string key;
  bool ok = Lua(st)
    .func("get_branch_key")
    .push_str(branchname())
    .call(1,1)
    .extract_str(key)
    .ok();

  k = key;
  return ok;
}

bool 
lua_hooks::hook_get_author(cert_value const & branchname, 
                           string & author)
{
  return Lua(st)
    .func("get_author")
    .push_str(branchname())
    .call(1,1)
    .extract_str(author)
    .ok();
}

bool 
lua_hooks::hook_edit_comment(string const & commentary,
                             string const & user_log_message,
                             string & result)
{
  return Lua(st)
    .func("edit_comment")
    .push_str(commentary)
    .push_str(user_log_message)
    .call(2,1)
    .extract_str(result)
    .ok();
}

bool 
lua_hooks::hook_ignore_file(file_path const & p)
{
  bool ignore_it = false;
  bool exec_ok = Lua(st)
    .func("ignore_file")
    .push_str(p.as_external())
    .call(1,1)
    .extract_bool(ignore_it)
    .ok();
  return exec_ok && ignore_it;
}

bool 
lua_hooks::hook_ignore_branch(std::string const & branch)
{
  bool ignore_it = false;
  bool exec_ok = Lua(st)
    .func("ignore_branch")
    .push_str(branch)
    .call(1,1)
    .extract_bool(ignore_it)
    .ok();
  return exec_ok && ignore_it;
}

bool 
lua_hooks::hook_non_blocking_rng_ok()
{
  bool ok = false;
  bool exec_ok = Lua(st)
    .func("non_blocking_rng_ok")
    .call(0,1)
    .extract_bool(ok)
    .ok();
  return exec_ok && ok;
}

static inline bool
shared_trust_function_body(Lua & ll,
                           std::set<rsa_keypair_id> const & signers,
                           hexenc<id> const & id,
                           cert_name const & name,
                           cert_value const & val)
{
  ll.push_table();
  
  int k = 1;
  for (set<rsa_keypair_id>::const_iterator v = signers.begin();
       v != signers.end(); ++v)
    {
      ll.push_int(k);
      ll.push_str((*v)());
      ll.set_table();
      ++k;
    }

  bool ok;
  bool exec_ok = ll
    .push_str(id())
    .push_str(name())
    .push_str(val())
    .call(4, 1)
    .extract_bool(ok)
    .ok();
  
  return exec_ok && ok;
}

bool 
lua_hooks::hook_get_revision_cert_trust(std::set<rsa_keypair_id> const & signers,
                                       hexenc<id> const & id,
                                       cert_name const & name,
                                       cert_value const & val)
{
  Lua ll(st);
  ll.func("get_revision_cert_trust");  
  return shared_trust_function_body(ll, signers, id, name, val);
}

bool 
lua_hooks::hook_get_manifest_cert_trust(std::set<rsa_keypair_id> const & signers,
                                        hexenc<id> const & id,
                                        cert_name const & name,
                                        cert_value const & val)
{
  Lua ll(st);
  ll.func("get_manifest_cert_trust");
  return shared_trust_function_body(ll, signers, id, name, val);
}

bool 
lua_hooks::hook_accept_testresult_change(map<rsa_keypair_id, bool> const & old_results,
                                         map<rsa_keypair_id, bool> const & new_results)
{
  Lua ll(st);
  ll
    .func("accept_testresult_change")
    .push_table();

  for (map<rsa_keypair_id, bool>::const_iterator i = old_results.begin();
       i != old_results.end(); ++i)
    {
      ll.push_str(i->first());
      ll.push_bool(i->second);
      ll.set_table();
    }

  ll.push_table();

  for (map<rsa_keypair_id, bool>::const_iterator i = new_results.begin();
       i != new_results.end(); ++i)
    {
      ll.push_str(i->first());
      ll.push_bool(i->second);
      ll.set_table();
    }

  bool ok;
  bool exec_ok = ll
    .call(2, 1)
    .extract_bool(ok)
    .ok();

  return exec_ok && ok;  
}



bool 
lua_hooks::hook_merge3(file_path const & anc_path,
                       file_path const & left_path,
                       file_path const & right_path,
                       file_path const & merged_path,
                       data const & ancestor, 
                       data const & left, 
                       data const & right, 
                       data & result)
{
  string res;
  bool ok = Lua(st)
    .func("merge3")
    .push_str(anc_path.as_external())
    .push_str(left_path.as_external())
    .push_str(right_path.as_external())
    .push_str(merged_path.as_external())
    .push_str(ancestor())
    .push_str(left())
    .push_str(right())
    .call(7,1)
    .extract_str(res)
    .ok();
  result = res;
  return ok;
}

bool
lua_hooks::hook_external_diff(file_path const & path,
                              data const & data_old,
                              data const & data_new,
                              bool is_binary,
                              bool diff_args_provided,
                              std::string const & diff_args,
                              std::string const & oldrev,
                              std::string const & newrev)
{
  Lua ll(st);

  ll
    .func("external_diff")
    .push_str(path.as_external());

  if (oldrev.length() != 0)
    ll.push_str(data_old());
  else
    ll.push_nil();

  ll.push_str(data_new());

  ll.push_bool(is_binary);

  if (diff_args_provided)
    ll.push_str(diff_args);
  else
    ll.push_nil();

  ll.push_str(oldrev);
  ll.push_str(newrev);

  return ll.call(7,0).ok();
}

bool
lua_hooks::hook_use_inodeprints()
{
  bool use = false, exec_ok = false;

  exec_ok = Lua(st)
    .func("use_inodeprints")
    .call(0, 1)
    .extract_bool(use)
    .ok();
  return use && exec_ok;
}

bool 
lua_hooks::hook_get_netsync_read_permitted(std::string const & branch, 
                                           rsa_keypair_id const & identity)
{
  bool permitted = false, exec_ok = false;

  exec_ok = Lua(st)
    .func("get_netsync_read_permitted")
    .push_str(branch)
    .push_str(identity())
    .call(2,1)
    .extract_bool(permitted)
    .ok();

  return exec_ok && permitted;
}

// Anonymous no-key version
bool 
lua_hooks::hook_get_netsync_read_permitted(std::string const & branch)
{
  bool permitted = false, exec_ok = false;

  exec_ok = Lua(st)
    .func("get_netsync_read_permitted")
    .push_str(branch)
    .push_nil()
    .call(2,1)
    .extract_bool(permitted)
    .ok();

  return exec_ok && permitted;
}

bool 
lua_hooks::hook_get_netsync_write_permitted(rsa_keypair_id const & identity)
{
  bool permitted = false, exec_ok = false;

  exec_ok = Lua(st)
    .func("get_netsync_write_permitted")
    .push_str(identity())
    .call(1,1)
    .extract_bool(permitted)
    .ok();

  return exec_ok && permitted;  
}

bool 
lua_hooks::hook_init_attributes(file_path const & filename,
                                std::map<std::string, std::string> & attrs)
{
  Lua ll(st);

  ll
    .push_str("attr_init_functions")
    .get_tab();
  
  L(FL("calling attr_init_function for %s") % filename);
  ll.begin();
  while (ll.next())
    {
      L(FL("  calling an attr_init_function for %s") % filename);
      ll.push_str(filename.as_external());
      ll.call(1, 1);

      if (lua_isstring(st, -1))
        {
          string key, value;

          ll.extract_str(value);
          ll.pop();
          ll.extract_str(key);

          attrs[key] = value;
          L(FL("  added attr %s = %s") % key % value);
        }
      else
        {
          L(FL("  no attr added"));
          ll.pop();
        }
    }

  return ll.pop().ok();
}

bool 
lua_hooks::hook_apply_attribute(string const & attr, 
                                file_path const & filename, 
                                string const & value)
{
  return Lua(st)
    .push_str("attr_functions")
    .get_tab()
    .push_str(attr)
    .get_fn(-2)
    .push_str(filename.as_external())
    .push_str(value)
    .call(2,0)
    .ok();
}


bool 
lua_hooks::hook_get_system_linesep(string & linesep)
{
  return Lua(st)
    .func("get_system_linesep")
    .call(0,1)
    .extract_str(linesep)
    .ok();
}

bool 
lua_hooks::hook_get_charset_conv(file_path const & p, 
                                 std::string & db, 
                                 std::string & ext)
{
  Lua ll(st);
  ll
    .func("get_charset_conv")
    .push_str(p.as_external())
    .call(1,1)
    .begin();
  
  ll.next();
  ll.extract_str(db).pop();

  ll.next();
  ll.extract_str(ext).pop();
  return ll.ok();
}

bool 
lua_hooks::hook_get_linesep_conv(file_path const & p, 
                                 std::string & db, 
                                 std::string & ext)
{
  Lua ll(st);
  ll
    .func("get_linesep_conv")
    .push_str(p.as_external())
    .call(1,1)
    .begin();
  
  ll.next();
  ll.extract_str(db).pop();

  ll.next();
  ll.extract_str(ext).pop();
  return ll.ok();
}

bool 
lua_hooks::hook_note_commit(revision_id const & new_id,
                            revision_data const & rdat,
                            map<cert_name, cert_value> const & certs)
{
  Lua ll(st);
  ll
    .func("note_commit")
    .push_str(new_id.inner()())
    .push_str(rdat.inner()());

  ll.push_table();

  for (map<cert_name, cert_value>::const_iterator i = certs.begin();
       i != certs.end(); ++i)
    {
      ll.push_str(i->first());
      ll.push_str(i->second());
      ll.set_table();
    }
  
  ll.call(3, 0);
  return ll.ok();
}

bool 
lua_hooks::hook_note_netsync_revision_received(revision_id const & new_id,
                                               revision_data const & rdat,
                            set<pair<rsa_keypair_id,
                                     pair<cert_name,
                                          cert_value> > > const & certs)
{
  Lua ll(st);
  ll
    .func("note_netsync_revision_received")
    .push_str(new_id.inner()())
    .push_str(rdat.inner()());

  ll.push_table();
  
  typedef set<pair<rsa_keypair_id, pair<cert_name, cert_value> > > cdat;

  int n=0;
  for (cdat::const_iterator i = certs.begin(); i != certs.end(); ++i)
    {
      ll.push_int(n++);
      ll.push_table();
      ll.push_str("key");
      ll.push_str(i->first());
      ll.set_table();
      ll.push_str("name");
      ll.push_str(i->second.first());
      ll.set_table();
      ll.push_str("value");
      ll.push_str(i->second.second());
      ll.set_table();
      ll.set_table();
    }

  ll.call(3, 0);
  return ll.ok();
}

bool
lua_hooks::hook_note_netsync_pubkey_received(rsa_keypair_id const & kid)
{
  Lua ll(st);
  ll
    .func("note_netsync_pubkey_received")
    .push_str(kid());

  ll.call(1, 0);
  return ll.ok();
}

bool
lua_hooks::hook_note_netsync_cert_received(revision_id const & rid,
                                           rsa_keypair_id const & kid,
                                           cert_name const & name,
                                           cert_value const & value)
{
  Lua ll(st);
  ll
    .func("note_netsync_cert_received")
    .push_str(rid.inner()())
    .push_str(kid())
    .push_str(name())
    .push_str(value());

  ll.call(4, 0);
  return ll.ok();
}
