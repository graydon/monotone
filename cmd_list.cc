// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <algorithm>
#include <iostream>
#include <map>
#include <utility>

#include <boost/tuple/tuple.hpp>

#include "basic_io.hh"
#include "cert.hh"
#include "charset.hh"
#include "cmd.hh"
#include "database.hh"
#include "globish.hh"
#include "keys.hh"
#include "restrictions.hh"
#include "revision.hh"
#include "simplestring_xform.hh"
#include "transforms.hh"
#include "ui.hh"

using std::cout;
using std::endl;
using std::make_pair;
using std::map;
using std::ostream_iterator;
using std::pair;
using std::set;
using std::sort;
using std::string;
using std::vector;

static void
ls_certs(string const & name, app_state & app, vector<utf8> const & args)
{
  if (args.size() != 1)
    throw usage(name);

  vector<cert> certs;

  transaction_guard guard(app.db, false);

  revision_id ident;
  complete(app, idx(args, 0)(), ident);
  vector< revision<cert> > ts;
  app.db.get_revision_certs(ident, ts);
  for (size_t i = 0; i < ts.size(); ++i)
    certs.push_back(idx(ts, i).inner());

  {
    set<rsa_keypair_id> checked;
    for (size_t i = 0; i < certs.size(); ++i)
      {
        if (checked.find(idx(certs, i).key) == checked.end() &&
            !app.db.public_key_exists(idx(certs, i).key))
          P(F("no public key '%s' found in database")
            % idx(certs, i).key);
        checked.insert(idx(certs, i).key);
      }
  }

  // Make the output deterministic; this is useful for the test suite, in
  // particular.
  sort(certs.begin(), certs.end());

  string str     = _("Key   : %s\n"
                     "Sig   : %s\n"
                     "Name  : %s\n"
                     "Value : %s\n");
  string extra_str = "      : %s\n";

  string::size_type colon_pos = str.find(':');

  if (colon_pos != string::npos)
    {
      string substr(str, 0, colon_pos);
      colon_pos = display_width(substr);
      extra_str = string(colon_pos, ' ') + ": %s\n";
    }

  for (size_t i = 0; i < certs.size(); ++i)
    {
      cert_status status = check_cert(app, idx(certs, i));
      cert_value tv;
      decode_base64(idx(certs, i).value, tv);
      string washed;
      if (guess_binary(tv()))
        {
          washed = "<binary data>";
        }
      else
        {
          washed = tv();
        }

      string stat;
      switch (status)
        {
        case cert_ok:
          stat = _("ok");
          break;
        case cert_bad:
          stat = _("bad");
          break;
        case cert_unknown:
          stat = _("unknown");
          break;
        }

      vector<string> lines;
      split_into_lines(washed, lines);
      I(lines.size() > 0);

      cout << string(guess_terminal_width(), '-') << '\n'
           << (i18n_format(str)
               % idx(certs, i).key()
               % stat
               % idx(certs, i).name()
               % idx(lines, 0));

      for (size_t i = 1; i < lines.size(); ++i)
        cout << (i18n_format(extra_str) % idx(lines, i));
    }

  if (certs.size() > 0)
    cout << "\n";

  guard.commit();
}

