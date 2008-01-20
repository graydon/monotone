// 2007 Timothy Brownawell <tbrownaw@gmail.com>
// GNU GPL V2 or later

#include "base.hh"
#include "vector.hh"

#include "cert.hh"
#include "database.hh"
#include "project.hh"
#include "revision.hh"
#include "transforms.hh"

using std::string;
using std::set;
using std::vector;
using std::multimap;
using std::make_pair;

project_t::project_t(database & db)
  : db(db)
{}

void
project_t::get_branch_list(std::set<branch_name> & names, bool check_certs_valid)
{
  if (indicator.outdated())
    {
      std::vector<std::string> got;
      indicator = db.get_branches(got);
      branches.clear();
      multimap<revision_id, revision_id> inverse_graph_cache;
  
      for (std::vector<std::string>::iterator i = got.begin();
           i != got.end(); ++i)
        {
          // check that the branch has at least one non-suspended head
          const branch_name branch(*i);
          std::set<revision_id> heads;

          if (check_certs_valid)
            get_branch_heads(branch, heads, &inverse_graph_cache);
          
          if (!check_certs_valid || !heads.empty())
            branches.insert(branch);
        }
    }

  names = branches;
}

void
project_t::get_branch_list(globish const & glob,
                           std::set<branch_name> & names,
                           bool check_certs_valid)
{
  std::vector<std::string> got;
  db.get_branches(glob, got);
  names.clear();
  multimap<revision_id, revision_id> inverse_graph_cache;
  
  for (std::vector<std::string>::iterator i = got.begin();
       i != got.end(); ++i)
    {
      // check that the branch has at least one non-suspended head
      const branch_name branch(*i);
      std::set<revision_id> heads;

      if (check_certs_valid)
        get_branch_heads(branch, heads, &inverse_graph_cache);

      if (!check_certs_valid || !heads.empty())
        names.insert(branch);
    }
}

namespace
{
  struct not_in_branch : public is_failure
  {
    database & db;
    base64<cert_value > const & branch_encoded;
    not_in_branch(database & db,
                  base64<cert_value> const & branch_encoded)
      : db(db), branch_encoded(branch_encoded)
    {}
    virtual bool operator()(revision_id const & rid)
    {
      vector< revision<cert> > certs;
      db.get_revision_certs(rid,
                            cert_name(branch_cert_name),
                            branch_encoded,
                            certs);
      erase_bogus_certs(certs, db);
      return certs.empty();
    }
  };

  struct suspended_in_branch : public is_failure
  {
    database & db;
    base64<cert_value > const & branch_encoded;
    suspended_in_branch(database & db,
                  base64<cert_value> const & branch_encoded)
      : db(db), branch_encoded(branch_encoded)
    {}
    virtual bool operator()(revision_id const & rid)
    {
      vector< revision<cert> > certs;
      db.get_revision_certs(rid,
                            cert_name(suspend_cert_name),
                            branch_encoded,
                            certs);
      erase_bogus_certs(certs, db);
      return !certs.empty();
    }
  };
}

void
project_t::get_branch_heads(branch_name const & name, std::set<revision_id> & heads,
                            multimap<revision_id, revision_id> *inverse_graph_cache_ptr)
{
  std::pair<branch_name, suspended_indicator> cache_index(name,
    db.get_opt_ignore_suspend_certs());
  std::pair<outdated_indicator, std::set<revision_id> > & branch = branch_heads[cache_index];
  if (branch.first.outdated())
    {
      L(FL("getting heads of branch %s") % name);
      base64<cert_value> branch_encoded;
      encode_base64(cert_value(name()), branch_encoded);

      outdated_indicator stamp;
      branch.first = db.get_revisions_with_cert(cert_name(branch_cert_name),
                                                    branch_encoded,
                                                    branch.second);

      not_in_branch p(db, branch_encoded);
      erase_ancestors_and_failures(branch.second, p, db, inverse_graph_cache_ptr);
      
      if (!db.get_opt_ignore_suspend_certs())
        {
          suspended_in_branch s(db, branch_encoded);
          std::set<revision_id>::iterator it = branch.second.begin();
          while (it != branch.second.end())
            if (s(*it))
              branch.second.erase(it++);
            else
              it++;
        }
      
      L(FL("found heads of branch %s (%s heads)")
        % name % branch.second.size());
    }
  heads = branch.second;
}

