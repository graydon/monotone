// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <iostream>
#include <string>

#include <boost/optional.hpp>
#include <boost/regex.hpp>
#include <boost/lexical_cast.hpp>

#include "app_state.hh"
#include "packet.hh"
#include "sanity.hh"
#include "transforms.hh"

using namespace boost;
using namespace std;

// --- packet db writer --

typedef enum {prereq_file, prereq_manifest} prereq_type;
class delayed_packet;

class 
prerequisite
{
  hexenc<id> ident;
  prereq_type type;
  set< shared_ptr<delayed_packet> > delayed;
public:
  prerequisite(hexenc<id> const & i, prereq_type pt) 
    : ident(i), type(pt)
  {}
  void add_dependent(shared_ptr<delayed_packet> p);
  bool has_live_dependents();
  void satisfy(shared_ptr<prerequisite> self,
	       packet_db_writer & pw);
  bool operator<(prerequisite const & other)
  {
    return type < other.type ||
      (type == other.type && ident < other.ident);
  }  
};

class 
delayed_packet
{
  set< shared_ptr<prerequisite> > unsatisfied_prereqs;
  set< shared_ptr<prerequisite> > satisfied_prereqs;
public:
  void add_prerequisite(shared_ptr<prerequisite> p);
  bool all_prerequisites_satisfied();
  void prerequisite_satisfied(shared_ptr<prerequisite> p, 
			      packet_db_writer & pw);
  virtual void apply_delayed_packet(packet_db_writer & pw) = 0;
  virtual ~delayed_packet() {}
};

void 
prerequisite::add_dependent(shared_ptr<delayed_packet> d)
{
  delayed.insert(d);
}

void
prerequisite::satisfy(shared_ptr<prerequisite> self,
		      packet_db_writer & pw)
{
  set< shared_ptr<delayed_packet> > dead;
  for (set< shared_ptr<delayed_packet> >::const_iterator i = delayed.begin();
       i != delayed.end(); ++i)
    {
      (*i)->prerequisite_satisfied(self, pw);
      if ((*i)->all_prerequisites_satisfied())
	dead.insert(*i);
    }
  for (set< shared_ptr<delayed_packet> >::const_iterator i = dead.begin();
       i != dead.end(); ++i)
    {
      delayed.erase(*i);
    }
}

void 
delayed_packet::add_prerequisite(shared_ptr<prerequisite> p)
{
  unsatisfied_prereqs.insert(p);
}

bool 
delayed_packet::all_prerequisites_satisfied()
{
  return unsatisfied_prereqs.empty();
}

void 
delayed_packet::prerequisite_satisfied(shared_ptr<prerequisite> p, 
				       packet_db_writer & pw)
{
  I(unsatisfied_prereqs.find(p) != unsatisfied_prereqs.end());
  unsatisfied_prereqs.erase(p);
  satisfied_prereqs.insert(p);
  if (all_prerequisites_satisfied())
    {
      apply_delayed_packet(pw);
    }
}


// concrete delayed packets

class 
delayed_manifest_data_packet 
  : public delayed_packet
{
  manifest_id ident;
  manifest_data dat;
public:
  delayed_manifest_data_packet(manifest_id const & i, 
			       manifest_data const & md) 
    : ident(i), dat(md)
  {}
  virtual void apply_delayed_packet(packet_db_writer & pw);
  virtual ~delayed_manifest_data_packet();
};

class 
delayed_file_delta_packet 
  : public delayed_packet
{
  file_id old_id;
  file_id new_id;
  file_delta del;
public:
  delayed_file_delta_packet(file_id const & oi, 
			    file_id const & ni,
			    file_delta const & md) 
    : old_id(oi), new_id(ni), del(md)
  {}
  virtual void apply_delayed_packet(packet_db_writer & pw);
  virtual ~delayed_file_delta_packet();
};

class 
delayed_manifest_delta_packet 
  : public delayed_packet
{
  manifest_id old_id;
  manifest_id new_id;
  manifest_delta del;
public:
  delayed_manifest_delta_packet(manifest_id const & oi, 
				manifest_id const & ni,
				manifest_delta const & md) 
    : old_id(oi), new_id(ni), del(md)
  {}
  virtual void apply_delayed_packet(packet_db_writer & pw);
  virtual ~delayed_manifest_delta_packet();
};

