// copyright (C) 2005 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <deque>
#include <map>
#include <set>
#include <vector>

#include "cset.hh"
#include "enumerator.hh"
#include "revision.hh"
#include "vocab.hh"

using std::deque;
using std::make_pair;
using std::map;
using std::multimap;
using std::pair;
using std::set;
using std::vector;

revision_enumerator::revision_enumerator(enumerator_callbacks & cb,
                                         app_state & app,
                                         set<revision_id> const & initial,
                                         set<revision_id> const & terminal)
  : cb(cb), app(app), terminal_nodes(terminal)
{
  for (set<revision_id>::const_iterator i = initial.begin();
       i != initial.end(); ++i)
    revs.push_back(*i);
  load_graphs();
}

revision_enumerator::revision_enumerator(enumerator_callbacks & cb,
                                         app_state & app)
  : cb(cb), app(app)
{
  revision_id root;
  revs.push_back(root);
  load_graphs();
}

void 
revision_enumerator::load_graphs()
{
  app.db.get_revision_ancestry(graph);
  for (multimap<revision_id, revision_id>::const_iterator i = graph.begin();
       i != graph.end(); ++i)
    {
      inverse_graph.insert(make_pair(i->second, i->first));
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
                  if (i->first == r)
                    if (enumerated_nodes.find(i->first) == enumerated_nodes.end())
                      revs.push_back(i->second);
                }
            }

          enumerated_nodes.insert(r);

          if (null_id(r))
            continue;

          if (cb.process_this_rev(r))
            {
              L(FL("revision_enumerator::step expanding "
                  "contents of rev '%d'\n") % r);

              revision_set rs;
              app.db.get_revision(r, rs);
              for (edge_map::const_iterator i = rs.edges.begin();
                   i != rs.edges.end(); ++i)
                {
                  cset const & cs = edge_changes(i);
                    
                  // Queue up all the file-adds
                  for (map<split_path, file_id>::const_iterator fa = cs.files_added.begin();
                       fa != cs.files_added.end(); ++fa)
                    {
                      if (cb.queue_this_file(fa->second.inner()))
                        {
                          enumerator_item item;
                          item.tag = enumerator_item::fdata;
                          item.ident_a = fa->second.inner();
                          items.push_back(item);
                        }
                    }
                    
                  // Queue up all the file-deltas
                  for (map<split_path, std::pair<file_id, file_id> >::const_iterator fd
                         = cs.deltas_applied.begin();
                       fd != cs.deltas_applied.end(); ++fd)
                    {
                      if (cb.queue_this_file(fd->second.second.inner()))
                        {
                          enumerator_item item;
                          item.tag = enumerator_item::fdelta;
                          item.ident_a = fd->second.first.inner();
                          item.ident_b = fd->second.second.inner();
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
          app.db.get_revision_certs(r, hashes);
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
          L(FL("revision_enumerator::step extracting item\n"));

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
