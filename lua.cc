

#include "base.hh"
#include "lua.hh"

#include "globish.hh"
#include "sanity.hh"
#include "platform.hh"
#include "pcrewrap.hh"

#include <set>
#include "vector.hh"
#include <utility>
#include <cstring> // strlen
#include <algorithm> // std::sort

using std::pair;
using std::set;
using std::sort;
using std::string;
using std::vector;
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
//  I(lua_isstring(st, -1));
  string err = string(lua_tostring(st, -1), lua_strlen(st, -1));
  W(i18n_format("%s") % err);
  L(FL("lua stack: %s") % dump_stack(st));
  lua_pop(st, 1);
  failed = true;
}

bool
Lua::check_stack(int count)
{
  if (!lua_checkstack(st, count))
    {
      fail((FL("lua stack limit '%d' reached") % LUAI_MAXCSTACK).str());
      return false;
    }
  return true;
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
  i = lua_tointeger(st, -1);
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
  check_stack(1);
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
  if (!check_stack(1)) return false;
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
  if (!check_stack(1)) return *this;
  lua_pushlstring(st, str.c_str(), str.size());
  return *this;
}

Lua &
Lua::push_int(int num)
{
  if (failed) return *this;
  if (!check_stack(1)) return *this;
  lua_pushnumber(st, num);
  return *this;
}

Lua &
Lua::push_double(double num)
{
  if (failed) return *this;
  if (check_stack(1)) return *this;
  lua_pushnumber(st, num);
  return *this;
}

Lua &
Lua::push_bool(bool b)
{
  if (failed) return *this;
  if (!check_stack(1)) return *this;
  lua_pushboolean(st, b);
  return *this;
}

Lua &
Lua::push_nil()
{
  if (failed) return *this;
  if (!check_stack(1)) return *this;
  lua_pushnil(st);
  return *this;
}

Lua &
Lua::push_table()
{
  if (failed) return *this;
  if (!check_stack(1)) return *this;
  lua_newtable(st);
  return *this;
}

Lua &
Lua::set_table(int idx)
{
  if (failed) return *this;
  if (!check_stack(1)) return *this;
  lua_settable(st, idx);
  return *this;
}

Lua &
Lua::set_field(const string& key, int idx)
{
  if (failed) return *this;
  lua_setfield(st, idx, key.c_str());
  return *this;
}

Lua &
Lua::call(int in, int out)
{
  if (failed) return *this;
  if (!check_stack(out)) return *this;
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
Lua::loadstring(char const * str, char const * identity)
{
  if (!failed)
    {
      if (luaL_loadbuffer(st, str, strlen(str), identity))
        {
          report_error();
        }
    }
  return *this;
}

Lua &
Lua::loadfile(char const * filename)
{
  if (!failed)
    {
      if (luaL_loadfile(st, filename))
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
          lua_pushvalue(st, -1);
          lua_setfield(st, LUA_GLOBALSINDEX, table.c_str());
        }
      for (luaext::fmap::const_iterator j = i->second.begin();
           j != i->second.end(); ++j)
        {
          if (table == "")
            lua_register(st, j->first.c_str(), j->second);
          else
            {
              lua_pushcfunction(st, j->second);
              lua_setfield(st, -2, j->first.c_str());
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

  bool res = run_file(L, path);

  lua_pushboolean(L, res);
  return 1;
}

LUAEXT(includedir, )
{
  const char *pathstr = luaL_checkstring(L, -1);
  N(pathstr, F("%s called with an invalid parameter") % "IncludeDir");

  run_directory(L, pathstr, "*");
  lua_pushboolean(L, true);
  return 1;
}

LUAEXT(includedirpattern, )
{
  const char *pathstr = luaL_checkstring(L, -2);
  const char *pattern = luaL_checkstring(L, -1);
  N(pathstr && pattern,
    F("%s called with an invalid parameter") % "IncludeDirPattern");

  run_directory(L, pathstr, pattern);
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
  } catch (informative_failure & e) {
    lua_pushstring(L, e.what());
    return lua_error(L);
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
run_string(lua_State * st, char const * str, char const * identity)
{
  I(st);
  return
    Lua(st)
    .loadstring(str, identity)
    .call(0,1)
    .ok();
}

bool
run_file(lua_State * st, char const * filename)
{
  I(st);
  return
    Lua(st)
    .loadfile(filename)
    .call(0,1)
    .ok();
}

namespace
{
  struct ignore_directories : public dirent_consumer
  { virtual void consume(const char *) {} };
  struct record_if_matches : public dirent_consumer
  {
    record_if_matches(string const & b, char const * p,
                      vector<string> & t)
      : base(b + "/"), glob(p), target(t)
    { target.clear(); }

    virtual void consume(const char * component)
    {
      if (glob.matches(component))
        target.push_back(base + component);
    }
  private:
    string base;
    globish glob;
    vector<string> & target;
  };
}

// ??? should maybe deal in system_paths and use read_directory.
void
run_directory(lua_State * st, char const * pathstr, char const * pattern)
{
  string path(pathstr);
  switch (get_path_status(path))
    {
    case path::nonexistent:
      N(false, F("Directory '%s' does not exist") % pathstr);
    case path::file:
      N(false, F("'%s' is not a directory") % pathstr);
    case path::directory:
      break;
    }

  // directory, iterate over it, skipping subdirs, taking every filename
  // matching the pattern, sorting them and loading in sorted order
  vector<string> arr;
  {
    ignore_directories id;
    record_if_matches rim(path, pattern, arr);
    do_read_directory(path, rim, id, id);
  }

  sort(arr.begin(), arr.end());
  for (vector<string>::iterator i= arr.begin(); i != arr.end(); ++i)
    {
      L(FL("opening rcfile '%s'") % *i);
      bool res = Lua(st)
        .loadfile(i->c_str())
        .call(0,1)
        .ok();
      N(res, F("lua error while loading rcfile '%s'") % *i);
      L(FL("'%s' is ok") % *i);
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