static void
ls_keys(string const & name, app_state & app,
        vector<utf8> const & args)
{
  vector<rsa_keypair_id> pubs;
  vector<rsa_keypair_id> privkeys;
  string pattern;
  if (args.size() == 1)
    pattern = idx(args, 0)();
  else if (args.size() > 1)
    throw usage(name);

  if (app.db.database_specified())
    {
      transaction_guard guard(app.db, false);
      app.db.get_key_ids(pattern, pubs);
      guard.commit();
    }
  app.keys.get_key_ids(pattern, privkeys);

  // true if it is in the database, false otherwise
  map<rsa_keypair_id, bool> pubkeys;
  for (vector<rsa_keypair_id>::const_iterator i = pubs.begin();
       i != pubs.end(); i++)
    pubkeys[*i] = true;

  bool all_in_db = true;
  for (vector<rsa_keypair_id>::const_iterator i = privkeys.begin();
       i != privkeys.end(); i++)
    {
      if (pubkeys.find(*i) == pubkeys.end())
        {
          pubkeys[*i] = false;
          all_in_db = false;
        }
    }

  if (pubkeys.size() > 0)
    {
      cout << "\n" << "[public keys]" << "\n";
      for (map<rsa_keypair_id, bool>::iterator i = pubkeys.begin();
           i != pubkeys.end(); i++)
        {
          base64<rsa_pub_key> pub_encoded;
          hexenc<id> hash_code;
          rsa_keypair_id keyid = i->first;
          bool indb = i->second;

          if (indb)
            app.db.get_key(keyid, pub_encoded);
          else
            {
              keypair kp;
              app.keys.get_key_pair(keyid, kp);
              pub_encoded = kp.pub;
            }
          key_hash_code(keyid, pub_encoded, hash_code);
          if (indb)
            cout << hash_code << " " << keyid << "\n";
          else
            cout << hash_code << " " << keyid << "   (*)" << "\n";
        }
      if (!all_in_db)
        cout << (F("(*) - only in %s/")
                 % app.keys.get_key_dir()) << "\n";
      cout << "\n";
    }

  if (privkeys.size() > 0)
    {
      cout << "\n" << "[private keys]" << "\n";
      for (vector<rsa_keypair_id>::iterator i = privkeys.begin();
           i != privkeys.end(); i++)
        {
          keypair kp;
          hexenc<id> hash_code;
          app.keys.get_key_pair(*i, kp);
          key_hash_code(*i, kp.priv, hash_code);
          cout << hash_code << " " << *i << "\n";
        }
      cout << "\n";
    }

  if (pubkeys.size() == 0 &&
      privkeys.size() == 0)
    {
      if (args.size() == 0)
        P(F("no keys found"));
      else
        W(F("no keys found matching '%s'") % idx(args, 0)());
    }
}

static void
ls_branches(string name, app_state & app, vector<utf8> const & args)
{
  utf8 inc("*");
  utf8 exc;
  if (args.size() == 1)
    inc = idx(args,0);
  else if (args.size() > 1)
    throw usage(name);
  combine_and_check_globish(app.opts.exclude_patterns, exc);
  globish_matcher match(inc, exc);
  vector<string> names;
  app.db.get_branches(names);

  sort(names.begin(), names.end());
  for (size_t i = 0; i < names.size(); ++i)
    if (match(idx(names, i))
        && !app.lua.hook_ignore_branch(idx(names, i)))
      cout << idx(names, i) << "\n";
}

static void
ls_epochs(string name, app_state & app, vector<utf8> const & args)
{
  map<cert_value, epoch_data> epochs;
  app.db.get_epochs(epochs);

  if (args.size() == 0)
    {
      for (map<cert_value, epoch_data>::const_iterator
             i = epochs.begin();
           i != epochs.end(); ++i)
        {
          cout << i->second << " " << i->first << "\n";
        }
    }
  else
    {
      for (vector<utf8>::const_iterator i = args.begin();
           i != args.end();
           ++i)
        {
          map<cert_value, epoch_data>::const_iterator j = epochs.find(cert_value((*i)()));
          N(j != epochs.end(), F("no epoch for branch %s") % *i);
          cout << j->second << " " << j->first << "\n";
        }
    }
}

static void
ls_tags(string name, app_state & app, vector<utf8> const & args)
{
  vector< revision<cert> > certs;
  app.db.get_revision_certs(tag_cert_name, certs);

  set< pair<cert_value, pair<revision_id, rsa_keypair_id> > >
    sorted_vals;

  for (vector< revision<cert> >::const_iterator i = certs.begin();
       i != certs.end(); ++i)
    {
      cert_value name;
      cert c = i->inner();
      decode_base64(c.value, name);
      sorted_vals.insert(make_pair(name, make_pair(c.ident, c.key)));
    }
  for (set<pair<cert_value, pair<revision_id,
         rsa_keypair_id> > >::const_iterator i = sorted_vals.begin();
       i != sorted_vals.end(); ++i)
    {
      cout << i->first << " "
           << i->second.first  << " "
           << i->second.second  << "\n";
    }
}

