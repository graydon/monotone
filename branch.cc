// 2007 Timothy Brownawell <tbrownaw@gmail.com>
// GNU GPL V2 or later

#include "branch.hh"
#include "cert.hh"

branch::branch(app_state & app, utf8 const & name)
  : app(app), name(name)
{}

void
branch::heads(std::set<revision_id> & h)
{
  if (stamp.outdated())
    {
      stamp = get_branch_heads(name(), app, _heads);
    }
  h = _heads;
}
