#ifndef __GRAPH__HH__

// Copyright (C) 2006 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// This file contains generic graph algorithms.  They are split out from any
// particular concrete graph (e.g., the revision graph, the delta storage
// graphs) to easy re-use, and to make them easier to test on their own.  We
// have a number of graph algorithms that are not genericized in this way
// (e.g., in revision.cc); FIXME it would be good to move them in here as
// opportunity permits.

#include <string>
#include <set>
#include <vector>

struct reconstruction_graph
{
  virtual bool is_base(std::string const & node) const = 0;
  virtual void get_next(std::string const & from, std::set<std::string> & next) const = 0;
  virtual ~reconstruction_graph() {};
};

typedef std::vector<std::string> reconstruction_path;

void
get_reconstruction_path(std::string const & start,
                        reconstruction_graph const & graph,
                        reconstruction_path & path);

#endif // __GRAPH__HH__
