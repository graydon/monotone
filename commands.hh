#ifndef __COMMANDS_HH__
#define __COMMANDS_HH__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <string>
#include <vector>
#include <set>
#include <boost/shared_ptr.hpp>

#include "options.hh"
#include "vocab.hh"

using boost::shared_ptr;

// this defines a global function which processes command-line-like things,
// possibly from the command line and possibly internal scripting if we ever
// bind tcl or lua or something in here

class app_state;
class utf8;

struct usage
{
  usage(std::string const & w) : which(w) {}
  std::string which;
};

namespace commands {
  void explain_usage(std::string const & cmd, std::ostream & out);
  std::string complete_command(std::string const & cmd);
  int process(app_state & app, std::string const & cmd, std::vector<utf8> const & args);
  options::options_type command_options(std::vector<utf8> const & cmdline);
  options::options_type toplevel_command_options(std::string const & cmd);
};

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
