#ifndef __RCS_FILE_HH__
#define __RCS_FILE_HH__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <vector>
#include <string>
#include <map>
#include <boost/shared_ptr.hpp>

struct rcs_admin
{
  std::string head;
  std::string branch;
  std::multimap<std::string, std::string> symbols;
};

struct rcs_delta
{
  std::string num;
  std::string date;
  std::string author;
  std::vector<std::string> branches;
  std::string next;
  std::string state; // dead, Exp  (or Stab, Rel)
};

struct rcs_deltatext
{
  std::string num;
  std::string log;
  std::string text;
};

struct rcs_file
{
  rcs_admin admin;
  std::map<std::string, boost::shared_ptr<rcs_delta> > deltas;
  std::map<std::string, boost::shared_ptr<rcs_deltatext> > deltatexts;
  void push_delta(rcs_delta const & d)
  {
    boost::shared_ptr<rcs_delta> dp(new rcs_delta(d));
    deltas.insert(make_pair(dp->num,dp));
  }
  void push_deltatext(rcs_deltatext const & dt)
  {
    boost::shared_ptr<rcs_deltatext> dp(new rcs_deltatext(dt));
    deltatexts.insert(make_pair(dp->num, dp));
  }
};

void parse_rcs_file(std::string const & filename, rcs_file & r);

#endif // __RCS_FILE_HH__
