// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "cert.hh"
#include "packet.hh"
#include "app_state.hh"
#include "interner.hh"
#include "keys.hh"
#include "mac.hh"
#include "netio.hh"
#include "sanity.hh"
#include "patch_set.hh"
#include "transforms.hh"
#include "ui.hh"

#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/dynamic_bitset.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_comparison.hpp>

#include <string>
#include <limits>
#include <sstream>
#include <vector>

using namespace std;
using namespace boost;

// cert destroyer!

struct 
bogus_cert_p
{
  app_state & app;
  bogus_cert_p(app_state & a) : app(a) {};
  
  bool cert_is_bogus(cert const & c) const
  {
    cert_status status = check_cert(app, c);
    if (status == cert_ok)
      {
	L(F("cert ok\n"));
	return false;
      }
    else if (status == cert_bad)
      {
	string txt;
	cert_signable_text(c, txt);
	W(F("ignoring bad signature by '%s' on '%s'\n") % c.key() % txt);
	return true;
      }
    else
      {
	I(status == cert_unknown);
	string txt;
	cert_signable_text(c, txt);
	W(F("ignoring unknown signature by '%s' on '%s'\n") % c.key() % txt);
	return true;
      }
  }

  bool operator()(manifest<cert> const & c) const 
  {
    return cert_is_bogus(c.inner());
  }

  bool operator()(file<cert> const & c) const 
  {
    return cert_is_bogus(c.inner());
  }
};


void 
erase_bogus_certs(vector< manifest<cert> > & certs,
		  app_state & app)
{
  typedef vector< manifest<cert> >::iterator it;
  it e = remove_if(certs.begin(), certs.end(), bogus_cert_p(app));
  certs.erase(e, certs.end());

  vector< manifest<cert> > tmp_certs;

  // sorry, this is a crazy data structure
  typedef tuple< hexenc<id>, cert_name, base64<cert_value> > trust_key;
  typedef map< trust_key, pair< shared_ptr< set<rsa_keypair_id> >, it > > trust_map;
  trust_map trust;

  for (it i = certs.begin(); i != certs.end(); ++i)
    {
      trust_key key = trust_key(i->inner().ident, i->inner().name, i->inner().value);
      trust_map::iterator j = trust.find(key);
      shared_ptr< set<rsa_keypair_id> > s;
      if (j == trust.end())
	{
	  s.reset(new set<rsa_keypair_id>());
	  trust.insert(make_pair(key, make_pair(s, i)));
	}
      else
	s = j->second.first;
      s->insert(i->inner().key);
    }

  for (trust_map::const_iterator i = trust.begin();
       i != trust.end(); ++i)
    {
      cert_value decoded_value;
      decode_base64(get<2>(i->first), decoded_value);
      if (app.lua.hook_get_manifest_cert_trust(*(i->second.first),
					       get<0>(i->first),
					       get<1>(i->first),
					       decoded_value))
	{
	  L(F("trust function liked %d signers of %s cert on manifest %s\n")
	    % i->second.first->size() % get<1>(i->first) % get<0>(i->first));
	  tmp_certs.push_back(*(i->second.second));
	}
      else
	{
	  W(F("trust function disliked %d signers of %s cert on manifest %s\n")
	    % i->second.first->size() % get<1>(i->first) % get<0>(i->first));
	}
    }
  certs = tmp_certs;
}

void 
erase_bogus_certs(vector< file<cert> > & certs,
		  app_state & app)
{
  typedef vector< file<cert> >::iterator it;
  it e = remove_if(certs.begin(), certs.end(), bogus_cert_p(app));
  certs.erase(e, certs.end());

  vector< file<cert> > tmp_certs;

  // sorry, this is a crazy data structure
  typedef tuple< hexenc<id>, cert_name, base64<cert_value> > trust_key;
  typedef map< trust_key, pair< shared_ptr< set<rsa_keypair_id> >, it > > trust_map;
  trust_map trust;

  for (it i = certs.begin(); i != certs.end(); ++i)
    {
      trust_key key = trust_key(i->inner().ident, i->inner().name, i->inner().value);
      trust_map::iterator j = trust.find(key);
      shared_ptr< set<rsa_keypair_id> > s;
      if (j == trust.end())
	{
	  s.reset(new set<rsa_keypair_id>());
	  trust.insert(make_pair(key, make_pair(s, i)));
	}
      else
	s = j->second.first;
      s->insert(i->inner().key);
    }

  for (trust_map::const_iterator i = trust.begin();
       i != trust.end(); ++i)
    {
      cert_value decoded_value;
      decode_base64(get<2>(i->first), decoded_value);
      if (app.lua.hook_get_manifest_cert_trust(*(i->second.first),
					       get<0>(i->first),
					       get<1>(i->first),
					       decoded_value))
	{
	  L(F("trust function liked %d signers of %s cert on file %s\n")
	    % i->second.first->size() % get<1>(i->first) % get<0>(i->first));
	  tmp_certs.push_back(*(i->second.second));
	}
      else
	{
	  W(F("trust function disliked %d signers of %s cert on file %s\n")
	    % i->second.first->size() % get<1>(i->first) % get<0>(i->first));
	}
    }
  certs = tmp_certs;
}


// cert-managing routines

cert::cert() 
{}

cert::cert(hexenc<id> const & ident,
	 cert_name const & name,
	 base64<cert_value> const & value,
	 rsa_keypair_id const & key)
  : ident(ident), name(name), value(value), key(key)
{}

cert::cert(hexenc<id> const & ident, 
	 cert_name const & name,
	 base64<cert_value> const & value,
	 rsa_keypair_id const & key,
	 base64<rsa_sha1_signature> const & sig)
  : ident(ident), name(name), value(value), key(key), sig(sig)
{}

bool 
cert::operator<(cert const & other) const
{
  return (ident < other.ident)
    || ((ident == other.ident) && name < other.name)
    || (((ident == other.ident) && name < other.name) 
	&& value < other.value)    
    || ((((ident == other.ident) && name < other.name) 
	 && value == other.value) && key < other.key)
    || (((((ident == other.ident) && name < other.name) 
	  && value == other.value) && key == other.key) && sig < other.sig);
}

