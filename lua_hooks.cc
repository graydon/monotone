// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.


#include "base.hh"
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include <set>
#include <map>
#include <fstream>
#include <iostream>

#include "lua.hh"

#include "app_state.hh"
#include "file_io.hh"
#include "lua_hooks.hh"
#include "netcmd.hh"
#include "sanity.hh"
#include "vocab.hh"
#include "transforms.hh"
#include "paths.hh"
#include "uri.hh"
#include "cmd.hh"
#include "commands.hh"
#include "globish.hh"

// defined in std_hooks.c, generated from std_hooks.lua
extern char const std_hooks_constant[];

using std::make_pair;
using std::map;
using std::pair;
using std::set;
using std::sort;
using std::string;
using std::vector;

static int panic_thrower(lua_State * st)
{
  throw oops("lua panic");
}

// this lets the lua callbacks (monotone_*_for_lua) have access to the
// app_state the're associated with.
// it was added so that the confdir (normally ~/.monotone) can be specified on
// the command line (and so known only to the app_state), and still be
// available to lua
// please *don't* use it for complex things that can throw errors
static map<lua_State*, app_state*> map_of_lua_to_app;

extern "C"
{
  static int
  monotone_get_confdir_for_lua(lua_State *L)
  {
    map<lua_State*, app_state*>::iterator i = map_of_lua_to_app.find(L);
    if (i != map_of_lua_to_app.end())
      {
        system_path dir = i->second->opts.conf_dir;
        string confdir = dir.as_external();
        lua_pushstring(L, confdir.c_str());
      }
    else
      lua_pushnil(L);
    return 1;
  }
}

app_state*
get_app_state(lua_State *L)
{
  map<lua_State*, app_state*>::iterator i = map_of_lua_to_app.find(L);
  if (i != map_of_lua_to_app.end())
    return i->second;
  else
    return NULL;
}

lua_hooks::lua_hooks(app_state * app)
{
  st = luaL_newstate();
  I(st);

  lua_atpanic (st, &panic_thrower);

  luaL_openlibs(st);

  lua_register(st, "get_confdir", monotone_get_confdir_for_lua);
  add_functions(st);

  // Disable any functions we don't want. This is easiest
  // to do just by running a lua string.
  static char const disable_dangerous[] =
    "os.execute = function(c) "
    " error(\"os.execute disabled for security reasons.  Try spawn().\") "
    "end "
    "io.popen = function(c,t) "
    " error(\"io.popen disabled for security reasons.  Try spawn_pipe().\") "
    "end ";
  
    if (!run_string(st, disable_dangerous,
                    "<disabled dangerous functions>"))
    throw oops("lua error while disabling existing functions");

  map_of_lua_to_app.insert(make_pair(st, app));
}

lua_hooks::~lua_hooks()
{
  map<lua_State*, app_state*>::iterator i = map_of_lua_to_app.find(st);
  if (st)
    lua_close (st);
  if (i != map_of_lua_to_app.end())
    map_of_lua_to_app.erase(i);
}

bool
lua_hooks::check_lua_state(lua_State * p_st) const
{
  return (p_st == st);
}

void
lua_hooks::add_std_hooks()
{
  if (!run_string(st, std_hooks_constant, "<std hooks>"))
    throw oops("lua error while setting up standard hooks");
}

void
lua_hooks::load_rcfile(utf8 const & rc)
{
  I(st);
  if (rc() != "-" && directory_exists(system_path(rc)))
    run_directory(st, system_path(rc).as_external().c_str(), "*");
  else
    {
      data dat;
      L(FL("opening rcfile '%s'") % rc);
      read_data_for_command_line(rc, dat);
      N(run_string(st, dat().c_str(), rc().c_str()),
        F("lua error while loading rcfile '%s'") % rc);
      L(FL("'%s' is ok") % rc);
    }
}

void
lua_hooks::load_rcfile(any_path const & rc, bool required)
{
  I(st);
  bool exists;
  try
    {
      exists = path_exists(rc);
    }
  catch (informative_failure & e)
    {
      if (!required)
        {
          L(FL("skipping rcfile '%s': %s") % rc % e.what());
          return;
        }
      else
        throw;
    }

  if (exists)
    {
      L(FL("opening rcfile '%s'") % rc);
      N(run_file(st, rc.as_external().c_str()),
        F("lua error while loading '%s'") % rc);
      L(FL("'%s' is ok") % rc);
    }
  else
    {
      N(!required, F("rcfile '%s' does not exist") % rc);
      L(FL("skipping nonexistent rcfile '%s'") % rc);
    }
}

