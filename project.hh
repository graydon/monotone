// 2007 Timothy Brownawell <tbrownaw@gmail.com>
// GNU GPL V2 or later

#ifndef __BRANCH_HH__
#define __BRANCH_HH__

#include <map>
#include <set>

#include "cert.hh"
#include "outdated_indicator.hh"
#include "vocab.hh"

class app_state;

class branch
{
  app_state & app;
  utf8 name;
  outdated_indicator stamp;
  std::set<revision_id> _heads;
public:
  branch(app_state & app, utf8 const & name);

  void heads(std::set<revision_id> & h);
};

class project_t
{
  app_state & app;
  std::map<utf8, branch> known_branches;
  std::set<utf8> actual_branches;
  outdated_indicator indicator;

public:
  project_t(app_state & app);

  void get_branch_list(std::set<utf8> & names);
  void get_branch_list(utf8 const & glob, std::set<utf8> & names);
  branch & get_branch(utf8 const & name);
};

#endif
