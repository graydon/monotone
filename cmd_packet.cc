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

#include "cmd.hh"
#include "app_state.hh"
#include "database.hh"
#include "key_store.hh"
#include "packet.hh"

using std::cin;
using std::cout;
using std::istringstream;
using std::vector;

CMD(pubkey, "pubkey", "", CMD_REF(packet_io), N_("ID"), 
    N_("Prints a public key packet"),
    "",
    options::opts::none)
{
  database db(app);
  key_store keys(app);

  if (args.size() != 1)
    throw usage(execid);

  rsa_keypair_id ident(idx(args, 0)());
  bool exists(false);
  rsa_pub_key key;
  if (db.database_specified() && db.public_key_exists(ident))
    {
      db.get_key(ident, key);
      exists = true;
    }
  if (keys.key_pair_exists(ident))
    {
      keypair kp;
      keys.get_key_pair(ident, kp);
      key = kp.pub;
      exists = true;
    }
  N(exists,
    F("public key '%s' does not exist") % idx(args, 0)());

  packet_writer pw(cout);
  pw.consume_public_key(ident, key);
}

CMD(privkey, "privkey", "", CMD_REF(packet_io), N_("ID"), 
    N_("Prints a private key packet"),
    "",
    options::opts::none)
{
  key_store keys(app);

  if (args.size() != 1)
    throw usage(execid);

  rsa_keypair_id ident(idx(args, 0)());
  N(keys.key_pair_exists(ident),
    F("public and private key '%s' do not exist in keystore")
    % idx(args, 0)());

  packet_writer pw(cout);
  keypair kp;
  keys.get_key_pair(ident, kp);
  pw.consume_key_pair(ident, kp);
}

namespace
{
  // this writer injects packets it receives to the database
  // and/or keystore.

  struct packet_db_writer : public packet_consumer
  {
    database & db;
    key_store & keys;

  public:
    packet_db_writer(database & db, key_store & keys)
      : db(db), keys(keys)
    {}
    virtual ~packet_db_writer() {}
    virtual void consume_file_data(file_id const & ident,
                                   file_data const & dat)
    {
      transaction_guard guard(db);
      db.put_file(ident, dat);
      guard.commit();
    }

    virtual void consume_file_delta(file_id const & old_id,
                                    file_id const & new_id,
                                    file_delta const & del)
    {
      transaction_guard guard(db);
      db.put_file_version(old_id, new_id, del);
      guard.commit();
    }

    virtual void consume_revision_data(revision_id const & ident,
                                       revision_data const & dat)
    {
      transaction_guard guard(db);
      db.put_revision(ident, dat);
      guard.commit();
    }
    
    virtual void consume_revision_cert(revision<cert> const & t)
    {
      transaction_guard guard(db);
      db.put_revision_cert(t);
      guard.commit();
    }

    virtual void consume_public_key(rsa_keypair_id const & ident,
                                    rsa_pub_key const & k)
    {
      transaction_guard guard(db);
      db.put_key(ident, k);
      guard.commit();
    }
    
    virtual void consume_key_pair(rsa_keypair_id const & ident,
                                  keypair const & kp)
    {
      keys.put_key_pair(ident, kp);
    }

    virtual void consume_old_private_key(rsa_keypair_id const & ident,
                                         old_arc4_rsa_priv_key const & k)
    {
      rsa_pub_key dummy;
      keys.migrate_old_key_pair(ident, k, dummy);
    }
  };
}

// Name : read_packets
// Arguments:
//   packet-data
// Added in: 9.0
// Purpose:
//   Store public keys (and incidentally anything else that can be
//   represented as a packet) into the database.
// Input format:
//   The format of the packet-data argument is identical to the output
//   of "mtn pubkey <keyname>" (or other packet output commands).
// Output format:
//   No output.
// Error conditions:
//   Invalid input formatting.
CMD_AUTOMATE(read_packets, N_("PACKET-DATA"),
             N_("Load the given packets into the database."),
             "",
             options::opts::none)
{
  N(args.size() == 1,
    F("wrong argument count"));

  database db(app);
  key_store keys(app);
  packet_db_writer dbw(db, keys);

  istringstream ss(idx(args,0)());
  read_packets(ss, dbw);
}

CMD(read, "read", "", CMD_REF(packet_io), "[FILE1 [FILE2 [...]]]",
    N_("Reads packets from files"),
    N_("If no files are provided, the standard input is used."),
    options::opts::none)
{
  database db(app);
  key_store keys(app);
  packet_db_writer dbw(db, keys);
  size_t count = 0;
  if (args.empty())
    {
      count += read_packets(cin, dbw);
      N(count != 0, F("no packets found on stdin"));
    }
  else
    {
      for (args_vector::const_iterator i = args.begin(); 
           i != args.end(); ++i)
        {
          data dat;
          read_data(system_path(*i), dat);
          istringstream ss(dat());
          count += read_packets(ss, dbw);
        }
      N(count != 0, FP("no packets found in given file",
                       "no packets found in given files",
                       args.size()));
    }
  P(FP("read %d packet", "read %d packets", count) % count);
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