void
lua_hooks::load_rcfiles(options & opts)
{
  // Built-in rc settings are defaults.
  if (!opts.nostd)
    add_std_hooks();

  // ~/.monotone/monotonerc overrides that, and
  // _MTN/monotonerc overrides *that*.

  if (!opts.norc)
    {
      load_rcfile(opts.conf_dir / "monotonerc", false);
      load_rcfile(bookkeeping_root / "monotonerc", false);
    }

  // Command-line rcfiles override even that.

  for (args_vector::const_iterator i = opts.extra_rcfiles.begin();
       i != opts.extra_rcfiles.end(); ++i)
    load_rcfile(*i);
}

bool
lua_hooks::hook_exists(std::string const & func_name)
{
  return Lua(st)
    .func(func_name)
    .ok();
}

// concrete hooks

// nb: if you're hooking lua to return your passphrase, you don't care if we
// keep a couple extra temporaries of your passphrase around.
bool
lua_hooks::hook_get_passphrase(rsa_keypair_id const & k, string & phrase)
{
  return Lua(st)
    .func("get_passphrase")
    .push_str(k())
    .call(1,1)
    .extract_classified_str(phrase)
    .ok();
}

bool
lua_hooks::hook_persist_phrase_ok()
{
  bool persist_ok = false;
  bool executed_ok = Lua(st)
    .func("persist_phrase_ok")
    .call(0,1)
    .extract_bool(persist_ok)
    .ok();
  return executed_ok && persist_ok;
}

bool
lua_hooks::hook_expand_selector(string const & sel,
                                string & exp)
{
  return Lua(st)
    .func("expand_selector")
    .push_str(sel)
    .call(1,1)
    .extract_str(exp)
    .ok();
}

bool
lua_hooks::hook_expand_date(string const & sel,
                            string & exp)
{
  exp.clear();
  bool res= Lua(st)
    .func("expand_date")
    .push_str(sel)
    .call(1,1)
    .extract_str(exp)
    .ok();
  return res && exp.size();
}

bool
lua_hooks::hook_get_branch_key(branch_name const & branchname,
                               rsa_keypair_id & k)
{
  string key;
  bool ok = Lua(st)
    .func("get_branch_key")
    .push_str(branchname())
    .call(1,1)
    .extract_str(key)
    .ok();

  k = rsa_keypair_id(key);
  return ok;
}

bool
lua_hooks::hook_get_author(branch_name const & branchname,
                           rsa_keypair_id const & k,
                           string & author)
{
  return Lua(st)
    .func("get_author")
    .push_str(branchname())
    .push_str(k())
    .call(2,1)
    .extract_str(author)
    .ok();
}

bool
lua_hooks::hook_edit_comment(external const & commentary,
                             external const & user_log_message,
                             external & result)
{
  string result_str;
  bool is_ok = Lua(st)
                 .func("edit_comment")
                 .push_str(commentary())
                 .push_str(user_log_message())
                 .call(2,1)
                 .extract_str(result_str)
                 .ok();
  result = external(result_str);
  return is_ok;
}

bool
lua_hooks::hook_ignore_file(file_path const & p)
{
  bool ignore_it = false;
  bool exec_ok = Lua(st)
    .func("ignore_file")
    .push_str(p.as_external())
    .call(1,1)
    .extract_bool(ignore_it)
    .ok();
  return exec_ok && ignore_it;
}

bool
lua_hooks::hook_ignore_branch(branch_name const & branch)
{
  bool ignore_it = false;
  bool exec_ok = Lua(st)
    .func("ignore_branch")
    .push_str(branch())
    .call(1,1)
    .extract_bool(ignore_it)
    .ok();
  return exec_ok && ignore_it;
}

static inline bool
shared_trust_function_body(Lua & ll,
                           set<rsa_keypair_id> const & signers,
                           hexenc<id> const & hash,
                           cert_name const & name,
                           cert_value const & val)
{
  ll.push_table();

  int k = 1;
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
    .push_str(hash())
    .push_str(name())
    .push_str(val())
    .call(4, 1)
    .extract_bool(ok)
    .ok();

  return exec_ok && ok;
}

