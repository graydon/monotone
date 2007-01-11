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

class tag_t
{
public:
  revision_id ident;
  utf8 name;
  rsa_keypair_id key;
  tag_t(revision_id const & ident, utf8 const & name, rsa_keypair_id const & key);
};
bool operator < (tag_t const & a, tag_t const & b);

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

  outdated_indicator get_tags(std::set<tag_t> & tags);

  bool revision_is_in_branch(revision_id const & id, utf8 const & branch);
  outdated_indicator get_revision_cert_hashes(revision_id const & id,
                                              std::vector<hexenc<id> > & hashes);
  outdated_indicator get_revision_certs(revision_id const & id,
					std::vector<revision<cert> > & certs);
  outdated_indicator get_revision_certs_by_name(revision_id const & id,
						cert_name const & name,
						std::vector<revision<cert> > & certs);
  outdated_indicator get_revision_branches(revision_id const & id,
					   std::set<utf8> & branches);
  outdated_indicator get_branch_certs(utf8 const & branch,
				      std::vector<revision<cert> > & certs);
};

#endif
