#include "cmd.hh"

#include "globish.hh"
#include "restrictions.hh"
#include "revision.hh"
#include "transforms.hh"
#include "database.hh"
#include "ui.hh"
#include "keys.hh"

#include <iostream>
using std::cout;
#include <utility>
using std::pair;
using std::set;
#include <map>
using std::map;

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

      cout << std::string(guess_terminal_width(), '-') << '\n'
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
ls_keys(string const & name, app_state & app, vector<utf8> const & args)
{
  vector<rsa_keypair_id> pubs;
  vector<rsa_keypair_id> privkeys;
  std::string pattern;
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
        cout << F("(*) - only in %s/") % app.keys.get_key_dir() << "\n";
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
        P(F("no keys found\n"));
      else
        W(F("no keys found matching '%s'\n") % idx(args, 0)());
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
  combine_and_check_globish(app.exclude_patterns, exc);
  globish_matcher match(inc, exc);
  vector<string> names;
  app.db.get_branches(names);

  sort(names.begin(), names.end());
  for (size_t i = 0; i < names.size(); ++i)
    if (match(idx(names, i)) && !app.lua.hook_ignore_branch(idx(names, i)))
      cout << idx(names, i) << "\n";
}

static void 
ls_epochs(string name, app_state & app, vector<utf8> const & args)
{
  std::map<cert_value, epoch_data> epochs;
  app.db.get_epochs(epochs);

  if (args.size() == 0)
    {
      for (std::map<cert_value, epoch_data>::const_iterator i = epochs.begin();
           i != epochs.end(); ++i)
        {
          cout << i->second << " " << i->first << "\n";
        }
    }
  else
    {
      for (vector<utf8>::const_iterator i = args.begin(); i != args.end();
           ++i)
        {
          std::map<cert_value, epoch_data>::const_iterator j = epochs.find(cert_value((*i)()));
          N(j != epochs.end(), F("no epoch for branch %s\n") % *i);
          cout << j->second << " " << j->first << "\n";
        }
    }  
}

static void 
ls_tags(string name, app_state & app, vector<utf8> const & args)
{
  vector< revision<cert> > certs;
  app.db.get_revision_certs(tag_cert_name, certs);

  std::set< pair<cert_value, pair<revision_id, rsa_keypair_id> > > sorted_vals;

  for (vector< revision<cert> >::const_iterator i = certs.begin();
       i != certs.end(); ++i)
    {
      cert_value name;
      cert c = i->inner();
      decode_base64(c.value, name);
      sorted_vals.insert(std::make_pair(name, std::make_pair(c.ident, c.key)));
    }
  for (std::set<std::pair<cert_value, std::pair<revision_id, 
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
  for (std::map<var_key, var_value>::const_iterator i = vars.begin();
       i != vars.end(); ++i)
    {
      if (filterp && !(i->first.first == filter))
        continue;
      external ext_domain, ext_name;
      externalize_var_domain(i->first.first, ext_domain);
      cout << ext_domain << ": " << i->first.second << " " << i->second << "\n";
    }
}

static void
ls_known(app_state & app, vector<utf8> const & args)
{
  roster_t old_roster, new_roster;
  temp_node_id_source nis;

  app.require_workspace();
  get_base_and_current_roster_shape(old_roster, new_roster, nis, app);

  restriction mask(args, app.exclude_patterns, new_roster, app);

  node_map const & nodes = new_roster.all_nodes();
  for (node_map::const_iterator i = nodes.begin(); i != nodes.end(); ++i)
    {
      node_id nid = i->first;

      if (!new_roster.is_root(nid) && mask.includes(new_roster, nid))
        {
          split_path sp;
          new_roster.get_name(nid, sp);
          cout << file_path(sp) << "\n";
        }
    }
}

static void
ls_unknown_or_ignored(app_state & app, bool want_ignored, vector<utf8> const & args)
{
  app.require_workspace();

  path_set unknown, ignored;
  find_unknown_and_ignored(app, args, unknown, ignored);

  if (want_ignored)
    for (path_set::const_iterator i = ignored.begin(); i != ignored.end(); ++i)
      cout << file_path(*i) << "\n";
  else
    for (path_set::const_iterator i = unknown.begin(); i != unknown.end(); ++i)
      cout << file_path(*i) << "\n";
}

static void
ls_missing(app_state & app, vector<utf8> const & args)
{
  path_set missing;
  find_missing(app, args, missing);

  for (path_set::const_iterator i = missing.begin(); i != missing.end(); ++i)
    {
      cout << file_path(*i) << "\n";
    }
}


static void
ls_changed(app_state & app, vector<utf8> const & args)
{
  roster_t old_roster, new_roster;
  cset included, excluded;
  std::set<file_path> files;
  temp_node_id_source nis;

  app.require_workspace();

  get_base_and_current_roster_shape(old_roster, new_roster, nis, app);

  restriction mask(args, app.exclude_patterns, old_roster, new_roster, app);
      
  update_current_roster_from_filesystem(new_roster, mask, app);
  make_restricted_csets(old_roster, new_roster, included, excluded, mask);
  check_restricted_cset(old_roster, included);

  // FIXME: this would probably be better as a function of roster.cc
  // set<node_id> nodes;
  // select_nodes_modified_by_cset(included, old_roster, new_roster, nodes);

  for (path_set::const_iterator i = included.nodes_deleted.begin();
       i != included.nodes_deleted.end(); ++i)
    {
      if (mask.includes(*i))
        files.insert(file_path(*i));
    }
  for (std::map<split_path, split_path>::const_iterator 
         i = included.nodes_renamed.begin();
       i != included.nodes_renamed.end(); ++i)
    {
      // FIXME: is reporting the old name the "right" thing to do?
      if (mask.includes(i->first))
        files.insert(file_path(i->first));
    }
  for (path_set::const_iterator i = included.dirs_added.begin();
       i != included.dirs_added.end(); ++i)
    {
      if (mask.includes(*i))
        files.insert(file_path(*i));
    }
  for (std::map<split_path, file_id>::const_iterator i = included.files_added.begin();
       i != included.files_added.end(); ++i)
    {
      if (mask.includes(i->first))
        files.insert(file_path(i->first));
    }
  for (std::map<split_path, std::pair<file_id, file_id> >::const_iterator
         i = included.deltas_applied.begin(); i != included.deltas_applied.end(); 
       ++i)
    {
      if (mask.includes(i->first))
        files.insert(file_path(i->first));
    }
  // FIXME: should attr changes count?

  copy(files.begin(), files.end(),
       std::ostream_iterator<const file_path>(cout, "\n"));
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
    N_("show database objects, or the current workspace manifest, or known,\n"
       "unknown, intentionally ignored, missing, or changed state files"),
    OPT_DEPTH % OPT_EXCLUDE)
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
