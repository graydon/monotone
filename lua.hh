#ifndef __LUA__
#define __LUA__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// this file defines a typed C++ interface to the various hooks
// we expose to the user as lua functions or variables

#include <string>
#include <set>
#include "file_io.hh"
#include "vocab.hh"

struct lua_State;

class lua_hooks
{
  struct lua_State * st;
  template <typename RES>
  std::pair<bool,RES> call_hook(char const * name, int nargs, ...);

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
  bool hook_get_passphrase(rsa_keypair_id const & k, std::string & phrase);
  bool hook_get_author(cert_value const & branchname, std::string & author);
  bool hook_edit_comment(std::string const & commentary, std::string & result);  
  bool hook_persist_phrase_ok();
  bool hook_non_blocking_rng_ok();


  // network hooks
  bool hook_get_news_sender(url const & serv, std::string & sender);
  bool hook_get_mail_sender(url const & serv, std::string & sender);
  bool hook_get_http_auth(url const & serv, rsa_keypair_id & pubkey);
  bool hook_get_post_targets(cert_value const & branchname, 
			     std::set<url> & targets);
  bool hook_get_fetch_sources(cert_value const & branchname, 
			      std::set<url> & sources);


  // local repo hooks
  bool hook_ignore_file(file_path const & p);
  bool hook_get_sorter(std::string const & certname, std::string & sort_type);
  bool hook_merge2(data const & left, 
		   data const & right, 
		   data & result);
  bool hook_merge3(data const & ancestor, 
		   data const & left, 
		   data const & right, 
		   data & result);

  // attribute hooks
  bool hook_apply_attribute(std::string const & attr, 
			    file_path const & filename, 
			    std::string const & value);
    

};

#endif // __LUA_HH__
