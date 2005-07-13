#ifndef __NETCMD_HH__
#define __NETCMD_HH__
// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>
#include <vector>
#include <utility>

#include "merkle_tree.hh"
#include "numeric_vocab.hh"
#include "vocab.hh"
#include "hmac.hh"

typedef enum 
  { 
    source_role = 1, 
    sink_role = 2, 
    source_and_sink_role = 3
  }
protocol_role;

typedef enum 
  { 
    // general commands
    error_cmd = 0,
    bye_cmd = 1,

    // authentication commands
    hello_cmd = 2,
    anonymous_cmd = 3,
    auth_cmd = 4,
    confirm_cmd = 5,
      
    // refinement commands
    refine_cmd = 6,
    done_cmd = 7,
      
    // transmission commands
    send_data_cmd = 8,
    send_delta_cmd = 9,
    data_cmd = 10,
    delta_cmd = 11,
    nonexistant_cmd = 12
  }
netcmd_code;

class netcmd
{
private:
  u8 version;
  netcmd_code cmd_code;
  std::string payload;
public:
  netcmd();
  netcmd(u8 _version);
  netcmd_code get_cmd_code() const {return cmd_code;}
  u8 get_version() const {return version;}
  size_t encoded_size();
  bool operator==(netcmd const & other) const;


  // basic cmd i/o (including checksums)
  void write(std::string & out,
             chained_hmac & hmac) const;
  bool read(std::string & inbuf,
            chained_hmac & hmac);

  // i/o functions for each type of command payload
  void read_error_cmd(std::string & errmsg) const;
  void write_error_cmd(std::string const & errmsg);

//void read_bye_cmd() {}
  void write_bye_cmd() {cmd_code = bye_cmd;}

  void read_hello_cmd(rsa_keypair_id & server_keyname,
                      rsa_pub_key & server_key,
                      id & nonce) const;
  void write_hello_cmd(rsa_keypair_id const & server_keyname,
                       rsa_pub_key const & server_key,
                       id const & nonce);

  void read_anonymous_cmd(protocol_role & role,
                          utf8 & include_pattern,
                          utf8 & exclude_pattern,
                          rsa_oaep_sha_data & hmac_key_encrypted) const;
  void write_anonymous_cmd(protocol_role role, 
                           utf8 const & include_pattern,
                           utf8 const & exclude_pattern,
                           rsa_oaep_sha_data const & hmac_key_encrypted);

  void read_auth_cmd(protocol_role & role, 
                     utf8 & include_pattern,
                     utf8 & exclude_pattern,
                     id & client, 
                     id & nonce1, 
                     rsa_oaep_sha_data & hmac_key_encrypted,
                     std::string & signature) const;
  void write_auth_cmd(protocol_role role, 
                      utf8 const & include_pattern, 
                      utf8 const & exclude_pattern, 
                      id const & client,
                      id const & nonce1, 
                      rsa_oaep_sha_data const & hmac_key_encrypted,
                      std::string const & signature);

  void read_confirm_cmd() const;
  void write_confirm_cmd();

  void read_refine_cmd(merkle_node & node) const;
  void write_refine_cmd(merkle_node const & node);

  void read_done_cmd(size_t & level, netcmd_item_type & type) const;
  void write_done_cmd(size_t level, netcmd_item_type type);

  void read_send_data_cmd(netcmd_item_type & type,
                          id & item) const;
  void write_send_data_cmd(netcmd_item_type type,
                           id const & item);

  void read_send_delta_cmd(netcmd_item_type & type,
                           id & base,
                           id & ident) const;
  void write_send_delta_cmd(netcmd_item_type type,
                            id const & base,
                            id const & ident);

  void read_data_cmd(netcmd_item_type & type,
                     id & item,
                     std::string & dat) const;
  void write_data_cmd(netcmd_item_type type,
                      id const & item,
                      std::string const & dat);

  void read_delta_cmd(netcmd_item_type & type,
                      id & base, id & ident, 
                      delta & del) const;
  void write_delta_cmd(netcmd_item_type & type,
                       id const & base, id const & ident, 
                       delta const & del);

  void read_nonexistant_cmd(netcmd_item_type & type,
                            id & item) const;
  void write_nonexistant_cmd(netcmd_item_type type,
                             id const & item);

};

#endif // __NETCMD_HH__
