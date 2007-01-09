// 2007 Timothy Brownawell <tbrownaw@gmail.com>
// GNU GPL V2 or later

#ifndef __BRANCH_HH__
#define __BRANCH_HH__

#include <set>

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

#endif