class 
delayed_manifest_cert_packet 
  : public delayed_packet
{
  manifest<cert> c;
public:
  delayed_manifest_cert_packet(manifest<cert> const & c) 
    : c(c)
  {}
  virtual void apply_delayed_packet(packet_db_writer & pw);
  virtual ~delayed_manifest_cert_packet();
};

class 
delayed_file_cert_packet 
  : public delayed_packet
{
  file<cert> c;
public:
  delayed_file_cert_packet(file<cert> const & c) 
    : c(c)
  {}
  virtual void apply_delayed_packet(packet_db_writer & pw);
  virtual ~delayed_file_cert_packet();
};

void 
delayed_manifest_data_packet::apply_delayed_packet(packet_db_writer & pw)
{
  L(F("writing delayed manifest data packet for %s\n") % ident);
  pw.consume_manifest_data(ident, dat);
}

delayed_manifest_data_packet::~delayed_manifest_data_packet()
{
  if (!all_prerequisites_satisfied())
    W(F("discarding manifest data packet with unmet dependencies\n"));
}

void 
delayed_manifest_delta_packet::apply_delayed_packet(packet_db_writer & pw)
{
  L(F("writing delayed manifest delta packet for %s -> %s\n") 
    % old_id % new_id);
  pw.consume_manifest_delta(old_id, new_id, del);
}

delayed_manifest_delta_packet::~delayed_manifest_delta_packet()
{
  if (!all_prerequisites_satisfied())
    W(F("discarding manifest delta packet with unmet dependencies\n"));
}

void 
delayed_file_delta_packet::apply_delayed_packet(packet_db_writer & pw)
{
  L(F("writing delayed file delta packet for %s -> %s\n") 
    % old_id % new_id);
  pw.consume_file_delta(old_id, new_id, del);
}

delayed_file_delta_packet::~delayed_file_delta_packet()
{
  if (!all_prerequisites_satisfied())
    W(F("discarding file delta packet with unmet dependencies\n"));
}

void 
delayed_manifest_cert_packet::apply_delayed_packet(packet_db_writer & pw)
{
  L(F("writing delayed manifest cert on %s\n") % c.inner().ident);
  pw.consume_manifest_cert(c);
}

delayed_manifest_cert_packet::~delayed_manifest_cert_packet()
{
  if (!all_prerequisites_satisfied())
    W(F("discarding manifest cert packet with unmet dependencies\n"));
}

void 
delayed_file_cert_packet::apply_delayed_packet(packet_db_writer & pw)
{
  L(F("writing delayed file cert on %s\n") % c.inner().ident);
  pw.consume_file_cert(c);
}

delayed_file_cert_packet::~delayed_file_cert_packet()
{
  if (!all_prerequisites_satisfied())
    W(F("discarding file cert packet with unmet dependencies\n"));
}

struct packet_db_writer::impl
{
  app_state & app;
  bool take_keys;
  size_t count;
  manifest_edge_analyzer * analyzer;

  map<file_id, shared_ptr<prerequisite> > file_prereqs;
  map<manifest_id, shared_ptr<prerequisite> > manifest_prereqs;
  map<manifest_id, bool> existing_manifest_cache;
  map<file_id, bool> existing_file_cache;
  bool manifest_version_exists(manifest_id const & m);
  bool file_version_exists(file_id const & f);
  void get_file_prereq(file_id const & file, shared_ptr<prerequisite> & p);
  void get_manifest_prereq(manifest_id const & manifest, shared_ptr<prerequisite> & p);
  void accepted_file(file_id const & f, packet_db_writer & dbw);
  void accepted_manifest(manifest_id const & m, packet_db_writer & dbw);
  void accepted_manifest_cert_on(manifest_id const & m, packet_db_writer & dbw);
  impl(app_state & app, bool take_keys, manifest_edge_analyzer * ana) 
    : app(app), take_keys(take_keys), count(0), analyzer(ana)
  {}
};


packet_db_writer::packet_db_writer(app_state & app, bool take_keys, manifest_edge_analyzer * ana) 
  : pimpl(new impl(app, take_keys, ana))
{}

