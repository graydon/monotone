#ifndef __PREDICAMENT_HH__
#define __PREDICAMENT_HH__

// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>
#include <vector>
#include <map>
#include <boost/shared_ptr.hpp>

struct app_state;

struct solution
{
  std::string name;
  virtual bool apply() = 0;
  virtual ~solution() {}
};

struct problem
{
  std::string name;
  std::map<std::string, boost::shared_ptr<solution> > solutions;
  problem(std::string const & n) : name(n) {}
};

struct predicament
{
  std::string name;
  std::vector<problem> problems;
  bool active;
  app_state & app;
  predicament(std::string const & n, app_state & app) : name(n), active(true), app(app) {}
  void solve();
};

#endif // __PREDICAMENT_HH__
