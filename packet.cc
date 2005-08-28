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
#include "change_set.hh"
#include "packet.hh"
#include "revision.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "keys.hh"

using namespace std;
using boost::shared_ptr;
using boost::lexical_cast;
using boost::match_default;
using boost::match_results;
using boost::regex;

// --- packet db writer --
//
// FIXME: this comment is out of date, and untrustworthy.
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
/////////////////////////////////////////////////////////////////
// 
// this same machinery is also re-used for the "valved" packet writer, as a
// convenient way to queue up commands in memory while the valve is closed.
// in this usage, we simply never add any prerequisites to any packet, and
// just call apply_delayed_packet when the valve opens.

typedef enum 
  {
    prereq_revision,
    prereq_manifest,
    prereq_file
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
  // we need to be able to avoid circular dependencies between prerequisite and
  // delayed_packet shared_ptrs.
  void cleanup() { delayed.clear(); }
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
delayed_revision_data_packet 
  : public delayed_packet
{
  revision_id ident;
  revision_data dat;
public:
  delayed_revision_data_packet(revision_id const & i, 
                               revision_data const & md) 
    : ident(i), dat(md)
  {}
  virtual void apply_delayed_packet(packet_db_writer & pw);
  virtual ~delayed_revision_data_packet();
};

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
delayed_file_data_packet 
  : public delayed_packet
{
  file_id ident;
  file_data dat;
public:
  delayed_file_data_packet(file_id const & i, 
                           file_data const & fd) 
    : ident(i), dat(fd)
  {}
  virtual void apply_delayed_packet(packet_db_writer & pw);
  virtual ~delayed_file_data_packet();
};

class 
delayed_file_delta_packet 
  : public delayed_packet
{
  file_id old_id;
  file_id new_id;
  file_delta del;
  bool forward_delta;
public:
  delayed_file_delta_packet(file_id const & oi, 
                            file_id const & ni,
                            file_delta const & md,
                            bool fwd) 
    : old_id(oi), new_id(ni), del(md), forward_delta(fwd)
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
  bool forward_delta;
public:
  delayed_manifest_delta_packet(manifest_id const & oi, 
                                manifest_id const & ni,
                                manifest_delta const & md,
                                bool fwd) 
    : old_id(oi), new_id(ni), del(md), forward_delta(fwd)
  {}
  virtual void apply_delayed_packet(packet_db_writer & pw);
  virtual ~delayed_manifest_delta_packet();
};

class 
delayed_revision_cert_packet 
  : public delayed_packet
{
  revision<cert> c;
public:
  delayed_revision_cert_packet(revision<cert> const & c) 
    : c(c)
  {}
  virtual void apply_delayed_packet(packet_db_writer & pw);
  virtual ~delayed_revision_cert_packet();
};

class 
delayed_public_key_packet 
  : public delayed_packet
{
  rsa_keypair_id id;
  base64<rsa_pub_key> key;
public:
  delayed_public_key_packet(rsa_keypair_id const & id,
                            base64<rsa_pub_key> key)
    : id(id), key(key)
  {}
  virtual void apply_delayed_packet(packet_db_writer & pw);
  virtual ~delayed_public_key_packet();
};

class 
delayed_private_key_packet 
  : public delayed_packet
{
  rsa_keypair_id id;
  base64< arc4<rsa_priv_key> > key;
public:
  delayed_private_key_packet(rsa_keypair_id const & id,
                             base64< arc4<rsa_priv_key> > key)
    : id(id), key(key)
  {}
  virtual void apply_delayed_packet(packet_db_writer & pw);
  virtual ~delayed_private_key_packet();
};

void 
delayed_revision_data_packet::apply_delayed_packet(packet_db_writer & pw)
{
  L(F("writing delayed revision data packet for %s\n") % ident);
  pw.consume_revision_data(ident, dat);
}

delayed_revision_data_packet::~delayed_revision_data_packet()
{
  if (!all_prerequisites_satisfied())
    W(F("discarding revision data packet %s with unmet dependencies\n") % ident);
}

void 
delayed_manifest_data_packet::apply_delayed_packet(packet_db_writer & pw)
{
  L(F("writing delayed manifest data packet for %s\n") % ident);
  pw.consume_manifest_data(ident, dat);
}

delayed_manifest_data_packet::~delayed_manifest_data_packet()
{
  if (!all_prerequisites_satisfied())
    W(F("discarding manifest data packet %s with unmet dependencies\n") % ident);
}

void 
delayed_file_data_packet::apply_delayed_packet(packet_db_writer & pw)
{
  L(F("writing delayed file data packet for %s\n") % ident);
  pw.consume_file_data(ident, dat);
}

delayed_file_data_packet::~delayed_file_data_packet()
{
  // files have no prerequisites
  I(all_prerequisites_satisfied());
}

void 
delayed_manifest_delta_packet::apply_delayed_packet(packet_db_writer & pw)
{
  L(F("writing delayed manifest %s packet for %s -> %s\n") 
    % (forward_delta ? "delta" : "reverse delta") 
    % (forward_delta ? old_id : new_id)
    % (forward_delta ? new_id : old_id));
  if (forward_delta)
    pw.consume_manifest_delta(old_id, new_id, del);
  else
    pw.consume_manifest_reverse_delta(new_id, old_id, del);
}

delayed_manifest_delta_packet::~delayed_manifest_delta_packet()
{
  if (!all_prerequisites_satisfied())
    W(F("discarding manifest delta packet %s -> %s with unmet dependencies\n")
        % old_id % new_id);
}

void 
delayed_file_delta_packet::apply_delayed_packet(packet_db_writer & pw)
{
  L(F("writing delayed file %s packet for %s -> %s\n") 
    % (forward_delta ? "delta" : "reverse delta")
    % (forward_delta ? old_id : new_id)
    % (forward_delta ? new_id : old_id));
  if (forward_delta)
    pw.consume_file_delta(old_id, new_id, del);
  else
    pw.consume_file_reverse_delta(new_id, old_id, del);
}

delayed_file_delta_packet::~delayed_file_delta_packet()
{
  if (!all_prerequisites_satisfied())
    W(F("discarding file delta packet %s -> %s with unmet dependencies\n")
        % old_id % new_id);
}

void 
delayed_revision_cert_packet::apply_delayed_packet(packet_db_writer & pw)
{
  L(F("writing delayed revision cert on %s\n") % c.inner().ident);
  pw.consume_revision_cert(c);
}

delayed_revision_cert_packet::~delayed_revision_cert_packet()
{
  if (!all_prerequisites_satisfied())
    W(F("discarding revision cert packet %s with unmet dependencies\n")
      % c.inner().ident);
}

void 
delayed_public_key_packet::apply_delayed_packet(packet_db_writer & pw)
{
  L(F("writing delayed public key %s\n") % id());
  pw.consume_public_key(id, key);
}

delayed_public_key_packet::~delayed_public_key_packet()
{
  // keys don't have dependencies
  I(all_prerequisites_satisfied());
}

void 
delayed_private_key_packet::apply_delayed_packet(packet_db_writer & pw)
{
  L(F("writing delayed private key %s\n") % id());
  pw.consume_private_key(id, key);
}

delayed_private_key_packet::~delayed_private_key_packet()
{
  // keys don't have dependencies
  I(all_prerequisites_satisfied());
}


void
packet_consumer::set_on_revision_written(boost::function1<void,
                                                        revision_id> const & x)
{
  on_revision_written=x;
}

void
packet_consumer::set_on_cert_written(boost::function1<void,
                                                      cert const &> const & x)
{
  on_cert_written=x;
}

void
packet_consumer::set_on_pubkey_written(boost::function1<void, rsa_keypair_id>
                                                  const & x)
{
  on_pubkey_written=x;
}

void
packet_consumer::set_on_privkey_written(boost::function1<void, rsa_keypair_id>
                                                  const & x)
{
  on_privkey_written=x;
}


struct packet_db_writer::impl
{
  app_state & app;
  bool take_keys;
  size_t count;

  map<revision_id, shared_ptr<prerequisite> > revision_prereqs;
  map<manifest_id, shared_ptr<prerequisite> > manifest_prereqs;
  map<file_id, shared_ptr<prerequisite> > file_prereqs;

  //   ticker cert;
  //   ticker manc;
  //   ticker manw;
  //   ticker filec;

  bool revision_exists_in_db(revision_id const & r);
  bool manifest_version_exists_in_db(manifest_id const & m);
  bool file_version_exists_in_db(file_id const & f);

  void get_revision_prereq(revision_id const & revision, shared_ptr<prerequisite> & p);
  void get_manifest_prereq(manifest_id const & manifest, shared_ptr<prerequisite> & p);
  void get_file_prereq(file_id const & file, shared_ptr<prerequisite> & p);

  void accepted_revision(revision_id const & r, packet_db_writer & dbw);
  void accepted_manifest(manifest_id const & m, packet_db_writer & dbw);
  void accepted_file(file_id const & f, packet_db_writer & dbw);

  impl(app_state & app, bool take_keys) 
    : app(app), take_keys(take_keys), count(0)
    // cert("cert", 1), manc("manc", 1), manw("manw", 1), filec("filec", 1)
  {}

  ~impl();
};

packet_db_writer::packet_db_writer(app_state & app, bool take_keys) 
  : pimpl(new impl(app, take_keys))
{}

packet_db_writer::~packet_db_writer() 
{}

packet_db_writer::impl::~impl()
{

  // break any circular dependencies for unsatisfied prerequisites
  for (map<revision_id, shared_ptr<prerequisite> >::const_iterator i =
      revision_prereqs.begin(); i != revision_prereqs.end(); i++)
    {
      i->second->cleanup();
    }
  for (map<manifest_id, shared_ptr<prerequisite> >::const_iterator i =
      manifest_prereqs.begin(); i != manifest_prereqs.end(); i++)
    {
      i->second->cleanup();
    }
  for (map<file_id, shared_ptr<prerequisite> >::const_iterator i =
      file_prereqs.begin(); i != file_prereqs.end(); i++)
    {
      i->second->cleanup();
    }
}

bool 
packet_db_writer::impl::revision_exists_in_db(revision_id const & r)
{
  return app.db.revision_exists(r);
}

bool 
packet_db_writer::impl::manifest_version_exists_in_db(manifest_id const & m)
{
  return app.db.manifest_version_exists(m);
}

bool 
packet_db_writer::impl::file_version_exists_in_db(file_id const & f)
{
  return app.db.file_version_exists(f);
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
packet_db_writer::impl::get_revision_prereq(revision_id const & rev, 
                                            shared_ptr<prerequisite> & p)
{
  map<revision_id, shared_ptr<prerequisite> >::const_iterator i;
  i = revision_prereqs.find(rev);
  if (i != revision_prereqs.end())
    p = i->second;
  else
    {
      p = shared_ptr<prerequisite>(new prerequisite(rev.inner(), prereq_revision));
      revision_prereqs.insert(make_pair(rev, p));
    }
}


void 
packet_db_writer::impl::accepted_revision(revision_id const & r, packet_db_writer & dbw)
{
  L(F("noting acceptence of revision %s\n") % r);
  map<revision_id, shared_ptr<prerequisite> >::iterator i = revision_prereqs.find(r);
  if (i != revision_prereqs.end())
    {
      shared_ptr<prerequisite> prereq = i->second;
      revision_prereqs.erase(i);
      prereq->satisfy(prereq, dbw);
    }
}

void 
packet_db_writer::impl::accepted_manifest(manifest_id const & m, packet_db_writer & dbw)
{
  L(F("noting acceptence of manifest %s\n") % m);
  map<manifest_id, shared_ptr<prerequisite> >::iterator i = manifest_prereqs.find(m);
  if (i != manifest_prereqs.end())
    {
      shared_ptr<prerequisite> prereq = i->second;
      manifest_prereqs.erase(i);
      prereq->satisfy(prereq, dbw);
    }
}

void 
packet_db_writer::impl::accepted_file(file_id const & f, packet_db_writer & dbw)
{
  L(F("noting acceptence of file %s\n") % f);
  map<file_id, shared_ptr<prerequisite> >::iterator i = file_prereqs.find(f);  
  if (i != file_prereqs.end())
    {
      shared_ptr<prerequisite> prereq = i->second;
      file_prereqs.erase(i);
      prereq->satisfy(prereq, dbw);
    }
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
          data new_dat;
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
          dp = shared_ptr<delayed_packet>(new delayed_file_delta_packet(old_id, new_id, del, true));
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
packet_db_writer::consume_file_reverse_delta(file_id const & new_id,
                                             file_id const & old_id,
                                             file_delta const & del)
{
  transaction_guard guard(pimpl->app.db);
  if (! pimpl->file_version_exists_in_db(old_id))
    {
      if (pimpl->file_version_exists_in_db(new_id))
        {
          file_id confirm;
          file_data new_dat;
          data old_dat;
          pimpl->app.db.get_file_version(new_id, new_dat);
          patch(new_dat.inner(), del.inner(), old_dat);
          calculate_ident(file_data(old_dat), confirm);
          if (confirm == old_id)
            {
              pimpl->app.db.put_file_reverse_version(new_id, old_id, del);
              pimpl->accepted_file(old_id, *this);
            }
          else
            {
              W(F("reconstructed file from reverse delta '%s' -> '%s' has wrong id '%s'\n") 
                % new_id % old_id % confirm);
            }
        }
      else
        {
          L(F("delaying reverse file delta %s -> %s for preimage\n") % new_id % old_id);
          shared_ptr<delayed_packet> dp;
          dp = shared_ptr<delayed_packet>(new delayed_file_delta_packet(old_id, new_id, del, false));
          shared_ptr<prerequisite> fp;
          pimpl->get_file_prereq(new_id, fp); 
          dp->add_prerequisite(fp);
          fp->add_dependent(dp);
        }
    }
  else
    L(F("skipping reverse delta to existing file version %s\n") % old_id);
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
      pimpl->app.db.put_manifest(ident, dat);
      pimpl->accepted_manifest(ident, *this);
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
  if (! pimpl->manifest_version_exists_in_db(new_id))
    {
      if (pimpl->manifest_version_exists_in_db(old_id))
        {
          manifest_id confirm;
          manifest_data old_dat;
          data new_dat;
          pimpl->app.db.get_manifest_version(old_id, old_dat);
          patch(old_dat.inner(), del.inner(), new_dat);
          calculate_ident(manifest_data(new_dat), confirm);
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
          L(F("delaying manifest delta %s -> %s for preimage\n") % old_id % new_id);
          shared_ptr<delayed_packet> dp;
          dp = shared_ptr<delayed_packet>(new delayed_manifest_delta_packet(old_id, new_id, del, true));
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
packet_db_writer::consume_manifest_reverse_delta(manifest_id const & new_id,
                                                 manifest_id const & old_id,
                                                 manifest_delta const & del)
{
  transaction_guard guard(pimpl->app.db);
  if (! pimpl->manifest_version_exists_in_db(old_id))
    {
      if (pimpl->manifest_version_exists_in_db(new_id))
        {
          manifest_id confirm;
          manifest_data new_dat;
          data old_dat;
          pimpl->app.db.get_manifest_version(new_id, new_dat);
          patch(new_dat.inner(), del.inner(), old_dat);
          calculate_ident(manifest_data(old_dat), confirm);
          if (confirm == old_id)
            {
              pimpl->app.db.put_manifest_reverse_version(new_id, old_id, del);
              pimpl->accepted_manifest(old_id, *this);
            }
          else
            {
              W(F("reconstructed manifest from reverse delta '%s' -> '%s' has wrong id '%s'\n") 
                % new_id % old_id % confirm);
            }
        }
      else
        {
          L(F("delaying manifest reverse delta %s -> %s for preimage\n") % new_id % old_id);
          shared_ptr<delayed_packet> dp;
          dp = shared_ptr<delayed_packet>(new delayed_manifest_delta_packet(old_id, new_id, del, false));
          shared_ptr<prerequisite> fp;
          pimpl->get_manifest_prereq(new_id, fp); 
          dp->add_prerequisite(fp);
          fp->add_dependent(dp);
        }
    }
  else
    L(F("skipping reverse delta to existing manifest version %s\n") % old_id);
  ++(pimpl->count);
  guard.commit();
}


void 
packet_db_writer::consume_revision_data(revision_id const & ident, 
                                        revision_data const & dat)
{
  transaction_guard guard(pimpl->app.db);
  if (! pimpl->revision_exists_in_db(ident))
    {

      shared_ptr<delayed_packet> dp;
      dp = shared_ptr<delayed_packet>(new delayed_revision_data_packet(ident, dat));
      
      revision_set rev;
      read_revision_set(dat, rev);

      if (! pimpl->manifest_version_exists_in_db(rev.new_manifest))
        {
          L(F("delaying revision %s for new manifest %s\n") 
            % ident % rev.new_manifest);
          shared_ptr<prerequisite> fp;
          pimpl->get_manifest_prereq(rev.new_manifest, fp);
          dp->add_prerequisite(fp);
          fp->add_dependent(dp);
        }
      
      for (edge_map::const_iterator i = rev.edges.begin(); 
           i != rev.edges.end(); ++i)
        {
          if (! (edge_old_manifest(i).inner()().empty() 
                 || pimpl->manifest_version_exists_in_db(edge_old_manifest(i))))
            {
              L(F("delaying revision %s for old manifest %s\n") 
                % ident % edge_old_manifest(i));
              shared_ptr<prerequisite> fp;
              pimpl->get_manifest_prereq(edge_old_manifest(i), fp);
              dp->add_prerequisite(fp);
              fp->add_dependent(dp);
            }
          if (! (edge_old_revision(i).inner()().empty() 
                 || pimpl->revision_exists_in_db(edge_old_revision(i))))
            {
              L(F("delaying revision %s for old revision %s\n") 
                % ident % edge_old_revision(i));
              shared_ptr<prerequisite> fp;
              pimpl->get_revision_prereq(edge_old_revision(i), fp);
              dp->add_prerequisite(fp);
              fp->add_dependent(dp);
            }
          for (change_set::delta_map::const_iterator d = edge_changes(i).deltas.begin();
               d != edge_changes(i).deltas.end(); ++d)
            {
              if (! (delta_entry_src(d).inner()().empty() 
                     || pimpl->file_version_exists_in_db(delta_entry_src(d))))
                {
                  L(F("delaying revision %s for old file %s\n") 
                    % ident % delta_entry_src(d));
                  shared_ptr<prerequisite> fp;
                  pimpl->get_file_prereq(delta_entry_src(d), fp);
                  dp->add_prerequisite(fp);
                  fp->add_dependent(dp);
                }
              I(!delta_entry_dst(d).inner()().empty());
              if (! pimpl->file_version_exists_in_db(delta_entry_dst(d)))
                {
                  L(F("delaying revision %s for new file %s\n") 
                    % ident % delta_entry_dst(d));
                  shared_ptr<prerequisite> fp;
                  pimpl->get_file_prereq(delta_entry_dst(d), fp);
                  dp->add_prerequisite(fp);
                  fp->add_dependent(dp);
                }
            }     
        }

      if (dp->all_prerequisites_satisfied())
        {
          pimpl->app.db.put_revision(ident, dat);
          if(on_revision_written) on_revision_written(ident);
          pimpl->accepted_revision(ident, *this);
        }
    }
  else
    L(F("skipping existing revision %s\n") % ident);  
  ++(pimpl->count);
  guard.commit();
}

void 
packet_db_writer::consume_revision_cert(revision<cert> const & t)
{
  transaction_guard guard(pimpl->app.db);
  if (! pimpl->app.db.revision_cert_exists(t))
    {
      if (pimpl->revision_exists_in_db(revision_id(t.inner().ident)))
        {
          pimpl->app.db.put_revision_cert(t);
          if(on_cert_written) on_cert_written(t.inner());
        }
      else
        {
          L(F("delaying revision cert on %s\n") % t.inner().ident);
          shared_ptr<delayed_packet> dp;
          dp = shared_ptr<delayed_packet>(new delayed_revision_cert_packet(t));
          shared_ptr<prerequisite> fp;
          pimpl->get_revision_prereq(revision_id(t.inner().ident), fp); 
          dp->add_prerequisite(fp);
          fp->add_dependent(dp);
        }
    }
  else
    {
      string s;
      cert_signable_text(t.inner(), s);
      L(F("skipping existing revision cert %s\n") % s);
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
    {
      pimpl->app.db.put_key(ident, k);
      if(on_pubkey_written) on_pubkey_written(ident);
    }
  else
    {
      base64<rsa_pub_key> tmp;
      pimpl->app.db.get_key(ident, tmp);
      if (!keys_match(ident, tmp, ident, k))
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
    {
      pimpl->app.db.put_key(ident, k);
      if(on_privkey_written) on_privkey_written(ident);
    }
  else
    L(F("skipping existing private key %s\n") % ident);
  ++(pimpl->count);
  guard.commit();
}


// --- valved packet writer ---

struct packet_db_valve::impl
{
  packet_db_writer writer;
  std::vector< boost::shared_ptr<delayed_packet> > packets;
  bool valve_is_open;
  impl(app_state & app, bool take_keys)
    : writer(app, take_keys),
      valve_is_open(false)
  {}
  void do_packet(boost::shared_ptr<delayed_packet> packet)
  {
    if (valve_is_open)
      packet->apply_delayed_packet(writer);
    else
      packets.push_back(packet);
  }
};

packet_db_valve::packet_db_valve(app_state & app, bool take_keys)
  : pimpl(new impl(app, take_keys))
{}
    
packet_db_valve::~packet_db_valve()
{}

void
packet_db_valve::open_valve()
{
  L(F("packet valve opened\n"));
  pimpl->valve_is_open = true;
  int written = 0;
  for (std::vector< boost::shared_ptr<delayed_packet> >::reverse_iterator
         i = pimpl->packets.rbegin();
       i != pimpl->packets.rend();
       ++i)
    {
      pimpl->do_packet(*i);
      ++written;
    }
  pimpl->packets.clear();
  L(F("wrote %i queued packets\n") % written);
}

#define DOIT(x) pimpl->do_packet(boost::shared_ptr<delayed_packet>(new x));

void
packet_db_valve::set_on_revision_written(boost::function1<void,
                                                        revision_id> const & x)
{
  on_revision_written=x;
  pimpl->writer.set_on_revision_written(x);
}

void
packet_db_valve::set_on_cert_written(boost::function1<void,
                                                      cert const &> const & x)
{
  on_cert_written=x;
  pimpl->writer.set_on_cert_written(x);
}

void
packet_db_valve::set_on_pubkey_written(boost::function1<void, rsa_keypair_id>
                                                  const & x)
{
  on_pubkey_written=x;
  pimpl->writer.set_on_pubkey_written(x);
}

void
packet_db_valve::set_on_privkey_written(boost::function1<void, rsa_keypair_id>
                                                  const & x)
{
  on_privkey_written=x;
  pimpl->writer.set_on_privkey_written(x);
}

void
packet_db_valve::consume_file_data(file_id const & ident, 
                                   file_data const & dat)
{
  DOIT(delayed_file_data_packet(ident, dat));
}

void
packet_db_valve::consume_file_delta(file_id const & id_old, 
                                    file_id const & id_new,
                                    file_delta const & del)
{
  DOIT(delayed_file_delta_packet(id_old, id_new, del, true));
}

void
packet_db_valve::consume_file_reverse_delta(file_id const & id_new,
                                            file_id const & id_old,
                                            file_delta const & del)
{
  DOIT(delayed_file_delta_packet(id_old, id_new, del, false));
}

void
packet_db_valve::consume_manifest_data(manifest_id const & ident, 
                                       manifest_data const & dat)
{
  DOIT(delayed_manifest_data_packet(ident, dat));
}

void
packet_db_valve::consume_manifest_delta(manifest_id const & id_old, 
                                        manifest_id const & id_new,
                                        manifest_delta const & del)
{
  DOIT(delayed_manifest_delta_packet(id_old, id_new, del, true));
}

void
packet_db_valve::consume_manifest_reverse_delta(manifest_id const & id_new,
                                                manifest_id const & id_old,
                                                manifest_delta const & del)
{
  DOIT(delayed_manifest_delta_packet(id_old, id_new, del, false));
}

void
packet_db_valve::consume_revision_data(revision_id const & ident, 
                                       revision_data const & dat)
{
  DOIT(delayed_revision_data_packet(ident, dat));
}

void
packet_db_valve::consume_revision_cert(revision<cert> const & t)
{
  DOIT(delayed_revision_cert_packet(t));
}

void
packet_db_valve::consume_public_key(rsa_keypair_id const & ident,
                                    base64< rsa_pub_key > const & k)
{
  DOIT(delayed_public_key_packet(ident, k));
}

void
packet_db_valve::consume_private_key(rsa_keypair_id const & ident,
                                     base64< arc4<rsa_priv_key> > const & k)
{
  DOIT(delayed_private_key_packet(ident, k));
}

#undef DOIT

// --- packet writer ---

packet_writer::packet_writer(ostream & o) : ost(o) {}

void 
packet_writer::consume_file_data(file_id const & ident, 
                                 file_data const & dat)
{
  base64<gzip<data> > packed;
  pack(dat.inner(), packed);
  ost << "[fdata " << ident.inner()() << "]" << endl 
      << trim_ws(packed()) << endl
      << "[end]" << endl;
}

void 
packet_writer::consume_file_delta(file_id const & old_id, 
                                  file_id const & new_id,
                                  file_delta const & del)
{
  base64<gzip<delta> > packed;
  pack(del.inner(), packed);
  ost << "[fdelta " << old_id.inner()() << endl 
      << "        " << new_id.inner()() << "]" << endl 
      << trim_ws(packed()) << endl
      << "[end]" << endl;
}

void 
packet_writer::consume_file_reverse_delta(file_id const & new_id, 
                                          file_id const & old_id,
                                          file_delta const & del)
{
  base64<gzip<delta> > packed;
  pack(del.inner(), packed);
  ost << "[frdelta " << new_id.inner()() << endl 
      << "         " << old_id.inner()() << "]" << endl 
      << trim_ws(packed()) << endl
      << "[end]" << endl;
}

void 
packet_writer::consume_manifest_data(manifest_id const & ident, 
                                     manifest_data const & dat)
{
  base64<gzip<data> > packed;
  pack(dat.inner(), packed);
  ost << "[mdata " << ident.inner()() << "]" << endl 
      << trim_ws(packed()) << endl
      << "[end]" << endl;
}

void 
packet_writer::consume_revision_data(revision_id const & ident, 
                                     revision_data const & dat)
{
  base64<gzip<data> > packed;
  pack(dat.inner(), packed);
  ost << "[rdata " << ident.inner()() << "]" << endl 
      << trim_ws(packed()) << endl
      << "[end]" << endl;
}

void 
packet_writer::consume_manifest_delta(manifest_id const & old_id, 
                                      manifest_id const & new_id,
                                      manifest_delta const & del)
{
  base64<gzip<delta> > packed;
  pack(del.inner(), packed);
  ost << "[mdelta " << old_id.inner()() << endl 
      << "        " << new_id.inner()() << "]" << endl 
      << trim_ws(packed()) << endl
      << "[end]" << endl;
}

void 
packet_writer::consume_manifest_reverse_delta(manifest_id const & new_id, 
                                              manifest_id const & old_id,
                                              manifest_delta const & del)
{
  base64<gzip<delta> > packed;
  pack(del.inner(), packed);
  ost << "[mrdelta " << new_id.inner()() << endl 
      << "         " << old_id.inner()() << "]" << endl 
      << trim_ws(packed()) << endl
      << "[end]" << endl;
}

void 
packet_writer::consume_revision_cert(revision<cert> const & t)
{
  ost << "[rcert " << t.inner().ident() << endl
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
        base64<gzip<data> > body_packed(trim_ws(string(res[3].first, res[3].second)));
        data body;
        unpack(body_packed, body);
        if (head == "rdata")
          cons.consume_revision_data(revision_id(hexenc<id>(ident)), 
                                     revision_data(body));
        else if (head == "mdata")
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
        string src_id(res[5].first, res[5].second);
        string dst_id(res[6].first, res[6].second);
        base64<gzip<delta> > body_packed(trim_ws(string(res[7].first, res[7].second)));
        delta body;
        unpack(body_packed, body);
        if (head == "mdelta")
          cons.consume_manifest_delta(manifest_id(hexenc<id>(src_id)), 
                                      manifest_id(hexenc<id>(dst_id)),
                                      manifest_delta(body));
        else if (head == "fdelta")
          cons.consume_file_delta(file_id(hexenc<id>(src_id)), 
                                  file_id(hexenc<id>(dst_id)), 
                                  file_delta(body));
        else if (head == "mrdelta")
          cons.consume_manifest_reverse_delta(manifest_id(hexenc<id>(src_id)), 
                                              manifest_id(hexenc<id>(dst_id)), 
                                              manifest_delta(body));
        else if (head == "frdelta")
          cons.consume_file_reverse_delta(file_id(hexenc<id>(src_id)), 
                                          file_id(hexenc<id>(dst_id)), 
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
        string body(trim_ws(string(res[13].first, res[13].second)));

        // canonicalize the base64 encodings to permit searches
        cert t = cert(hexenc<id>(ident),
                      cert_name(certname),
                      base64<cert_value>(canonical_base64(val)),
                      rsa_keypair_id(key),
                      base64<rsa_sha1_signature>(canonical_base64(body)));
        if (head == "rcert")
          cons.consume_revision_cert(revision<cert>(t));
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
  string const certhead("(rcert)");
  string const datahead("([mfr]data)");
  string const deltahead("([mf]r?delta)");
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
    file_data fdata(data("this is some file data"));
    file_id fid;
    calculate_ident(fdata, fid);
    pw.consume_file_data(fid, fdata);

    // an fdelta packet    
    file_data fdata2(data("this is some file data which is not the same as the first one"));
    file_id fid2;
    calculate_ident(fdata2, fid);
    delta del;
    diff(fdata.inner(), fdata2.inner(), del);
    pw.consume_file_delta(fid, fid2, file_delta(del));

    // a cert packet
    base64<cert_value> val;
    encode_base64(cert_value("peaches"), val);
    base64<rsa_sha1_signature> sig;
    encode_base64(rsa_sha1_signature("blah blah there is no way this is a valid signature"), sig);    
    // should be a type violation to use a file id here instead of a revision
    // id, but no-one checks...
    cert c(fid.inner(), cert_name("smell"), val, 
           rsa_keypair_id("fun@moonman.com"), sig);
    pw.consume_revision_cert(revision<cert>(c));
    
    // a manifest data packet
    manifest_map mm;
    manifest_data mdata;
    manifest_id mid;
    mm.insert(make_pair(file_path_internal("foo/bar.txt"),
                        file_id(hexenc<id>("cfb81b30ab3133a31b52eb50bd1c86df67eddec4"))));
    write_manifest_map(mm, mdata);
    calculate_ident(mdata, mid);
    pw.consume_manifest_data(mid, mdata);

    // a manifest delta packet
    manifest_map mm2;
    manifest_data mdata2;
    manifest_id mid2;
    manifest_delta mdelta;
    mm2.insert(make_pair(file_path_internal("foo/bar.txt"),
                         file_id(hexenc<id>("5b20eb5e5bdd9cd674337fc95498f468d80ef7bc"))));
    mm2.insert(make_pair(file_path_internal("bunk.txt"),
                         file_id(hexenc<id>("54f373ed07b4c5a88eaa93370e1bbac02dc432a8"))));
    write_manifest_map(mm2, mdata2);
    calculate_ident(mdata2, mid2);
    delta del2;
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
