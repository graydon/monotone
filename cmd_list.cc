// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <algorithm>
#include "safe_map.hh"
#include <utility>
#include <iostream>
#include <iterator>

#include <boost/tuple/tuple.hpp>

#include "basic_io.hh"
#include "cert.hh"
#include "charset.hh"
#include "cmd.hh"
#include "roster.hh"
#include "database.hh"
#include "globish.hh"
#include "keys.hh"
#include "key_store.hh"
#include "restrictions.hh"
#include "revision.hh"
#include "simplestring_xform.hh"
#include "transforms.hh"
#include "ui.hh"
#include "vocab_cast.hh"
#include "app_state.hh"
#include "project.hh"
#include "work.hh"

using std::cout;
using std::make_pair;
using std::map;
using std::ostream_iterator;
using std::pair;
using std::set;
using std::sort;
using std::copy;
using std::string;
using std::vector;

CMD_GROUP(list, "list", "ls", CMD_REF(informative),
          N_("Shows database objects"),
          N_("This command is used to query information from the database.  "
             "It shows database objects, or the current workspace manifest, "
             "or known, unknown, intentionally ignored, missing, or "
             "changed-state files."));

CMD(certs, "certs", "", CMD_REF(list), "ID",
    N_("Lists certificates attached to an identifier"),
    "",
    options::opts::depth | options::opts::exclude)
{
  if (args.size() != 1)
    throw usage(execid);

  database db(app);
  project_t project(db);
  vector<cert> certs;

  transaction_guard guard(db, false);

  revision_id ident;
  complete(app.opts, app.lua,  project, idx(args, 0)(), ident);
  vector< revision<cert> > ts;
  project.get_revision_certs(ident, ts);

  for (size_t i = 0; i < ts.size(); ++i)
    certs.push_back(idx(ts, i).inner());

  {
    set<rsa_keypair_id> checked;
    for (size_t i = 0; i < certs.size(); ++i)
      {
        if (checked.find(idx(certs, i).key) == checked.end() &&
            !db.public_key_exists(idx(certs, i).key))
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
      colon_pos = display_width(utf8(substr));
      extra_str = string(colon_pos, ' ') + ": %s\n";
    }

  for (size_t i = 0; i < certs.size(); ++i)
    {
      cert_status status = check_cert(db, idx(certs, i));
      cert_value tv = idx(certs, i).value;
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
      std::string value_first_line = lines.size() > 0 ? idx(lines, 0) : "";

      cout << string(guess_terminal_width(), '-') << '\n'
           << (i18n_format(str)
               % idx(certs, i).key()
               % stat
               % idx(certs, i).name()
               % value_first_line);

      for (size_t i = 1; i < lines.size(); ++i)
        cout << (i18n_format(extra_str) % idx(lines, i));
    }

  if (certs.size() > 0)
    cout << '\n';

  guard.commit();
}

CMD(keys, "keys", "", CMD_REF(list), "[PATTERN]",
    N_("Lists keys that match a pattern"),
    "",
    options::opts::depth | options::opts::exclude)
{
  database db(app);
  key_store keys(app);

  vector<rsa_keypair_id> pubs;
  vector<rsa_keypair_id> privkeys;
  globish pattern("*");
  if (args.size() == 1)
    pattern = globish(idx(args, 0)());
  else if (args.size() > 1)
    throw usage(execid);

  if (db.database_specified())
    db.get_key_ids(pattern, pubs);

  keys.get_key_ids(pattern, privkeys);

  // true if it is in the database, false otherwise
  map<rsa_keypair_id, bool> pubkeys;
  for (vector<rsa_keypair_id>::const_iterator i = pubs.begin();
       i != pubs.end(); i++)
    pubkeys[*i] = true;

  set<rsa_keypair_id> bad_keys;

  bool all_in_db = true;
  for (vector<rsa_keypair_id>::const_iterator i = privkeys.begin();
       i != privkeys.end(); i++)
    {
      if (pubkeys.find(*i) == pubkeys.end())
        {
          pubkeys[*i] = false;
          all_in_db = false;
        }
      else if (db.database_specified())
        {
          // we've found a key that should have both a public and a private version
          rsa_pub_key pub_key;
          keypair priv_key;
          db.get_key(*i, pub_key);
          keys.get_key_pair(*i, priv_key);
          if (!keys_match(*i, pub_key, *i, priv_key.pub))
            bad_keys.insert(*i);
        }
    }

  if (pubkeys.size() > 0)
    {
      cout << "\n[public keys]\n";
      for (map<rsa_keypair_id, bool>::iterator i = pubkeys.begin();
           i != pubkeys.end(); i++)
        {
          rsa_pub_key pub_encoded;
          hexenc<id> hash_code;
          rsa_keypair_id keyid = i->first;
          bool indb = i->second;

          if (indb)
            db.get_key(keyid, pub_encoded);
          else
            {
              keypair kp;
              keys.get_key_pair(keyid, kp);
              pub_encoded = kp.pub;
            }
          key_hash_code(keyid, pub_encoded, hash_code);
          if (indb)
            cout << hash_code << ' ' << keyid << '\n';
          else
            cout << hash_code << ' ' << keyid << "   (*)\n";
        }
      if (!all_in_db)
        cout << (F("(*) - only in %s/")
                 % keys.get_key_dir()) << '\n';
      cout << '\n';
    }

  if (privkeys.size() > 0)
    {
      cout << "\n[private keys]\n";
      for (vector<rsa_keypair_id>::iterator i = privkeys.begin();
           i != privkeys.end(); i++)
        {
          keypair kp;
          hexenc<id> hash_code;
          keys.get_key_pair(*i, kp);
          key_hash_code(*i, kp.priv, hash_code);
          cout << hash_code << ' ' << *i << '\n';
        }
      cout << '\n';
    }

  if (!bad_keys.empty())
    {
      W(F("Some keys in the database have the same ID as, "
          "but different hashes to, keys in your local key store!"));
      for (set<rsa_keypair_id>::const_iterator i = bad_keys.begin(); i != bad_keys.end(); i++)
        {
          W(F("Mismatched Key: %s") % *i);
        }
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

CMD(branches, "branches", "", CMD_REF(list), "[PATTERN]",
    N_("Lists branches in the database that match a pattern"),
    "",
    options::opts::depth | options::opts::exclude)
{
  globish inc("*");
  if (args.size() == 1)
    inc = globish(idx(args,0)());
  else if (args.size() > 1)
    throw usage(execid);

  database db(app);
  project_t project(db);
  globish exc(app.opts.exclude_patterns);
  set<branch_name> names;
  project.get_branch_list(inc, names, !app.opts.ignore_suspend_certs);

  for (set<branch_name>::const_iterator i = names.begin();
       i != names.end(); ++i)
    if (!exc.matches((*i)()) && !app.lua.hook_ignore_branch(*i))
      cout << *i << '\n';
}

CMD(epochs, "epochs", "", CMD_REF(list), "[BRANCH [...]]",
    N_("Lists the current epoch of branches that match a pattern"),
    "",
    options::opts::depth | options::opts::exclude)
{
  database db(app);
  map<branch_name, epoch_data> epochs;
  db.get_epochs(epochs);

  if (args.size() == 0)
    {
      for (map<branch_name, epoch_data>::const_iterator
             i = epochs.begin();
           i != epochs.end(); ++i)
        {
          cout << i->second << ' ' << i->first << '\n';
        }
    }
  else
    {
      for (args_vector::const_iterator i = args.begin();
           i != args.end();
           ++i)
        {
          map<branch_name, epoch_data>::const_iterator j = epochs.find(branch_name((*i)()));
          N(j != epochs.end(), F("no epoch for branch %s") % *i);
          cout << j->second << ' ' << j->first << '\n';
        }
    }
}

CMD(tags, "tags", "", CMD_REF(list), "",
    N_("Lists all tags in the database"),
    "",
    options::opts::depth | options::opts::exclude)
{
  database db(app);
  set<tag_t> tags;
  project_t project(db);
  project.get_tags(tags);

  for (set<tag_t>::const_iterator i = tags.begin(); i != tags.end(); ++i)
    {
      cout << i->name << ' '
           << i->ident  << ' '
           << i->key  << '\n';
    }
}

CMD(vars, "vars", "", CMD_REF(list), "[DOMAIN]",
    N_("Lists variables in the whole database or a domain"),
    "",
    options::opts::depth | options::opts::exclude)
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
    throw usage(execid);

  database db(app);
  map<var_key, var_value> vars;
  db.get_vars(vars);
  for (map<var_key, var_value>::const_iterator i = vars.begin();
       i != vars.end(); ++i)
    {
      if (filterp && !(i->first.first == filter))
        continue;
      external ext_domain, ext_name;
      externalize_var_domain(i->first.first, ext_domain);
      cout << ext_domain << ": "
           << i->first.second << ' '
           << i->second << '\n';
    }
}

CMD(known, "known", "", CMD_REF(list), "",
    N_("Lists workspace files that belong to the current branch"),
    "",
    options::opts::depth | options::opts::exclude)
{
  database db(app);
  workspace work(app);

  roster_t new_roster;
  temp_node_id_source nis;
  work.get_current_roster_shape(db, nis, new_roster);

  node_restriction mask(work, args_to_paths(args),
                        args_to_paths(app.opts.exclude_patterns),
                        app.opts.depth,
                        new_roster);

  // to be printed sorted
  vector<file_path> print_paths;

  node_map const & nodes = new_roster.all_nodes();
  for (node_map::const_iterator i = nodes.begin();
       i != nodes.end(); ++i)
    {
      node_id nid = i->first;

      if (!new_roster.is_root(nid)
          && mask.includes(new_roster, nid))
        {
          file_path p;
          new_roster.get_name(nid, p);
          print_paths.push_back(p);
        }
    }
    
  sort(print_paths.begin(), print_paths.end());
  copy(print_paths.begin(), print_paths.end(),
       ostream_iterator<file_path>(cout, "\n"));
}

CMD(unknown, "unknown", "ignored", CMD_REF(list), "",
    N_("Lists workspace files that do not belong to the current branch"),
    "",
    options::opts::depth | options::opts::exclude)
{
  database db(app);
  workspace work(app);

  vector<file_path> roots = args_to_paths(args);
  path_restriction mask(work, roots, args_to_paths(app.opts.exclude_patterns),
                        app.opts.depth);
  set<file_path> unknown, ignored;

  // if no starting paths have been specified use the workspace root
  if (roots.empty())
    roots.push_back(file_path());

  work.find_unknown_and_ignored(db, mask, roots, unknown, ignored);

  utf8 const & realname = execid[execid.size() - 1];
  if (realname() == "ignored")
    copy(ignored.begin(), ignored.end(),
         ostream_iterator<file_path>(cout, "\n"));
  else
    {
      I(realname() == "unknown");
      copy(unknown.begin(), unknown.end(),
           ostream_iterator<file_path>(cout, "\n"));
    }
}

CMD(missing, "missing", "", CMD_REF(list), "",
    N_("Lists files that belong to the branch but are not in the workspace"),
    "",
    options::opts::depth | options::opts::exclude)
{
  database db(app);
  workspace work(app);
  temp_node_id_source nis;
  roster_t current_roster_shape;
  work.get_current_roster_shape(db, nis, current_roster_shape);
  node_restriction mask(work, args_to_paths(args),
                        args_to_paths(app.opts.exclude_patterns),
                        app.opts.depth,
                        current_roster_shape);

  set<file_path> missing;
  work.find_missing(current_roster_shape, mask, missing);

  copy(missing.begin(), missing.end(),
       ostream_iterator<file_path>(cout, "\n"));
}


CMD(changed, "changed", "", CMD_REF(list), "",
    N_("Lists files that have changed with respect to the current revision"),
    "",
    options::opts::depth | options::opts::exclude)
{
  database db(app);
  workspace work(app);

  parent_map parents;
  roster_t new_roster;
  temp_node_id_source nis;
  work.get_current_roster_shape(db, nis, new_roster);
  work.update_current_roster_from_filesystem(new_roster);

  work.get_parent_rosters(db, parents);

  node_restriction mask(work, args_to_paths(args),
                        args_to_paths(app.opts.exclude_patterns),
                        app.opts.depth,
                        parents, new_roster);

  revision_t rrev;
  make_restricted_revision(parents, new_roster, mask, rrev);

  // to be printed sorted, with duplicates removed
  set<file_path> print_paths;

  for (edge_map::const_iterator i = rrev.edges.begin();
       i != rrev.edges.end(); i++)
    {
      set<node_id> nodes;
      roster_t const & old_roster
        = *safe_get(parents, edge_old_revision(i)).first;
      select_nodes_modified_by_cset(edge_changes(i),
                                    old_roster, new_roster, nodes);

      for (set<node_id>::const_iterator i = nodes.begin(); i != nodes.end();
           ++i)
        {
          file_path p;
          if (new_roster.has_node(*i))
            new_roster.get_name(*i, p);
          else
            old_roster.get_name(*i, p);
          print_paths.insert(p);
        }
    }

  copy(print_paths.begin(), print_paths.end(),
       ostream_iterator<file_path>(cout, "\n"));
}

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
CMD_AUTOMATE(keys, "",
             N_("Lists all keys in the keystore"),
             "",
             options::opts::none)
{
  N(args.size() == 0,
    F("no arguments needed"));

  database db(app);
  key_store keys(app);

  vector<rsa_keypair_id> dbkeys;
  vector<rsa_keypair_id> kskeys;
  // public_hash, private_hash, public_location, private_location
  map<string, boost::tuple<hexenc<id>, hexenc<id>,
                           vector<string>,
                           vector<string> > > items;
  if (db.database_specified())
    db.get_key_ids(dbkeys);

  keys.get_key_ids(kskeys);

  for (vector<rsa_keypair_id>::iterator i = dbkeys.begin();
       i != dbkeys.end(); i++)
    {
      rsa_pub_key pub_encoded;
      hexenc<id> hash_code;

      db.get_key(*i, pub_encoded);
      key_hash_code(*i, pub_encoded, hash_code);
      items[(*i)()].get<0>() = hash_code;
      items[(*i)()].get<2>().push_back("database");
    }

  for (vector<rsa_keypair_id>::iterator i = kskeys.begin();
       i != kskeys.end(); i++)
    {
      keypair kp;
      hexenc<id> privhash, pubhash;
      keys.get_key_pair(*i, kp);
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
CMD_AUTOMATE(certs, N_("REV"),
             N_("Prints all certificates attached to a revision"),
             "",
             options::opts::none)
{
  N(args.size() == 1,
    F("wrong argument count"));

  database db(app);
  project_t project(db);

  vector<cert> certs;

  transaction_guard guard(db, false);

  revision_id rid(idx(args, 0)());
  N(db.revision_exists(rid), F("no such revision '%s'") % rid);
  hexenc<id> ident(rid.inner());

  vector< revision<cert> > ts;
  // FIXME_PROJECTS: after projects are implemented,
  // use the db version instead if no project is specified.
  project.get_revision_certs(rid, ts);

  for (size_t i = 0; i < ts.size(); ++i)
    certs.push_back(idx(ts, i).inner());

  {
    set<rsa_keypair_id> checked;
    for (size_t i = 0; i < certs.size(); ++i)
      {
        if (checked.find(idx(certs, i).key) == checked.end() &&
            !db.public_key_exists(idx(certs, i).key))
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
      cert_status status = check_cert(db, idx(certs, i));
      cert_value tv = idx(certs, i).value;
      cert_name name = idx(certs, i).name;
      set<rsa_keypair_id> signers;

      rsa_keypair_id keyid = idx(certs, i).key;
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
