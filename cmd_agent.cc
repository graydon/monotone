#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <iostream>
#include <fstream>

#include "cmd.hh"
#include "keys.hh"
#include "cert.hh"
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
agent_export(string const & name, app_state & app, vector<utf8> const & args)
{
  if (args.size() != 0)
    throw usage(name);

  rsa_keypair_id id;
  keypair key;
  get_user_key(id, app);
  app.keys.get_key_pair(id, key);
  shared_ptr<RSA_PrivateKey> priv = get_private_key(app.lua, id, key.priv);
  utf8 new_phrase;
  get_passphrase(app.lua, id, new_phrase, true, true, "enter new passphrase");
  Pipe p;
  p.start_msg();
  if (new_phrase().length())
    {
      Botan::PKCS8::encrypt_key(*priv, p, new_phrase(), "PBE-PKCS5v20(SHA-1,TripleDES/CBC)");
    }
  else
    {
      Botan::PKCS8::encode(*priv, p);
    }
  string decoded_key = p.read_all_as_string();
  cout << decoded_key;
}

CMD(ssh_agent_export, N_("key and cert"),
    "",
    N_("export your monotone key for use with ssh-agent in PKCS8 PEM format"),
    options::opts::none)
{
  if (args.size() != 0)
    throw usage(name);

  agent_export(name, app, args);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