bool 
cert::operator==(cert const & other) const
{
  return 
    (ident == other.ident)
    && (name == other.name)
    && (value == other.value)
    && (key == other.key)
    && (sig == other.sig);
}

// netio support
			 
void 
read_cert(string const & in, cert & t)
{
  size_t pos = 0;
  id hash = extract_substring(in, pos, 
			      constants::merkle_hash_length_in_bytes, 
			      "cert hash");
  id ident = extract_substring(in, pos, 
			       constants::merkle_hash_length_in_bytes, 
			       "cert ident");
  string name, val, key, sig;
  extract_variable_length_string(in, name, pos, "cert name");
  extract_variable_length_string(in, val, pos, "cert val");
  extract_variable_length_string(in, key, pos, "cert key");
  extract_variable_length_string(in, sig, pos, "cert sig");
  assert_end_of_buffer(in, pos, "cert"); 
  
  hexenc<id> hid;
  base64<cert_value> bval;
  base64<rsa_sha1_signature> bsig;

  encode_hexenc(ident, hid);
  encode_base64(cert_value(val), bval);
  encode_base64(rsa_sha1_signature(sig), bsig);

  cert tmp(hid, cert_name(name), bval, rsa_keypair_id(key), bsig);

  hexenc<id> hcheck;
  id check;
  cert_hash_code(tmp, hcheck);
  decode_hexenc(hcheck, check);
  if (!(check == hash))
    {
      hexenc<id> hhash;
      encode_hexenc(hash, hhash);
      throw bad_decode(F("calculated cert hash '%s' does not match '%s'")
		       % hcheck % hhash);
    }
  t = tmp;
}

void 
write_cert(cert const & t, string & out)
{  
  string name, key;
  hexenc<id> hash;
  id ident_decoded, hash_decoded;
  rsa_sha1_signature sig_decoded;
  cert_value value_decoded;

  cert_hash_code(t, hash);
  decode_base64(t.value, value_decoded);
  decode_base64(t.sig, sig_decoded);
  decode_hexenc(t.ident, ident_decoded);
  decode_hexenc(hash, hash_decoded);

  out.append(hash_decoded());
  out.append(ident_decoded());
  insert_variable_length_string(t.name(), out);
  insert_variable_length_string(value_decoded(), out);
  insert_variable_length_string(t.key(), out);
  insert_variable_length_string(sig_decoded(), out);
}

void 
cert_signable_text(cert const & t,
		       string & out)
{
  out = (F("[%s@%s:%s]") % t.name % t.ident % remove_ws(t.value())).str();
  L(F("cert: signable text %s\n") % out);
}

void 
cert_hash_code(cert const & t, hexenc<id> & out)
{
  string tmp(t.ident()
	     + ":" + t.name()
	     + ":" + remove_ws(t.value())
	     + ":" + t.key()
	     + ":" + remove_ws(t.sig()));
  data tdat(tmp);
  calculate_ident(tdat, out);
}

void 
calculate_cert(app_state & app, cert & t)
{
  string signed_text;
  base64< arc4<rsa_priv_key> > priv;
  cert_signable_text(t, signed_text);

  static std::map<rsa_keypair_id, base64< arc4<rsa_priv_key> > > privkeys;
  bool persist_ok = (!privkeys.empty()) || app.lua.hook_persist_phrase_ok();

  if (persist_ok
      && privkeys.find(t.key) != privkeys.end())
    {
      priv = privkeys[t.key];
    }
  else
    {
      N(app.db.private_key_exists(t.key),
	F("no private key '%s' found in database") % t.key);
      app.db.get_key(t.key, priv);
      if (persist_ok)
	privkeys.insert(make_pair(t.key, priv));
    }

  make_signature(app.lua, t.key, priv, signed_text, t.sig);
}

cert_status 
check_cert(app_state & app, cert const & t)
{

  base64< rsa_pub_key > pub;

  static std::map<rsa_keypair_id, base64< rsa_pub_key > > pubkeys;
  bool persist_ok = (!pubkeys.empty()) || app.lua.hook_persist_phrase_ok();

  if (persist_ok
      && pubkeys.find(t.key) != pubkeys.end())
    {
      pub = pubkeys[t.key];
    }
  else
    {
      if (!app.db.public_key_exists(t.key))
	return cert_unknown;
      app.db.get_key(t.key, pub);
      if (persist_ok)
	pubkeys.insert(make_pair(t.key, pub));
    }

  string signed_text;
  cert_signable_text(t, signed_text);
  if (check_signature(app.lua, t.key, pub, signed_text, t.sig))
    return cert_ok;
  else
    return cert_bad;
}


// "special certs"

string const ancestor_cert_name("ancestor");
string const branch_cert_name("branch");

bool 
guess_default_key(rsa_keypair_id & key, 
		  app_state & app)
{

  if (app.signing_key() != "")
    {
      key = app.signing_key;
      return true;
    }

  if (app.branch_name() != "")
    {
      cert_value branch(app.branch_name());
      if (app.lua.hook_get_branch_key(branch, key))
	return true;
    }
  
  vector<rsa_keypair_id> all_privkeys;
  app.db.get_private_keys(all_privkeys);
  if (all_privkeys.size() != 1) 
    return false;
  else
    {
      key = all_privkeys[0];  
      return true;
    }
}

void 
guess_branch(manifest_id const & id,
	     app_state & app,
	     cert_value & branchname)
{
  if (app.branch_name() != "")
    {
      branchname = app.branch_name();
    }
  else
    {
      vector< manifest<cert> > certs;
      cert_name branch(branch_cert_name);
      app.db.get_manifest_certs(id, branch, certs);
      erase_bogus_certs(certs, app);

      N(certs.size() != 0, 
	F("no branch certs found for manifest %s, "
	  "please provide a branch name") % id);
      
      N(certs.size() == 1,
	F("multiple branch certs found for manifest %s, "
	  "please provide a branch name") % id);
      
      decode_base64(certs[0].inner().value, branchname);
    }
}