static void
ls_vars(string name, app_state & app, vector<utf8> const & args)
{
  bool filterp;
  var_domain filter;
  if (args.size() == 0)
    {
      filterp = false;
    }
  else if (args.size() == 1)
    {
      filterp = true;
      internalize_var_domain(idx(args, 0), filter);
    }
  else
    throw usage(name);

  map<var_key, var_value> vars;
  app.db.get_vars(vars);
  for (map<var_key, var_value>::const_iterator i = vars.begin();
       i != vars.end(); ++i)
    {
      if (filterp && !(i->first.first == filter))
        continue;
      external ext_domain, ext_name;
      externalize_var_domain(i->first.first, ext_domain);
      cout << ext_domain << ": "
           << i->first.second << " "
           << i->second << "\n";
    }
}

static void
ls_known(app_state & app, vector<utf8> const & args)
{
  roster_t old_roster, new_roster;
  temp_node_id_source nis;

  app.require_workspace();
  app.work.get_base_and_current_roster_shape(old_roster, new_roster, nis);

  node_restriction mask(args_to_paths(args),
                        args_to_paths(app.opts.exclude_patterns),
                        app.opts.depth,
                        new_roster, app);

  node_map const & nodes = new_roster.all_nodes();
  for (node_map::const_iterator i = nodes.begin();
       i != nodes.end(); ++i)
    {
      node_id nid = i->first;

      if (!new_roster.is_root(nid)
          && mask.includes(new_roster, nid))
        {
          split_path sp;
          new_roster.get_name(nid, sp);
          cout << file_path(sp) << "\n";
        }
    }
}

static void
ls_unknown_or_ignored(app_state & app, bool want_ignored,
                      vector<utf8> const & args)
{
  app.require_workspace();

  vector<file_path> roots = args_to_paths(args);
  path_restriction mask(roots, args_to_paths(app.opts.exclude_patterns),
                        app.opts.depth, app);
  path_set unknown, ignored;

  // if no starting paths have been specified use the workspace root
  if (roots.empty())
    roots.push_back(file_path());

  app.work.find_unknown_and_ignored(mask, roots, unknown, ignored);

  if (want_ignored)
    for (path_set::const_iterator i = ignored.begin();
         i != ignored.end(); ++i)
      cout << file_path(*i) << "\n";
  else
    for (path_set::const_iterator i = unknown.begin();
         i != unknown.end(); ++i)
      cout << file_path(*i) << "\n";
}

static void
ls_missing(app_state & app, vector<utf8> const & args)
{
  temp_node_id_source nis;
  roster_t current_roster_shape;
  app.work.get_current_roster_shape(current_roster_shape, nis);
  node_restriction mask(args_to_paths(args),
                        args_to_paths(app.opts.exclude_patterns),
                        app.opts.depth,
                        current_roster_shape, app);

  path_set missing;
  app.work.find_missing(current_roster_shape, mask, missing);

  for (path_set::const_iterator i = missing.begin();
       i != missing.end(); ++i)
    {
      cout << file_path(*i) << "\n";
    }
}


static void
ls_changed(app_state & app, vector<utf8> const & args)
{
  roster_t old_roster, new_roster;
  cset included, excluded;
  set<file_path> files;
  temp_node_id_source nis;

  app.require_workspace();

  app.work.get_base_and_current_roster_shape(old_roster, new_roster, nis);

  node_restriction mask(args_to_paths(args),
                        args_to_paths(app.opts.exclude_patterns),
                        app.opts.depth,
                        old_roster, new_roster, app);

  app.work.update_current_roster_from_filesystem(new_roster, mask);
  make_restricted_csets(old_roster, new_roster,
                        included, excluded, mask);
  check_restricted_cset(old_roster, included);

  set<node_id> nodes;
  select_nodes_modified_by_cset(included, old_roster, new_roster, nodes);

  for (set<node_id>::const_iterator i = nodes.begin(); i != nodes.end();
       ++i)
    {
      split_path sp;
      if (old_roster.has_node(*i))
        old_roster.get_name(*i, sp);
      else
        new_roster.get_name(*i, sp);
      cout << sp << endl;
    }

}


