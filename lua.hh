#ifndef __LUA_HH__
#define __LUA_HH__

#include <set>
#include <string>

struct lua_State;

// This Lua object represents a single imperative transaction with the lua
// interpreter. if it fails at any point, all further commands in the
// transaction are ignored. it cleans the lua stack up when it is
// destructed, so no need to pop values when you're done.

struct
Lua
{
  lua_State * st;
  bool failed;
  static std::set<std::string> missing_functions;

  Lua(lua_State * s);
  ~Lua();

  void fail(std::string const & reason);
  bool ok();
  void report_error();

  // getters
  Lua & get(int idx = LUA_GLOBALSINDEX);
  Lua & get_fn(int idx = LUA_GLOBALSINDEX);
  Lua & get_tab(int idx = LUA_GLOBALSINDEX);
  Lua & get_str(int idx = LUA_GLOBALSINDEX);
  Lua & get_num(int idx = LUA_GLOBALSINDEX);
  Lua & get_bool(int idx = LUA_GLOBALSINDEX);

  // extractors
  Lua & extract_str_nolog(std::string & str);
  Lua & extract_str(std::string & str);
  Lua & extract_classified_str(std::string & str);
  Lua & extract_int(int & i);
  Lua & extract_double(double & i);
  Lua & extract_bool(bool & i);

  // table iteration
  Lua & begin();
  bool next();

  // pushers
  Lua & push_str(std::string const & str);
  Lua & push_int(int num);
  Lua & push_int(double num);
  Lua & push_bool(bool b);
  Lua & push_nil();
  Lua & push_table();
  Lua & set_table(int idx = -3);
  Lua & call(int in, int out);
  Lua & pop(int count = 1);
  Lua & func(std::string const & fname);
  Lua & loadstring(std::string const & str, std::string const & identity);
  Lua & loadfile(std::string const & filename);
};

bool run_string(lua_State * st,
                std::string const &str,
                std::string const & identity);
bool run_file(lua_State * st, std::string const &filename);
void add_functions(lua_State * st);

#endif
