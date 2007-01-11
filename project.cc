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

bool
project_t::revision_is_in_branch(revision_id const & id,
				 utf8 const & branch)
{
  base64<cert_value> branch_encoded;
  encode_base64(cert_value(branch()), branch_encoded);

  vector<revision<cert> > certs;
  app.db.get_revision_certs(id, branch_cert_name, branch_encoded, certs);

  int num = certs.size();

  erase_bogus_certs(certs, app);

  L(FL("found %d (%d valid) %s branch certs on revision %s")
    % num
    % certs.size()
    % branch
    % id);

  return !certs.empty();
}

outdated_indicator
project_t::get_revision_cert_hashes(revision_id const & id,
                                    std::vector<hexenc<id> > & hashes)
{
  return app.db.get_revision_certs(id, hashes);
}

outdated_indicator
project_t::get_revision_certs(revision_id const & id,
			      std::vector<revision<cert> > & certs)
{
  return app.db.get_revision_certs(id, certs);
}

outdated_indicator
project_t::get_revision_certs_by_name(revision_id const & id,
				      cert_name const & name,
				      std::vector<revision<cert> > & certs)
{
  outdated_indicator i = app.db.get_revision_certs(id, name, certs);
  erase_bogus_certs(certs, app);
  return i;
}

outdated_indicator
project_t::get_revision_branches(revision_id const & id,
				 std::set<utf8> & branches)
{
  std::vector<revision<cert> > certs;
  outdated_indicator i = get_revision_certs_by_name(id, branch_cert_name, certs);
  branches.clear();
  for (std::vector<revision<cert> >::const_iterator i = certs.begin();
       i != certs.end(); ++i)
    {
      cert_value b;
      decode_base64(i->inner().value, b);
      branches.insert(utf8(b()));
    }
  return i;
}

outdated_indicator
project_t::get_branch_certs(utf8 const & branch,
			    std::vector<revision<cert> > & certs)
{
  base64<cert_value> branch_encoded;
  encode_base64(cert_value(branch()), branch_encoded);

  return app.db.get_revision_certs(branch_cert_name, branch_encoded, certs);
}

tag_t::tag_t(revision_id const & ident,
	     utf8 const & name,
	     rsa_keypair_id const & key)
  : ident(ident), name(name), key(key)
{}

bool
operator < (tag_t const & a, tag_t const & b)
{
  if (a.name < b.name)
    return true;
  else if (a.name == b.name)
    {
      if (a.ident < b.ident)
	return true;
      else if (a.ident == b.ident)
	{
	  if (a.key < b.key)
	    return true;
	}
    }
  return false;
}

outdated_indicator
project_t::get_tags(set<tag_t> & tags)
{
  std::vector<revision<cert> > certs;
  outdated_indicator i = app.db.get_revision_certs(tag_cert_name, certs);
  erase_bogus_certs(certs, app);
  tags.clear();
  for (std::vector<revision<cert> >::const_iterator i = certs.begin();
       i != certs.end(); ++i)
    {
      cert_value value;
      decode_base64(i->inner().value, value);
      tags.insert(tag_t(i->inner().ident, value(), i->inner().key));
    }
  return i;
}
