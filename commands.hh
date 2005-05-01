#ifndef __COMMANDS_HH__
#define __COMMANDS_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>
#include <vector>

// this defines a global function which processes command-line-like things,
// possibly from the command line and possibly internal scripting if we ever
// bind tcl or lua or something in here

class app_state;
struct utf8;

struct usage 
{
  usage(std::string const & w) : which(w) {}
  std::string which;
};

namespace commands {
  void explain_usage(std::string const & cmd, std::ostream & out);
  std::string complete_command(std::string const & cmd);
  int process(app_state & app, std::string const & cmd, std::vector<utf8> const & args);
  std::set<int> command_options(std::string const & cmd);
};

#endif
