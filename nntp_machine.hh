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

class nntp_state;
struct nntp_edge
{
  nntp_state * targ;
  int code;
  std::string msg;
  std::vector<std::string> lines;  
  explicit nntp_edge(nntp_state * next_state, 
		     int code, 
		     std::string const & message, 
		     std::vector<std::string> const & lines);
};

class nntp_state 
{
private:
  int res_code;
  static bool debug;
  std::map< int, std::pair<bool, nntp_state *> > codes;
  nntp_edge handle_response(std::istream & net);

protected:
  nntp_edge step_lines(std::iostream & net, 
		       std::vector<std::string> const & send_lines);
  
  nntp_edge step_cmd(std::iostream & net, 
		     std::string const & cmd,
		     std::vector<std::string> const & args);
  
public:
  int get_res_code() { return res_code; }
  static void set_debug(bool b);
  void add_edge(int code, nntp_state * targ, bool read_lines = false);
  virtual ~nntp_state();
  virtual nntp_edge drive(std::iostream & net, nntp_edge const & e) = 0;
};


class cmd_state : public nntp_state
{
  std::string cmd;
  std::vector<std::string> args;
public:
  cmd_state(std::string const & c);
  cmd_state(std::string const & c, std::string const & arg1);
  cmd_state(std::string const & c, std::string const & arg1, std::string const & arg2);
  virtual ~cmd_state();
  virtual nntp_edge drive(std::iostream & net, nntp_edge const & e);
};

void run_nntp_state_machine(nntp_state * machine,
			    std::iostream & link);

#endif // __NNTP_MACHINE_HH__
