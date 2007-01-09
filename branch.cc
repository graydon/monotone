// 2007 Timothy Brownawell <tbrownaw@gmail.com>
// GNU GPL V2 or later

#include "app_state.hh"
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


branch_list::branch_list(app_state & app)
  : app(app)
{}

void
branch_list::list_all(std::set<utf8> & names)
{
  if (indicator.outdated())
    {
      std::vector<std::string> got;
      indicator = app.db.get_branches(got);
      actual.clear();
      for (std::vector<std::string>::iterator i = got.begin();
	   i != got.end(); ++i)
	{
	  actual.insert(*i);
	}
    }

  names = actual;
}

branch &
branch_list::get(utf8 const & name)
{
  std::pair<std::map<utf8, branch>::iterator, bool> res;
  res = known.insert(std::make_pair(name, branch(app, name)));
  return res.first->second;
}
