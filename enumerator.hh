#ifndef __ENUMERATOR_H__
#define __ENUMERATOR_H__

// copyright (C) 2005 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <deque>
#include <map>
#include <set>

#include "app_state.hh"
#include "vocab.hh"

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
  virtual bool queue_this_cert(hexenc<id> const & c) = 0;
  virtual bool queue_this_file(hexenc<id> const & c) = 0;

  virtual void note_file_data(file_id const & f) = 0;
  virtual void note_file_delta(file_id const & src, file_id const & dst) = 0;
  virtual void note_rev(revision_id const & rev) = 0;
  virtual void note_cert(hexenc<id> const & c) = 0;
  virtual ~enumerator_callbacks() {}
};

struct 
enumerator_item
{
  enum { fdata, fdelta, rev, cert } tag;
  hexenc<id> ident_a;
  hexenc<id> ident_b;
};

struct
revision_enumerator
{
  enumerator_callbacks & cb;
  app_state & app;
  std::set<revision_id> terminal_nodes;
  std::set<revision_id> enumerated_nodes;
  std::deque<revision_id> revs;
  std::deque<enumerator_item> items;
  std::multimap<revision_id, revision_id> graph;
  std::multimap<revision_id, revision_id> inverse_graph;

  revision_enumerator(enumerator_callbacks & cb,
                      app_state & app,
                      std::set<revision_id> const & initial,
                      std::set<revision_id> const & terminal);
  revision_enumerator(enumerator_callbacks & cb,
                      app_state & app);
  void load_graphs();
  bool all_parents_enumerated(revision_id const & child);
  void files_for_revision(revision_id const & r, 
                          std::set<file_id> & full_files, 
                          std::set<std::pair<file_id,file_id> > & del_files);
  void step();
  bool done();
};

#endif // __ENUMERATOR_H__
