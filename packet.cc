// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <iostream>
#include <string>

#include <boost/optional.hpp>
#include <boost/regex.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/shared_ptr.hpp>

#include "app_state.hh"
#include "packet.hh"
#include "sanity.hh"
#include "transforms.hh"

using namespace boost;
using namespace std;

// --- packet db writer --
//
// the packet_db_writer::impl class (see below) manages writes to the
// database. it also ensures that those writes follow the semantic
// dependencies implied by the objects being written.
//
// an incoming manifest delta has three states:
//
// when it is first received, it is (probably) "non-constructable".
// this means that we do not have a way of building its preimage from either
// the database or from the memory cache of deltas we keep in this class
// 
// a non-constructable manifest delta is given a prerequisite of
// constructibility on its preimage.
//
// when the preimage becomes constructable, the manifest delta (probably)
// changes to "non-writable" state. this means that we have a way to build
// the manifest, we haven't received all the files which depend on it yet,
// so we won't write it to the database.
//
// when a manifest becomes constructable (but not necessarily writable) we
// call an analyzer back, if we have one, with the pre- and post-states of
// the delta.  this happens in order to give the netsync layer a chance to
// request all the file deltas which accompany the manifest delta.
// 
// a non-writable manifest delta is given prerequisites on all its
// non-existing underlying files, and delayed again.
//
// when all the files arrive, a non-writable manifest is written to the
// database.
//
// files are delayed to depend on their preimage, like non-constructable
// manifests. however, once they are constructable they are immediately
// written to the database.
//
// manifest certs are delayed to depend on their manifest being writable.
// 
// file certs are delayed to depend on their files being writable.
//
/////////////////////////////////////////////////////////////////
//
// how it's done:
//
// each manifest or file has a companion class called a "prerequisite". a
// prerequisite has a set of delayed packets which depend on it. these
// delayed packets are also called dependents. a prerequisite can either be
// "unsatisfied" or "satisfied". when it is first constructed, it is
// unsatisfied. when it is satisfied, it calls all its dependents to inform
// them that it has become satisfied.
//
// when all the prerequisites of a given dependent is satisfied, the
// dependent writes itself back to the db writer. the dependent is then
// dead, and the prerequisite will forget about it.
//
// dependents' lifetimes are managed by prerequisites. when all
// prerequisites forget about their dependents, the dependent is destroyed
// (it is reference counted with a shared pointer). similarly, the
// packet_db_writer::impl holds references to prerequisites, and when
// a prerequisite no longer has any dependents, it is dropped from the
// packet_db_writer::impl, destroying it.
// 

typedef enum 
  {
    prereq_file, 
    prereq_manifest_constructable,
    prereq_manifest_writable 
  } 
prereq_type;

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
delayed_nonconstructable_manifest_delta_packet
  : public delayed_packet
{
  manifest_id old_id;
  manifest_id new_id;
  manifest_delta del;
public:
  delayed_nonconstructable_manifest_delta_packet(manifest_id const & oi, 
						 manifest_id const & ni,
						 manifest_delta const & md) 
    : old_id(oi), new_id(ni), del(md)
  {}
  virtual void apply_delayed_packet(packet_db_writer & pw);
  virtual ~delayed_nonconstructable_manifest_delta_packet();
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
  pw.consume_constructable_manifest_delta(old_id, new_id, del);
}

delayed_manifest_delta_packet::~delayed_manifest_delta_packet()
{
  if (!all_prerequisites_satisfied())
    W(F("discarding manifest delta packet with unmet dependencies\n"));
}

void 
delayed_nonconstructable_manifest_delta_packet::apply_delayed_packet(packet_db_writer & pw)
{
  L(F("writing delayed non-constructable manifest delta packet for %s -> %s\n") 
    % old_id % new_id);
  pw.consume_manifest_delta(old_id, new_id, del);
}

