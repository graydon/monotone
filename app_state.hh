#ifndef __APP_STATE_HH__
#define __APP_STATE_HH__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

class app_state;
class lua_hooks;

#include <map>
#include <vector>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/shared_ptr.hpp>

#include "database.hh"
#include "key_store.hh"
#include "lua_hooks.hh"
#include "options.hh"
#include "paths.hh"
#include "vocab.hh"
#include "work.hh"

namespace Botan
{
  class PK_Signer;
  class RSA_PrivateKey;
  class PK_Verifier;
  class RSA_PublicKey;
};

// This class is supposed to hold all (or.. well, most) of the state
// of the application, barring some unfortunate static objects like
// the debugging / logging system and the command objects, for the
// time being. The vague intent being to make life easier for anyone
// who wants to embed this program as a library someday.

class app_state
{
public:
  rsa_keypair_id signing_key;
  utf8 branch_name;
  database db;
  lua_hooks lua;
  key_store keys;
  workspace work;
  bool recursive;
  bool stdhooks;
  bool rcfiles;
  bool diffs;
  bool no_merges;
  bool set_default;
  bool verbose;
  utf8 message;
  utf8 message_file;
  bool date_set;
  boost::posix_time::ptime date;
  utf8 author;
  system_path search_root;
  std::vector<utf8> revision_selectors;
  std::vector<utf8> exclude_patterns;
  std::vector<utf8> extra_rcfiles;
  bool found_workspace;
  long depth;
  long last;
  long next;
  system_path pidfile;
  diff_type diff_format;
  bool diff_args_provided;
  bool diff_show_encloser;
  utf8 diff_args;
  bool execute;
  utf8 bind_address;
  utf8 bind_port;
  bool bind_stdio;
  bool use_transport_auth;
  bool missing;
  bool unknown;
  bool brief;
  std::vector<rsa_keypair_id> keys_to_push;
  system_path confdir;
  bool have_set_key_dir;
  bool have_set_key;
  std::set<std::string> attrs_to_drop;
  bool no_files;
  bool requested_help;
  bool branch_is_sticky;
  size_t automate_stdio_size;

  std::set<std::string> explicit_options;  // in set if the value of the flag was explicitly given on the command line
  void set_is_explicit_option (std::string o);
  bool is_explicit_option(std::string o) const;

  // These are used to cache signers/verifiers (if the hook allows).
  // They can't be function-static variables in key.cc, since they
  // must be destroyed before the Botan deinitialize() function is
  // called.

  std::map<rsa_keypair_id,
    std::pair<boost::shared_ptr<Botan::PK_Signer>,
        boost::shared_ptr<Botan::RSA_PrivateKey> > > signers;
  std::map<rsa_keypair_id,
    std::pair<boost::shared_ptr<Botan::PK_Verifier>,
        boost::shared_ptr<Botan::RSA_PublicKey> > > verifiers;

  void allow_workspace();
  void process_options();
  void require_workspace(std::string const & explanation = "");
  void create_workspace(system_path const & dir);

  // Set the branch name. If you only invoke set_branch, the branch
  // name is not sticky (and won't be written to the workspace and
  // reused by subsequent monotone invocations).  Commands which
  // switch the working to a different branch should invoke
  // make_branch_sticky (before require_workspace because this
  // function updates the workspace).

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
  void set_next(long next);
  void set_pidfile(system_path const & pidfile);
  void add_revision(utf8 const & selector);
  void add_exclude(utf8 const & exclude_pattern);
  void set_diff_format(diff_type dtype);
  void set_diff_args(utf8 const & args);
  void add_key_to_push(utf8 const & key);
  void set_recursive(bool r = true);

  void set_stdhooks(bool b);
  void set_rcfiles(bool b);
  void set_verbose(bool b);
  void add_rcfile(utf8 const & filename);

  void set_confdir(system_path const & cd);
  system_path get_confdir();
  void set_automate_stdio_size(long size);

  explicit app_state();
  ~app_state();

private:
  void load_rcfiles();
  void write_options();
};

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __APP_STATE_HH__
