
// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "cert.hh"
#include "packet.hh"
#include "app_state.hh"
#include "keys.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "ui.hh"

#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/dynamic_bitset.hpp>

#include <string>
#include <vector>

using std::string;

// cert destroyer!

struct bogus_cert_p
{
  app_state & app;
  bogus_cert_p(app_state & a) : app(a) {};
  bool operator()(manifest<cert> const & c) const 
  {
  string txt;
  cert_signable_text(c.inner(), txt);
  L("checking cert %s\n", txt.c_str());
  if (check_cert(app,c.inner()))
    {
      L("cert ok\n");
      return false;
    }
  else
    {
      ui.warn("bad signature by '" + c.inner().key() + "' on '" + txt + "'\n");
      return true;
    }
  }
};

void erase_bogus_certs(vector< manifest<cert> > & certs,
			      app_state & app)
{
  vector< manifest<cert> >::iterator e = 
    remove_if(certs.begin(), certs.end(), bogus_cert_p(app));
  certs.erase(e, certs.end());      
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

bool cert::operator<(cert const & other) const
{
  return (ident < other.ident)
    || ((ident == other.ident) && value < other.value)
    || (((ident == other.ident) && value == other.value) && key < other.key)
    || ((((ident == other.ident) && value == other.value) && key == other.key) && sig < other.sig);
}

bool cert::operator==(cert const & other) const
{
  return 
    (ident == other.ident)
    && (value == other.value)
    && (key == other.key)
    && (sig == other.sig);
}


void cert_signable_text(cert const & t,
		       string & out)
{
  out = 
    string("[") 
    + t.name() + "@" + t.ident() 
    + ":" + remove_ws(t.value()) +  
    + "]";
  L("cert: signable text %s\n", out.c_str());
}

void calculate_cert(app_state & app, cert & t)
{
  string signed_text;
  base64< arc4<rsa_priv_key> > priv;
  cert_signable_text(t, signed_text);
  N(app.db.private_key_exists(t.key),
    "no private key " + t.key() + " found in database");
  app.db.get_key(t.key, priv);
  make_signature(app.lua, t.key, priv, signed_text, t.sig);
}

bool check_cert(app_state & app, cert const & t)
{
  if (!app.db.public_key_exists(t.key))
    return false;
  string signed_text;
  base64< rsa_pub_key > pub;
  cert_signable_text(t, signed_text);
  app.db.get_key(t.key, pub);
  return check_signature(pub, signed_text, t.sig);
}


// "special certs"

string const ancestor_cert_name("ancestor");
string const branch_cert_name("branch");

bool guess_default_key(rsa_keypair_id & key, app_state & app)
{

  if (app.signing_key() != "")
    {
      key = app.signing_key;
      return true;
    }

  if (app.branch_name != "")
    {
      cert_value branch(app.branch_name);
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

void guess_branch(manifest_id const & id,
		  app_state & app,
		  cert_value & branchname)
{
  if (app.branch_name != "")
    {
      branchname = app.branch_name;
    }
  else
    {
      vector< manifest<cert> > certs;
      cert_name branch(branch_cert_name);
      app.db.get_manifest_certs(id, branch, certs);
      erase_bogus_certs(certs, app);

      N(certs.size() != 0, 
	string("no branch certs found for manifest ")
	+ id.inner()() + ", please provide a branch name");

      N(certs.size() == 1,
	string("multiple branch certs found for manifest ")
	+ id.inner()() + ", please provide a branch name");
      
      decode_base64(certs[0].inner().value, branchname);
    }
}

void make_simple_cert(hexenc<id> const & id,
		      cert_name const & nm,
		      cert_value const & cv,
		      app_state & app,
		      cert & c)
{
  rsa_keypair_id key;
  N(guess_default_key(key,app),
    "no unique private key for cert construction");  
  base64<cert_value> encoded_val;
  encode_base64(cv, encoded_val);
  cert t(id, nm, encoded_val, key);
  calculate_cert(app, t);
  c = t;
}


static void put_simple_manifest_cert(manifest_id const & id,
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

static void put_simple_file_cert(file_id const & id,
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

void cert_manifest_in_branch(manifest_id const & man, 
			     cert_value const & branchname,
			     app_state & app,
			     packet_consumer & pc)
{
  put_simple_manifest_cert (man, branch_cert_name,
			    branchname, app, pc);
}


// nb: "heads" only makes sense in the context of manifests (at the
// moment). we'll see if anyone cares to try branch certs on files. it
// doesn't sound terribly useful, but who knows.

void get_branch_heads(cert_value const & branchname,
		     app_state & app,
		     vector<manifest_id> & heads)
{
  heads.clear();
  vector< manifest<cert> > certs;
  base64<cert_value> branch_encoded;
  encode_base64(branchname, branch_encoded);
  
  L("getting branch certs for %s\n", branchname().c_str());
  app.db.get_manifest_certs(cert_name(branch_cert_name), branch_encoded, certs);
  erase_bogus_certs(certs, app);
  L("got %d branch members\n", certs.size());
  for (vector< manifest<cert> >::const_iterator i = certs.begin();
       i != certs.end(); ++i)
    {
      vector< manifest<cert> > children;
      cert_value tv = cert_value(i->inner().ident());
      base64<cert_value> id_encoded;
      encode_base64(tv, id_encoded);
      app.db.get_manifest_certs(ancestor_cert_name, id_encoded, children);
      erase_bogus_certs(children, app);
      if (children.size() == 0)
	{
	  L("found head %s\n", i->inner().ident().c_str());
	  heads.push_back(manifest_id(i->inner().ident));
	}
      else
	{
	  L("found non-head %s\n", i->inner().ident().c_str());
	}
    }
}
		   
void cert_file_ancestor(file_id const & parent, 
		       file_id const & child,
		       app_state & app,
			packet_consumer & pc)
{
  put_simple_file_cert (child, ancestor_cert_name,
			parent.inner()(), app, pc);
}

void cert_manifest_ancestor(manifest_id const & parent, 
			   manifest_id const & child,
			   app_state & app,
			    packet_consumer & pc)
{
  put_simple_manifest_cert (child, ancestor_cert_name,
			    parent.inner()(), app, pc);
}


bool find_common_ancestor(manifest_id const & left,
			  manifest_id const & right,
			  manifest_id & anc,
			  app_state & app)
{
  // nb: I think this is not exactly a "least common ancestor"
  // algorithm. it just looks for *a* common ancestor that's probably
  // pretty close to the least. I think. proof? murky. maybe try
  // something a little more precise later?

  set<manifest_id> left_ancestors, right_ancestors;
  vector<manifest_id> left_frontier, right_frontier;
  cert_name tn(ancestor_cert_name);
  left_frontier.push_back(left);
  right_frontier.push_back(right);
  L("searching for common ancestors of %s and %s\n",
    left.inner()().c_str(), right.inner()().c_str());
  while(left_frontier.size() > 0 || right_frontier.size() > 0)
    {
      vector<manifest_id> next_left_frontier, next_right_frontier;

      for(vector<manifest_id>::const_iterator i = left_frontier.begin();
	  i != left_frontier.end(); ++i)
	{
	  vector< manifest<cert> > tmp;
	  app.db.get_manifest_certs(*i, tn, tmp);
	  erase_bogus_certs(tmp, app);
	  for(vector< manifest<cert> >::const_iterator j = tmp.begin();
	      j != tmp.end(); ++j)
	    {
	      cert_value tv;
	      decode_base64(j->inner().value, tv);
	      manifest_id m = manifest_id(tv());
	      if (right_ancestors.find(m) != right_ancestors.end())
		{
		  L("found common ancestor %s\n", m.inner()().c_str());
		  anc = m;
		  return true;
		}
	      else
		{
		  L("recording ancestor edge %s -> %s\n",
		    i->inner()().c_str(), m.inner()().c_str());
		  next_left_frontier.push_back(m);
		  left_ancestors.insert(m);
		}
	    }	  
	}

      for(vector<manifest_id>::const_iterator i = right_frontier.begin();
	  i != right_frontier.end(); ++i)
	{
	  vector< manifest<cert> > tmp;
	  app.db.get_manifest_certs(*i, tn, tmp);
	  erase_bogus_certs(tmp, app);
	  for(vector< manifest<cert> >::const_iterator j = tmp.begin();
	      j != tmp.end(); ++j)
	    {
	      cert_value tv;
	      decode_base64(j->inner().value, tv);
	      manifest_id m = manifest_id(tv());
	      if (left_ancestors.find(m) != left_ancestors.end())
		{
		  L("found common ancestor %s\n", m.inner()().c_str());
		  anc = m;
		  return true;
		}
	      else
		{
		  L("recording ancestor edge %s -> %s\n",
		    i->inner()().c_str(), m.inner()().c_str());
		  next_right_frontier.push_back(m);
		  right_ancestors.insert(m);
		}
	    }	  
	}
      left_frontier = next_left_frontier;
      right_frontier = next_right_frontier;      
    }
  return false;
}

// "standard certs"

string const date_cert_name = "date";
string const author_cert_name = "author";
string const tag_cert_name = "tag";
string const changelog_cert_name = "changelog";
string const comment_cert_name = "comment";
string const approval_cert_name = "approval";
string const testresult_cert_name = "testresult";


static
void cert_manifest_date(manifest_id const & m, 
			boost::posix_time::ptime t,
			app_state & app,
			packet_consumer & pc)
{
  string val = boost::posix_time::to_iso_extended_string(t);
  put_simple_manifest_cert(m, date_cert_name, val, app, pc);
}

void cert_manifest_date_time(manifest_id const & m, 
			     time_t t,
			     app_state & app,
			     packet_consumer & pc)
{
  // make sure you do all your CVS conversions by 2038!
  boost::posix_time::ptime tmp(boost::gregorian::date(1970,1,1), 
			       boost::posix_time::seconds(static_cast<long>(t)));
  cert_manifest_date(m, tmp, app, pc);
}

void cert_manifest_date_now(manifest_id const & m, 
			    app_state & app,
			    packet_consumer & pc)
{
  cert_manifest_date(m, boost::posix_time::second_clock::universal_time(), app, pc);
}

void cert_manifest_author(manifest_id const & m, 
			  string const & author,
			  app_state & app,
			  packet_consumer & pc)
{
  put_simple_manifest_cert(m, author_cert_name, author, app, pc);  
}

void cert_manifest_author_default(manifest_id const & m, 
				  app_state & app,
				  packet_consumer & pc)
{
  string author;
  N(app.lua.hook_get_author(app.branch_name, author),
    (string("no default author name for branch '") + app.branch_name + "'"));
  put_simple_manifest_cert(m, author_cert_name, author, app, pc);
}

void cert_manifest_tag(manifest_id const & m, 
		       string const & tagname,
		       app_state & app,
		       packet_consumer & pc)
{
  put_simple_manifest_cert(m, tag_cert_name, tagname, app, pc);  
}


void cert_manifest_changelog(manifest_id const & m, 
			     string const & changelog,
			     app_state & app,
			     packet_consumer & pc)
{
  put_simple_manifest_cert(m, changelog_cert_name, changelog, app, pc);  
}

void cert_file_comment(file_id const & f, 
		       string const & comment,
		       app_state & app,
		       packet_consumer & pc)
{
  put_simple_file_cert(f, comment_cert_name, comment, app, pc);  
}

void cert_manifest_comment(manifest_id const & m, 
			   string const & comment,
			   app_state & app,
			   packet_consumer & pc)
{
  put_simple_manifest_cert(m, comment_cert_name, comment, app, pc);  
}

void cert_file_approval(file_id const & f, 
			bool const approval,
			app_state & app,
			packet_consumer & pc)
{
  string approved = approval ? "true" : "false";
  put_simple_file_cert(f, approval_cert_name, approved, app, pc);
}

void cert_manifest_approval(manifest_id const & m, 
			    bool const approval,
			    app_state & app,
			    packet_consumer & pc)
{
  string approved = approval ? "true" : "false";
  put_simple_manifest_cert(m, approval_cert_name, approved, app, pc);  
}

void cert_manifest_testresult(manifest_id const & m, 
			      string const & suitename,
			      string const & results,
			      app_state & app,
			      packet_consumer & pc)
{
  if (results.find_first_not_of("01") != string::npos)
    throw oops("test results must be a contiguous sequence of '0' and '1' characters");
  put_simple_manifest_cert(m, testresult_cert_name, results, app, pc); 
}



#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

#endif // BUILD_UNIT_TESTS
