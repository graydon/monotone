#ifndef __PROTO_MACHINE_HH__
#define __PROTO_MACHINE_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <vector>
#include <string>
#include <map>
#include <iosfwd>

// this file describes the interface to netnews in terms
// of NNTP / SMTP protocol state machines.

using namespace std;

class proto_state;
struct proto_edge
{
  proto_state * targ;
  int code;
  string msg;
  vector<string> lines;  
  explicit proto_edge(proto_state * next_state, 
		      int code, 
		      string const & message, 
		      vector<string> const & lines);
};

class proto_state 
{
private:
  int res_code;
  static bool debug;
  map< int, pair<bool, proto_state *> > codes;
  proto_edge handle_response(istream & net);
  
protected:
  proto_edge step_lines(iostream & net, 
			vector<string> const & send_lines);
  
  proto_edge step_cmd(iostream & net, 
		      string const & cmd,
		      vector<string> const & args);
  
public:
  int get_res_code() { return res_code; }
  static void set_debug(bool b);
  void add_edge(int code, proto_state * targ, bool read_lines = false);
  virtual ~proto_state();
  virtual proto_edge drive(iostream & net, proto_edge const & e) = 0;
};


class cmd_state : public proto_state
{
  string cmd;
  vector<string> args;
public:
  cmd_state(string const & c);
  cmd_state(string const & c, string const & arg1);
  cmd_state(string const & c, string const & arg1, string const & arg2);
  virtual ~cmd_state();
  virtual proto_edge drive(iostream & net, proto_edge const & e);
};

void run_proto_state_machine(proto_state * machine,
			     iostream & link);

#endif // __PROTO_MACHINE_HH__
