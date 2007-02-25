// 2007 Timothy Brownawell <tbrownaw@gmail.com>
// GNU GPL V2 or later

#ifndef __PROJECT_HH__
#define __PROJECT_HH__

#include <map>
#include <set>
#include <string>

#include "cert.hh"
#include "outdated_indicator.hh"
#include "vocab.hh"

#include <boost/date_time/posix_time/posix_time.hpp>


class app_state;
class packet_consumer;

class tag_t
{
public:
  revision_id ident;
  utf8 name;
  rsa_keypair_id key;
  tag_t(revision_id const & ident, utf8 const & name, rsa_keypair_id const & key);
};
bool operator < (tag_t const & a, tag_t const & b);

inline boost::posix_time::ptime now()
{
  return boost::posix_time::second_clock::universal_time();
}

boost::posix_time::ptime time_from_time_t(time_t time);

class project_t
{
  app_state & app;
  std::map<branch_name, std::pair<outdated_indicator, std::set<revision_id> > > branch_heads;
  std::set<branch_name> branches;
  outdated_indicator indicator;

public:
  project_t(app_state & app);

  void get_branch_list(std::set<branch_name> & names);
  void get_branch_list(globish const & glob, std::set<branch_name> & names);
  void get_branch_heads(branch_name const & name, std::set<revision_id> & heads);

  outdated_indicator get_tags(std::set<tag_t> & tags);
  void put_tag(revision_id const & id, std::string const & name, packet_consumer & pc);

  bool revision_is_in_branch(revision_id const & id, branch_name const & branch);
  void put_revision_in_branch(revision_id const & id,
                              branch_name const & branch,
                              packet_consumer & pc);

  outdated_indicator get_revision_cert_hashes(revision_id const & id,
                                              std::vector<hexenc<id> > & hashes);
  outdated_indicator get_revision_certs(revision_id const & id,
                                        std::vector<revision<cert> > & certs);
  outdated_indicator get_revision_certs_by_name(revision_id const & id,
                                                cert_name const & name,
                                                std::vector<revision<cert> > & certs);
  outdated_indicator get_revision_branches(revision_id const & id,
                                           std::set<branch_name> & branches);
  outdated_indicator get_branch_certs(branch_name const & branch,
                                      std::vector<revision<cert> > & certs);

  void put_standard_certs(revision_id const & id,
                          branch_name const & branch,
                          utf8 const & changelog,
                          boost::posix_time::ptime const & time,
                          utf8 const & author,
                          packet_consumer & pc);
  void put_standard_certs_from_options(revision_id const & id,
                                       branch_name const & branch,
                                       utf8 const & changelog,
                                       packet_consumer & pc);

  void put_cert(revision_id const & id,
                cert_name const & name,
                cert_value const & value,
                packet_consumer & pc);
};

#endif


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

