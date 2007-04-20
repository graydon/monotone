// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <iostream>
#include <utility>

#include "charset.hh"
#include "cmd.hh"
#include "database_check.hh"
#include "revision.hh"
#include "constants.hh"
#include "app_state.hh"

using std::cin;
using std::cout;
using std::make_pair;
using std::pair;
using std::set;
using std::string;

// Deletes a revision from the local database.  This can be used to
// 'undo' a changed revision from a local database without leaving
// (much of) a trace.

static void
kill_rev_locally(app_state& app, string const& id)
{
  revision_id ident;
  complete(app, id, ident);
  N(app.db.revision_exists(ident),
    F("no such revision '%s'") % ident);

  //check that the revision does not have any children
  set<revision_id> children;
  app.db.get_revision_children(ident, children);
  N(!children.size(),
    F("revision %s already has children. We cannot kill it.") % ident);

  app.db.delete_existing_rev_and_certs(ident);
}

CMD(db, "", CMD_REF(database),
    N_("init\n"
      "info\n"
      "version\n"
      "dump\n"
      "load\n"
      "migrate\n"
      "execute\n"
      "kill_rev_locally ID\n"
      "kill_branch_certs_locally BRANCH\n"
      "kill_tag_locally TAG\n"
      "check\n"
      "changesetify\n"
      "rosterify\n"
      "regenerate_caches\n"
      "set_epoch BRANCH EPOCH\n"),
    N_("Manipulates database state"),
    N_("This set of commands are used to create and maintain the database "
       "that monotone uses to maintain the certificates' data."),
    options::opts::drop_attr)
{
  if (args.size() == 1)
    {
      if (idx(args, 0)() == "init")
        app.db.initialize();
      else if (idx(args, 0)() == "info")
        app.db.info(cout);
      else if (idx(args, 0)() == "version")
        app.db.version(cout);
      else if (idx(args, 0)() == "dump")
        app.db.dump(cout);
      else if (idx(args, 0)() == "load")
        app.db.load(cin);
      else if (idx(args, 0)() == "migrate")
        app.db.migrate();
      else if (idx(args, 0)() == "check")
        check_db(app);
      else if (idx(args, 0)() == "changesetify")
        build_changesets_from_manifest_ancestry(app);
      else if (idx(args, 0)() == "rosterify")
        build_roster_style_revs_from_manifest_style_revs(app);
      else if (idx(args, 0)() == "regenerate_caches")
        regenerate_caches(app);
      else
        throw usage(ident());
    }
  else if (args.size() == 2)
    {
      if (idx(args, 0)() == "execute")
        app.db.debug(idx(args, 1)(), cout);
      else if (idx(args, 0)() == "kill_rev_locally")
        kill_rev_locally(app,idx(args, 1)());
      else if (idx(args, 0)() == "clear_epoch")
        app.db.clear_epoch(branch_name(idx(args, 1)()));
      else if (idx(args, 0)() == "kill_branch_certs_locally")
        app.db.delete_branch_named(cert_value(idx(args, 1)()));
      else if (idx(args, 0)() == "kill_tag_locally")
        app.db.delete_tag_named(cert_value(idx(args, 1)()));
      else
        throw usage(ident());
    }
  else if (args.size() == 3)
    {
      if (idx(args, 0)() == "set_epoch")
        {
          epoch_data ed(idx(args,2)());
          N(ed.inner()().size() == constants::epochlen,
            F("The epoch must be %s characters") 
            % constants::epochlen);
          app.db.set_epoch(branch_name(idx(args, 1)()), ed);
        }
      else
        throw usage(ident());
    }
  else
    throw usage(ident());
}

CMD(set, "", CMD_REF(variables), N_("DOMAIN NAME VALUE"),
    N_("Sets a database variable"),
    N_("This command modifies (or adds if it did not exist before) the "
       "variable named NAME, stored in the database, and sets it to the "
       "given value in VALUE.  The variable is placed in the domain DOMAIN."),
    options::opts::none)
{
  if (args.size() != 3)
    throw usage(ident());

  var_domain d;
  var_name n;
  var_value v;
  internalize_var_domain(idx(args, 0), d);
  n = var_name(idx(args, 1)());
  v = var_value(idx(args, 2)());
  app.db.set_var(make_pair(d, n), v);
}

CMD(unset, "", CMD_REF(variables), N_("DOMAIN NAME"),
    N_("Unsets a database variable"),
    N_("This command removes the variable NAME from domain DOMAIN, which"
       "was previously stored in the database."),
    options::opts::none)
{
  if (args.size() != 2)
    throw usage(ident());

  var_domain d;
  var_name n;
  internalize_var_domain(idx(args, 0), d);
  n = var_name(idx(args, 1)());
  var_key k(d, n);
  N(app.db.var_exists(k), 
    F("no var with name %s in domain %s") % n % d);
  app.db.clear_var(k);
}

CMD(complete, "", CMD_REF(informative), N_("(revision|file|key) PARTIAL-ID"),
    N_("Completes a partial identifier"),
    N_(""),
    options::opts::verbose)
{
  if (args.size() != 2)
    throw usage(ident());

  bool verbose = app.opts.verbose;

  N(idx(args, 1)().find_first_not_of("abcdef0123456789") == string::npos,
    F("non-hex digits in partial id"));

  if (idx(args, 0)() == "revision")
    {
      set<revision_id> completions;
      app.db.complete(idx(args, 1)(), completions);
      for (set<revision_id>::const_iterator i = completions.begin();
           i != completions.end(); ++i)
        {
          if (!verbose) cout << i->inner()() << '\n';
          else cout << describe_revision(app, *i) << '\n';
        }
    }
  else if (idx(args, 0)() == "file")
    {
      set<file_id> completions;
      app.db.complete(idx(args, 1)(), completions);
      for (set<file_id>::const_iterator i = completions.begin();
           i != completions.end(); ++i)
        cout << i->inner()() << '\n';
    }
  else if (idx(args, 0)() == "key")
    {
      typedef set< pair<key_id, utf8 > > completions_t;
      completions_t completions;
      app.db.complete(idx(args, 1)(), completions);
      for (completions_t::const_iterator i = completions.begin();
           i != completions.end(); ++i)
        {
          cout << i->first.inner()();
          if (verbose) cout << ' ' << i->second();
          cout << '\n';
        }
    }
  else
    throw usage(ident());
}

CMD(test_migration_step, "", CMD_REF(hidden), "SCHEMA",
    N_("Runs one step of migration on the specified database"),
    N_("This command migrates the given database from the specified schema "
       "in SCHEMA to its successor."),
    options::opts::none)
{
  if (args.size() != 1)
    throw usage(ident());
  app.db.test_migration_step(idx(args,0)());
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
