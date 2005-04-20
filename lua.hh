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
#include <map>
#include "file_io.hh"
#include "vocab.hh"

struct patch_set;

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
  void working_copy_rcfilename(fs::path & file);
  void default_rcfilename(fs::path & file);
  void load_rcfile(fs::path const & file, bool required);

  // cert hooks
  bool hook_expand_selector(std::string const & sel, std::string & exp);
  bool hook_get_branch_key(cert_value const & branchname, rsa_keypair_id & k);
  bool lua_hooks::hook_get_priv_key(rsa_keypair_id const & k,
                                   base64< arc4<rsa_priv_key> > & priv_key );
  bool hook_get_passphrase(rsa_keypair_id const & k, std::string & phrase);
  bool hook_get_author(cert_value const & branchname, std::string & author);
  bool hook_edit_comment(std::string const & commentary,
                         std::string const & user_log_message,
                         std::string & result);  
  bool hook_persist_phrase_ok();
  bool hook_non_blocking_rng_ok();
  bool hook_get_revision_cert_trust(std::set<rsa_keypair_id> const & signers,
                                   hexenc<id> const & id,
                                   cert_name const & name,
                                   cert_value const & val);
  bool hook_get_manifest_cert_trust(std::set<rsa_keypair_id> const & signers,
                                    hexenc<id> const & id,
                                    cert_name const & name,
                                    cert_value const & val);
  bool hook_accept_testresult_change(std::map<rsa_keypair_id, bool> const & old_results,
                                     std::map<rsa_keypair_id, bool> const & new_results);

  // network hooks
  bool hook_get_netsync_read_permitted(std::string const & collection, 
                                       rsa_keypair_id const & identity);
  bool hook_get_netsync_anonymous_read_permitted(std::string const & collection);
  bool hook_get_netsync_write_permitted(std::string const & collection, 
                                        rsa_keypair_id const & identity);

  // local repo hooks
  bool hook_ignore_file(file_path const & p);
  bool hook_ignore_branch(std::string const & branch);
  bool hook_merge2(file_path const & left_path,
                   file_path const & right_path,
                   file_path const & merged_path,
                   data const & left, 
                   data const & right, 
                   data & result);
  bool hook_merge3(file_path const & anc_path,
                   file_path const & left_path,
                   file_path const & right_path,
                   file_path const & merged_path,
                   data const & ancestor, 
                   data const & left, 
                   data const & right, 
                   data & result);

  bool hook_resolve_file_conflict(file_path const & anc,
                                  file_path const & a,
                                  file_path const & b,
                                  file_path & res);

  bool hook_resolve_dir_conflict(file_path const & anc,
                                 file_path const & a,
                                 file_path const & b,
                                 file_path & res);

  // working copy hooks
  bool hook_use_inodeprints();

  // attribute hooks
  bool hook_init_attributes(file_path const & filename,
                            std::map<std::string, std::string> & attrs);
  bool hook_apply_attribute(std::string const & attr, 
                            file_path const & filename, 
                            std::string const & value);

  // conversion hooks
  bool hook_get_system_linesep(std::string & linesep);
  bool hook_get_charset_conv(file_path const & p, 
                             std::string & db, std::string & ext);
  bool hook_get_linesep_conv(file_path const & p, 
                             std::string & db, std::string & ext);

  // notification hooks
  bool hook_note_commit(revision_id const & new_id,
                        std::map<cert_name, cert_value> const & certs);
};

#endif // __LUA_HH__