packet_db_writer::~packet_db_writer() 
{}

bool 
packet_db_writer::impl::manifest_version_exists(manifest_id const & m)
{
  map<manifest_id, bool>::const_iterator i = existing_manifest_cache.find(m);
  if (i == existing_manifest_cache.end())
    {
      bool exists = app.db.manifest_version_exists(m);
      existing_manifest_cache.insert(make_pair(m, exists));
      return exists;
    }
  else
    return i->second;
}

bool 
packet_db_writer::impl::file_version_exists(file_id const & f)
{
  map<file_id, bool>::const_iterator i = existing_file_cache.find(f);
  if (i == existing_file_cache.end())
    {
      bool exists = app.db.file_version_exists(f);
      existing_file_cache.insert(make_pair(f, exists));
      return exists;
    }
  else
    return i->second;
}

void 
packet_db_writer::impl::get_file_prereq(file_id const & file, 
					shared_ptr<prerequisite> & p)
{
  map<file_id, shared_ptr<prerequisite> >::const_iterator i;
  i = file_prereqs.find(file);
  if (i != file_prereqs.end())
    p = i->second;
  else
    {
      p = shared_ptr<prerequisite>(new prerequisite(file.inner(), prereq_file));
      file_prereqs.insert(make_pair(file, p));
    }
}

void
packet_db_writer::impl::get_manifest_prereq(manifest_id const & man, 
					    shared_ptr<prerequisite> & p)
{
  map<manifest_id, shared_ptr<prerequisite> >::const_iterator i;
  i = manifest_prereqs.find(man);
  if (i != manifest_prereqs.end())
    p = i->second;
  else
    {
      p = shared_ptr<prerequisite>(new prerequisite(man.inner(), prereq_manifest));
      manifest_prereqs.insert(make_pair(man, p));
    }
}

void 
packet_db_writer::impl::accepted_file(file_id const & f, packet_db_writer & dbw)
{
  existing_file_cache[f] = true;
  map<file_id, shared_ptr<prerequisite> >::iterator i = file_prereqs.find(f);  
  if (i == file_prereqs.end())
    return;
  i->second->satisfy(i->second, dbw);
  file_prereqs.erase(i);
}

void 
packet_db_writer::impl::accepted_manifest(manifest_id const & m, packet_db_writer & dbw)
{
  existing_manifest_cache[m] = true;
  map<manifest_id, shared_ptr<prerequisite> >::iterator i = manifest_prereqs.find(m);
  if (i == manifest_prereqs.end())
    return;
  i->second->satisfy(i->second, dbw);
  manifest_prereqs.erase(i);
}

void 
packet_db_writer::impl::accepted_manifest_cert_on(manifest_id const & m, packet_db_writer & dbw)
{
}

void 
packet_db_writer::consume_file_data(file_id const & ident, 
				    file_data const & dat)
{
  transaction_guard guard(pimpl->app.db);
  if (! pimpl->file_version_exists(ident))
    {
      pimpl->app.db.put_file(ident, dat);
      pimpl->accepted_file(ident, *this);
    }
  else
    L(F("skipping existing file version %s\n") % ident);
  ++(pimpl->count);
  guard.commit();
}

void 
packet_db_writer::consume_file_delta(file_id const & old_id, 
				     file_id const & new_id,
				     file_delta const & del)
{
  transaction_guard guard(pimpl->app.db);
  if (! pimpl->file_version_exists(new_id))
    {
      if (pimpl->file_version_exists(old_id))
	{
	  file_id confirm;
	  file_data old_dat;
	  base64< gzip<data> > new_dat;
	  pimpl->app.db.get_file_version(old_id, old_dat);
	  patch(old_dat.inner(), del.inner(), new_dat);
	  calculate_ident(file_data(new_dat), confirm);
	  if (confirm == new_id)
	    {
	      pimpl->app.db.put_file_version(old_id, new_id, del);
	      pimpl->accepted_file(new_id, *this);
	    }
	  else
	    {
	      W(F("reconstructed file from delta '%s' -> '%s' has wrong id '%s'\n") 
		% old_id % new_id % confirm);
	    }
	}
      else
	{
	  L(F("delaying file delta %s -> %s for preimage\n") % old_id % new_id);
	  shared_ptr<delayed_packet> dp;
	  dp = shared_ptr<delayed_packet>(new delayed_file_delta_packet(old_id, new_id, del));
	  shared_ptr<prerequisite> fp;
	  pimpl->get_file_prereq(old_id, fp); 
	  dp->add_prerequisite(fp);
	  fp->add_dependent(dp);
	}
    }
  else
    L(F("skipping delta to existing file version %s\n") % new_id);
  ++(pimpl->count);
  guard.commit();
}