void 
make_simple_cert(hexenc<id> const & id,
		 cert_name const & nm,
		 cert_value const & cv,
		 app_state & app,
		 cert & c)
{
  rsa_keypair_id key;
  N(guess_default_key(key,app),
    F("no unique private key for cert construction"));
  base64<cert_value> encoded_val;
  encode_base64(cv, encoded_val);
  cert t(id, nm, encoded_val, key);
  calculate_cert(app, t);
  c = t;
}


static void 
put_simple_manifest_cert(manifest_id const & id,
			 cert_name const & nm,
			 cert_value const & val,
			 app_state & app,
			 packet_consumer & pc)
{
  cert t;
  make_simple_cert(id.inner(), nm, val, app, t);
  manifest<cert> cc(t);
  pc.consume_manifest_cert(cc);
}

static void 
put_simple_file_cert(file_id const & id,
		     cert_name const & nm,
		     cert_value const & val,
		     app_state & app,
		     packet_consumer & pc)
{
  cert t;
  make_simple_cert(id.inner(), nm, val, app, t);
  file<cert> fc(t);
  pc.consume_file_cert(fc);
}

void 
cert_manifest_in_branch(manifest_id const & man, 
			cert_value const & branchname,
			app_state & app,
			packet_consumer & pc)
{
  put_simple_manifest_cert (man, branch_cert_name,
			    branchname, app, pc);
}


static void 
get_parents(manifest_id const & child,
	    set<manifest_id> & parents,
	    app_state & app)
{
  vector< manifest<cert> > certs;
  parents.clear();
  app.db.get_manifest_certs(child, ancestor_cert_name, certs);
  erase_bogus_certs(certs, app);
  for(vector< manifest<cert> >::const_iterator i = certs.begin();
      i != certs.end(); ++i)
    {
      cert_value tv;
      decode_base64(i->inner().value, tv);
      manifest_id parent(tv());
      vector< manifest<cert> > disapprove_certs;
      app.db.get_manifest_certs(child, disapproval_cert_name, 
				i->inner().value, disapprove_certs);
      erase_bogus_certs(disapprove_certs, app);
      if (disapprove_certs.empty())      
	parents.insert(parent);
    }
}


static bool 
find_relevant_edges(manifest_id const & ancestor,
		    manifest_id const & child,
		    app_state & app,
		    multimap <manifest_id, manifest_id> & relevant_edges,
		    set<manifest_id> & visited_nodes)
{
  if (ancestor == child)
    return true;
 
  visited_nodes.insert(child);
 
  set<manifest_id> parents;
  get_parents(child, parents, app);
  if (parents.size() == 0)
    return false;

  bool relevant_child = false;    

  for(set<manifest_id>::const_iterator i = parents.begin();
      i != parents.end(); ++i)
    {
      if (relevant_edges.find(*i) != relevant_edges.end())
	{
	  // edge was already deemed relevant; don't re-traverse!
	  relevant_child = true;
	}
      else if (visited_nodes.find(*i) != visited_nodes.end())
	{
	  // node was visited (and presumably deemed irrelevant);
	  // don't re-traverse!
	}
      else if (find_relevant_edges(ancestor, *i, app, 
				   relevant_edges, visited_nodes))
	{	  
	  relevant_child = true;
	  relevant_edges.insert(make_pair(child, *i));
	}
    }  

  return relevant_child;
}


void 
write_ancestry_paths(manifest_id const & ancestor,
		     manifest_id const & begin,
		     app_state & app,
		     packet_consumer & pc)
{

  typedef multimap < manifest_id, manifest_id > emap;
  typedef pair< shared_ptr<data>, shared_ptr<manifest_map> > frontier_entry;
  typedef map<manifest_id, frontier_entry> fmap;

  shared_ptr<fmap> frontier(new fmap());
  shared_ptr<fmap> next_frontier(new fmap());
  emap relevant_edges;
  set<manifest_id> visited;

  find_relevant_edges(ancestor, begin, app, relevant_edges, visited);

  shared_ptr<data> begin_data(new data());
  shared_ptr<manifest_map> begin_map(new manifest_map());
  {
    manifest_data mdat;
    app.db.get_manifest_version(begin, mdat);
    unpack(mdat.inner(), *begin_data);
  }
  read_manifest_map(*begin_data, *begin_map);

  P(F("writing %d historical edges\n") % relevant_edges.size());
  ticker n_edges("edges");

  frontier->insert(make_pair(begin, make_pair(begin_data, begin_map)));

  while (!frontier->empty())
    {
      for (fmap::const_iterator child = frontier->begin();
	   child != frontier->end(); ++child)
	{
	  manifest_id child_id = child->first;

	  pair<emap::const_iterator, emap::const_iterator> range;
	  shared_ptr<data> child_data = child->second.first;
	  shared_ptr<manifest_map> child_map = child->second.second;

	  range = relevant_edges.equal_range(child_id);

	  for (emap::const_iterator edge = range.first; 
	       edge != range.second; ++edge)
	    {
	      manifest_id parent_id = edge->second;

	      L(F("queueing edge %s -> %s\n") % parent_id % child_id);

	      // queue all the certs for this parent
	      vector< manifest<cert> > certs;
	      app.db.get_manifest_certs(parent_id, certs);
	      for(vector< manifest<cert> >::const_iterator cert = certs.begin();
		  cert != certs.end(); ++cert)
		pc.consume_manifest_cert(*cert);

	      // construct the parent
	      shared_ptr<data> parent_data(new data());
	      shared_ptr<manifest_map> parent_map(new manifest_map());

	      if (app.db.manifest_delta_exists(child_id, parent_id))
		app.db.compute_older_version(child_id, parent_id,
					     *child_data, *parent_data);
	      else
		{
		  manifest_data mdata;
		  app.db.get_manifest_version(parent_id, mdata);
		  unpack(mdata.inner(), *parent_data);
		}
	      
	      ++n_edges;

	      read_manifest_map(*parent_data, *parent_map);

	      // queue the delta to the parent
	      patch_set ps;	  
	      manifests_to_patch_set(*parent_map, *child_map, app, ps);
	      patch_set_to_packets(ps, app, pc);

	      // store the parent for the next cycle
	      next_frontier->insert
		(make_pair(parent_id, make_pair(parent_data, parent_map)));	      
	    }
	}
      swap(frontier, next_frontier);
      next_frontier->clear();
    }
}

