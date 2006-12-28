
#include "config.h"

#include "lua.hh"

#include "globish.hh"
#include "sanity.hh"
#include "pcrewrap.hh"

#include <string>
#include <set>
#include <vector>
#include <utility>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>

using std::pair;
using std::set;
using std::sort;
using std::string;
using std::vector;
using std::strerror;
using std::malloc;
using std::free;
namespace fs = boost::filesystem;

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
//  I(lua_isstring(st, -1));
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
Lua::push_double(double num)
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

luaext::ftmap * luaext::fns;

luaext::extfn::extfn(std::string const & name, std::string const & table, int (*func) (lua_State *))
{
  static bool first(true);
  if (first)
    {
      first = false;
      fns = new ftmap;
    }
  (*fns)[table].insert(make_pair(name, func));
}

void add_functions(lua_State * st)
{
  for (luaext::ftmap::const_iterator i = luaext::fns->begin();
       i != luaext::fns->end(); ++i)
    {
      std::string const & table(i->first);
      if (table != "")
        {
          lua_newtable(st);
          lua_pushstring(st, table.c_str());
          lua_pushvalue(st, -2);
          lua_settable(st, LUA_GLOBALSINDEX);
        }
      for (luaext::fmap::const_iterator j = i->second.begin();
           j != i->second.end(); ++j)
        {
          if (table == "")
            lua_register(st, j->first.c_str(), j->second);
          else
            {
              lua_pushstring(st, j->first.c_str());
              lua_pushcfunction(st, j->second);
              lua_settable(st, -3);
            }
        }
      if (table != "")
        lua_pop(st, 1);
    }
}

LUAEXT(include, )
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

LUAEXT(includedir, )
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

LUAEXT(includedirpattern, )
{
  const char *pathstr = luaL_checkstring(L, -2);
  const char *pattern = luaL_checkstring(L, -1);
  N(pathstr && pattern,
    F("%s called with an invalid parameter") % "IncludeDirPattern");

  fs::path locpath(pathstr, fs::native);
  N(fs::exists(locpath), F("Directory '%s' does not exist") % pathstr);
  N(fs::is_directory(locpath), F("'%s' is not a directory") % pathstr);

  // directory, iterate over it, skipping subdirs, taking every filename
  // matching the pattern, sorting them and loading in sorted order
  fs::directory_iterator it(locpath);
  string r(pattern);
  string n;
  globish_matcher glob(r, n);
  vector<fs::path> arr;
  while (it != fs::directory_iterator())
    {
      if (!fs::is_directory(*it) && glob(it->string()))
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

LUAEXT(search, regex)
{
  const char *re = luaL_checkstring(L, -2);
  const char *str = luaL_checkstring(L, -1);

  bool result = false;
  try {
    result = pcre::regex(re).match(str);
  } catch (pcre::compile_error & e) {
    return luaL_error(L, e.what());
  } catch (pcre::match_error & e) {
    return luaL_error(L, e.what());
  }
  lua_pushboolean(L, result);
  return 1;
}

LUAEXT(gettext, )
{
  const char *msgid = luaL_checkstring(L, -1);
  lua_pushstring(L, gettext(msgid));
  return 1;
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

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