void 
packet_db_writer::consume_file_cert(file<cert> const & t)
{
  transaction_guard guard(pimpl->app.db);
  if (! pimpl->app.db.file_cert_exists(t))
    {
      if (pimpl->file_version_exists(file_id(t.inner().ident)))
	{
	  pimpl->app.db.put_file_cert(t);
	}
      else
	{
	  L(F("delaying file cert on %s\n") % t.inner().ident);
	  shared_ptr<delayed_packet> dp;
	  dp = shared_ptr<delayed_packet>(new delayed_file_cert_packet(t));
	  shared_ptr<prerequisite> fp;
	  pimpl->get_file_prereq(file_id(t.inner().ident), fp); 
	  dp->add_prerequisite(fp);
	  fp->add_dependent(dp);
	}
    }
  else
    {
      string s;
      cert_signable_text(t.inner(), s);
      L(F("skipping existing file cert %s\n") % s);
    }
  ++(pimpl->count);
  guard.commit();
}

void 
packet_db_writer::consume_manifest_data(manifest_id const & ident, 
					manifest_data const & dat)
{
  transaction_guard guard(pimpl->app.db);
  if (! pimpl->manifest_version_exists(ident))
    {
      manifest_map mm;
      read_manifest_map(dat, mm);
      set<file_id> unsatisfied_files;
      for (manifest_map::const_iterator i = mm.begin(); i != mm.end(); ++i)
	{
	  path_id_pair pip(i);
	  if (! pimpl->file_version_exists(pip.ident()))
	    unsatisfied_files.insert(pip.ident());
	}
      if (unsatisfied_files.empty())
	{
	  pimpl->app.db.put_manifest(ident, dat);
	  pimpl->accepted_manifest(ident, *this);
	}
      else
	{
	  L(F("delaying manifest data packet %s for %d files\n") 
	    % ident % unsatisfied_files.size());
	  shared_ptr<delayed_packet> dp;
	  dp = shared_ptr<delayed_packet>(new delayed_manifest_data_packet(ident, dat));
	  for (set<file_id>::const_iterator i = unsatisfied_files.begin();
	       i != unsatisfied_files.end(); ++i)
	    {
	      shared_ptr<prerequisite> fp;
	      pimpl->get_file_prereq(*i, fp); 
	      dp->add_prerequisite(fp);
	      fp->add_dependent(dp);
	    }	  
	}
    }
  else
    L(F("skipping existing manifest version %s\n") % ident);  
  ++(pimpl->count);
  guard.commit();
}

