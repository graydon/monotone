

#include "config.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}
#include <errno.h>
#include <signal.h>
//#include <stdlib.h>
//#include <string.h>
//#include <stdarg.h>

#include "lua.hh"

#include "sanity.hh"
#include "platform.hh"
#include "mkstemp.hh"
#include "globish.hh"
#include "basic_io.hh"
#include "file_io.hh"

#include <string>
#include <fstream>
#include <set>
#include <vector>
#include <utility>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/regex.hpp>

using std::ifstream;
using std::ios_base;
using std::pair;
using std::set;
using std::sort;
using std::string;
using std::vector;
using std::strerror;
using std::malloc;
using std::free;

// adapted from "programming in lua", section 24.2.3
// http://www.lua.org/pil/24.2.3.html
// output is from bottom (least accessible) to top (most accessible, where
// push and pop happen).
static string
dump_stack(lua_State * st)
{
  string out;
  int i;
  int top = lua_gettop(st);
  for (i = 1; i <= top; i++) {  /* repeat for each level */
    int t = lua_type(st, i);
    switch (t) {
    case LUA_TSTRING:  /* strings */
      out += '`';
      out += string(lua_tostring(st, i), lua_strlen(st, i));
      out += '\'';
      break;

    case LUA_TBOOLEAN:  /* booleans */
      out += (lua_toboolean(st, i) ? "true" : "false");
      break;

    case LUA_TNUMBER:  /* numbers */
      out += (FL("%g") % lua_tonumber(st, i)).str();
      break;

    default:  /* other values */
      out += std::string(lua_typename(st, t));
      break;

    }
    out += "  ";  /* put a separator */
  }
  return out;
}


Lua::Lua(lua_State * s) :
  st(s), failed(false)
{}

Lua::~Lua()
{
  lua_settop(st, 0);
}

void
Lua::fail(string const & reason)
{
  L(FL("lua failure: %s; stack = %s") % reason % dump_stack(st));
  failed = true;
}

bool
Lua::ok()
{
  if (failed)
    L(FL("Lua::ok(): failed"));
  return !failed;
}

void
Lua::report_error()
{
  I(lua_isstring(st, -1));
  string err = string(lua_tostring(st, -1), lua_strlen(st, -1));
  W(i18n_format("%s") % err);
  L(FL("lua stack: %s") % dump_stack(st));
  lua_pop(st, 1);
  failed = true;
}

// getters

Lua &
Lua::get(int idx)
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

Lua &
Lua::get_fn(int idx)
{
  if (failed) return *this;
  get(idx);
  if (!lua_isfunction (st, -1))
    fail("isfunction() in get_fn");
  return *this;
}

Lua &
Lua::get_tab(int idx)
{
  if (failed) return *this;
  get(idx);
  if (!lua_istable (st, -1))
    fail("istable() in get_tab");
  return *this;
}

Lua &
Lua::get_str(int idx)
{
  if (failed) return *this;
  get(idx);
  if (!lua_isstring (st, -1))
    fail("isstring() in get_str");
  return *this;
}

Lua &
Lua::get_num(int idx)
{
  if (failed) return *this;
  get(idx);
  if (!lua_isnumber (st, -1))
    fail("isnumber() in get_num");
  return *this;
}

Lua &
Lua::get_bool(int idx)
{
  if (failed) return *this;
  get(idx);
  if (!lua_isboolean (st, -1))
    fail("isboolean() in get_bool");
  return *this;
}

// extractors

Lua &
Lua::extract_str_nolog(string & str)
{
  if (failed) return *this;
  if (!lua_isstring (st, -1))
    {
      fail("isstring() in extract_str");
      return *this;
    }
  str = string(lua_tostring(st, -1), lua_strlen(st, -1));
  return *this;
}

Lua &
Lua::extract_str(string & str)
{
  extract_str_nolog(str);
  L(FL("lua: extracted string = %s") % str);
  return *this;
}

Lua &
Lua::extract_classified_str(string & str)
{
  extract_str_nolog(str);
  L(FL("lua: extracted string [CLASSIFIED]"));
  return *this;
}

Lua &
Lua::extract_int(int & i)
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

Lua &
Lua::extract_double(double & i)
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


Lua &
Lua::extract_bool(bool & i)
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

Lua &
Lua::begin()
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

bool
Lua::next()
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

Lua &
Lua::push_str(string const & str)
{
  if (failed) return *this;
  I(lua_checkstack (st, 1));
  lua_pushlstring(st, str.c_str(), str.size());
  return *this;
}

Lua &
Lua::push_int(int num)
{
  if (failed) return *this;
  I(lua_checkstack (st, 1));
  lua_pushnumber(st, num);
  return *this;
}

Lua &
Lua::push_int(double num)
{
  if (failed) return *this;
  I(lua_checkstack (st, 1));
  lua_pushnumber(st, num);
  return *this;
}

Lua &
Lua::push_bool(bool b)
{
  if (failed) return *this;
  I(lua_checkstack (st, 1));
  lua_pushboolean(st, b);
  return *this;
}

Lua &
Lua::push_nil()
{
  if (failed) return *this;
  I(lua_checkstack (st, 1));
  lua_pushnil(st);
  return *this;
}

