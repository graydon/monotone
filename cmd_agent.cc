#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <iostream>
#include <fstream>

#include "cmd.hh"
#include "keys.hh"
#include "ssh_agent.hh"
#include "botan/rsa.h"
#include "botan/base64.h"
#include "botan/pipe.h"

using std::cout;
using std::string;
using std::vector;
using std::fstream;
using boost::scoped_ptr;
using boost::shared_ptr;
using Botan::RSA_PublicKey;
using Botan::RSA_PrivateKey;
using Botan::Base64_Encoder;
using Botan::Pipe;

static void
agent_list(string const & name, app_state & app, vector<utf8> const & args)
{
  if (args.size() != 0)
    throw usage(name);

  scoped_ptr<ssh_agent> a(new ssh_agent());
  a->connect();
  a->get_keys();
}

static void
agent_export(string const & name, app_state & app, vector<utf8> const & args)
{
  if (args.size() != 0 && args.size() != 1)
    throw usage(name);

  vector<rsa_keypair_id> keys;
  keypair key;
  if (args.size() == 0)
    app.keys.get_keys(keys);
  else
    app.keys.get_key_ids(idx(args,0)(), keys);
  for (vector<rsa_keypair_id>::const_iterator
         i = keys.begin(); i != keys.end(); ++i) {
    app.keys.get_key_pair(*i, key);
    //cout << key.priv << "\n";
    shared_ptr<RSA_PrivateKey> priv = get_private_key(app.lua, *i, key.priv);

    utf8 new_phrase;
    get_passphrase(app.lua, *i, new_phrase, true, true, "enter new passphrase");
    Pipe p;
    p.start_msg();
    Botan::PKCS8::encrypt_key(*priv, p, new_phrase(),
                              "PBE-PKCS5v20(SHA-1,TripleDES/CBC)");
    string decoded_key = p.read_all_as_string();
    fstream fout;
    fout.open("id_monotone", fstream::out | fstream::trunc);
    fout << decoded_key;
    fout.close();
  }
}

static void
agent_test(string const & name, app_state & app, vector<utf8> const & args)
{
  scoped_ptr<ssh_agent> a(new ssh_agent());
  a->connect();
  vector<RSA_PublicKey> ssh_keys = a->get_keys();
  L(FL("ssh-agent keys:"));
  for (vector<RSA_PublicKey>::const_iterator
         i = ssh_keys.begin(); i != ssh_keys.end(); ++i) {
    L(FL(" n: %s") % (*i).get_n());
    L(FL(" e: %s") % (*i).get_e());
  }
  L(FL("monotone keys:"));
  vector<rsa_keypair_id> mtn_keys;
  keypair key;
  app.keys.get_keys(mtn_keys);
  for (vector<rsa_keypair_id>::const_iterator
         i = mtn_keys.begin(); i != mtn_keys.end(); ++i) {
    app.keys.get_key_pair(*i, key);
    shared_ptr<RSA_PrivateKey> priv = get_private_key(app.lua, *i, key.priv);
    L(FL(" n: %s") % priv->get_n());
    L(FL(" e: %s") % priv->get_e());
    for (vector<RSA_PublicKey>::const_iterator
           si = ssh_keys.begin(); si != ssh_keys.end(); ++si) {
      if ((*priv).get_e() == (*si).get_e()
          && (*priv).get_n() == (*si).get_n()) {
        L(FL("  ssh key matches monotone key"));
        string sdata;
        a->sign_data(*si, "hello", sdata);
      }
    }
  }
}

CMD(agent, N_("informative"),
    N_("list\n"
       "export\n"
       "test"),
    N_("interact with the agent"),
    options::opts::depth | options::opts::exclude)
{
  if (args.size() == 0)
    throw usage(name);

  vector<utf8>::const_iterator i = args.begin();
  ++i;
  vector<utf8> removed (i, args.end());
  if (idx(args, 0)() == "list")
    agent_list(name, app, removed);
  else if (idx(args, 0)() == "export")
    agent_export(name, app, removed);
  else if (idx(args, 0)() == "test")
    agent_test(name, app, removed);
  else
    throw usage(name);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