static inline bool
shared_trust_function_body(Lua & ll,
                           set<rsa_keypair_id> const & signers,
                           id const & hash,
                           cert_name const & name,
                           cert_value const & val)
{
  hexenc<id> hid(encode_hexenc(hash()));
  return shared_trust_function_body(ll, signers, hid, name, val);
};

bool
lua_hooks::hook_get_revision_cert_trust(set<rsa_keypair_id> const & signers,
                                       hexenc<id> const & id,
                                       cert_name const & name,
                                       cert_value const & val)
{
  Lua ll(st);
  ll.func("get_revision_cert_trust");
  return shared_trust_function_body(ll, signers, id, name, val);
}

bool
lua_hooks::hook_get_revision_cert_trust(set<rsa_keypair_id> const & signers,
                                       revision_id const & id,
                                       cert_name const & name,
                                       cert_value const & val)
{
  Lua ll(st);
  ll.func("get_revision_cert_trust");
  return shared_trust_function_body(ll, signers, id.inner(), name, val);
}

bool
lua_hooks::hook_get_manifest_cert_trust(set<rsa_keypair_id> const & signers,
                                        hexenc<id> const & id,
                                        cert_name const & name,
                                        cert_value const & val)
{
  Lua ll(st);
  ll.func("get_manifest_cert_trust");
  return shared_trust_function_body(ll, signers, id, name, val);
}

bool
lua_hooks::hook_get_manifest_cert_trust(set<rsa_keypair_id> const & signers,
                                        manifest_id const & id,
                                        cert_name const & name,
                                        cert_value const & val)
{
  Lua ll(st);
  ll.func("get_manifest_cert_trust");
  return shared_trust_function_body(ll, signers, id.inner(), name, val);
}

