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

struct netcmd
{
  u8 version;
  netcmd_code cmd_code;
  std::string payload;
  netcmd();
  size_t encoded_size();
  bool operator==(netcmd const & other) const;
};

// basic cmd i/o (including checksums)
void write_netcmd(netcmd const & in, std::string & out);
bool read_netcmd(std::string & inbuf, netcmd & out);

// i/o functions for each type of command payload
void read_error_cmd_payload(std::string const & in, 
                            std::string & errmsg);
void write_error_cmd_payload(std::string const & errmsg, 
                             std::string & out);

void read_hello_cmd_payload(std::string const & in, 
                            rsa_keypair_id & server_keyname,
                            rsa_pub_key & server_key,
                            id & nonce);
void write_hello_cmd_payload(rsa_keypair_id const & server_keyname,
                             rsa_pub_key const & server_key,
                             id const & nonce, 
                             std::string & out);

void read_anonymous_cmd_payload(std::string const & in, 
                                protocol_role & role, 
                                std::string & collection,
                                id & nonce2);
void write_anonymous_cmd_payload(protocol_role role, 
                                 std::string const & collection,
                                 id const & nonce2,
                                 std::string & out);

void read_auth_cmd_payload(std::string const & in, 
                           protocol_role & role, 
                           std::string & collection,
                           id & client, 
                           id & nonce1, 
                           id & nonce2,
                           std::string & signature);
void write_auth_cmd_payload(protocol_role role, 
                            std::string const & collection, 
                            id const & client,
                            id const & nonce1, 
                            id const & nonce2, 
                            std::string const & signature, 
                            std::string & out);

void read_confirm_cmd_payload(std::string const & in, 
                              std::string & signature);
void write_confirm_cmd_payload(std::string const & signature, 
                               std::string & out);

void read_refine_cmd_payload(std::string const & in, merkle_node & node);
void write_refine_cmd_payload(merkle_node const & node, std::string & out);

void read_done_cmd_payload(std::string const & in, size_t & level, netcmd_item_type & type);
void write_done_cmd_payload(size_t level, netcmd_item_type type, std::string & out);

void read_send_data_cmd_payload(std::string const & in, 
                                netcmd_item_type & type,
                                id & item);
void write_send_data_cmd_payload(netcmd_item_type type,
                                 id const & item,
                                 std::string & out);

void read_send_delta_cmd_payload(std::string const & in, 
                                 netcmd_item_type & type,
                                 id & base,
                                 id & ident);
void write_send_delta_cmd_payload(netcmd_item_type type,
                                  id const & base,
                                  id const & ident,
                                  std::string & out);

void read_data_cmd_payload(std::string const & in,
                           netcmd_item_type & type,
                           id & item,
                           std::string & dat);
void write_data_cmd_payload(netcmd_item_type type,
                            id const & item,
                            std::string const & dat,
                            std::string & out);

void read_delta_cmd_payload(std::string const & in, 
                            netcmd_item_type & type,
                            id & base, id & ident, 
                            delta & del);
void write_delta_cmd_payload(netcmd_item_type & type,
                             id const & base, id const & ident, 
                             delta const & del,
                             std::string & out);

void read_nonexistant_cmd_payload(std::string const & in, 
                                  netcmd_item_type & type,
                                  id & item);
void write_nonexistant_cmd_payload(netcmd_item_type type,
                                   id const & item,
                                   std::string & out);


#endif // __NETCMD_HH__
