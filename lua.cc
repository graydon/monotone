// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "config.h"

extern "C" {
#include LUA_H
#include LUALIB_H
}

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <boost/lexical_cast.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>

#include <vector>
#include <map>

#include "lua.hh"
#include "sanity.hh"
#include "vocab.hh"
#include "file_io.hh"
#include "app_state.hh"

// defined in {std,test}_hooks.lua, converted
#include "test_hooks.h"
#include "std_hooks.h"

using std::string;
using boost::lexical_cast;

static int panic_thrower(lua_State * st)
{
  throw oops("lua panic");
}

lua_hooks::lua_hooks()
{
  st = lua_open (0);  
  I(st);

  // no atpanic support in 4.x
  // lua_atpanic (st, &panic_thrower);

  lua_baselibopen(st);
  lua_iolibopen(st);
  lua_strlibopen(st);
  lua_mathlibopen(st);
}

lua_hooks::~lua_hooks()
{
  if (st)
    lua_close (st);
}

#ifdef BUILD_UNIT_TESTS
void lua_hooks::add_test_hooks()
{
  I(st);
  if (lua_dostring(st, test_hooks_constant) != 0)
    throw oops("lua error while setting up testing hooks");
}
#endif

void lua_hooks::add_std_hooks()
{
  I(st);
  if (lua_dostring(st, std_hooks_constant) != 0)
    throw oops("lua error while setting up standard hooks");
}

void lua_hooks::default_rcfilename(fs::path & file)
{
  char * home = getenv("HOME");
  if (!home)
    throw oops("no HOME directory in $ENV");
  file = fs::path(home) / fs::path(".monotonerc");
}

void lua_hooks::add_rcfile(fs::path const & rc)
{
  I(st);  
  if (fs::exists(rc))
    {
      L(F("opening rcfile '%s' ... ") % rc.string());
      if (lua_dofile(st, rc.string().c_str()) != 0)
	{
	  L(F("no good\n"));
	  throw oops("lua error while loading " + rc.string());
	}
      L("ok\n");
    }
  else
    L(F("skipping nonexistent rcfile '%s'\n") % rc.string());
}

template <typename RES>
static inline RES extract(struct lua_State * st);

template<>
static inline double extract<double>(struct lua_State * st)
{ return lua_tonumber(st, -1); }

template<>
static inline string extract<string>(struct lua_State * st)
{ 
  N((lua_type(st, -1) == LUA_TSTRING ||
     lua_type(st, -1) == LUA_TNUMBER),
    F("need lua string or number value"));
  return string(lua_tostring(st, -1), lua_strlen(st, -1)); 
}

template<>
static inline pair<string, string> 
extract< pair<string, string> >(struct lua_State * st)
{ 
  lua_pushnumber(st, 1);
  lua_rawget(st, -2);
  string p1 = extract<string>(st);
  lua_pop(st, 1);

  lua_pushnumber(st, 2);
  lua_rawget(st, -2);
  string p2 = extract<string>(st);
  lua_pop(st, 1);
  
  return make_pair(p1, p2); 
}

template<>
static inline vector< pair<url, group> >
extract< vector< pair<url, group> > >(struct lua_State * st)
{ 
  vector< pair<url, group> > vec; 
  N((lua_type(st, -1) == LUA_TTABLE), F("need lua table"));
  lua_pushnil(st);
  while (lua_next(st, -2) != 0) 
    {
      // now stack is ... [table] [index] [subtable]
      if (lua_type(st, -2) == LUA_TNUMBER &&
	  lua_type(st, -1) == LUA_TTABLE)
	{
	  pair<string,string> p = extract< pair<string,string> >(st);
	  vec.push_back(make_pair(url(p.first), group(p.second)));
	}
      lua_pop(st, 1);
      // now stack is ... [table] [index]
    }
  return vec;
}


template <typename T> static inline T dummy();
template <> static inline string dummy<string>() { return string(); }
template <> static inline double dummy<double>() { return 0.; }

template <> static inline pair<string,string> 
dummy< pair<string,string> >() { return make_pair("",""); }

template <> static inline vector< pair<url, group> > 
dummy<vector< pair<url, group> > >() { return vector< pair<url, group> >(); }

template <typename R> static string render_result(R const & r) { return "..."; }
template <> static string render_result<string>(string const & r) { return lexical_cast<string>(r.size()) + " chars"; }
template <> static string render_result<double>(double const & r) { return lexical_cast<string>(r); }