CMD(list, N_("informative"),
    N_("certs ID\n"
       "keys [PATTERN]\n"
       "branches [PATTERN]\n"
       "epochs [BRANCH [...]]\n"
       "tags\n"
       "vars [DOMAIN]\n"
       "known\n"
       "unknown\n"
       "ignored\n"
       "missing\n"
       "changed"),
    N_("show database objects, or the current workspace manifest, \n"
       "or known, unknown, intentionally ignored, missing, or \n"
       "changed-state files"),
    options::opts::depth | options::opts::exclude)
{
  if (args.size() == 0)
    throw usage(name);

  vector<utf8>::const_iterator i = args.begin();
  ++i;
  vector<utf8> removed (i, args.end());
  if (idx(args, 0)() == "certs")
    ls_certs(name, app, removed);
  else if (idx(args, 0)() == "keys")
    ls_keys(name, app, removed);
  else if (idx(args, 0)() == "branches")
    ls_branches(name, app, removed);
  else if (idx(args, 0)() == "epochs")
    ls_epochs(name, app, removed);
  else if (idx(args, 0)() == "tags")
    ls_tags(name, app, removed);
  else if (idx(args, 0)() == "vars")
    ls_vars(name, app, removed);
  else if (idx(args, 0)() == "known")
    ls_known(app, removed);
  else if (idx(args, 0)() == "unknown")
    ls_unknown_or_ignored(app, false, removed);
  else if (idx(args, 0)() == "ignored")
    ls_unknown_or_ignored(app, true, removed);
  else if (idx(args, 0)() == "missing")
    ls_missing(app, removed);
  else if (idx(args, 0)() == "changed")
    ls_changed(app, removed);
  else
    throw usage(name);
}

ALIAS(ls, list)

namespace
{
  namespace syms
  {
    symbol const key("key");
    symbol const signature("signature");
    symbol const name("name");
    symbol const value("value");
    symbol const trust("trust");

    symbol const public_hash("public_hash");
    symbol const private_hash("private_hash");
    symbol const public_location("public_location");
    symbol const private_location("private_location");
  }
};

// Name: keys
// Arguments: none
// Added in: 1.1
// Purpose: Prints all keys in the keystore, and if a database is given
//   also all keys in the database, in basic_io format.
// Output format: For each key, a basic_io stanza is printed. The items in
//   the stanza are:
//     name - the key identifier
//     public_hash - the hash of the public half of the key
//     private_hash - the hash of the private half of the key
//     public_location - where the public half of the key is stored
//     private_location - where the private half of the key is stored
//   The *_location items may have multiple values, as shown below
//   for public_location.
//   If the private key does not exist, then the private_hash and
//   private_location items will be absent.
//
// Sample output:
//               name "tbrownaw@gmail.com"
//        public_hash [475055ec71ad48f5dfaf875b0fea597b5cbbee64]
//       private_hash [7f76dae3f91bb48f80f1871856d9d519770b7f8a]
//    public_location "database" "keystore"
//   private_location "keystore"
//
//              name "njs@pobox.com"
//       public_hash [de84b575d5e47254393eba49dce9dc4db98ed42d]
//   public_location "database"
//
//               name "foo@bar.com"
//        public_hash [7b6ce0bd83240438e7a8c7c207d8654881b763f6]
//       private_hash [bfc3263e3257087f531168850801ccefc668312d]
//    public_location "keystore"
//   private_location "keystore"
//
// Error conditions: None.
AUTOMATE(keys, "")
{
  if (args.size() != 0)
    throw usage(help_name);
  vector<rsa_keypair_id> dbkeys;
  vector<rsa_keypair_id> kskeys;
  // public_hash, private_hash, public_location, private_location
  map<string, boost::tuple<hexenc<id>, hexenc<id>,
                           vector<string>,
                           vector<string> > > items;
  if (app.db.database_specified())
    {
      transaction_guard guard(app.db, false);
      app.db.get_key_ids("", dbkeys);
      guard.commit();
    }
  app.keys.get_key_ids("", kskeys);

  for (vector<rsa_keypair_id>::iterator i = dbkeys.begin();
       i != dbkeys.end(); i++)
    {
      base64<rsa_pub_key> pub_encoded;
      hexenc<id> hash_code;

      app.db.get_key(*i, pub_encoded);
      key_hash_code(*i, pub_encoded, hash_code);
      items[(*i)()].get<0>() = hash_code;
      items[(*i)()].get<2>().push_back("database");
    }

  for (vector<rsa_keypair_id>::iterator i = kskeys.begin();
       i != kskeys.end(); i++)
    {
      keypair kp;
      hexenc<id> privhash, pubhash;
      app.keys.get_key_pair(*i, kp);
      key_hash_code(*i, kp.pub, pubhash);
      key_hash_code(*i, kp.priv, privhash);
      items[(*i)()].get<0>() = pubhash;
      items[(*i)()].get<1>() = privhash;
      items[(*i)()].get<2>().push_back("keystore");
      items[(*i)()].get<3>().push_back("keystore");
    }
  basic_io::printer prt;
  for (map<string, boost::tuple<hexenc<id>, hexenc<id>,
                                     vector<string>,
                                     vector<string> > >::iterator
         i = items.begin(); i != items.end(); ++i)
    {
      basic_io::stanza stz;
      stz.push_str_pair(syms::name, i->first);
      stz.push_hex_pair(syms::public_hash, i->second.get<0>());
      if (!i->second.get<1>()().empty())
        stz.push_hex_pair(syms::private_hash, i->second.get<1>());
      stz.push_str_multi(syms::public_location, i->second.get<2>());
      if (!i->second.get<3>().empty())
        stz.push_str_multi(syms::private_location, i->second.get<3>());
      prt.print_stanza(stz);
    }
  output.write(prt.buf.data(), prt.buf.size());
}