// nb: "heads" only makes sense in the context of manifests (at the
// moment). we'll see if anyone cares to try branch certs on files. it
// doesn't sound terribly useful, but who knows.

void 
get_branch_heads(cert_value const & branchname,
		 app_state & app,
		 set<manifest_id> & heads)
{
  heads.clear();

  vector< manifest<cert> > 
    branch_certs, 
    ancestor_certs, 
    disapproval_certs;

  base64<cert_value> branch_encoded;
  encode_base64(branchname, branch_encoded);

  P(F("fetching heads of branch '%s'\n") % branchname);

  app.db.get_head_candidates(branch_encoded(), 
			     branch_certs, 
			     ancestor_certs, 
			     disapproval_certs);

  L(F("erasing bogus certs on '%s'\n") % branchname);

  erase_bogus_certs(branch_certs, app);
  erase_bogus_certs(ancestor_certs, app);
  erase_bogus_certs(disapproval_certs, app);

  for (vector< manifest<cert> >::const_iterator i = branch_certs.begin();
       i != branch_certs.end(); ++i)
    {
      heads.insert(i->inner().ident);
    }

  L(F("began with %d candidate heads\n") % heads.size());

  // Remove every manifest with descendents.
  for (vector< manifest<cert> >::const_iterator i = ancestor_certs.begin();
       i != ancestor_certs.end(); ++i)
    {      
      cert_value tv;
      decode_base64(i->inner().value, tv);
      manifest_id parent(tv());
      set<manifest_id>::const_iterator j = heads.find(parent);
      if (j != heads.end())
	{
	  heads.erase(j);
	}
    }
  
  for (vector< manifest<cert> >::const_iterator i = disapproval_certs.begin();
       i != disapproval_certs.end(); ++i)
    {
      set<manifest_id>::const_iterator j = heads.find(i->inner().ident);
      if (j != heads.end())
	{
	  L(F("removed disapproved head candidate '%s'\n") % i->inner().ident);
	  heads.erase(j);
	}
    }

  L(F("reduced to %d heads\n") % heads.size());
}
		   
void 
cert_file_ancestor(file_id const & parent, 
		   file_id const & child,
		   app_state & app,
		   packet_consumer & pc)
{
  if (parent == child)
    {
      W(F("parent file %d is same as child, skipping edge\n") % parent);
      return;
    }
  put_simple_file_cert (child, ancestor_cert_name,
			parent.inner()(), app, pc);
}

void 
cert_manifest_ancestor(manifest_id const & parent, 
		       manifest_id const & child,
		       app_state & app,
		       packet_consumer & pc)
{
  if (parent == child)
    {
      W(F("parent manifest %d is same as child, skipping edge\n") % parent);
      return;
    }
  put_simple_manifest_cert (child, ancestor_cert_name,
			    parent.inner()(), app, pc);
}


// calculating least common ancestors is a delicate thing.
// 
// it turns out that we cannot choose the simple "least common ancestor"
// for purposes of a merge, because it is possible that there are two
// equally reachable common ancestors, and this produces ambiguity in the
// merge. the result -- in a pathological case -- is silently accepting one
// set of edits while discarding another; not exactly what you want a
// version control tool to do.
//
// a conservative approximation, is what we'll call a "subgraph recurring"
// LCA algorithm. this is somewhat like locating the least common dominator
// node, but not quite. it is actually just a vanilla LCA search, except
// that any time there's a fork (a historical merge looks like a fork from
// our perspective, working backwards from children to parents) it reduces
// the fork to a common parent via a sequence of pairwise recursive calls
// to itself before proceeding. this will always resolve to a common parent
// with no ambiguity, unless it falls off the root of the graph.
//
// unfortunately the subgraph recurring algorithm sometimes goes too far
// back in history -- for example if there is an unambiguous propagate from
// one branch to another, the entire subgraph preceeding the propagate on
// the recipient branch is elided, since it is a merge.
//
// our current hypothesis is that the *exact* condition we're looking for,
// when doing a merge, is the least node which dominates one side of the
// merge and is an ancestor of the other.

static void 
ensure_parents_loaded(unsigned long man,
		      map< unsigned long, shared_ptr< dynamic_bitset<> > > & parents,
		      interner<unsigned long> & intern,
		      app_state & app)
{
  if (parents.find(man) != parents.end())
    return;

  L(F("loading parents for node %d\n") % man);

  set<manifest_id> imm_parents;
  get_parents(manifest_id(intern.lookup(man)), imm_parents, app);

  shared_ptr< dynamic_bitset<> > bits = 
    shared_ptr< dynamic_bitset<> >(new dynamic_bitset<>(parents.size()));
  
  for (set<manifest_id>::const_iterator p = imm_parents.begin();
       p != imm_parents.end(); ++p)
    {
      unsigned long pn = intern.intern(p->inner()());
      L(F("parent %s -> node %d\n") % *p % pn);
      if (pn >= bits->size()) 
	bits->resize(pn+1);
      bits->set(pn);
    }
    
  parents.insert(make_pair(man, bits));
}

