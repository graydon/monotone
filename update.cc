
// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <algorithm>
#include <set>
#include <vector>
#include <string>
#include <boost/dynamic_bitset.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/shared_ptr.hpp>

#include "app_state.hh"
#include "database.hh"
#include "manifest.hh"
#include "sanity.hh"
#include "cert.hh"
#include "transforms.hh"
#include "ui.hh"
#include "update.hh"
#include "vocab.hh"

// these functions just encapsulate the (somewhat complex) logic behind
// picking an update target. the actual updating takes place in
// commands.cc, along with most other file-modifying actions.

using boost::dynamic_bitset;
using boost::lexical_cast;
using boost::shared_ptr;

static void find_descendents(manifest_id const & ident,
			     app_state & app,
			     set<manifest_id> & descendents)
{
  set<manifest_id> frontier;  
  bool done = false;
  descendents.clear();
  
  frontier.insert(ident);

  while(!done)
    {
      done = true;
      set<manifest_id> new_frontier;
      for (set<manifest_id>::iterator i = frontier.begin(); 
	   i != frontier.end(); ++i)
	{
	  cert_value val(i->inner()().c_str());
	  base64<cert_value> enc_val;
	  encode_base64(val, enc_val);	  

	  vector< manifest<cert> > certs;
	  app.db.get_manifest_certs(cert_name(ancestor_cert_name), enc_val, certs);

	  erase_bogus_certs(certs, app);

	  for (vector< manifest<cert> >::iterator j = certs.begin();
	       j != certs.end(); ++j)
	    {
	      manifest_id nxt = j->inner().ident;
	      if (descendents.find(nxt) != descendents.end())
		L(F("skipping cyclical ancestry edge %s -> %s\n")
		  % (*i) % nxt);
	      else
		{
		  done = false;
		  new_frontier.insert(nxt);
		  descendents.insert(nxt);
		}
	    }
	}
      frontier = new_frontier;
    }
}


static void filter_by_branch(app_state & app,
			    set<manifest_id> const & candidates,
			    set<manifest_id> & branch_filtered)
{
  cert_value val(app.branch_name);
  base64<cert_value> enc_val;

  branch_filtered.clear();
  encode_base64(val, enc_val);	

  for (set<manifest_id>::iterator i = candidates.begin();
       i != candidates.end(); ++i)
    {
      vector< manifest<cert> > certs;
      app.db.get_manifest_certs(*i, cert_name(branch_cert_name), enc_val, certs);

      erase_bogus_certs(certs, app);
      
      if (certs.size() > 0)
	branch_filtered.insert(certs[0].inner().ident);
    }
}


struct sorter
{
  virtual bool operator()(cert_value const & a, cert_value const &  b) = 0;
};

struct ancestry_sorter : public sorter
{
  app_state & app;
  ancestry_sorter(app_state & a) : app(a) {}  
  virtual bool operator()(cert_value const & a, cert_value const & b)
  {
    set<manifest_id> descendents;
    find_descendents (manifest_id(a()), app, descendents);
    // a < b 
    // iff a is an ancestor of b
    // iff b is a descendent of a
    return descendents.find(manifest_id(b())) != descendents.end();
  }
};

struct bitset_sorter : public sorter
{
  virtual bool operator()(cert_value const & a, cert_value const & b)
  {
    dynamic_bitset<> az(a()), bz(b());
    size_t asz = az.size();
    size_t bsz = bz.size();
    size_t sz = (asz > bsz) ? asz : bsz;
    az.resize(sz, false);
    bz.resize(sz, false);
    return az.is_proper_subset_of(bz);
  }
};

struct string_sorter : public sorter
{
  virtual bool operator()(cert_value const & a, cert_value const & b)
  {
    return a() < b();
  }
};

struct integer_sorter : public sorter
{
  virtual bool operator()(cert_value const & a, cert_value const & b)
  {
    return lexical_cast<int>(a()) < lexical_cast<int>(b());
  }
};



static bool pick_sorter(string const & certname, 
			app_state & app, 
			shared_ptr<sorter> & sort)
{
  string sort_type;

  L(F("picking sort operator for cert name '%s'\n") % certname);

  if (certname == ancestor_cert_name)
    {
      sort = shared_ptr<sorter>(new ancestry_sorter(app));
      return true;
    }

  if (!app.lua.hook_get_sorter(certname, sort_type))
    return false;

  if (sort_type == "bitset")
    {
      sort = shared_ptr<sorter>(new bitset_sorter());
      return true;
    }
  else if (sort_type == "string")
    {
      sort = shared_ptr<sorter>(new string_sorter());
      return true;
    }
  else if (sort_type == "integer")
    {
      sort = shared_ptr<sorter>(new integer_sorter());
      return true;
    }
  return false;
}