void 
packet_db_writer::consume_manifest_delta(manifest_id const & old_id, 
					 manifest_id const & new_id,
					 manifest_delta const & del)
{
  transaction_guard guard(pimpl->app.db);
  if (! pimpl->manifest_version_exists(new_id))
    {
      if (pimpl->manifest_version_exists(old_id))
	{
	  manifest_data old_dat;
	  base64< gzip<data> > new_dat;
	  manifest_map mm;
	  pimpl->app.db.get_manifest_version(old_id, old_dat);
	  patch(old_dat.inner(), del.inner(), new_dat);
	  read_manifest_map(manifest_data(new_dat), mm);

	  // callback to a listener, yuck.
	  //
	  // FIXME: doing this under the guard ot "manifest_version_exists"
	  // is a bit pessimistic; it means that we will only analyze the
	  // manifest edge when its preimage has been *written* to the db,
	  // and the preimage is only written when all its files have
	  // arrived. you'll notice a very empty protocol pipeline
	  // currently due to this "lock-stepness".
	  // 
	  // what we *should* be doing here is analyznig the edge when the
	  // preimage arrives, period. that will require spending a bit of
	  // complexity differentiating the "no preimage" prerequisite
	  // condidition from the "missing some files" prerequisite
	  // condition. it's late so I'm kinda out of energy to do this
	  // now. it's at least "correct" at the moment.

	  if (pimpl->analyzer != NULL)
	    {
	      manifest_map mm_old;
	      read_manifest_map(manifest_data(old_dat), mm_old);
	      pimpl->analyzer->analyze_manifest_edge(mm_old, mm);
	    }
	  
	  set<file_id> unsatisfied_files;
	  for (manifest_map::const_iterator i = mm.begin(); i != mm.end(); ++i)
	    {
	      path_id_pair pip(i);
	      if (! pimpl->file_version_exists(pip.ident()))
		unsatisfied_files.insert(pip.ident());
	    }
	  if (unsatisfied_files.empty())
	    {
	      manifest_id confirm;
	      calculate_ident(mm, confirm);
	      if (confirm == new_id)
		{
		  pimpl->app.db.put_manifest_version(old_id, new_id, del);
		  pimpl->accepted_manifest(new_id, *this);
		}
	      else
		{
		  W(F("reconstructed manifest from delta '%s' -> '%s' has wrong id '%s'\n") 
		    % old_id % new_id % confirm);
		}
	    }
	  else
	    {
	      L(F("delaying manifest delta packet %s -> %s for %d files\n") 
		% old_id % new_id % unsatisfied_files.size());
	      shared_ptr<delayed_packet> dp;
	      dp = shared_ptr<delayed_packet>(new delayed_manifest_delta_packet(old_id, new_id, del));
	      for (set<file_id>::const_iterator i = unsatisfied_files.begin();
		   i != unsatisfied_files.end(); ++i)
		{
		  shared_ptr<prerequisite> fp;
		  pimpl->get_file_prereq(*i, fp); 
		  dp->add_prerequisite(fp);
		  fp->add_dependent(dp);
		}
	    }  
	}
      else
	{
	  L(F("delaying manifest delta %s -> %s for preimage\n") % old_id % new_id);
	  shared_ptr<delayed_packet> dp;
	  dp = shared_ptr<delayed_packet>(new delayed_manifest_delta_packet(old_id, new_id, del));
	  shared_ptr<prerequisite> fp;
	  pimpl->get_manifest_prereq(old_id, fp); 
	  dp->add_prerequisite(fp);
	  fp->add_dependent(dp);
	}
    }
  else
    L(F("skipping delta to existing manifest version %s\n") % new_id);  
  ++(pimpl->count);
  guard.commit();
}

void 
packet_db_writer::consume_manifest_cert(manifest<cert> const & t)
{
  transaction_guard guard(pimpl->app.db);
  if (! pimpl->app.db.manifest_cert_exists(t))
    {
      if (pimpl->manifest_version_exists(manifest_id(t.inner().ident)))
	{
	  pimpl->app.db.put_manifest_cert(t);
	  pimpl->accepted_manifest_cert_on(manifest_id(t.inner().ident), *this);
	}
      else
	{
	  L(F("delaying manifest cert on %s\n") % t.inner().ident);
	  shared_ptr<delayed_packet> dp;
	  dp = shared_ptr<delayed_packet>(new delayed_manifest_cert_packet(t));
	  shared_ptr<prerequisite> fp;
	  pimpl->get_manifest_prereq(manifest_id(t.inner().ident), fp); 
	  dp->add_prerequisite(fp);
	  fp->add_dependent(dp);
	}
    }
  else
    {
      string s;
      cert_signable_text(t.inner(), s);
      L(F("skipping existing manifest cert %s\n") % s);
    }
  ++(pimpl->count);
  guard.commit();
}

void 
packet_db_writer::consume_public_key(rsa_keypair_id const & ident,
				     base64< rsa_pub_key > const & k)
{
  transaction_guard guard(pimpl->app.db);
  if (! pimpl->take_keys) 
    {
      W(F("skipping prohibited public key %s\n") % ident);
      return;
    }
  if (! pimpl->app.db.public_key_exists(ident))
    pimpl->app.db.put_key(ident, k);
  else
    L(F("skipping existing public key %s\n") % ident);
  ++(pimpl->count);
  guard.commit();
}