static bool 
expand_dominators(map< unsigned long, shared_ptr< dynamic_bitset<> > > & parents,
		  map< unsigned long, shared_ptr< dynamic_bitset<> > > & dominators,
		  interner<unsigned long> & intern,
		  app_state & app)
{
  bool something_changed = false;
  vector<unsigned long> nodes;

  nodes.reserve(dominators.size());

  // pass 1, pull out all the node numbers we're going to scan this time around
  for (map< unsigned long, shared_ptr< dynamic_bitset<> > >::const_iterator e = dominators.begin(); 
       e != dominators.end(); ++e)
    nodes.push_back(e->first);
  
  // pass 2, update any of the dominator entries we can
  for (vector<unsigned long>::const_iterator n = nodes.begin(); n != nodes.end(); ++n)
    {
      shared_ptr< dynamic_bitset<> > bits = dominators[*n];
      dynamic_bitset<> saved(*bits);
      if (bits->size() <= *n)
	bits->resize(*n + 1);
      bits->set(*n);
      
      ensure_parents_loaded(*n, parents, intern, app);
      shared_ptr< dynamic_bitset<> > n_parents = parents[*n];
      
      dynamic_bitset<> intersection(bits->size());
      
      bool first = true;
      for (unsigned long parent = 0; parent != n_parents->size(); ++parent)
	{
	  if (! n_parents->test(parent))
	    continue;

	  if (dominators.find(parent) == dominators.end())
	    dominators.insert(make_pair(parent, 
					shared_ptr< dynamic_bitset<> >(new dynamic_bitset<>())));
	  shared_ptr< dynamic_bitset<> > pbits = dominators[parent];

	  if (bits->size() > pbits->size())
	    pbits->resize(bits->size());

	  if (pbits->size() > bits->size())
	    bits->resize(pbits->size());

	  if (first)
	    {
	      intersection = (*pbits);
	      first = false;
	    }
	  else
	    intersection &= (*pbits);
	}

      (*bits) |= intersection;
      if (*bits != saved)
	something_changed = true;
    }
  return something_changed;
}


static bool 
expand_ancestors(map< unsigned long, shared_ptr< dynamic_bitset<> > > & parents,
		 map< unsigned long, shared_ptr< dynamic_bitset<> > > & ancestors,
		 interner<unsigned long> & intern,
		 app_state & app)
{
  bool something_changed = false;
  vector<unsigned long> nodes;

  nodes.reserve(ancestors.size());

  // pass 1, pull out all the node numbers we're going to scan this time around
  for (map< unsigned long, shared_ptr< dynamic_bitset<> > >::const_iterator e = ancestors.begin(); 
       e != ancestors.end(); ++e)
    nodes.push_back(e->first);
  
  // pass 2, update any of the ancestor entries we can
  for (vector<unsigned long>::const_iterator n = nodes.begin(); n != nodes.end(); ++n)
    {
      shared_ptr< dynamic_bitset<> > bits = ancestors[*n];
      dynamic_bitset<> saved(*bits);
      if (bits->size() <= *n)
	bits->resize(*n + 1);
      bits->set(*n);

      ensure_parents_loaded(*n, parents, intern, app);
      shared_ptr< dynamic_bitset<> > n_parents = parents[*n];
      for (unsigned long parent = 0; parent != n_parents->size(); ++parent)
	{
	  if (! n_parents->test(parent))
	    continue;

	  if (bits->size() <= parent)
	    bits->resize(parent + 1);
	  bits->set(parent);

	  if (ancestors.find(parent) == ancestors.end())
	    ancestors.insert(make_pair(parent, 
					shared_ptr< dynamic_bitset<> >(new dynamic_bitset<>())));
	  shared_ptr< dynamic_bitset<> > pbits = ancestors[parent];

	  if (bits->size() > pbits->size())
	    pbits->resize(bits->size());

	  if (pbits->size() > bits->size())
	    bits->resize(pbits->size());

	  (*bits) |= (*pbits);
	}
      if (*bits != saved)
	something_changed = true;
    }
  return something_changed;
}

static bool 
find_intersecting_node(dynamic_bitset<> & fst, 
		       dynamic_bitset<> & snd, 
		       interner<unsigned long> const & intern, 
		       manifest_id & anc)
{

  if (fst.size() > snd.size())
    snd.resize(fst.size());
  else if (snd.size() > fst.size())
    fst.resize(snd.size());

  dynamic_bitset<> intersection = fst & snd;
  if (intersection.any())
    {
      L(F("found %d intersecting nodes") % intersection.count());
      for (unsigned long i = 0; i < intersection.size(); ++i)
	{
	  if (intersection.test(i))
	    {
	      anc = manifest_id(intern.lookup(i));
	      return true;
	    }
	}
    }
  return false;
}

//  static void
//  dump_bitset_map(string const & hdr,
//  		map< unsigned long, shared_ptr< dynamic_bitset<> > > const & mm)
//  {
//    L(F("dumping [%s] (%d entries)\n") % hdr % mm.size());
//    for (map< unsigned long, shared_ptr< dynamic_bitset<> > >::const_iterator i = mm.begin();
//         i != mm.end(); ++i)
//      {
//        L(F("dump [%s]: %d -> %s\n") % hdr % i->first % (*(i->second)));
//      }
//  }

