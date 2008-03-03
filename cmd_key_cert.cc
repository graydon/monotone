// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <iostream>
#include <sstream>
#include <fstream>
#include <iterator>

#include "charset.hh"
#include "cmd.hh"
#include "app_state.hh"
#include "database.hh"
#include "project.hh"
#include "keys.hh"
#include "key_store.hh"
#include "transforms.hh"

using std::cout;
using std::ostream_iterator;
using std::ostringstream;
using std::set;
using std::string;
using std::ofstream;

CMD(genkey, "genkey", "", CMD_REF(key_and_cert), N_("KEYID"),
    N_("Generates an RSA key-pair"),
    "",
    options::opts::none)
{
  database db(app);
  key_store keys(app);

  if (args.size() != 1)
    throw usage(execid);

  rsa_keypair_id ident;
  internalize_rsa_keypair_id(idx(args, 0), ident);

  keys.create_key_pair(db, ident);
}

CMD(dropkey, "dropkey", "", CMD_REF(key_and_cert), N_("KEYID"),
    N_("Drops a public and/or private key"),
    "",
    options::opts::none)
{
  database db(app);
  key_store keys(app);
  bool key_deleted = false;
  bool checked_db = false;

  if (args.size() != 1)
    throw usage(execid);

  rsa_keypair_id ident(idx(args, 0)());
  if (db.database_specified())
    {
      transaction_guard guard(db);
      if (db.public_key_exists(ident))
        {
          P(F("dropping public key '%s' from database") % ident);
          db.delete_public_key(ident);
          key_deleted = true;
        }
      guard.commit();
      checked_db = true;
    }

  if (keys.key_pair_exists(ident))
    {
      P(F("dropping key pair '%s' from keystore") % ident);
      keys.delete_key(ident);
      key_deleted = true;
    }

  i18n_format fmt;
  if (checked_db)
    fmt = F("public or private key '%s' does not exist "
            "in keystore or database");
  else
    fmt = F("public or private key '%s' does not exist "
            "in keystore, and no database was specified");
  N(key_deleted, fmt % idx(args, 0)());
}

CMD(passphrase, "passphrase", "", CMD_REF(key_and_cert), N_("KEYID"),
    N_("Changes the passphrase of a private RSA key"),
    "",
    options::opts::none)
{
  key_store keys(app);

  if (args.size() != 1)
    throw usage(execid);

  rsa_keypair_id ident;
  internalize_rsa_keypair_id(idx(args, 0), ident);

  keys.change_key_passphrase(ident);
  P(F("passphrase changed"));
}

CMD(ssh_agent_export, "ssh_agent_export", "", CMD_REF(key_and_cert),
    N_("[FILENAME]"),
    N_("Exports a private key for use with ssh-agent"),
    "",
    options::opts::none)
{
  database db(app);
  key_store keys(app);

  if (args.size() > 1)
    throw usage(execid);

  rsa_keypair_id id;
  get_user_key(app.opts, app.lua, db, keys, id);

  if (args.size() == 0)
    keys.export_key_for_agent(id, cout);
  else
    {
      string external_path = system_path(idx(args, 0)).as_external();
      ofstream fout(external_path.c_str(), ofstream::out);
      keys.export_key_for_agent(id, fout);
    }
}

CMD(ssh_agent_add, "ssh_agent_add", "", CMD_REF(key_and_cert), "",
    N_("Adds a private key to ssh-agent"),
    "",
    options::opts::none)
{
  database db(app);
  key_store keys(app);

  if (args.size() > 1)
    throw usage(execid);

  rsa_keypair_id id;
  get_user_key(app.opts, app.lua, db, keys, id);
  keys.add_key_to_agent(id);
}

CMD(cert, "cert", "", CMD_REF(key_and_cert),
    N_("REVISION CERTNAME [CERTVAL]"),
    N_("Creates a certificate for a revision"),
    "",
    options::opts::none)
{
  database db(app);
  key_store keys(app);
  project_t project(db);

  if ((args.size() != 3) && (args.size() != 2))
    throw usage(execid);

  transaction_guard guard(db);

  revision_id rid;
  complete(app.opts, app.lua,  project, idx(args, 0)(), rid);

  cert_name cname;
  internalize_cert_name(idx(args, 1), cname);

  cache_user_key(app.opts, app.lua, db, keys);

  cert_value val;
  if (args.size() == 3)
    val = cert_value(idx(args, 2)());
  else
    {
      data dat;
      read_data_stdin(dat);
      val = cert_value(dat());
    }

  project.put_cert(keys, rid, cname, val);
  guard.commit();
}