delayed_nonconstructable_manifest_delta_packet::~delayed_nonconstructable_manifest_delta_packet()
{
  if (!all_prerequisites_satisfied())
    W(F("discarding non-constructable manifest delta packet with unmet dependencies\n"));
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
  map<manifest_id, shared_ptr<prerequisite> > manifest_construction_prereqs;
  map<manifest_id, shared_ptr<prerequisite> > manifest_write_prereqs;
  set<manifest_id> analyzed_manifests;

  map<manifest_id, shared_ptr< map<manifest_id, manifest_delta> > > manifest_delta_cache;
  map<manifest_id, bool> existing_manifest_cache;
  map<manifest_id, bool> manifest_constructable_cache;
  map<file_id, bool> existing_file_cache;

  // this is essential for making cascading reconstruction happen fast
  manifest_id cached_id;
  manifest_data cached_mdata;

  //   ticker cert;
  //   ticker manc;
  //   ticker manw;
  //   ticker filec;

  bool manifest_version_constructable(manifest_id const & m);
  void construct_manifest_version(manifest_id const & m, manifest_data & mdat);

  bool manifest_version_exists_in_db(manifest_id const & m);
  bool file_version_exists_in_db(file_id const & f);

  void get_file_prereq(file_id const & file, shared_ptr<prerequisite> & p);
  void get_manifest_constructable_prereq(manifest_id const & manifest, shared_ptr<prerequisite> & p);
  void get_manifest_writable_prereq(manifest_id const & manifest, shared_ptr<prerequisite> & p);

  void accepted_file(file_id const & f, packet_db_writer & dbw);
  void accepted_manifest_constructable(manifest_id const & m, 
				       manifest_id const & pred,
				       manifest_delta const & del,
				       packet_db_writer & dbw);
  void accepted_manifest_writable(manifest_id const & m, packet_db_writer & dbw);
  void accepted_manifest_cert_on(manifest_id const & m, packet_db_writer & dbw);

  impl(app_state & app, bool take_keys, manifest_edge_analyzer * ana) 
    : app(app), take_keys(take_keys), count(0), analyzer(ana)
    // cert("cert", 1), manc("manc", 1), manw("manw", 1), filec("filec", 1)
  {}
};


packet_db_writer::packet_db_writer(app_state & app, bool take_keys, manifest_edge_analyzer * ana) 
  : pimpl(new impl(app, take_keys, ana))
{}

packet_db_writer::~packet_db_writer() 
{}

static bool
recursive_constructable(manifest_id const & m,
			packet_db_writer::impl & impl,
			set<manifest_id> & protector)
{
  if (impl.manifest_version_exists_in_db(m))
    return true;

  map<manifest_id, bool>::const_iterator i = impl.manifest_constructable_cache.find(m);
  if (i != impl.manifest_constructable_cache.end())
    return i->second;
  else
    {
      map<manifest_id, shared_ptr< map<manifest_id, manifest_delta> > >::const_iterator i;
      i = impl.manifest_delta_cache.find(m);
      if (i != impl.manifest_delta_cache.end())
	{
	  shared_ptr< map<manifest_id, manifest_delta> > preds = i->second;	  
	  for (map<manifest_id, manifest_delta>::const_iterator j = preds->begin(); 
	       j != preds->end(); ++j)
	    {
	      if (protector.find(j->first) != protector.end())
		continue;
	      protector.insert(j->first);
	      if (recursive_constructable(j->first, impl, protector))
		{
		  impl.manifest_constructable_cache.insert(make_pair(m, true));
		  return true;
		}
	      protector.erase(j->first);
	    }
	}
    }
  return false;
}

bool
packet_db_writer::impl::manifest_version_constructable(manifest_id const & m)
{
  set<manifest_id> protector;
  return recursive_constructable(m, *this, protector);
}

