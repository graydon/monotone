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
    describe_cmd = 7,
    description_cmd = 8,
      
    // transmission-phase commands
    send_data_cmd = 9,
    send_delta_cmd = 10,
    data_cmd = 11,
    delta_cmd = 12,
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
			    std::string & server, 
			    std::string & nonce);
void write_hello_cmd_payload(std::string const & server, 
			     std::string const & nonce, 
			     std::string & out);

void read_auth_cmd_payload(std::string const & in, 
			   protocol_role & role, 
			   std::string & collection,
			   std::string & client, 
			   std::string & nonce1, 
			   std::string & nonce2,
			   std::string & signature);
void write_auth_cmd_payload(protocol_role role, 
			    std::string const & collection, 
			    std::string const & client,
			    std::string const & nonce1, 
			    std::string const & nonce2, 
			    std::string const & signature, 
			    std::string & out);

void read_confirm_cmd_payload(std::string const & in, std::string & signature);
void write_confirm_cmd_payload(std::string const & signature, std::string & out);

void read_refine_cmd_payload(std::string const & in, merkle_node & node);
void write_refine_cmd_payload(merkle_node const & node, std::string & out);

void read_done_cmd_payload(std::string const & in, u8 & level);
void write_done_cmd_payload(u8 level, std::string & out);

void read_describe_cmd_payload(std::string const & in, std::string & id);
void write_describe_cmd_payload(std::string const & id, std::string & out);

void read_description_cmd_payload(std::string const & in, 
				  std::string & head, 
				  u64 & len,
				  std::vector<std::string> & predecessors);
void write_description_cmd_payload(std::string const & head, 
				   u64 len,
				   std::vector<std::string> const & predecessors,
				   std::string & out);

void read_send_data_cmd_payload(std::string const & in, 
				std::string & head,
				std::vector<std::pair<u64, u64> > & fragments);
void write_send_data_cmd_payload(std::string const & head,
				 std::vector<std::pair<u64, u64> > const & fragments,
				 std::string & out);

void read_send_delta_cmd_payload(std::string const & in, 
				 std::string & head,
				 std::string & base);
void write_send_delta_cmd_payload(std::string const & head,
				  std::string const & base,
				  std::string & out);

void read_data_cmd_payload(std::string const & in,
			   std::string & id,
			   std::vector< std::pair<std::pair<u64,u64>,std::string> > & fragments);
void write_data_cmd_payload(std::string const & id,
			    std::vector< std::pair<std::pair<u64,u64>,std::string> > const & fragments,
			    std::string & out);

void read_delta_cmd_payload(std::string const & in, 
			    std::string & src, std::string & dst, 
			    u64 & src_len, std::string & del);
void write_delta_cmd_payload(std::string const & src, std::string const & dst, 
			     u64 src_len, std::string const & del,
			     std::string & out);

#endif // __NETCMD_HH__
