#include "cmd.hh"

#include "packet.hh"

#include <sstream>
using std::istringstream;
#include <iostream>
using std::cout;
using std::cin;

CMD(pubkey, N_("packet i/o"), N_("ID"), N_("write public key packet to stdout"),
    OPT_NONE)
{
  if (args.size() != 1)
    throw usage(name);

  rsa_keypair_id ident(idx(args, 0)());
  bool exists(false);
  base64< rsa_pub_key > key;
  if (app.db.database_specified() && app.db.public_key_exists(ident))
    {
      app.db.get_key(ident, key);
      exists = true;
    }
  if (app.keys.key_pair_exists(ident))
    {
      keypair kp;
      app.keys.get_key_pair(ident, kp);
      key = kp.pub;
      exists = true;
    }
  N(exists,
    F("public key '%s' does not exist") % idx(args, 0)());

  packet_writer pw(cout);
  pw.consume_public_key(ident, key);
}

CMD(privkey, N_("packet i/o"), N_("ID"), N_("write private key packet to stdout"),
    OPT_NONE)
{
  if (args.size() != 1)
    throw usage(name);

  rsa_keypair_id ident(idx(args, 0)());
  N(app.keys.key_pair_exists(ident),
    F("public and private key '%s' do not exist in keystore")
      % idx(args, 0)());

  packet_writer pw(cout);
  keypair kp;
  app.keys.get_key_pair(ident, kp);
  pw.consume_key_pair(ident, kp);
}


CMD(read, N_("packet i/o"), "[FILE1 [FILE2 [...]]]",
    N_("read packets from files or stdin"),
    OPT_NONE)
{
  packet_db_writer dbw(app);
  size_t count = 0;
  if (args.empty())
    {
      count += read_packets(cin, dbw, app);
      N(count != 0, F("no packets found on stdin"));
    }
  else
    {
      for (std::vector<utf8>::const_iterator i = args.begin(); i != args.end(); ++i)
        {
          data dat;
          read_data(system_path(*i), dat);
          istringstream ss(dat());
          count += read_packets(ss, dbw, app);
        }
      N(count != 0, FP("no packets found in given file",
                       "no packets found in given files",
                       args.size()));
    }
  P(FP("read %d packet", "read %d packets", count) % count);
}