bool 
find_common_ancestor(manifest_id const & left,
		     manifest_id const & right,
		     manifest_id & anc,
		     app_state & app)
{
  interner<unsigned long> intern;
  map< unsigned long, shared_ptr< dynamic_bitset<> > > 
    parents, ancestors, dominators;
  
  unsigned long ln = intern.intern(left.inner()());
  unsigned long rn = intern.intern(right.inner()());
  
  shared_ptr< dynamic_bitset<> > lanc = shared_ptr< dynamic_bitset<> >(new dynamic_bitset<>());
  shared_ptr< dynamic_bitset<> > ranc = shared_ptr< dynamic_bitset<> >(new dynamic_bitset<>());
  shared_ptr< dynamic_bitset<> > ldom = shared_ptr< dynamic_bitset<> >(new dynamic_bitset<>());
  shared_ptr< dynamic_bitset<> > rdom = shared_ptr< dynamic_bitset<> >(new dynamic_bitset<>());
  
  ancestors.insert(make_pair(ln, lanc));
  ancestors.insert(make_pair(rn, ranc));
  dominators.insert(make_pair(ln, ldom));
  dominators.insert(make_pair(rn, rdom));
  
  L(F("searching for common ancestor, left=%s right=%s\n") % left % right);
  
  while (expand_ancestors(parents, ancestors, intern, app) ||
	 expand_dominators(parents, dominators, intern, app))
    {
      L(F("common ancestor scan [par=%d,anc=%d,dom=%d]\n") % 
	parents.size() % ancestors.size() % dominators.size());

      if (find_intersecting_node(*lanc, *rdom, intern, anc))
	{
	  L(F("found node %d, ancestor of left %s and dominating right %s\n")
	    % anc % left % right);
	  return true;
	}
      
      else if (find_intersecting_node(*ranc, *ldom, intern, anc))
	{
	  L(F("found node %d, ancestor of right %s and dominating left %s\n")
	    % anc % right % left);
	  return true;
	}
    }
//      dump_bitset_map("ancestors", ancestors);
//      dump_bitset_map("dominators", dominators);
//      dump_bitset_map("parents", parents);
  return false;
}


// stuff for handling rename certs / rename edges

// rename edges associate a particular name mapping with a particular edge
// in the ancestry graph. they assist the algorithm in patch_set.cc in
// determining which add/del pairs count as moves.

rename_edge::rename_edge(rename_edge const & other)
{
  parent = other.parent;
  child = other.child;
  mapping = other.mapping;
}

static void 
include_rename_edge(rename_edge const & in, 
		    rename_edge & out)
{
  L(F("merging rename edge %s -> %s with %s -> %s\n")
    % in.parent % in.child % out.parent % out.child);

  set<file_path> rename_targets;
  for (rename_set::const_iterator i = out.mapping.begin();
       i != out.mapping.end(); ++i)
    {
      I(rename_targets.find(i->second) == rename_targets.end());
      rename_targets.insert(i->second);
    }

  for (rename_set::const_iterator i = in.mapping.begin();
       i != in.mapping.end(); ++i)
    {
      rename_set::const_iterator other = out.mapping.find(i->first);
      if (other == out.mapping.end())
	I(rename_targets.find(i->second) == rename_targets.end());
      else      
	N(other->second == i->second,
	  F("impossible historical record of renames: %s renamed to both %s and %s")
	  % i->first % i->second % other->second);

      L(F("merged in rename of %s -> %s\n")
	% i->first % i->second);
      rename_targets.insert(i->second);
      out.mapping.insert(*i);
    }  
}

static void 
compose_rename_edges(rename_edge const & a,
		     rename_edge const & b,
		     rename_edge & out)
{
  I(a.child == b.parent);
  out.mapping.clear();
  out.parent = a.parent;
  out.child = b.child;
  set<file_path> rename_targets;

  L(F("composing rename edges %s -> %s and %s -> %s\n")
    % a.parent % a.child % b.parent % b.child);

  for (rename_set::const_iterator i = a.mapping.begin();
       i != a.mapping.end(); ++i)
    {
      I(rename_targets.find(i->second) == rename_targets.end());
      I(out.mapping.find(i->first) == out.mapping.end());
      rename_targets.insert(i->second);

      rename_set::const_iterator j = b.mapping.find(i->second);
      if (j != b.mapping.end())
	{
	  L(F("composing rename %s -> %s with %s -> %s\n")
	    % i->first % i->second % j->first % j->second);
	  out.mapping.insert(make_pair(i->first, j->second));
	}
      else
	{
	  L(F("composing lone rename %s -> %s\n")
	    % i->first % i->second);
	  out.mapping.insert(*i);
	}
    }
}

static void 
write_rename_edge(rename_edge const & edge,
		  string & val)
{
  ostringstream oss;
  gzip<data> compressed;
  oss << edge.parent << "\n";
  for (rename_set::const_iterator i = edge.mapping.begin();
       i != edge.mapping.end(); ++i)
    {
      oss << i->first << "\n" << i->second << "\n";
    }
  encode_gzip(data(oss.str()), compressed);
  val = compressed();
}

static void 
read_rename_edge(hexenc<id> const & node,
		 base64<cert_value> const & val,
		 rename_edge & edge)
{
  edge.child = manifest_id(node);
  cert_value decoded;
  data decompressed_data;
  string decompressed;
  decode_base64(val, decoded);
  decode_gzip(gzip<data>(decoded()), decompressed_data);
  decompressed = decompressed_data();

  vector<string> lines;
  split_into_lines(decompressed, lines);

  N(lines.size() >= 1 && lines.size() % 2 == 1, 
    F("malformed rename cert"));

  edge.parent = manifest_id(idx(lines, 0));
  set<file_path> rename_targets;

  for (size_t i = 1; i+1 < lines.size(); ++i)
    {
    std::string src(idx(lines, i));
    ++i;
    std::string dst(idx(lines, i));
    N(edge.mapping.find(file_path(src)) == edge.mapping.end(), 
      F("duplicate rename src entry for %s") % src);
    N(rename_targets.find(file_path(dst)) == rename_targets.end(),
      F("duplicate rename dst entry for %s") % dst);
    rename_targets.insert(file_path(dst));      
    edge.mapping.insert(make_pair(file_path(src), file_path(dst)));    
    }
}

/* 
 * The idea with this algorithm is to walk from child up to ancestor,
 * recursively, accumulating all the rename_edges associated with
 * intermediate nodes into *one big rename_edge*. don't confuse an ancestry
 * edge with a rename edge here: when we get_parents, that's loading
 * ancestry edges. rename edges are a secondary graph overlaid on some --
 * but not all -- edges in the ancestry graph. I know, it's a real party.
 *
 * clever readers will realize this is an overlapping-subproblem type
 * situation (as is the relevant_edges algorithm later on) and thus needs
 * to keep a dynamic programming map to keep itself in linear complexity.
 *
 * in fact, we keep two: one which maps to computed results (partial_edges)
 * and one which just keeps a set of all nodes we traversed
 * (visited_nodes). in theory it could be one map with an extra bool stuck
 * on each entry, but I think that would make it even less readable. it's
 * already atrocious.
 */