// Name: certs
// Arguments:
//   1: a revision id
// Added in: 1.0
// Purpose: Prints all certificates associated with the given revision
//   ID. Each certificate is contained in a basic IO stanza. For each
//   certificate, the following values are provided:
//
//   'key' : a string indicating the key used to sign this certificate.
//   'signature': a string indicating the status of the signature.
//   Possible values of this string are:
//     'ok'        : the signature is correct
//     'bad'       : the signature is invalid
//     'unknown'   : signature was made with an unknown key
//   'name' : the name of this certificate
//   'value' : the value of this certificate
//   'trust' : is this certificate trusted by the defined trust metric
//   Possible values of this string are:
//     'trusted'   : this certificate is trusted
//     'untrusted' : this certificate is not trusted
//
// Output format: All stanzas are formatted by basic_io. Stanzas are
// seperated by a blank line. Values will be escaped, '\' -> '\\' and
// '"' -> '\"'.
//
// Error conditions: If a certificate is signed with an unknown public
// key, a warning message is printed to stderr. If the revision
// specified is unknown or invalid prints an error message to stderr
// and exits with status 1.
AUTOMATE(certs, N_("REV"))
{
  if (args.size() != 1)
    throw usage(help_name);

  vector<cert> certs;

  transaction_guard guard(app.db, false);

  revision_id rid(idx(args, 0)());
  N(app.db.revision_exists(rid), F("No such revision %s") % rid);
  hexenc<id> ident(rid.inner());

  vector< revision<cert> > ts;
  app.db.get_revision_certs(rid, ts);
  for (size_t i = 0; i < ts.size(); ++i)
    certs.push_back(idx(ts, i).inner());

  {
    set<rsa_keypair_id> checked;
    for (size_t i = 0; i < certs.size(); ++i)
      {
        if (checked.find(idx(certs, i).key) == checked.end() &&
            !app.db.public_key_exists(idx(certs, i).key))
          W(F("no public key '%s' found in database")
            % idx(certs, i).key);
        checked.insert(idx(certs, i).key);
      }
  }

  // Make the output deterministic; this is useful for the test suite,
  // in particular.
  sort(certs.begin(), certs.end());

  basic_io::printer pr;

  for (size_t i = 0; i < certs.size(); ++i)
    {
      basic_io::stanza st;
      cert_status status = check_cert(app, idx(certs, i));
      cert_value tv;
      cert_name name = idx(certs, i).name();
      set<rsa_keypair_id> signers;

      decode_base64(idx(certs, i).value, tv);

      rsa_keypair_id keyid = idx(certs, i).key();
      signers.insert(keyid);

      bool trusted =
        app.lua.hook_get_revision_cert_trust(signers, ident,
                                             name, tv);

      st.push_str_pair(syms::key, keyid());

      string stat;
      switch (status)
        {
        case cert_ok:
          stat = "ok";
          break;
        case cert_bad:
          stat = "bad";
          break;
        case cert_unknown:
          stat = "unknown";
          break;
        }
      st.push_str_pair(syms::signature, stat);

      st.push_str_pair(syms::name, name());
      st.push_str_pair(syms::value, tv());
      st.push_str_pair(syms::trust, (trusted ? "trusted" : "untrusted"));

      pr.print_stanza(st);
    }
  output.write(pr.buf.data(), pr.buf.size());

  guard.commit();
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