struct sort_adaptor
{
  shared_ptr<sorter> sort;
  sort_adaptor(shared_ptr<sorter> s) : sort(s) {}
  bool operator()(manifest<cert> const & a,
		  manifest<cert> const & b) const
  { 
    I(sort.get() != NULL);
    cert_value aval, bval;
    decode_base64(a.inner().value, aval);
    decode_base64(b.inner().value, bval);
    return (*sort)(aval, bval);
  }
};

struct insert_id_with_value
{
  app_state & app;
  set<manifest_id> & candidates;
  base64<cert_value> const & val;
  insert_id_with_value(app_state & app, set<manifest_id> & c, 
		       base64<cert_value> const & v)
    : app(app), candidates(c), val(v)
  {}
  void operator()(manifest<cert> const & t) const
  {
    if (t.inner().value == val)
      candidates.insert(manifest_id(t.inner().ident));
  }
};

static void filter_by_sorting(vector<string> const & certnames,
			      set<manifest_id> const & in_candidates,
			      app_state & app,
			      set<manifest_id> & candidates)
{  
  copy(in_candidates.begin(), in_candidates.end(), 
       inserter(candidates, candidates.begin()));
  vector< manifest<cert> > certs;
  
  for (size_t i = 0; i < certnames.size(); ++i)
    {
      certs.clear();
      shared_ptr<sorter> sort;
      string certname = certnames[i];

      if (!pick_sorter(certname, app, sort))
	{
	  W(F("skipping cert '%s', no sort operator found\n") % certname);
	  continue;
	}
      
      // pull the next set of certs to examine out of the db
      for (set<manifest_id>::iterator i = candidates.begin();
	   i != candidates.end(); ++i)
	{
	  vector< manifest<cert> > tmpcerts;
	  app.db.get_manifest_certs(*i, certname, tmpcerts);
	  copy(tmpcerts.begin(), tmpcerts.end(), back_inserter(certs));
	}

      erase_bogus_certs(certs, app);

      // pick the most favourable cert value, via a sorter
      vector< manifest<cert> >::const_iterator max = 
	max_element(certs.begin(), certs.end(), sort_adaptor(sort));
      if (max == certs.end())
	{
	  L(F("skipping sort cert '%s' with no maximum value\n") % certname);
	  continue;
	}

      // rebuild candidates from surviving certs (if any)
      // nb: certs contains only valid candidates now anyways
      candidates.clear();
      for_each(certs.begin(), certs.end(), 
	       insert_id_with_value(app, candidates, max->inner().value));
      I(candidates.size() != 0);
      
      // stop early if we've found an update target
      if (candidates.size() == 1)
	break;
    }
}


void pick_update_target(manifest_id const & base_ident,
			vector<string> const & in_sort_certs,
			app_state & app,
			manifest_id & chosen)
{

  vector<string> sort_certs(in_sort_certs);
  set<manifest_id> candidates;

  if (find(sort_certs.begin(), sort_certs.end(), ancestor_cert_name) 
      == sort_certs.end())
    {
      L(F("adding ancestry as final sort operator\n"));
      sort_certs.push_back (ancestor_cert_name);
    }
  
  find_descendents(base_ident, app, candidates);
  if (candidates.size() == 0 &&
      app.db.manifest_version_exists(base_ident))
    candidates.insert(base_ident);
  
  if (candidates.size() > 1
      && (app.branch_name != ""))
    {
      set<manifest_id> branch;
      filter_by_branch(app, candidates, branch);
      N(branch.size() != 0,
	F("no update candidates after selecting branch"));
      candidates = branch;
    }
    
  if (candidates.size() > 1)
    {
      set<manifest_id> sorted;
      filter_by_sorting(sort_certs, candidates, app, sorted);
      N(sorted.size() != 0,
	F("no update candidates after sorting"));
      candidates = sorted;
    }

  N(candidates.size() != 0,
    F("no candidates remain after selection"));

  N(candidates.size() == 1,
    F("multiple candidates remain after selection"));
  
  chosen = *(candidates.begin());
}
  

