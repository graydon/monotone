// 2007 Timothy Brownawell <tbrownaw@gmail.com>
// GNU GPL V2 or later

#include <vector>

#include "app_state.hh"
#include "cert.hh"
#include "project.hh"
#include "revision.hh"
#include "transforms.hh"

using std::set;
using std::vector;

branch::branch(app_state & app, utf8 const & name)
  : app(app), name(name)
{}

namespace
{
  struct not_in_branch : public is_failure
  {
    app_state & app;
    base64<cert_value > const & branch_encoded;
    not_in_branch(app_state & app,
                  base64<cert_value> const & branch_encoded)
      : app(app), branch_encoded(branch_encoded)
    {}
    virtual bool operator()(revision_id const & rid)
    {
      vector< revision<cert> > certs;
      app.db.get_revision_certs(rid,
                                cert_name(branch_cert_name),
                                branch_encoded,
                                certs);
      erase_bogus_certs(certs, app);
      return certs.empty();
    }
  };
}

outdated_indicator
get_branch_heads(cert_value const & branchname,
                 app_state & app,
                 set<revision_id> & heads)
{
  L(FL("getting heads of branch %s") % branchname);
  base64<cert_value> branch_encoded;
  encode_base64(branchname, branch_encoded);

  outdated_indicator stamp;
  stamp = app.db.get_revisions_with_cert(cert_name(branch_cert_name),
                                         branch_encoded,
                                         heads);

  not_in_branch p(app, branch_encoded);
  erase_ancestors_and_failures(heads, p, app);
  L(FL("found heads of branch %s (%s heads)") % branchname % heads.size());
  return stamp;
}

void
branch::heads(std::set<revision_id> & h)
{
  if (stamp.outdated())
    {
      stamp = get_branch_heads(name(), app, _heads);
    }
  h = _heads;
}


project_t::project_t(app_state & app)
  : app(app)
{}

void
project_t::get_branch_list(std::set<utf8> & names)
{
  if (indicator.outdated())
    {
      std::vector<std::string> got;
      indicator = app.db.get_branches(got);
      actual_branches.clear();
      for (std::vector<std::string>::iterator i = got.begin();
	   i != got.end(); ++i)
	{
	  actual_branches.insert(*i);
	}
    }

  names = actual_branches;
}

void
project_t::get_branch_list(utf8 const & glob,
			   std::set<utf8> & names)
{
  std::vector<std::string> got;
  app.db.get_branches(glob(), got);
  names.clear();
  for (std::vector<std::string>::iterator i = got.begin();
       i != got.end(); ++i)
    {
      names.insert(*i);
    }
}

branch &
project_t::get_branch(utf8 const & name)
{
  std::pair<std::map<utf8, branch>::iterator, bool> res;
  res = known_branches.insert(std::make_pair(name, branch(app, name)));
  return res.first->second;
}