template <typename RES>
pair<bool,RES> lua_hooks::call_hook(char const * name, int nargs, ...)
{
  va_list ap;

  string argstr;

  I(st);
  va_start(ap, nargs);
  lua_getglobal(st, name);
  if (! lua_isfunction(st, -1))
    {
      lua_pop(st, 1);
      RES dum = dummy<RES>();
      L(F("lua hook: %s not found\n") % string(name));
      return make_pair(false, dum);
    }

  int c = nargs;
  while (c--)
    {
      char * arg = va_arg(ap, char *);
      int len = va_arg(ap, int);
      lua_pushlstring(st, arg, len);
      argstr += string(c == nargs - 1 ? "" : ", ") + string(arg, len);
    }

  va_end(ap);

  L(F("calling lua hook %s (%s) -> ") % name % argstr);
  if (lua_call(st, nargs, 1) != 0)
    throw oops("lua error while running '" + string(name) + "' hook");

  RES res = extract<RES>(st);
  lua_pop(st, 1);
  L(F(" %s\n") % render_result<RES>(res));

  return make_pair(true, res);
}

// concrete hooks

// nb: if you're hooking lua to return your passphrase, you don't care if we
// keep a couple extra temporaries of your passphrase around.
bool lua_hooks::hook_get_passphrase(rsa_keypair_id const & k, string & phrase)
{
  pair<bool,string> res = call_hook<string>("get_passphrase", 1, 
					    k().data(), k().size());
  phrase = res.second;
  return res.first;
}

bool lua_hooks::hook_persist_phrase_ok()
{
  pair<bool,double> res = call_hook<double>("persist_phrase_ok", 0); 
  return res.first && (res.second != 0.);
}


bool lua_hooks::hook_get_branch_key(cert_value const & branchname, rsa_keypair_id & k)
{
  pair<bool,string> res = call_hook<string>("get_branch_key", 1, 
					    branchname().data(), branchname().size());
  k = res.second;
  return res.first;
}

bool lua_hooks::hook_get_author(cert_value const & branchname, string & author)
{
  pair<bool,string> res = call_hook<string>("get_author", 1,
					    branchname().data(), branchname().size());
  author = res.second;
  return res.first;    
}

bool lua_hooks::hook_edit_comment(string const & commentary, string & result)
{
  pair<bool,string> res = call_hook<string>("edit_comment", 1,
					    commentary.data(), commentary.size());
  result = res.second;
  return res.first; 
}


bool lua_hooks::hook_get_sorter(string const & certname, string & sort_type)
{
  pair<bool,string> res = call_hook<string>("get_sorter", 1,
					    certname.data(), certname.size());
  sort_type = res.second;
  return res.first;    
}

bool lua_hooks::hook_ignore_file(file_path const & p)
{
  pair<bool,double> res = call_hook<double>("ignore_file", 1, 
					    p().data(), p().size());
  return res.first && (res.second != 0.);
}


bool lua_hooks::hook_non_blocking_rng_ok()
{
  pair<bool,double> res = call_hook<double>("non_blocking_rng_ok", 0);
  return res.first && (res.second != 0.);
}



bool lua_hooks::hook_merge2(data const & left, 
			    data const & right, 
			    data & result)
{
  pair<bool,string> res = call_hook<string>("merge2", 2, 
					    left().data(), left().size(),
					    right().data(), right().size());
  result = res.second;
  return res.first;
}

bool lua_hooks::hook_merge3(data const & ancestor, 
			    data const & left, 
			    data const & right, 
			    data & result)
{
  pair<bool,string> res = call_hook<string>("merge3", 3,
					    ancestor().data(), ancestor().size(),
					    left().data(), left().size(),
					    right().data(), right().size());
  result = res.second;
  return res.first;
}


bool lua_hooks::hook_get_news_sender(url const & u, group const & g, 
				     string & sender)
{
  pair<bool,string> res = call_hook<string>("get_news_sender", 2,
					    u().data(), u().size(),
					    g().data(), g().size());
  sender = res.second;
  return res.first;
}

bool lua_hooks::hook_get_http_auth(url const & u, group const & g, 
				   rsa_keypair_id & pubkey)
{
  pair<bool, string > res = call_hook< string >("get_http_auth", 2,
						u().data(), u().size(),
						g().data(), g().size());
  pubkey = res.second;
  return res.first;
}

bool lua_hooks::hook_get_post_targets(cert_value const & branchname, 
				      vector< pair<url,group> > & targets)
{
  pair<bool, vector< pair<url, group> > > res = 
    call_hook< vector< pair<url, group> > >("get_post_targets", 1,
					    branchname().data(), branchname().size());
  targets = res.second;
  return res.first;
}

bool lua_hooks::hook_get_fetch_sources(cert_value const & branchname, 
				       vector< pair<url,group> > & sources)
{
  pair<bool, vector< pair<url, group> > > res = 
    call_hook< vector< pair<url, group> > >("get_fetch_sources", 1,
					    branchname().data(), branchname().size());
  sources = res.second;
  return res.first;
}
