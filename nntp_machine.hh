#ifndef __NNTP_MACHINE_HH__
#define __NNTP_MACHINE_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <vector>
#include <string>
#include <map>
#include <iosfwd>

// this file describes the interface to netnews in terms
// of NNTP protocol state machines.

using namespace std;

class nntp_state;
struct nntp_edge
{
  nntp_state * targ;
  int code;
  string msg;
  vector<string> lines;  
  explicit nntp_edge(nntp_state * next_state, 
		     int code, 
		     string const & message, 
		     vector<string> const & lines);
};

class nntp_state 
{
private:
  int res_code;
  static bool debug;
  map< int, pair<bool, nntp_state *> > codes;
  nntp_edge handle_response(istream & net);

protected:
  nntp_edge step_lines(iostream & net, 
		       vector<string> const & send_lines);
  
  nntp_edge step_cmd(iostream & net, 
		     string const & cmd,
		     vector<string> const & args);
  
public:
  int get_res_code() { return res_code; }
  static void set_debug(bool b);
  void add_edge(int code, nntp_state * targ, bool read_lines = false);
  virtual ~nntp_state();
  virtual nntp_edge drive(iostream & net, nntp_edge const & e) = 0;
};


class cmd_state : public nntp_state
{
  string cmd;
  vector<string> args;
public:
  cmd_state(string const & c);
  cmd_state(string const & c, string const & arg1);
  cmd_state(string const & c, string const & arg1, string const & arg2);
  virtual ~cmd_state();
  virtual nntp_edge drive(iostream & net, nntp_edge const & e);
};

void run_nntp_state_machine(nntp_state * machine,
			    iostream & link);

#endif // __NNTP_MACHINE_HH__
