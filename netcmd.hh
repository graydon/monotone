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
    // bye is valid in all phases
    bye_cmd = 1,

    // authentication-phase commands
    hello_cmd = 2,
    auth_cmd = 3,
    confirm_cmd = 4,
      
    // refinement-phase commands
    refine_cmd = 5,
    done_cmd = 6,
      
    // transmission-phase commands
    send_data_cmd = 7,
    send_delta_cmd = 8,
    data_cmd = 9,
    delta_cmd = 10,
    nonexistant_cmd = 11
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
void read_hello_cmd_payload(std::string const & in, 
			    id & server, 
			    id & nonce);
void write_hello_cmd_payload(id const & server, 
			     id const & nonce, 
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

void read_confirm_cmd_payload(std::string const & in, std::string & signature);
void write_confirm_cmd_payload(std::string const & signature, std::string & out);

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
