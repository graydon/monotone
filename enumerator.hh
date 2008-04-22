#ifndef __ENUMERATOR_H__
#define __ENUMERATOR_H__

// Copyright (C) 2005 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <deque>
#include <map>
#include <set>
#include "vector.hh"
#include "vocab.hh"

class database;
class project_t;

// The revision_enumerator struct acts as a cursor which emits files,
// deltas, revisions and certs in dependency-correct order. This is
// used for sending sections of the revision graph through netsync.

struct
enumerator_callbacks
{
  // Your callback will be asked whether you want the details of each rev
  // or cert, in order; you should return true for any rev or cert you want
  // to be notified about the contents of. The rev's children will be
  // traversed no matter what you return here.
  virtual bool process_this_rev(revision_id const & rev) = 0;
  virtual bool queue_this_cert(id const & c) = 0;
  virtual bool queue_this_file(id const & c) = 0;

  virtual void note_file_data(file_id const & f) = 0;
  virtual void note_file_delta(file_id const & src, file_id const & dst) = 0;
  virtual void note_rev(revision_id const & rev) = 0;
  virtual void note_cert(id const & c) = 0;
  virtual ~enumerator_callbacks() {}
};

struct
enumerator_item
{
  enum { fdata, fdelta, rev, cert } tag;
  id ident_a;
  id ident_b;
};

class
revision_enumerator
{
  project_t & project;
  enumerator_callbacks & cb;
  std::set<revision_id> terminal_nodes;
  std::set<revision_id> enumerated_nodes;
  std::deque<revision_id> revs;
  std::deque<enumerator_item> items;
  std::multimap<revision_id, revision_id> graph;
  std::multimap<revision_id, revision_id> inverse_graph;
  std::multimap<revision_id, id>          revision_certs;

  bool all_parents_enumerated(revision_id const & child);
  void files_for_revision(revision_id const & r,
                          std::set<file_id> & full_files,
                          std::set<std::pair<file_id,file_id> > & del_files);
  void get_revision_certs(revision_id const & rid,
			  std::vector<id> & certs);

public:
  revision_enumerator(project_t & project,
                      enumerator_callbacks & cb);
  void get_revision_parents(revision_id const & rid,
			    std::vector<revision_id> & parents);
  void note_cert(revision_id const & rid,
                 id const & cert_hash);
  void step();
  bool done();
};

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __ENUMERATOR_H__