static bool 
calculate_renames_recursive(manifest_id const & ancestor,
			    manifest_id const & child,
			    app_state & app,
			    rename_edge & edge,
			    map<manifest_id, shared_ptr<rename_edge> > & partial_edges,
			    set<manifest_id> & visited_nodes)
{

  if (ancestor == child)
    return false;

  visited_nodes.insert(child);

  set<manifest_id> parents;
  get_parents(child, parents, app);
  bool relevant_child = false;

  edge.child = child;
  map<manifest_id, rename_edge> incident_edges;

  rename_edge child_edge;
  vector< manifest<cert> > certs;
  app.db.get_manifest_certs(child, cert_name(rename_cert_name), certs);
  erase_bogus_certs(certs, app);

  L(F("found %d incident rename edges at node %s\n")
    % certs.size() % child);
  
  for(vector< manifest<cert> >::const_iterator j = certs.begin();
      j != certs.end(); ++j)
    {
      rename_edge curr_child_edge;
      read_rename_edge(j->inner().ident, j->inner().value, curr_child_edge);
      incident_edges.insert(make_pair(curr_child_edge.parent, curr_child_edge));
      relevant_child = true;
    }

  L(F("exploring renames from parents of %s, seeking towards %s\n")
    % child % ancestor);

  for(set<manifest_id>::const_iterator i = parents.begin();
      i != parents.end(); ++i)
    {

      bool relevant_parent = false;
      rename_edge curr_parent_edge;
      map<manifest_id, shared_ptr<rename_edge> >::const_iterator 
	j = partial_edges.find(*i);
      if (j != partial_edges.end()) 
	{
	  // a recursive call has traversed this parent before and found an
	  // existing rename edge. just reuse that rather than re-traversing
	  curr_parent_edge = *(j->second);
	  relevant_parent = true;
	}
      else if (visited_nodes.find(*i) != visited_nodes.end())
	{
	  // a recursive call has traversed this parent, but there was no
	  // rename edge on it, so the parent is irrelevant. skip.
	  relevant_parent = false;
	}
      else
	relevant_parent = calculate_renames_recursive(ancestor, *i, app, 
						      curr_parent_edge, 
						      partial_edges,
						      visited_nodes);

      if (relevant_parent)
	{
	  map<manifest_id, rename_edge>::const_iterator inc = incident_edges.find(*i);
	  if (inc != incident_edges.end())
	    {
	      L(F("ancestor edge %s -> %s is relevant, composing with edge %s -> %s\n") 
		% curr_parent_edge.parent % curr_parent_edge.child 
		% inc->second.parent % inc->second.child);
	      rename_edge tmp;
	      compose_rename_edges(curr_parent_edge, inc->second, tmp);
	      include_rename_edge(tmp, edge);
	      incident_edges.erase(*i);
	    }
	  else
	    {				    
	      L(F("ancestor edge %s -> %s is relevant, merging with current\n") % (*i) % child);
	      include_rename_edge(curr_parent_edge, edge);
	    }
	  relevant_child = true;
	}
    }

  // copy any remaining incident edges  
  for (map<manifest_id, rename_edge>::const_iterator i = incident_edges.begin();
       i != incident_edges.end(); ++i)
    {
      relevant_child = true;
      L(F("adding lone incident edge %s -> %s\n") 
	% i->second.parent % i->second.child);
      include_rename_edge(i->second, edge);
    }

  // store the partial edge from ancestor -> child, so that if anyone
  // re-traverses this edge they'll just fetch from the partial_edges
  // cache.
  if (relevant_child)
    partial_edges.insert(make_pair(child, shared_ptr<rename_edge>(new rename_edge(edge))));

  return relevant_child;
}

void 
calculate_renames(manifest_id const & ancestor,
		  manifest_id const & child,
		  app_state & app,
		  rename_edge & edge)
{
  // it's ok if we can't find any paths
  set<manifest_id> visited;
  map<manifest_id, shared_ptr<rename_edge> > partial;
  calculate_renames_recursive(ancestor, child, app, edge, partial, visited);
}


// "standard certs"

string const date_cert_name = "date";
string const author_cert_name = "author";
string const tag_cert_name = "tag";
string const changelog_cert_name = "changelog";
string const comment_cert_name = "comment";
string const disapproval_cert_name = "disapproval";
string const testresult_cert_name = "testresult";
string const rename_cert_name = "rename";
string const vcheck_cert_name = "vcheck";


static void 
cert_manifest_date(manifest_id const & m, 
		   boost::posix_time::ptime t,
		   app_state & app,
		   packet_consumer & pc)
{
  string val = boost::posix_time::to_iso_extended_string(t);
  put_simple_manifest_cert(m, date_cert_name, val, app, pc);
}

void 
cert_manifest_date_time(manifest_id const & m, 
			time_t t,
			app_state & app,
			packet_consumer & pc)
{
  // make sure you do all your CVS conversions by 2038!
  boost::posix_time::ptime tmp(boost::gregorian::date(1970,1,1), 
			       boost::posix_time::seconds(static_cast<long>(t)));
  cert_manifest_date(m, tmp, app, pc);
}

void 
cert_manifest_date_now(manifest_id const & m, 
		       app_state & app,
		       packet_consumer & pc)
{
  cert_manifest_date(m, boost::posix_time::second_clock::universal_time(), app, pc);
}

void 
cert_manifest_author(manifest_id const & m, 
		     string const & author,
		     app_state & app,
		     packet_consumer & pc)
{
  put_simple_manifest_cert(m, author_cert_name, author, app, pc);  
}

void 
cert_manifest_author_default(manifest_id const & m, 
			     app_state & app,
			     packet_consumer & pc)
{
  string author;
  N(app.lua.hook_get_author(app.branch_name(), author),
    F("no default author name for branch '%s'") % app.branch_name);
  put_simple_manifest_cert(m, author_cert_name, author, app, pc);
}

