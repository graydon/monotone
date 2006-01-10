#ifndef __APP_STATE_HH__
#define __APP_STATE_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

class app_state;
class lua_hooks;

#include <boost/shared_ptr.hpp>
#include <botan/pubkey.h>
#include <botan/rsa.h>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <vector>

#include "database.hh"
#include "lua.hh"
#include "work.hh"
#include "vocab.hh"
#include "paths.hh"
#include "key_store.hh"

// this class is supposed to hold all (or.. well, most) of the state of the
// application, barring some unfortunate static objects like the debugging /
// logging system and the command objects, for the time being. the vague intent
// being to make life easier for anyone who wants to embed this program as a
// library someday

class app_state
{
public:
  rsa_keypair_id signing_key;
  utf8 branch_name;
  database db;
  lua_hooks lua;
  key_store keys;
  bool stdhooks;
  bool rcfiles;
  bool diffs;
  bool no_merges;
  bool set_default;
  bool verbose;
  options_map options;
  utf8 message;
  utf8 message_file;
  bool date_set;
  boost::posix_time::ptime date;
  utf8 author;
  system_path search_root;
  std::vector<utf8> revision_selectors;
  std::set<utf8> exclude_patterns;
  std::vector<utf8> extra_rcfiles;
  path_set restrictions;
  path_set excludes;
  bool found_working_copy;
  long depth;
  long last;
  system_path pidfile;
  diff_type diff_format;
  bool diff_args_provided;
  utf8 diff_args;
  bool use_lca;
  bool execute;
  utf8 bind_address;
  utf8 bind_port;
  bool missing;
  bool unknown;
  std::vector<rsa_keypair_id> keys_to_push;
  system_path confdir;
  bool have_set_key_dir;
  std::set<std::string> attrs_to_drop;

  std::map<int, bool> explicit_option_map;  // set if the value of the flag was explicitly given on the command line
  void set_is_explicit_option (int option_id);
  bool is_explicit_option(int option_id) const;

  // These are used to cache signers/verifiers (if the hook allows).
  // They can't be function-static variables in key.cc, since they must be
  // destroyed before the Botan deinitialize() function is called. */
  std::map<rsa_keypair_id,
    std::pair<boost::shared_ptr<Botan::PK_Signer>, 
        boost::shared_ptr<Botan::RSA_PrivateKey> > > signers;
  std::map<rsa_keypair_id,
    std::pair<boost::shared_ptr<Botan::PK_Verifier>,
        boost::shared_ptr<Botan::RSA_PublicKey> > > verifiers;

  void allow_working_copy();
  void require_working_copy(std::string const & explanation = "");
  void create_working_copy(system_path const & dir);

  void set_restriction(path_set const & valid_paths, 
                       std::vector<utf8> const & paths);
  bool restriction_requires_parent(split_path const & path);
  bool restriction_includes(split_path const & path);

  // Set the branch name.  If you only invoke set_branch, the branch
  // name is not sticky (and won't be written to the working copy and
  // reused by subsequent monotone invocations).  Commands which
  // switch the working to a different branch should invoke
  // make_branch_sticky (before require_working_copy because this
  // function updates the working copy).
  void set_branch(utf8 const & name);
  void make_branch_sticky();

  void set_database(system_path const & filename);
  void set_key_dir(system_path const & filename);
  void set_signing_key(utf8 const & key);
  void set_root(system_path const & root);
  void set_message(utf8 const & message);
  void set_message_file(utf8 const & message_file);
  void set_date(utf8 const & date);
  void set_author(utf8 const & author);
  void set_depth(long depth);
  void set_last(long last);
  void set_pidfile(system_path const & pidfile);
  void add_revision(utf8 const & selector);
  void add_exclude(utf8 const & exclude_pattern);
  void set_diff_format(diff_type dtype);
  void set_diff_args(utf8 const & args);
  void add_key_to_push(utf8 const & key);

  void set_stdhooks(bool b);
  void set_rcfiles(bool b);
  void set_verbose(bool b);
  void add_rcfile(utf8 const & filename);

  void set_confdir(system_path const & cd);
  system_path get_confdir();

  explicit app_state();
  ~app_state();

private:
  void load_rcfiles();
  void read_options();
  void write_options();
};

#endif // __APP_STATE_HH__