static void
recursive_construct(manifest_id const & m,
		    manifest_data & mdat,
		    packet_db_writer::impl & impl,
		    set<manifest_id> & protector)
{
  I(impl.manifest_version_constructable(m));
  if (impl.cached_id == m)
    {
      mdat = impl.cached_mdata;
    }
  else if (impl.manifest_version_exists_in_db(m))
    {
      impl.app.db.get_manifest_version(m, mdat);
      impl.cached_id = m;
      impl.cached_mdata = mdat;
    }
  else
    {
      map<manifest_id, shared_ptr< map<manifest_id, manifest_delta> > >::const_iterator i;
      i = impl.manifest_delta_cache.find(m);
      I(i != impl.manifest_delta_cache.end());      
      shared_ptr< map<manifest_id, manifest_delta> > preds = i->second;
      for (map<manifest_id, manifest_delta>::const_iterator j = preds->begin(); 
	   j != preds->end(); ++j)
	{
	  if (protector.find(j->first) != protector.end())
	    continue;
	  if (impl.manifest_version_constructable(j->first))
	    {
	      manifest_data mtmp;
	      protector.insert(j->first);
	      recursive_construct(j->first, mtmp, impl, protector);
	      protector.erase(j->first);
	      base64< gzip<data> > new_data;
	      patch(mtmp.inner(), j->second.inner(), new_data);
	      mdat = manifest_data(new_data);
	      impl.cached_id = m;
	      impl.cached_mdata = mdat;
	      return;
	    }
	}
      // you should not be able to get here
      I(false);
    }
}

void 
packet_db_writer::impl::construct_manifest_version(manifest_id const & m, 
						   manifest_data & mdat)
{
  set<manifest_id> protector;
  recursive_construct(m, mdat, *this, protector);
}

bool 
packet_db_writer::impl::manifest_version_exists_in_db(manifest_id const & m)
{
  map<manifest_id, bool>::const_iterator i = existing_manifest_cache.find(m);
  if (i != existing_manifest_cache.end())
    return i->second;
  else
    {
      bool exists = app.db.manifest_version_exists(m);
      existing_manifest_cache.insert(make_pair(m, exists));
      return exists;
    }
}

