// Copyright (C) 2005 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <deque>
#include <map>
#include <set>
#include "vector.hh"

#include "cset.hh"
#include "enumerator.hh"
#include "revision.hh"
#include "vocab.hh"
#include "database.hh"
#include "project.hh"

using std::make_pair;
using std::map;
using std::multimap;
using std::pair;
using std::set;
using std::vector;

revision_enumerator::revision_enumerator(enumerator_callbacks & cb,
                                         project_t & project)
  : cb(cb), project(project)
{
  revision_id root;
  revs.push_back(root);

  project.db.get_revision_ancestry(graph);
  for (multimap<revision_id, revision_id>::const_iterator i = graph.begin();
       i != graph.end(); ++i)
    {
      inverse_graph.insert(make_pair(i->second, i->first));
    }
}

void
revision_enumerator::get_revision_parents(revision_id const & child,
					  vector<revision_id> & parents)
{
  parents.clear();
  typedef multimap<revision_id, revision_id>::const_iterator ci;
  pair<ci,ci> range = inverse_graph.equal_range(child);
  for (ci i = range.first; i != range.second; ++i)
    {
      if (i->first == child)
        {
	  parents.push_back(i->second);
	}
    }
}

bool
revision_enumerator::all_parents_enumerated(revision_id const & child)
{
  typedef multimap<revision_id, revision_id>::const_iterator ci;
  pair<ci,ci> range = inverse_graph.equal_range(child);
  for (ci i = range.first; i != range.second; ++i)
    {
      if (i->first == child)
        {
          if (enumerated_nodes.find(i->second) == enumerated_nodes.end())
            return false;
        }
    }
  return true;
}

bool
revision_enumerator::done()
{
  return revs.empty() && items.empty();
}

void
revision_enumerator::files_for_revision(revision_id const & r,
                                        set<file_id> & full_files,
                                        set<pair<file_id,file_id> > & del_files)
{
  // when we're sending a merge, we have to be careful if we
  // want to send as little data as possible. see bug #15846
  //
  // njs's solution: "when sending the files for a revision,
  // look at both csets. If a given hash is not listed as new
  // in _both_ csets, throw it out. Now, for everything left
  // over, if one side says "add" and the other says "delta",
  // do a delta. If both sides say "add", do a data."

  set<file_id> file_adds;
  // map<dst, src>.  src is arbitrary.
  map<file_id, file_id> file_deltas;
  map<file_id, size_t> file_edge_counts;

  revision_t rs;
  MM(rs);
  project.db.get_revision(r, rs);

  for (edge_map::const_iterator i = rs.edges.begin();
       i != rs.edges.end(); ++i)
    {
      set<file_id> file_dsts;
      cset const & cs = edge_changes(i);

      // Queue up all the file-adds
      for (map<file_path, file_id>::const_iterator fa = cs.files_added.begin();
           fa != cs.files_added.end(); ++fa)
        {
          file_adds.insert(fa->second);
          file_dsts.insert(fa->second);
        }

      // Queue up all the file-deltas
      for (map<file_path, pair<file_id, file_id> >::const_iterator fd
             = cs.deltas_applied.begin();
           fd != cs.deltas_applied.end(); ++fd)
        {
          file_deltas[fd->second.second] = fd->second.first;
          file_dsts.insert(fd->second.second);
        }

      // we don't want to be counting files twice in a single edge
      for (set<file_id>::const_iterator i = file_dsts.begin();
           i != file_dsts.end(); i++)
        file_edge_counts[*i]++;
    }

  del_files.clear();
  full_files.clear();
  size_t num_edges = rs.edges.size();

  for (map<file_id, size_t>::const_iterator i = file_edge_counts.begin();
       i != file_edge_counts.end(); i++)
    {
      MM(i->first);
      if (i->second < num_edges)
        continue;

      // first preference is to send as a delta...
      map<file_id, file_id>::const_iterator fd = file_deltas.find(i->first);
      if (fd != file_deltas.end())
        {
          del_files.insert(make_pair(fd->second, fd->first));
          continue;
        }

      // ... otherwise as a full file.
      set<file_id>::const_iterator f = file_adds.find(i->first);
      if (f != file_adds.end())
        {
          full_files.insert(*f);
          continue;
        }

      I(false);
    }
}