bool
project_t::revision_is_in_branch(revision_id const & id,
                                 branch_name const & branch)
{
  base64<cert_value> branch_encoded;
  encode_base64(cert_value(branch()), branch_encoded);

  vector<revision<cert> > certs;
  db.get_revision_certs(id, branch_cert_name, branch_encoded, certs);

  int num = certs.size();

  erase_bogus_certs(certs, db);

  L(FL("found %d (%d valid) %s branch certs on revision %s")
    % num
    % certs.size()
    % branch
    % id);

  return !certs.empty();
}

void
project_t::put_revision_in_branch(revision_id const & id,
                                  branch_name const & branch)
{
  cert_revision_in_branch(id, branch, db);
}

bool
project_t::revision_is_suspended_in_branch(revision_id const & id,
                                 branch_name const & branch)
{
  base64<cert_value> branch_encoded;
  encode_base64(cert_value(branch()), branch_encoded);

  vector<revision<cert> > certs;
  db.get_revision_certs(id, suspend_cert_name, branch_encoded, certs);

  int num = certs.size();

  erase_bogus_certs(certs, db);

  L(FL("found %d (%d valid) %s suspend certs on revision %s")
    % num
    % certs.size()
    % branch
    % id);

  return !certs.empty();
}

void
project_t::suspend_revision_in_branch(revision_id const & id,
                                  branch_name const & branch)
{
  cert_revision_suspended_in_branch(id, branch, db);
}


outdated_indicator
project_t::get_revision_cert_hashes(revision_id const & rid,
                                    std::vector<hexenc<id> > & hashes)
{
  return db.get_revision_certs(rid, hashes);
}

outdated_indicator
project_t::get_revision_certs(revision_id const & id,
                              std::vector<revision<cert> > & certs)
{
  return db.get_revision_certs(id, certs);
}

outdated_indicator
project_t::get_revision_certs_by_name(revision_id const & id,
                                      cert_name const & name,
                                      std::vector<revision<cert> > & certs)
{
  outdated_indicator i = db.get_revision_certs(id, name, certs);
  erase_bogus_certs(certs, db);
  return i;
}

outdated_indicator
project_t::get_revision_branches(revision_id const & id,
                                 std::set<branch_name> & branches)
{
  std::vector<revision<cert> > certs;
  outdated_indicator i = get_revision_certs_by_name(id, branch_cert_name, certs);
  branches.clear();
  for (std::vector<revision<cert> >::const_iterator i = certs.begin();
       i != certs.end(); ++i)
    {
      cert_value b;
      decode_base64(i->inner().value, b);
      branches.insert(branch_name(b()));
    }
  return i;
}

outdated_indicator
project_t::get_branch_certs(branch_name const & branch,
                            std::vector<revision<cert> > & certs)
{
  base64<cert_value> branch_encoded;
  encode_base64(cert_value(branch()), branch_encoded);

  return db.get_revision_certs(branch_cert_name, branch_encoded, certs);
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
  outdated_indicator i = db.get_revision_certs(tag_cert_name, certs);
  erase_bogus_certs(certs, db);
  tags.clear();
  for (std::vector<revision<cert> >::const_iterator i = certs.begin();
       i != certs.end(); ++i)
    {
      cert_value value;
      decode_base64(i->inner().value, value);
      tags.insert(tag_t(revision_id(i->inner().ident), utf8(value()), i->inner().key));
    }
  return i;
}

void
project_t::put_tag(revision_id const & id,
                   string const & name)
{
  cert_revision_tag(id, name, db);
}


void
project_t::put_standard_certs(revision_id const & id,
                              branch_name const & branch,
                              utf8 const & changelog,
                              date_t const & time,
                              utf8 const & author)
{
  cert_revision_in_branch(id, branch, db);
  cert_revision_changelog(id, changelog, db);
  cert_revision_date_time(id, time, db);
  if (!author().empty())
    cert_revision_author(id, author(), db);
  else
    cert_revision_author_default(id, db);
}

void
project_t::put_standard_certs_from_options(revision_id const & id,
                                           branch_name const & branch,
                                           utf8 const & changelog)
{
  put_standard_certs(id,
                     branch,
                     changelog,
                     db.get_opt_date_or_cur_date(),
                     db.get_opt_author());
}
void
project_t::put_cert(revision_id const & id,
                    cert_name const & name,
                    cert_value const & value)
{
  put_simple_revision_cert(id, name, value, db);
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