CMD(trusted, "trusted", "", CMD_REF(key_and_cert), 
    N_("REVISION NAME VALUE SIGNER1 [SIGNER2 [...]]"),
    N_("Tests whether a hypothetical certificate would be trusted"),
    N_("The current settings are used to run the test."),
    options::opts::none)
{
  database db(app);
  project_t project(db);

  if (args.size() < 4)
    throw usage(execid);

  set<revision_id> rids;
  expand_selector(app.opts, app.lua, project, idx(args, 0)(), rids);
  diagnose_ambiguous_expansion(project, idx(args, 0)(), rids);

  revision_id ident;
  if (!rids.empty())
    ident = *rids.begin();

  cert_name cname;
  internalize_cert_name(idx(args, 1), cname);

  cert_value value(idx(args, 2)());

  set<rsa_keypair_id> signers;
  for (unsigned int i = 3; i != args.size(); ++i)
    {
      rsa_keypair_id keyid;
      internalize_rsa_keypair_id(idx(args, i), keyid);
      signers.insert(keyid);
    }


  bool trusted = app.lua.hook_get_revision_cert_trust(signers, ident,
                                                      cname, value);


  ostringstream all_signers;
  copy(signers.begin(), signers.end(),
       ostream_iterator<rsa_keypair_id>(all_signers, " "));

  cout << (F("if a cert on: %s\n"
            "with key: %s\n"
            "and value: %s\n"
            "was signed by: %s\n"
            "it would be: %s")
    % encode_hexenc(ident.inner()())
    % cname
    % value
    % all_signers.str()
    % (trusted ? _("trusted") : _("UNtrusted")))
    << '\n'; // final newline is kept out of the translation
}

CMD(tag, "tag", "", CMD_REF(review), N_("REVISION TAGNAME"),
    N_("Puts a symbolic tag certificate on a revision"),
    "",
    options::opts::none)
{
  database db(app);
  key_store keys(app);
  project_t project(db);

  if (args.size() != 2)
    throw usage(execid);

  revision_id r;
  complete(app.opts, app.lua, project, idx(args, 0)(), r);

  cache_user_key(app.opts, app.lua, db, keys);
  project.put_tag(keys, r, idx(args, 1)());
}


CMD(testresult, "testresult", "", CMD_REF(review),
    N_("ID (pass|fail|true|false|yes|no|1|0)"),
    N_("Notes the results of running a test on a revision"),
    "",
    options::opts::none)
{
  database db(app);
  key_store keys(app);
  project_t project(db);

  if (args.size() != 2)
    throw usage(execid);

  revision_id r;
  complete(app.opts, app.lua, project, idx(args, 0)(), r);

  cache_user_key(app.opts, app.lua, db, keys);
  cert_revision_testresult(db, keys, r, idx(args, 1)());
}


CMD(approve, "approve", "", CMD_REF(review), N_("REVISION"),
    N_("Approves a particular revision"),
    "",
    options::opts::branch)
{
  database db(app);
  key_store keys(app);
  project_t project(db);

  if (args.size() != 1)
    throw usage(execid);

  revision_id r;
  complete(app.opts, app.lua, project, idx(args, 0)(), r);
  guess_branch(app.opts, project, r);
  N(app.opts.branchname() != "", F("need --branch argument for approval"));

  cache_user_key(app.opts, app.lua, db, keys);
  project.put_revision_in_branch(keys, r, app.opts.branchname);
}

CMD(suspend, "suspend", "", CMD_REF(review), N_("REVISION"),
    N_("Suspends a particular revision"),
    "",
    options::opts::branch)
{
  database db(app);
  key_store keys(app);
  project_t project(db);

  if (args.size() != 1)
    throw usage(execid);

  revision_id r;
  complete(app.opts, app.lua, project, idx(args, 0)(), r);
  guess_branch(app.opts, project, r);
  N(app.opts.branchname() != "", F("need --branch argument to suspend"));

  cache_user_key(app.opts, app.lua, db, keys);
  project.suspend_revision_in_branch(keys, r, app.opts.branchname);
}

CMD(comment, "comment", "", CMD_REF(review), N_("REVISION [COMMENT]"),
    N_("Comments on a particular revision"),
    "",
    options::opts::none)
{
  database db(app);
  key_store keys(app);
  project_t project(db);

  if (args.size() != 1 && args.size() != 2)
    throw usage(execid);

  utf8 comment;
  if (args.size() == 2)
    comment = idx(args, 1);
  else
    {
      external comment_external;
      N(app.lua.hook_edit_comment(external(""), external(""), comment_external),
        F("edit comment failed"));
      system_to_utf8(comment_external, comment);
    }

  N(comment().find_first_not_of("\n\r\t ") != string::npos,
    F("empty comment"));

  revision_id r;
  complete(app.opts, app.lua, project, idx(args, 0)(), r);

  cache_user_key(app.opts, app.lua, db, keys);
  cert_revision_comment(db, keys, r, comment);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