void
revision_enumerator::note_cert(revision_id const & rid,
			       hexenc<id> const & cert_hash)
{
  revision_certs.insert(make_pair(rid, cert_hash));
}


void
revision_enumerator::get_revision_certs(revision_id const & rid,
					vector<hexenc<id> > & hashes)
{
  hashes.clear();
  bool found_one = false;
  typedef multimap<revision_id, hexenc<id> >::const_iterator ci;
  pair<ci,ci> range = revision_certs.equal_range(rid);
  for (ci i = range.first; i != range.second; ++i)
    {
      found_one = true;
      if (i->first == rid)
	hashes.push_back(i->second);
    }
  if (!found_one)
    project.get_revision_cert_hashes(rid, hashes);
}

void
revision_enumerator::step()
{
  while (!done())
    {
      if (items.empty() && !revs.empty())
        {
          revision_id r = revs.front();
          revs.pop_front();

          // It's possible we've enumerated this node elsewhere since last
          // time around. Cull rather than reprocess.
          if (enumerated_nodes.find(r) != enumerated_nodes.end())
            continue;

          if (!all_parents_enumerated(r))
            {
              revs.push_back(r);
              continue;
            }

          if (terminal_nodes.find(r) == terminal_nodes.end())
            {
              typedef multimap<revision_id, revision_id>::const_iterator ci;
              pair<ci,ci> range = graph.equal_range(r);
              for (ci i = range.first; i != range.second; ++i)
                {
		  // We push_front here rather than push_back in order
		  // to improve database cache performance. It avoids
		  // skipping back and forth beween parallel lineages.
                  if (i->first == r)
                    if (enumerated_nodes.find(i->first) == enumerated_nodes.end())
                      revs.push_front(i->second);
                }
            }

          enumerated_nodes.insert(r);

          if (null_id(r))
            continue;

          if (cb.process_this_rev(r))
            {
              L(FL("revision_enumerator::step expanding "
                  "contents of rev '%d'\n") % r);

              // The rev's files and fdeltas
              {
                set<file_id> full_files;
                set<pair<file_id, file_id> > del_files;
                files_for_revision(r, full_files, del_files);

                for (set<file_id>::const_iterator f = full_files.begin();
                     f != full_files.end(); f++)
                  {
                    if (cb.queue_this_file(f->inner()))
                      {
                        enumerator_item item;
                        item.tag = enumerator_item::fdata;
                        item.ident_a = f->inner();
                        items.push_back(item);
                      }
                  }

                for (set<pair<file_id, file_id> >::const_iterator fd = del_files.begin();
                     fd != del_files.end(); fd++)
                  {
                    if (cb.queue_this_file(fd->second.inner()))
                      {
                        enumerator_item item;
                        item.tag = enumerator_item::fdelta;
                        item.ident_a = fd->first.inner();
                        item.ident_b = fd->second.inner();
                        items.push_back(item);
                      }
                  }
              }

              // Queue up the rev itself
              {
                enumerator_item item;
                item.tag = enumerator_item::rev;
                item.ident_a = r.inner();
                items.push_back(item);
              }
            }

          // Queue up some or all of the rev's certs
          vector<hexenc<id> > hashes;
          get_revision_certs(r, hashes);
          for (vector<hexenc<id> >::const_iterator i = hashes.begin();
               i != hashes.end(); ++i)
            {
              if (cb.queue_this_cert(*i))
                {
                  enumerator_item item;
                  item.tag = enumerator_item::cert;
                  item.ident_a = *i;
                  items.push_back(item);
                }
            }
        }

      if (!items.empty())
        {
          L(FL("revision_enumerator::step extracting item"));

          enumerator_item i = items.front();
          items.pop_front();
          I(!null_id(i.ident_a));

          switch (i.tag)
            {
            case enumerator_item::fdata:
              cb.note_file_data(file_id(i.ident_a));
              break;

            case enumerator_item::fdelta:
              I(!null_id(i.ident_b));
              cb.note_file_delta(file_id(i.ident_a),
                                 file_id(i.ident_b));
              break;

            case enumerator_item::rev:
              cb.note_rev(revision_id(i.ident_a));
              break;

            case enumerator_item::cert:
              cb.note_cert(i.ident_a);
              break;
            }
          break;
        }
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