void 
packet_db_writer::consume_private_key(rsa_keypair_id const & ident,
				      base64< arc4<rsa_priv_key> > const & k)
{
  transaction_guard guard(pimpl->app.db);
  if (! pimpl->take_keys) 
    {
      W(F("skipping prohibited private key %s\n") % ident);
      return;
    }
  if (! pimpl->app.db.private_key_exists(ident))
    pimpl->app.db.put_key(ident, k);
  else
    L(F("skipping existing private key %s\n") % ident);
  ++(pimpl->count);
  guard.commit();
}


// --- packet writer ---

packet_writer::packet_writer(ostream & o) : ost(o) {}

void 
packet_writer::consume_file_data(file_id const & ident, 
				 file_data const & dat)
{
  ost << "[fdata " << ident.inner()() << "]" << endl 
      << trim_ws(dat.inner()()) << endl
      << "[end]" << endl;
}

void 
packet_writer::consume_file_delta(file_id const & old_id, 
				  file_id const & new_id,
				  file_delta const & del)
{
  ost << "[fdelta " << old_id.inner()() << endl 
      << "        " << new_id.inner()() << "]" << endl 
      << trim_ws(del.inner()()) << endl
      << "[end]" << endl;
}

void 
packet_writer::consume_file_cert(file<cert> const & t)
{
  ost << "[fcert " << t.inner().ident() << endl
      << "       " << t.inner().name() << endl
      << "       " << t.inner().key() << endl
      << "       " << trim_ws(t.inner().value()) << "]" << endl
      << trim_ws(t.inner().sig()) << endl
      << "[end]" << endl;
}

void 
packet_writer::consume_manifest_data(manifest_id const & ident, 
				     manifest_data const & dat)
{
  ost << "[mdata " << ident.inner()() << "]" << endl 
      << trim_ws(dat.inner()()) << endl
      << "[end]" << endl;
}

void 
packet_writer::consume_manifest_delta(manifest_id const & old_id, 
				      manifest_id const & new_id,
				      manifest_delta const & del)
{
  ost << "[mdelta " << old_id.inner()() << endl 
      << "        " << new_id.inner()() << "]" << endl 
      << trim_ws(del.inner()()) << endl
      << "[end]" << endl;
}

void 
packet_writer::consume_manifest_cert(manifest<cert> const & t)
{
  ost << "[mcert " << t.inner().ident() << endl
      << "       " << t.inner().name() << endl
      << "       " << t.inner().key() << endl
      << "       " << trim_ws(t.inner().value()) << "]" << endl
      << trim_ws(t.inner().sig()) << endl
      << "[end]" << endl;
}

void 
packet_writer::consume_public_key(rsa_keypair_id const & ident,
				  base64< rsa_pub_key > const & k)
{
  ost << "[pubkey " << ident() << "]" << endl
      << trim_ws(k()) << endl
      << "[end]" << endl;
}

void 
packet_writer::consume_private_key(rsa_keypair_id const & ident,
				   base64< arc4<rsa_priv_key> > const & k)
{
  ost << "[privkey " << ident() << "]" << endl
      << trim_ws(k()) << endl
      << "[end]" << endl;
}


// -- remainder just deals with the regexes for reading packets off streams