bool 
packet_db_writer::impl::file_version_exists_in_db(file_id const & f)
{
  map<file_id, bool>::const_iterator i = existing_file_cache.find(f);
  if (i != existing_file_cache.end())
    return i->second;
  else
    {
      bool exists = app.db.file_version_exists(f);
      existing_file_cache.insert(make_pair(f, exists));
      return exists;
    }
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
packet_db_writer::impl::get_manifest_constructable_prereq(manifest_id const & man, 
							  shared_ptr<prerequisite> & p)
{
  map<manifest_id, shared_ptr<prerequisite> >::const_iterator i;
  i = manifest_construction_prereqs.find(man);
  if (i != manifest_construction_prereqs.end())
    p = i->second;
  else
    {
      p = shared_ptr<prerequisite>(new prerequisite(man.inner(), prereq_manifest_constructable));
      manifest_construction_prereqs.insert(make_pair(man, p));
    }
}

void
packet_db_writer::impl::get_manifest_writable_prereq(manifest_id const & man, 
						     shared_ptr<prerequisite> & p)
{
  map<manifest_id, shared_ptr<prerequisite> >::const_iterator i;
  i = manifest_write_prereqs.find(man);
  if (i != manifest_write_prereqs.end())
    p = i->second;
  else
    {
      p = shared_ptr<prerequisite>(new prerequisite(man.inner(), prereq_manifest_writable));
      manifest_write_prereqs.insert(make_pair(man, p));
    }
}

void 
packet_db_writer::impl::accepted_file(file_id const & f, packet_db_writer & dbw)
{
  // ++filec;
  existing_file_cache[f] = true;
  map<file_id, shared_ptr<prerequisite> >::iterator i = file_prereqs.find(f);  
  if (i != file_prereqs.end())
    {
      shared_ptr<prerequisite> prereq = i->second;
      file_prereqs.erase(i);
      prereq->satisfy(prereq, dbw);
    }
}

void 
packet_db_writer::impl::accepted_manifest_writable(manifest_id const & m, packet_db_writer & dbw)
{
  // ++manw;
  existing_manifest_cache[m] = true;
  manifest_delta_cache.erase(m);
  // fire anything waiting for writability
  map<manifest_id, shared_ptr<prerequisite> >::iterator i = manifest_write_prereqs.find(m);
  if (i != manifest_write_prereqs.end())
    {
      L(F("noting writability of %s in accept_manifest_writable\n") % m);
      shared_ptr<prerequisite> prereq = i->second;
      manifest_write_prereqs.erase(i);
      prereq->satisfy(prereq, dbw);
    }

  // fire anything writing for constructability
  map<manifest_id, shared_ptr<prerequisite> >::iterator j = manifest_construction_prereqs.find(m);
  if (j != manifest_construction_prereqs.end())
    {
      L(F("noting constructability of %s in accept_manifest_writable\n") % m);
      shared_ptr<prerequisite> prereq = j->second;
      manifest_construction_prereqs.erase(j);   
      prereq->satisfy(prereq, dbw);
    }

}

void 
packet_db_writer::impl::accepted_manifest_constructable(manifest_id const & m, 
							manifest_id const & pred,
							manifest_delta const & del,
							packet_db_writer & dbw)
{
  // ++manc;
  manifest_constructable_cache[m] = true;
  // first stash the delta for future use
  map<manifest_id, shared_ptr< map<manifest_id, manifest_delta> > >::const_iterator i;
  i = manifest_delta_cache.find(m);
  shared_ptr< map<manifest_id, manifest_delta> > preds;
  if (i == manifest_delta_cache.end())
    {
      preds = shared_ptr< map<manifest_id, manifest_delta> >(new map<manifest_id, manifest_delta>());
      manifest_delta_cache.insert(make_pair(m, preds));
    }
  else
    preds = i->second;

  if (preds->find(pred) == preds->end())
    preds->insert(make_pair(pred,  del));

  // fire anything writing for constructability
  map<manifest_id, shared_ptr<prerequisite> >::iterator j = manifest_construction_prereqs.find(m);
  if (j != manifest_construction_prereqs.end())
    {
      L(F("noting constructability of %s in accept_manifest_constructable\n") % m);
      shared_ptr<prerequisite> prereq = j->second;
      manifest_construction_prereqs.erase(j);   
      prereq->satisfy(prereq, dbw);
    }
}


void 
packet_db_writer::impl::accepted_manifest_cert_on(manifest_id const & m, packet_db_writer & dbw)
{
  // ++cert;
}

void 
packet_db_writer::consume_file_data(file_id const & ident, 
				    file_data const & dat)
{
  transaction_guard guard(pimpl->app.db);
  if (! pimpl->file_version_exists_in_db(ident))
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
  if (! pimpl->file_version_exists_in_db(new_id))
    {
      if (pimpl->file_version_exists_in_db(old_id))
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
      if (pimpl->file_version_exists_in_db(file_id(t.inner().ident)))
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
  if (! pimpl->manifest_version_exists_in_db(ident))
    {
      manifest_map mm;
      read_manifest_map(dat, mm);
      set<file_id> unsatisfied_files;
      for (manifest_map::const_iterator i = mm.begin(); i != mm.end(); ++i)
	{
	  path_id_pair pip(i);
	  if (! pimpl->file_version_exists_in_db(pip.ident()))
	    unsatisfied_files.insert(pip.ident());
	}
      if (unsatisfied_files.empty())
	{
	  pimpl->app.db.put_manifest(ident, dat);
	  pimpl->accepted_manifest_writable(ident, *this);
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
  L(F("consume_manifest_delta %s -> %s\n") % old_id % new_id);
  if (pimpl->manifest_version_constructable(old_id))
    {
      manifest_data old_dat;
      base64< gzip<data> > tdat;
      L(F("preimage %s is constructable\n") % old_id);
      consume_constructable_manifest_delta(old_id, new_id, del);
      pimpl->construct_manifest_version(old_id, old_dat);      
      patch(old_dat.inner(), del.inner(), tdat);
      pimpl->cached_id = new_id;
      pimpl->cached_mdata = manifest_data(tdat);
      pimpl->accepted_manifest_constructable(new_id, old_id, del, *this);  
    }
  else
    {
      L(F("delaying manifest delta %s -> %s for preimage\n") % old_id % new_id);
      shared_ptr<delayed_packet> dp;
      dp = shared_ptr<delayed_packet>(new delayed_nonconstructable_manifest_delta_packet(old_id, new_id, del));
      shared_ptr<prerequisite> fp;
      pimpl->get_manifest_constructable_prereq(old_id, fp); 
      dp->add_prerequisite(fp);
      fp->add_dependent(dp);
    }
}

void 
packet_db_writer::consume_constructable_manifest_delta(manifest_id const & old_id, 
						       manifest_id const & new_id,
						       manifest_delta const & del)
{
  manifest_map mm;

  L(F("consume_constructable_manifest_delta %s -> %s\n") % old_id % new_id);
  if (pimpl->manifest_version_exists_in_db(new_id))
    {
      L(F("skipping delta to existing manifest version %s\n") % new_id);  
      return;
    }

  I(pimpl->manifest_version_constructable(old_id));

  manifest_data old_dat;
  pimpl->construct_manifest_version(old_id, old_dat);      
  base64< gzip<data> > tdat;
  patch(old_dat.inner(), del.inner(), tdat);
  manifest_data new_dat(tdat);
  read_manifest_map(new_dat, mm);

  {
    // check constructed new map
    manifest_id confirm;
    calculate_ident(mm, confirm);
    if (! (confirm == new_id))
      {
	W(F("reconstructed manifest from delta '%s' -> '%s' has wrong id '%s'\n") 
	  % old_id % new_id % confirm);
	return;
      }
  }

  // maybe analyze the edge
  if (pimpl->analyzer != NULL
      && (pimpl->analyzed_manifests.find(new_id) 
	  == pimpl->analyzed_manifests.end()))
    {
      L(F("analyzing manifest edge %s -> %s\n") % old_id % new_id);
      manifest_map mm_old;
      read_manifest_map(manifest_data(old_dat), mm_old);
      pimpl->analyzer->analyze_manifest_edge(mm_old, mm);
      pimpl->analyzed_manifests.insert(new_id);
    }

  transaction_guard guard(pimpl->app.db);

  // now check to see if we can write it, or if we need to delay for files
  set<file_id> unsatisfied_files;
  for (manifest_map::const_iterator i = mm.begin(); i != mm.end(); ++i)
    {
      path_id_pair pip(i);
      if (! pimpl->file_version_exists_in_db(pip.ident()))
	unsatisfied_files.insert(pip.ident());
    }
  if (unsatisfied_files.empty() &&
      pimpl->manifest_version_exists_in_db(old_id))
    {
      L(F("manifest %s is satisfied\n") % new_id);
      pimpl->app.db.put_manifest_version(old_id, new_id, del);
      pimpl->accepted_manifest_writable(new_id, *this);
    }
  else
    {
      L(F("delaying manifest delta packet %s -> %s for %d files\n") 
	% old_id % new_id % unsatisfied_files.size());
      shared_ptr<delayed_packet> dp;
      shared_ptr<prerequisite> fp;
      dp = shared_ptr<delayed_packet>(new delayed_manifest_delta_packet(old_id, new_id, del));
      for (set<file_id>::const_iterator i = unsatisfied_files.begin();
	   i != unsatisfied_files.end(); ++i)
	{
	  pimpl->get_file_prereq(*i, fp); 
	  dp->add_prerequisite(fp);
	  fp->add_dependent(dp);
	}
      if (!pimpl->manifest_version_exists_in_db(old_id))
	{
	  // P(F("adding write of preimage to prerequisites\n"));
	  pimpl->get_manifest_writable_prereq(old_id, fp); 
	  dp->add_prerequisite(fp);
	  fp->add_dependent(dp);
	}
    }
  ++(pimpl->count);
  guard.commit();
}

void 
packet_db_writer::consume_manifest_cert(manifest<cert> const & t)
{
  transaction_guard guard(pimpl->app.db);
  if (! pimpl->app.db.manifest_cert_exists(t))
    {
      if (pimpl->manifest_version_exists_in_db(manifest_id(t.inner().ident)))
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
	  pimpl->get_manifest_writable_prereq(manifest_id(t.inner().ident), fp); 
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
    {
      base64<rsa_pub_key> tmp;
      pimpl->app.db.get_key(ident, tmp);
      if (!(tmp() == k()))
	W(F("key '%s' is not equal to key '%s' in database\n") % ident % ident);
      L(F("skipping existing public key %s\n") % ident);
    }
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
