#ifndef __LUA__
#define __LUA__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// this file defines a typed C++ interface to the various hooks
// we expose to the user as lua functions or variables

#include <string>
#include <vector>
#include "file_io.hh"
#include "vocab.hh"

struct lua_State;

using std::string;

class lua_hooks
{
  struct lua_State * st;
  template <typename RES>
  pair<bool,RES> call_hook(char const * name, int nargs, ...);

public:
  lua_hooks();
  ~lua_hooks();
#ifdef BUILD_UNIT_TESTS
  void add_test_hooks();
#endif
  void add_std_hooks();
  void default_rcfilename(fs::path & file);
  void add_rcfile(fs::path const & file);

  // cert hooks
  bool hook_get_branch_key(cert_value const & branchname, rsa_keypair_id & k);
  bool hook_get_passphrase(rsa_keypair_id const & k, string & phrase);
  bool hook_get_author(cert_value const & branchname, string & author);
  bool hook_edit_comment(string const & commentary, string & result);  
  bool hook_persist_phrase_ok();
  bool hook_non_blocking_rng_ok();


  // network hooks
  bool hook_get_news_sender(url const & serv, group const & grp, 
			    string & sender);
  bool hook_get_http_auth(url const & serv, group const & grp, 
			  rsa_keypair_id & pubkey);
  bool hook_get_post_targets(cert_value const & branchname, 
			     vector< pair<url,group> > & targets);
  bool hook_get_fetch_sources(cert_value const & branchname, 
			      vector< pair<url,group> > & sources);


  // local repo hooks
  bool hook_ignore_file(file_path const & p);
  bool hook_get_sorter(string const & certname, string & sort_type);
  bool hook_merge2(data const & left, 
		   data const & right, 
		   data & result);
  bool hook_merge3(data const & ancestor, 
		   data const & left, 
		   data const & right, 
		   data & result);
};

#endif // __LUA_HH__