struct 
feed_packet_consumer
{
  size_t & count;
  packet_consumer & cons;
  feed_packet_consumer(size_t & count, packet_consumer & c) : count(count), cons(c)
  {}
  bool operator()(match_results<std::string::const_iterator> const & res) const
  {
    if (res.size() != 17)
      throw oops("matched impossible packet with " 
		 + lexical_cast<string>(res.size()) + " matching parts: " +
		 string(res[0].first, res[0].second));
    
    if (res[1].matched)
      {
	L(F("read data packet\n"));
	I(res[2].matched);
	I(res[3].matched);
	string head(res[1].first, res[1].second);
	string ident(res[2].first, res[2].second);
	string body(trim_ws(string(res[3].first, res[3].second)));
	if (head == "mdata")
	  cons.consume_manifest_data(manifest_id(hexenc<id>(ident)), 
				     manifest_data(body));
	else if (head == "fdata")
	  cons.consume_file_data(file_id(hexenc<id>(ident)), 
				 file_data(body));
	else
	  throw oops("matched impossible data packet with head '" + head + "'");
      }
    else if (res[4].matched)
      {
	L(F("read delta packet\n"));
	I(res[5].matched);
	I(res[6].matched);
	I(res[7].matched);
	string head(res[4].first, res[4].second);
	string old_id(res[5].first, res[5].second);
	string new_id(res[6].first, res[6].second);
	string body(trim_ws(string(res[7].first, res[7].second)));
	if (head == "mdelta")
	  cons.consume_manifest_delta(manifest_id(hexenc<id>(old_id)), 
				      manifest_id(hexenc<id>(new_id)),
				      manifest_delta(body));
	else if (head == "fdelta")
	  cons.consume_file_delta(file_id(hexenc<id>(old_id)), 
				  file_id(hexenc<id>(new_id)), 
				  file_delta(body));
	else
	  throw oops("matched impossible delta packet with head '" + head + "'");
      }
    else if (res[8].matched)
      {
	L(F("read cert packet\n"));
	I(res[9].matched);
	I(res[10].matched);
	I(res[11].matched);
	I(res[12].matched);
	I(res[13].matched);
	string head(res[8].first, res[8].second);
	string ident(res[9].first, res[9].second);
	string certname(res[10].first, res[10].second);
	string key(res[11].first, res[11].second);
	string val(res[12].first, res[12].second);
	string body(res[13].first, res[13].second);

	// canonicalize the base64 encodings to permit searches
	cert t = cert(hexenc<id>(ident),
		      cert_name(certname),
		      base64<cert_value>(canonical_base64(val)),
		      rsa_keypair_id(key),
		      base64<rsa_sha1_signature>(canonical_base64(body)));
	if (head == "mcert")
	  cons.consume_manifest_cert(manifest<cert>(t));
	else if (head == "fcert")
	  cons.consume_file_cert(file<cert>(t));
	else
	  throw oops("matched impossible cert packet with head '" + head + "'");
      } 
    else if (res[14].matched)
      {
	L(F("read key data packet\n"));
	I(res[15].matched);
	I(res[16].matched);
	string head(res[14].first, res[14].second);
	string ident(res[15].first, res[15].second);
	string body(trim_ws(string(res[16].first, res[16].second)));
	if (head == "pubkey")
	  cons.consume_public_key(rsa_keypair_id(ident),
				  base64<rsa_pub_key>(body));
	else if (head == "privkey")
	  cons.consume_private_key(rsa_keypair_id(ident),
				   base64< arc4<rsa_priv_key> >(body));
	else
	  throw oops("matched impossible key data packet with head '" + head + "'");
      }
    else
      return true;
    ++count;
    return true;
  }
};

static size_t 
extract_packets(string const & s, packet_consumer & cons)
{  
  string const ident("([[:xdigit:]]{40})");
  string const sp("[[:space:]]+");
  string const bra("\\[");
  string const ket("\\]");
  string const certhead("(mcert|fcert)");
  string const datahead("(mdata|fdata)");
  string const deltahead("(mdelta|fdelta)");
  string const keyhead("(pubkey|privkey)");
  string const key("([-a-zA-Z0-9\\.@]+)");
  string const certname("([-a-zA-Z0-9]+)");
  string const base64("([a-zA-Z0-9+/=[:space:]]+)");
  string const end("\\[end\\]");
  string const data = bra + datahead + sp + ident + ket + base64 + end; 
  string const delta = bra + deltahead + sp + ident + sp + ident + ket + base64 + end;
  string const cert = bra 
    + certhead + sp + ident + sp + certname + sp + key + sp + base64 
    + ket 
    + base64 + end; 
  string const keydata = bra + keyhead + sp + key + ket + base64 + end;
  string const biggie = (data + "|" + delta + "|" + cert + "|" + keydata);
  regex expr(biggie);
  size_t count = 0;
  regex_grep(feed_packet_consumer(count, cons), s, expr, match_default);
  return count;
}