void 
cert_manifest_tag(manifest_id const & m, 
		  string const & tagname,
		  app_state & app,
		  packet_consumer & pc)
{
  put_simple_manifest_cert(m, tag_cert_name, tagname, app, pc);  
}


void 
cert_manifest_changelog(manifest_id const & m, 
			string const & changelog,
			app_state & app,
			packet_consumer & pc)
{
  put_simple_manifest_cert(m, changelog_cert_name, changelog, app, pc);  
}

void 
cert_file_comment(file_id const & f, 
		  string const & comment,
		  app_state & app,
		  packet_consumer & pc)
{
  put_simple_file_cert(f, comment_cert_name, comment, app, pc);  
}

void 
cert_manifest_comment(manifest_id const & m, 
		      string const & comment,
		      app_state & app,
		      packet_consumer & pc)
{
  put_simple_manifest_cert(m, comment_cert_name, comment, app, pc);  
}

void 
cert_file_approval(file_id const & f1, 
		   file_id const & f2, 
		   bool const approval,
		   app_state & app,
		   packet_consumer & pc)
{
  if (approval)
    put_simple_file_cert(f2, ancestor_cert_name, f1.inner()(), app, pc);
  else
    put_simple_file_cert(f2, disapproval_cert_name, f1.inner()(), app, pc);
}

void 
cert_manifest_approval(manifest_id const & m1, 
		       manifest_id const & m2, 
		       bool const approval,
		       app_state & app,
		       packet_consumer & pc)
{
  if (approval)
    put_simple_manifest_cert(m2, ancestor_cert_name, m1.inner()(), app, pc);
  else
    put_simple_manifest_cert(m2, disapproval_cert_name, m1.inner()(), app, pc);
}

void 
cert_manifest_testresult(manifest_id const & m, 
			 string const & results,
			 app_state & app,
			 packet_consumer & pc)
{
  bool passed = false;
  try
    {
      passed = lexical_cast<bool>(results);
    }
  catch (boost::bad_lexical_cast & e)
    {
      throw oops("test results must be a boolean value: '0' or '1'");
    }
  put_simple_manifest_cert(m, testresult_cert_name, lexical_cast<string>(passed), app, pc); 
}

void 
cert_manifest_rename(manifest_id const & m, 
		     rename_edge const & re,
		     app_state & app,
		     packet_consumer & pc)
{
  string val;
  write_rename_edge(re, val);
  put_simple_manifest_cert(m, rename_cert_name, val, app, pc);
}

			  
static void 
calculate_vcheck_mac(manifest_id const & m, 
		     string const & seed,
		     string & mac,
		     app_state & app)
{
  L(F("calculating vcheck cert on %s with seed %s\n") % m % seed);

  manifest_data mdat;
  manifest_map mm, mm_mac;
  app.db.get_manifest_version(m, mdat);
  read_manifest_map(mdat, mm);
  for (manifest_map::const_iterator i = mm.begin(); i != mm.end(); ++i)
    {
      path_id_pair pip(i);
      N(app.db.file_version_exists(pip.ident()),
	F("missing file version %s for %s") % pip.ident() % pip.path());

      file_data fdat;
      data dat;
      string fmac;

      app.db.get_file_version(pip.ident(), fdat);
      unpack(fdat.inner(), dat);
      calculate_mac(seed, dat(), fmac); 
      mm_mac.insert(make_pair(pip.path(), file_id(fmac)));
      L(F("mac of %s (seed=%s, id=%s) is %s\n") % pip.path() % seed % pip.ident() % fmac);
    }
  
  data dat;
  write_manifest_map(mm_mac, dat);
  calculate_mac(seed, dat(), mac); 
  L(F("mac of %d entry mac-manifest is %s\n") % mm_mac.size() % mac);
}

void 
cert_manifest_vcheck(manifest_id const & m, 
		     app_state & app,
		     packet_consumer & pc)
{
  string mac;
  string seed;
  make_random_seed(app, seed);
  P(F("calculating vcheck packet for %s with seed %s\n") % m % seed);
  calculate_vcheck_mac(m, seed, mac, app);
  string val = seed + ":" + mac;
  put_simple_manifest_cert(m, vcheck_cert_name, val, app, pc);
}

void 
check_manifest_vcheck(manifest_id const & m, 
		      app_state & app)
{

  vector< manifest<cert> > certs;
  app.db.get_manifest_certs(m, cert_name(vcheck_cert_name), certs);
  erase_bogus_certs(certs, app);
  N(certs.size() != 0,
    F("missing non-bogus vcheck certs on %s") % m);

  for (vector< manifest<cert> >::const_iterator cert = certs.begin();
       cert != certs.end(); ++cert)
    {
      cert_value tv;
      decode_base64(cert->inner().value, tv);

      string cv = tv();
      string::size_type colon_pos = cv.find(':');

      N(colon_pos != string::npos ||
	colon_pos +1 >= cv.size(),
	F("malformed vcheck cert on %s: %s") % m % cv);

      string seed = cv.substr(0, colon_pos);
      string their_mac = cv.substr(colon_pos + 1);
      string our_mac;

      P(F("confirming vcheck packet on %s from %s (%d bit seed)\n") 
	% m % cert->inner().key % (seed.size() * 4));

      calculate_vcheck_mac(m, seed, our_mac, app);

      if (their_mac != our_mac)
	{
	  W(F("vcheck FAILED: key %s, id %s\n") % cert->inner().key % m);
	  W(F("seed: %s\n") % seed);
	  W(F("their mac: %s\n") % their_mac);
	  W(F("our mac: %s\n") % our_mac);  
	  W(F("you should investigate the contents of manifest %s immediately\n") % m);
	}
      else 
	P(F("vcheck OK: key %s, id %s\n") % cert->inner().key % m);
    }
}

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

#endif // BUILD_UNIT_TESTS
