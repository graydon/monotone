// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "config.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
}

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <boost/lexical_cast.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>

#include <set>
#include <map>

#include "lua.hh"
#include "sanity.hh"
#include "vocab.hh"
#include "file_io.hh"
#include "app_state.hh"

// defined in {std,test}_hooks.lua, converted
#include "test_hooks.h"
#include "std_hooks.h"

using namespace std;
using boost::lexical_cast;

/*
static int panic_thrower(lua_State * st)
{
  throw oops("lua panic");
}
*/

lua_hooks::lua_hooks()
{
  st = lua_open ();  
  I(st);

  // no atpanic support in 4.x
  // lua_atpanic (st, &panic_thrower);

  lua_baselibopen(st);
  lua_iolibopen(st);
  lua_strlibopen(st);
  lua_mathlibopen(st);
  lua_tablibopen(st);
  lua_dblibopen(st);
}

lua_hooks::~lua_hooks()
{
  if (st)
    lua_close (st);
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

  Lua(lua_State * s) : 
    st(s), failed(false)
  {}
  
  ~Lua()
  {
    lua_settop(st, 0);
  }

  bool ok() 
  { 
    return !failed; 
  }
 
  // getters

  Lua & get(int idx = LUA_GLOBALSINDEX) 
  { 
    if (failed) return *this;
    if (!lua_istable (st, idx)) 
      { 
	L(F("lua istable() failed\n")); 
	failed = true; 
	return *this; 
      }
    if (lua_gettop (st) < 1) 
      { 
	L(F("lua stack top > 0 failed\n")); 
	failed = true; 
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
      { 
	L(F("lua isfunction() failed in get_fn\n")); 
	failed = true; 
      }
    return *this; 
  }

  Lua & get_tab(int idx = LUA_GLOBALSINDEX) 
  { 
    if (failed) return *this;
    get(idx);
    if (!lua_istable (st, -1)) 
      { 
	L(F("lua istable() failed in get_tab\n")); 
	failed = true; 
      }
    return *this; 
  }

  Lua & get_str(int idx = LUA_GLOBALSINDEX) 
  { 
    if (failed) return *this;
    get(idx);
    if (!lua_isstring (st, -1)) 
      { 
	L(F("lua isstring() failed in get_str\n")); 
	failed = true; 
      }
    return *this; 
  }

  Lua & get_num(int idx = LUA_GLOBALSINDEX) 
  { 
    if (failed) return *this;
    get(idx);
    if (!lua_isnumber (st, -1)) 
      { 
	L(F("lua isnumber() failed in get_num\n")); 
	failed = true; 
      }
    return *this; 
  }

  Lua & get_bool(int idx = LUA_GLOBALSINDEX) 
  { 
    if (failed) return *this;
    get(idx);
    if (!lua_isboolean (st, -1)) 
      { 
	L(F("lua isboolean() failed in get_bool\n")); 
	failed = true; 
      }
    return *this; 
  }

  // extractors

  Lua & extract_str(string & str)
  {
    if (failed) return *this;
    if (!lua_isstring (st, -1)) 
      { 
	L(F("lua isstring() failed in extract_str\n")); 
	failed = true; 
	return *this;
      }
    str = string(lua_tostring(st, -1), lua_strlen(st, -1));
    return *this;
  }

  Lua & extract_int(int & i)
  {
    if (failed) return *this;
    if (!lua_isnumber (st, -1)) 
      { 
	L(F("lua isnumber() failed in extract_int\n")); 
	failed = true; 
	return *this;
      }
    i = static_cast<int>(lua_tonumber(st, -1));
    return *this;
  }

  Lua & extract_double(double & i)
  {
    if (failed) return *this;
    if (!lua_isnumber (st, -1)) 
      { 
	L(F("lua isnumber() failed in extract_double\n")); 
	failed = true; 
	return *this;
      }
    i = lua_tonumber(st, -1);
    return *this;
  }


  Lua & extract_bool(bool & i)
  {
    if (failed) return *this;
    if (!lua_isboolean (st, -1)) 
      { 
	L(F("lua isboolean() failed in extract_bool\n")); 
	failed = true; 
	return *this;
      }
    i = (lua_toboolean(st, -1) == 1);
    return *this;
  }


  // table iteration

  Lua & begin()
  {
    if (failed) return *this;
    if (!lua_istable(st, -1)) 
      { 
	L(F("lua istable() failed in begin\n")); 
	failed = true; 
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
	L(F("lua istable() failed in next\n")); 
	failed = true; 
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
	I(lua_isstring (st, -1));
	string err = string(lua_tostring(st, -1), lua_strlen(st, -1));
	L(F("lua pcall() failed: %s\n") % err); 
	lua_pop(st, 1); 
	failed = true; 
      } 
    return *this; 
  }

  Lua & pop(int count = 1) 
  { 
    if (failed) return *this;
    if (lua_gettop (st) < count) 
      { 
	L(F("lua stack top >= count failed\n")); 
	failed = true; 
	return *this; 
      }
    lua_pop(st, count); 
    return *this; 
  }
};

static bool 
run_string(lua_State * st, string const &str)
{
  I(st);
  return 
    Lua(st)
    .push_str("loadstring")
    .get_fn()
    .push_str(str)
    .call(1,1)
    .call(0,0)
    .ok();
}

static bool 
run_file(lua_State * st, string const &filename)
{
  I(st);
  return 
    Lua(st)
    .push_str("loadfile")
    .get_fn()
    .push_str(filename)
    .call(1,1)
    .call(0,0)
    .ok();
}


#ifdef BUILD_UNIT_TESTS
void 
lua_hooks::add_test_hooks()
{
  if (!run_string(st, test_hooks_constant))
    throw oops("lua error while setting up testing hooks");
}
#endif

void 
lua_hooks::add_std_hooks()
{
  if (!run_string(st, std_hooks_constant))
    throw oops("lua error while setting up standard hooks");
}

void 
lua_hooks::default_rcfilename(fs::path & file)
{
  file = mkpath(get_homedir()) / mkpath(".monotonerc");
}

void 
lua_hooks::working_copy_rcfilename(fs::path & file)
{
  file = mkpath(book_keeping_dir) / mkpath("monotonerc");
}


void 
lua_hooks::add_rcfile(fs::path const & rc)
{
  I(st);  
  if (fs::exists(rc))
    {
      L(F("opening rcfile '%s' ...\n") % rc.string());
      if (!run_file(st, rc.string()))
	{
	  L(F("'%s' is no good\n") % rc.string());
	  throw oops("lua error while loading " + rc.string());
	}
      L(F("'%s' is ok\n") % rc.string());
    }
  else
    L(F("skipping nonexistent rcfile '%s'\n") % rc.string());
}


// concrete hooks

// nb: if you're hooking lua to return your passphrase, you don't care if we
// keep a couple extra temporaries of your passphrase around.
bool 
lua_hooks::hook_get_passphrase(rsa_keypair_id const & k, string & phrase)
{
  return Lua(st)
    .push_str("get_passphrase")
    .get_fn()
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
    .push_str("persist_phrase_ok")
    .get_fn()
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
    .push_str("expand_selector")
    .get_fn()
    .push_str(sel)
    .call(1,1)
    .extract_str(exp)
    .ok();
}

bool 
lua_hooks::hook_get_branch_key(cert_value const & branchname, 
			       rsa_keypair_id & k)
{
  string key;
  bool ok = Lua(st)
    .push_str("get_branch_key")
    .get_fn()
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
    .push_str("get_author")
    .get_fn()
    .push_str(branchname())
    .call(1,1)
    .extract_str(author)
    .ok();
}

bool 
lua_hooks::hook_edit_comment(string const & commentary, 
			     string & result)
{
  return Lua(st)
    .push_str("edit_comment")
    .get_fn()
    .push_str(commentary)
    .call(1,1)
    .extract_str(result)
    .ok();
}


bool 
lua_hooks::hook_get_sorter(string const & certname, 
			   string & sort_type)
{
  return Lua(st)
    .push_str("get_sorter")
    .get_fn()
    .push_str(certname)
    .call(1,1)
    .extract_str(sort_type)
    .ok();
}

bool 
lua_hooks::hook_ignore_file(file_path const & p)
{
  bool ignore_it = false;
  bool exec_ok = Lua(st)
    .push_str("ignore_file")
    .get_fn()
    .push_str(p())
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
    .push_str("non_blocking_rng_ok")
    .get_fn()
    .call(0,1)
    .extract_bool(ok)
    .ok();
  return exec_ok && ok;
}

bool 
lua_hooks::hook_get_manifest_cert_trust(std::set<rsa_keypair_id> const & signers,
					hexenc<id> const & id,
					cert_name const & name,
					cert_value const & val)
{
  Lua ll(st);
  ll
    .push_str("get_manifest_cert_trust")
    .get_fn()
    .push_table();

  int k = 0;
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
lua_hooks::hook_get_file_cert_trust(std::set<rsa_keypair_id> const & signers,
				    hexenc<id> const & id,
				    cert_name const & name,
				    cert_value const & val)
{
  Lua ll(st);
  ll
    .push_str("get_file_cert_trust")
    .get_fn()
    .push_table();

  int k = 0;
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
lua_hooks::hook_accept_testresult_change(map<rsa_keypair_id, bool> const & old_results,
					 map<rsa_keypair_id, bool> const & new_results)
{
  Lua ll(st);
  ll
    .push_str("accept_testresult_change")
    .get_fn()
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
lua_hooks::hook_merge2(data const & left, 
		       data const & right, 
		       data & result)
{
  string res;
  bool ok = Lua(st)
    .push_str("merge2")
    .get_fn()
    .push_str(left())
    .push_str(right())
    .call(2,1)
    .extract_str(res)
    .ok();
  result = res;
  return ok;
}

bool 
lua_hooks::hook_merge3(data const & ancestor, 
		       data const & left, 
		       data const & right, 
		       data & result)
{
  string res;
  bool ok = Lua(st)
    .push_str("merge3")
    .get_fn()
    .push_str(ancestor())
    .push_str(left())
    .push_str(right())
    .call(3,1)
    .extract_str(res)
    .ok();
  result = res;
  return ok;
}

bool 
lua_hooks::hook_get_netsync_read_permitted(std::string const & collection, 
					   rsa_keypair_id const & identity)
{
  bool permitted = false, exec_ok = false;

  exec_ok = Lua(st)
    .push_str("get_netsync_read_permitted")
    .get_fn()
    .push_str(collection)
    .push_str(identity())
    .call(2,1)
    .extract_bool(permitted)
    .ok();

  return exec_ok && permitted;
}

bool 
lua_hooks::hook_get_netsync_anonymous_read_permitted(std::string const & collection)
{
  bool permitted = false, exec_ok = false;

  exec_ok = Lua(st)
    .push_str("get_netsync_anonymous_read_permitted")
    .get_fn()
    .push_str(collection)
    .call(1,1)
    .extract_bool(permitted)
    .ok();

  return exec_ok && permitted;
}

bool 
lua_hooks::hook_get_netsync_write_permitted(std::string const & collection, 
					    rsa_keypair_id const & identity)
{
  bool permitted = false, exec_ok = false;

  exec_ok = Lua(st)
    .push_str("get_netsync_write_permitted")
    .get_fn()
    .push_str(collection)
    .push_str(identity())
    .call(2,1)
    .extract_bool(permitted)
    .ok();

  return exec_ok && permitted;  
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
    .push_str(filename())
    .push_str(value)
    .call(2,0)
    .ok();
}


bool 
lua_hooks::hook_get_system_linesep(string & linesep)
{
  return Lua(st)
    .push_str("get_system_linesep")
    .get_fn()
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
    .push_str("get_charset_conv")
    .get_fn()
    .push_str(p())
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
    .push_str("get_linesep_conv")
    .get_fn()
    .push_str(p())
    .call(1,1)
    .begin();
  
  ll.next();
  ll.extract_str(db).pop();

  ll.next();
  ll.extract_str(ext).pop();
  return ll.ok();
}