size_t 
read_packets(istream & in, packet_consumer & cons)
{
  string accum, tmp;
  size_t count = 0;
  size_t const bufsz = 0xff;
  char buf[bufsz];
  string const end("[end]");
  while(in)
    {
      in.read(buf, bufsz);
      accum.append(buf, in.gcount());      
      string::size_type endpos = string::npos;
      endpos = accum.rfind(end);
      if (endpos != string::npos)
	{
	  endpos += end.size();
	  string tmp = accum.substr(0, endpos);
	  count += extract_packets(tmp, cons);
	  if (endpos < accum.size() - 1)
	    accum = accum.substr(endpos+1);
	  else
	    accum.clear();
	}
    }
  return count;
}


#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"
#include "transforms.hh"
#include "manifest.hh"

static void 
packet_roundabout_test()
{
  string tmp;

  {
    ostringstream oss;
    packet_writer pw(oss);

    // an fdata packet
    base64< gzip<data> > gzdata;
    pack(data("this is some file data"), gzdata);
    file_data fdata(gzdata);
    file_id fid;
    calculate_ident(fdata, fid);
    pw.consume_file_data(fid, fdata);

    // an fdelta packet    
    base64< gzip<data> > gzdata2;
    pack(data("this is some file data which is not the same as the first one"), gzdata2);
    file_data fdata2(gzdata2);
    file_id fid2;
    calculate_ident(fdata2, fid);
    base64< gzip<delta> > del;
    diff(fdata.inner(), fdata2.inner(), del);
    pw.consume_file_delta(fid, fid2, file_delta(del));

    // a file cert packet
    base64<cert_value> val;
    encode_base64(cert_value("peaches"), val);
    base64<rsa_sha1_signature> sig;
    encode_base64(rsa_sha1_signature("blah blah there is no way this is a valid signature"), sig);    
    cert c(fid.inner(), cert_name("smell"), val, 
	   rsa_keypair_id("fun@moonman.com"), sig);
    pw.consume_file_cert(file<cert>(c));
    
    // a manifest cert packet
    pw.consume_manifest_cert(manifest<cert>(c));

    // a manifest data packet
    manifest_map mm;
    manifest_data mdata;
    manifest_id mid;
    mm.insert(make_pair(file_path("foo/bar.txt"),
			file_id(hexenc<id>("cfb81b30ab3133a31b52eb50bd1c86df67eddec4"))));
    write_manifest_map(mm, mdata);
    calculate_ident(mdata, mid);
    pw.consume_manifest_data(mid, mdata);

    // a manifest delta packet
    manifest_map mm2;
    manifest_data mdata2;
    manifest_id mid2;
    manifest_delta mdelta;
    mm2.insert(make_pair(file_path("foo/bar.txt"),
			 file_id(hexenc<id>("5b20eb5e5bdd9cd674337fc95498f468d80ef7bc"))));
    mm2.insert(make_pair(file_path("bunk.txt"),
			 file_id(hexenc<id>("54f373ed07b4c5a88eaa93370e1bbac02dc432a8"))));
    write_manifest_map(mm2, mdata2);
    calculate_ident(mdata2, mid2);
    base64< gzip<delta> > del2;
    diff(mdata.inner(), mdata2.inner(), del2);
    pw.consume_manifest_delta(mid, mid2, manifest_delta(del));
    
    // a public key packet
    base64<rsa_pub_key> puk;
    encode_base64(rsa_pub_key("this is not a real rsa key"), puk);
    pw.consume_public_key(rsa_keypair_id("test@lala.com"), puk);

    // a private key packet
    base64< arc4<rsa_priv_key> > pik;
    encode_base64(arc4<rsa_priv_key>
		  (rsa_priv_key("this is not a real rsa key either!")), pik);
    
    pw.consume_private_key(rsa_keypair_id("test@lala.com"), pik);
    
  }
  
  for (int i = 0; i < 10; ++i)
    {
      // now spin around sending and receiving this a few times
      ostringstream oss;
      packet_writer pw(oss);      
      istringstream iss(tmp);
      read_packets(iss, pw);
      BOOST_CHECK(oss.str() == tmp);
      tmp = oss.str();
    }
}

void 
add_packet_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&packet_roundabout_test));
}

#endif // BUILD_UNIT_TESTS