Lua &
Lua::push_table()
{
  if (failed) return *this;
  I(lua_checkstack (st, 1));
  lua_newtable(st);
  return *this;
}

Lua &
Lua::set_table(int idx)
{
  if (failed) return *this;
  I(lua_checkstack (st, 1));
  lua_settable(st, idx);
  return *this;
}

Lua &
Lua::call(int in, int out)
{
  if (failed) return *this;
  I(lua_checkstack (st, out));
  if (lua_pcall(st, in, out, 0) != 0)
    {
      report_error();
    }
  return *this;
}

Lua &
Lua::pop(int count)
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

Lua &
Lua::func(string const & fname)
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

Lua &
Lua::loadstring(string const & str, string const & identity)
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

Lua &
Lua::loadfile(string const & filename)
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

set<string> Lua::missing_functions;




extern "C"
{
  static int
  monotone_mkstemp_for_lua(lua_State *L)
  {
    int fd = -1;
    FILE **pf = NULL;
    char const *filename = luaL_checkstring (L, -1);
    string dup(filename);

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
    const char *exe = luaL_checkstring(L, -1);
    lua_pushnumber(L, existsonpath(exe));
    return 1;
  }

  static int
  monotone_is_executable_for_lua(lua_State *L)
  {
    const char *path = luaL_checkstring(L, -1);
    lua_pushboolean(L, is_executable(path));
    return 1;
  }

  static int
  monotone_make_executable_for_lua(lua_State *L)
  {
    const char *path = luaL_checkstring(L, -1);
    lua_pushnumber(L, make_executable(path));
    return 1;
  }

  static int
  monotone_spawn_for_lua(lua_State *L)
  {
    int n = lua_gettop(L);
    const char *path = luaL_checkstring(L, -n);
    char **argv = (char**)malloc((n+1)*sizeof(char*));
    int i;
    pid_t ret;
    if (argv==NULL)
      return 0;
    argv[0] = (char*)path;
    for (i=1; i<n; i++) argv[i] = (char*)luaL_checkstring(L, -(n - i));
    argv[i] = NULL;
    ret = process_spawn(argv);
    free(argv);
    lua_pushnumber(L, ret);
    return 1;
  }

  static int
  monotone_wait_for_lua(lua_State *L)
  {
    pid_t pid = static_cast<pid_t>(luaL_checknumber(L, -1));
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
    pid_t pid = static_cast<pid_t>(luaL_checknumber(L, -2));
    int sig;
    if (n>1)
      sig = static_cast<int>(luaL_checknumber(L, -1));
    else
      sig = SIGTERM;
    lua_pushnumber(L, process_kill(pid, sig));
    return 1;
  }

  static int
  monotone_sleep_for_lua(lua_State *L)
  {
    int seconds = static_cast<int>(luaL_checknumber(L, -1));
    lua_pushnumber(L, process_sleep(seconds));
    return 1;
  }

  static int
  monotone_guess_binary_file_contents_for_lua(lua_State *L)
  {
    const char *path = luaL_checkstring(L, -1);
    N(path, F("%s called with an invalid parameter") % "guess_binary");

    ifstream file(path, ios_base::binary);
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
    const char *path = luaL_checkstring(L, -1);
    N(path, F("%s called with an invalid parameter") % "Include");

    bool res =Lua(L)
    .loadfile(string(path, lua_strlen(L, -1)))
    .call(0,1)
    .ok();

    lua_pushboolean(L, res);
    return 1;
  }

  static int
  monotone_includedir_for_lua(lua_State *L)
  {
    const char *pathstr = luaL_checkstring(L, -1);
    N(pathstr, F("%s called with an invalid parameter") % "IncludeDir");

    fs::path locpath(pathstr, fs::native);
    N(fs::exists(locpath), F("Directory '%s' does not exist") % pathstr);
    N(fs::is_directory(locpath), F("'%s' is not a directory") % pathstr);

    // directory, iterate over it, skipping subdirs, taking every filename,
    // sorting them and loading in sorted order
    fs::directory_iterator it(locpath);
    vector<fs::path> arr;
    while (it != fs::directory_iterator())
      {
        if (!fs::is_directory(*it))
          arr.push_back(*it);
        ++it;
      }
    sort(arr.begin(), arr.end());
    for (vector<fs::path>::iterator i= arr.begin(); i != arr.end(); ++i)
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
    const char *re = luaL_checkstring(L, -2);
    const char *str = luaL_checkstring(L, -1);
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
    const char *re = luaL_checkstring(L, -2);
    const char *str = luaL_checkstring(L, -1);

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
    const char *msgid = luaL_checkstring(L, -1);
    lua_pushstring(L, gettext(msgid));
    return 1;
  }

  static int
  monotone_parse_basic_io_for_lua(lua_State *L)
  {
    vector<pair<string, vector<string> > > res;
    const string str(luaL_checkstring(L, -1), lua_strlen(L, -1));
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

bool
run_string(lua_State * st, string const &str, string const & identity)
{
  I(st);
  return
    Lua(st)
    .loadstring(str, identity)
    .call(0,1)
    .ok();
}

bool
run_file(lua_State * st, string const &filename)
{
  I(st);
  return
    Lua(st)
    .loadfile(filename)
    .call(0,1)
    .ok();
}

void
add_functions(lua_State * st)
{
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
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