bool
lua_hooks::hook_accept_testresult_change(map<rsa_keypair_id, bool> const & old_results,
                                         map<rsa_keypair_id, bool> const & new_results)
{
  Lua ll(st);
  ll
    .func("accept_testresult_change")
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
lua_hooks::hook_merge3(file_path const & anc_path,
                       file_path const & left_path,
                       file_path const & right_path,
                       file_path const & merged_path,
                       data const & ancestor,
                       data const & left,
                       data const & right,
                       data & result)
{
  string res;
  bool ok = Lua(st)
    .func("merge3")
    .push_str(anc_path.as_external())
    .push_str(left_path.as_external())
    .push_str(right_path.as_external())
    .push_str(merged_path.as_external())
    .push_str(ancestor())
    .push_str(left())
    .push_str(right())
    .call(7,1)
    .extract_str(res)
    .ok();
  result = data(res);
  return ok;
}

bool
lua_hooks::hook_external_diff(file_path const & path,
                              data const & data_old,
                              data const & data_new,
                              bool is_binary,
                              bool diff_args_provided,
                              string const & diff_args,
                              string const & oldrev,
                              string const & newrev)
{
  Lua ll(st);

  ll
    .func("external_diff")
    .push_str(path.as_external());

  if (oldrev.length() != 0)
    ll.push_str(data_old());
  else
    ll.push_nil();

  ll.push_str(data_new());

  ll.push_bool(is_binary);

  if (diff_args_provided)
    ll.push_str(diff_args);
  else
    ll.push_nil();

  ll.push_str(oldrev);
  ll.push_str(newrev);

  return ll.call(7,0).ok();
}

bool
lua_hooks::hook_get_encloser_pattern(file_path const & path,
                                     std::string & pattern)
{
  bool exec_ok
    = Lua(st)
    .func("get_encloser_pattern")
    .push_str(path.as_external())
    .call(1, 1)
    .extract_str(pattern)
    .ok();

  // If the hook fails, make sure pattern is set to something sane
  // (the empty string, which will disable enclosers for this file).
  if (!exec_ok)
    pattern = "";
  return exec_ok;
}

bool
lua_hooks::hook_use_inodeprints()
{
  bool use = false, exec_ok = false;

  exec_ok = Lua(st)
    .func("use_inodeprints")
    .call(0, 1)
    .extract_bool(use)
    .ok();
  return use && exec_ok;
}

bool
lua_hooks::hook_get_netsync_key(utf8 const & server_address,
                                globish const & include,
                                globish const & exclude,
                                rsa_keypair_id & k)
{
  string key_id;
  bool exec_ok
    = Lua(st)
    .func("get_netsync_key")
    .push_str(server_address())
    .push_str(include())
    .push_str(exclude())
    .call(3, 1)
    .extract_str(key_id)
    .ok();

  if (!exec_ok)
    key_id = "";
  k = rsa_keypair_id(key_id);
  return exec_ok;
}

static void
push_uri(uri const & u, Lua & ll)
{
  ll.push_table();

  if (!u.scheme.empty())
    {
      ll.push_str("scheme");
      ll.push_str(u.scheme);
      ll.set_table();
    }

  if (!u.user.empty())
    {
      ll.push_str("user");
      ll.push_str(u.user);
      ll.set_table();
    }

  if (!u.host.empty())
    {
      ll.push_str("host");
      ll.push_str(u.host);
      ll.set_table();
    }

  if (!u.port.empty())
    {
      ll.push_str("port");
      ll.push_str(u.port);
      ll.set_table();
    }

  if (!u.path.empty())
    {
      ll.push_str("path");
      ll.push_str(u.path);
      ll.set_table();
    }

  if (!u.query.empty())
    {
      ll.push_str("query");
      ll.push_str(u.query);
      ll.set_table();
    }

  if (!u.fragment.empty())
    {
      ll.push_str("fragment");
      ll.push_str(u.fragment);
      ll.set_table();
    }
}

bool
lua_hooks::hook_get_netsync_connect_command(netsync_connection_info & info,
                                            bool debug)
{
  bool cmd = false, exec_ok = false;
  Lua ll(st);
  ll.func("get_netsync_connect_command");

  push_uri(info.client.u, ll);

  ll.push_table();

  if (!info.client.include_pattern().empty())
    {
      ll.push_str("include");
      ll.push_str(info.client.include_pattern());
      ll.set_table();
    }

  if (!info.client.exclude_pattern().empty())
    {
      ll.push_str("exclude");
      ll.push_str(info.client.exclude_pattern());
      ll.set_table();
    }

  if (debug)
    {
      ll.push_str("debug");
      ll.push_bool(debug);
      ll.set_table();
    }

  ll.call(2,1);

  ll.begin();
  
  netsync_connection_info info2(info);

  info2.client.argv.clear();
  info2.client.use_argv = false;
  bool not_use_argv = false;
  while(ll.next())
    {
      std::string s;
      ll.extract_str(s).pop();
      if (!lua_isnumber(st, -1))
        {
          std::string k;
          ll.extract_str(k); // do not pop
          // can be URI parts, or include/exclude
          if (k == "scheme")
            info2.client.u.scheme = s;
          else if (k == "user")
            info2.client.u.user = s;
          else if (k == "host")
            info2.client.u.host = s;
          else if (k == "port")
            info2.client.u.port = s;
          else if (k == "path")
            info2.client.u.path = s;
          else if (k == "query")
            info2.client.u.query = s;
          else if (k == "fragment")
            info2.client.u.fragment = s;
          else if (k == "include")
            info2.client.include_pattern = s;
          else if (k == "exclude")
            info2.client.exclude_pattern = s;
          else
            W(F("get_netsync_connect_command hook returned unknown item '%s'")
              % k);
          
          not_use_argv = true;
        }
      else
        {
          info2.client.argv.push_back(s);
          info2.client.use_argv = true;
        }
    }
  E(!(info2.client.use_argv && not_use_argv),
    F("get_netsync_command returned inconsistent data"));
  
  if (ll.ok())
    {
      info = info2;
      return true;
    }
  else
    {
      return false;
    }
}


bool
lua_hooks::hook_use_transport_auth(uri const & u)
{
  bool use_auth = true;
  Lua ll(st);
  ll.func("use_transport_auth");
  push_uri(u, ll);
  ll.call(1,1);
  ll.extract_bool(use_auth);

  // NB: we want to return *true* here if there's a failure.
  return use_auth;
}


bool
lua_hooks::hook_get_netsync_read_permitted(string const & branch,
                                           rsa_keypair_id const & identity)
{
  bool permitted = false, exec_ok = false;

  exec_ok = Lua(st)
    .func("get_netsync_read_permitted")
    .push_str(branch)
    .push_str(identity())
    .call(2,1)
    .extract_bool(permitted)
    .ok();

  return exec_ok && permitted;
}

// Anonymous no-key version
bool
lua_hooks::hook_get_netsync_read_permitted(string const & branch)
{
  bool permitted = false, exec_ok = false;

  exec_ok = Lua(st)
    .func("get_netsync_read_permitted")
    .push_str(branch)
    .push_nil()
    .call(2,1)
    .extract_bool(permitted)
    .ok();

  return exec_ok && permitted;
}

bool
lua_hooks::hook_get_netsync_write_permitted(rsa_keypair_id const & identity)
{
  bool permitted = false, exec_ok = false;

  exec_ok = Lua(st)
    .func("get_netsync_write_permitted")
    .push_str(identity())
    .call(1,1)
    .extract_bool(permitted)
    .ok();

  return exec_ok && permitted;
}

bool
lua_hooks::hook_init_attributes(file_path const & filename,
                                map<string, string> & attrs)
{
  Lua ll(st);

  ll
    .push_str("attr_init_functions")
    .get_tab();

  L(FL("calling attr_init_function for %s") % filename);
  ll.begin();
  while (ll.next())
    {
      L(FL("  calling an attr_init_function for %s") % filename);
      ll.push_str(filename.as_external());
      ll.call(1, 1);

      if (lua_isstring(st, -1))
        {
          string key, value;

          ll.extract_str(value);
          ll.pop();
          ll.extract_str(key);

          attrs[key] = value;
          L(FL("  added attr %s = %s") % key % value);
        }
      else
        {
          L(FL("  no attr added"));
          ll.pop();
        }
    }

  return ll.pop().ok();
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
    .push_str(filename.as_external())
    .push_str(value)
    .call(2,0)
    .ok();
}


bool
lua_hooks::hook_validate_commit_message(utf8 const & message,
                                        revision_data const & new_rev,
                                        branch_name const & branchname,
                                        bool & validated,
                                        string & reason)
{
  validated = true;
  return Lua(st)
    .func("validate_commit_message")
    .push_str(message())
    .push_str(new_rev.inner()())
    .push_str(branchname())
    .call(3, 2)
    .extract_str(reason)
    // XXX When validated, the extra returned string is superfluous.
    .pop()
    .extract_bool(validated)
    .ok();
}

bool
lua_hooks::hook_note_commit(revision_id const & new_id,
                            revision_data const & rdat,
                            map<cert_name, cert_value> const & certs)
{
  Lua ll(st);
  ll
    .func("note_commit")
    .push_str(new_id.inner()())
    .push_str(rdat.inner()());

  ll.push_table();

  for (map<cert_name, cert_value>::const_iterator i = certs.begin();
       i != certs.end(); ++i)
    {
      ll.push_str(i->first());
      ll.push_str(i->second());
      ll.set_table();
    }

  ll.call(3, 0);
  return ll.ok();
}

bool
lua_hooks::hook_note_netsync_start(size_t session_id, string my_role,
                                   int sync_type, string remote_host,
                                   rsa_keypair_id remote_keyname,
                                   globish include_pattern,
                                   globish exclude_pattern)
{
  string type;
  switch (sync_type)
    {
    case 1:
      type = "push";
      break;
    case 2:
      type = "pull";
      break;
    case 3:
      type = "sync";
      break;
    default:
      type = "unknown";
      break;
    }
  Lua ll(st);
  return ll
    .func("note_netsync_start")
    .push_int(session_id)
    .push_str(my_role)
    .push_str(type)
    .push_str(remote_host)
    .push_str(remote_keyname())
    .push_str(include_pattern())
    .push_str(exclude_pattern())
    .call(7, 0)
    .ok();
}

bool
lua_hooks::hook_note_netsync_revision_received(revision_id const & new_id,
                                               revision_data const & rdat,
                            set<pair<rsa_keypair_id,
                                     pair<cert_name,
                                          cert_value> > > const & certs,
                                               size_t session_id)
{
  Lua ll(st);
  ll
    .func("note_netsync_revision_received")
    .push_str(new_id.inner()())
    .push_str(rdat.inner()());

  ll.push_table();

  typedef set<pair<rsa_keypair_id, pair<cert_name, cert_value> > > cdat;

  int n = 1;
  for (cdat::const_iterator i = certs.begin(); i != certs.end(); ++i)
    {
      ll.push_int(n++);
      ll.push_table();
      ll.push_str(i->first());
      ll.set_field("key");
      ll.push_str(i->second.first());
      ll.set_field("name");
      ll.push_str(i->second.second());
      ll.set_field("value");
      ll.set_table();
    }

  ll.push_int(session_id);
  ll.call(4, 0);
  return ll.ok();
}

bool
lua_hooks::hook_note_netsync_pubkey_received(rsa_keypair_id const & kid,
                                             size_t session_id)
{
  Lua ll(st);
  ll
    .func("note_netsync_pubkey_received")
    .push_str(kid())
    .push_int(session_id);

  ll.call(2, 0);
  return ll.ok();
}

bool
lua_hooks::hook_note_netsync_cert_received(revision_id const & rid,
                                           rsa_keypair_id const & kid,
                                           cert_name const & name,
                                           cert_value const & value,
                                           size_t session_id)
{
  Lua ll(st);
  ll
    .func("note_netsync_cert_received")
    .push_str(rid.inner()())
    .push_str(kid())
    .push_str(name())
    .push_str(value())
    .push_int(session_id);

  ll.call(5, 0);
  return ll.ok();
}

bool
lua_hooks::hook_note_netsync_end(size_t session_id, int status,
                                 size_t bytes_in, size_t bytes_out,
                                 size_t certs_in, size_t certs_out,
                                 size_t revs_in, size_t revs_out,
                                 size_t keys_in, size_t keys_out)
{
  Lua ll(st);
  return ll
    .func("note_netsync_end")
    .push_int(session_id)
    .push_int(status)
    .push_int(bytes_in)
    .push_int(bytes_out)
    .push_int(certs_in)
    .push_int(certs_out)
    .push_int(revs_in)
    .push_int(revs_out)
    .push_int(keys_in)
    .push_int(keys_out)
    .call(10, 0)
    .ok();
}

bool
lua_hooks::hook_note_mtn_startup(args_vector const & args)
{
  Lua ll(st);

  ll.func("note_mtn_startup");

  for (args_vector::const_iterator i = args.begin(); i != args.end(); ++i)
    ll.push_str((*i)());

  ll.call(args.size(), 0);
  return ll.ok();
}

namespace commands {
  class cmd_lua : public command
  {
    lua_State *st;
    std::string const f_name;
  public:
    cmd_lua(std::string const & primary_name,
                   std::string const & params,
                   std::string const & abstract,
                   std::string const & desc,
                   lua_State *L_st,
                   std::string const & func_name) :
         command(primary_name, "", CMD_REF(user), false, false, params,
                 abstract, desc, true, options::options_type() | options::opts::none, true),
                 st(L_st), f_name(func_name)
    {
      // because user commands are inserted after the normal initialisation process
      CMD_REF(user)->children().insert(this);
    }

    void exec(app_state & app, command_id const & execid, args_vector const & args) const;
  };
}

void commands::cmd_lua::exec(app_state & app,
                               command_id const & execid,
                               args_vector const & args) const
{
  I(st);
  I(app.lua.check_lua_state(st));
  
  app_state* app_p = get_app_state(st);
  I(app_p == & app);

  Lua ll(st);
  ll.func(f_name);
  
  for (args_vector::const_iterator it = args.begin(); it != args.end(); ++it)
    ll.push_str((*it)());

  app.mtn_automate_allowed = true;

  ll.call(args.size(),0);

  app.mtn_automate_allowed = false;

  E(ll.ok(), F("Call to user command %s (lua command: %s) failed.") % primary_name() % f_name);
}

LUAEXT(alias_command, )
{
  const char *old_cmd = luaL_checkstring(L, -2);
  const char *new_cmd = luaL_checkstring(L, -1);
  N(old_cmd && new_cmd,
    F("%s called with an invalid parameter") % "alias_command");

  args_vector args;
  args.push_back(arg_type(old_cmd));
  commands::command_id id = commands::complete_command(args);
  commands::command *old_cmd_p = CMD_REF(__root__)->find_command(id);

  old_cmd_p->add_alias(utf8(new_cmd));

  lua_pushboolean(L, true);
  return 1;
}


LUAEXT(register_command, )
{
  const char *cmd_name = luaL_checkstring(L, -5);
  const char *cmd_params = luaL_checkstring(L, -4);
  const char *cmd_abstract = luaL_checkstring(L, -3);
  const char *cmd_desc = luaL_checkstring(L, -2);
  const char *cmd_func = luaL_checkstring(L, -1);
  
  N(cmd_name && cmd_params && cmd_abstract && cmd_desc && cmd_func,
    F("%s called with an invalid parameter") % "register_command");
  
  new commands::cmd_lua(cmd_name, cmd_params, cmd_abstract, cmd_desc, L, cmd_func);  // leak this - commands can't be removed anyway
  
  lua_pushboolean(L, true);
  return 1;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
