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
using std::map;
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
}

revision_enumerator::revision_enumerator(enumerator_callbacks & cb,
                                         app_state & app)
  : cb(cb), app(app)
{
  revision_id root;
  revs.push_back(root);
}

bool
revision_enumerator::done()
{
  return revs.empty() && items.empty();
}

void 
revision_enumerator::step()
{
  P(F("stepping...\n"));
  while (!done())
    {
      if (items.empty() && !revs.empty())
        {
          revision_id r = revs.front();
          revs.pop_front();

          P(F("step examining rev '%d'\n") % r);
          
          if (terminal_nodes.find(r) == terminal_nodes.end())
            {
              set<revision_id> children;
              app.db.get_revision_children(r, children);
              P(F("step expanding %d children of rev '%d'\n") % children.size() % r);
              for (set<revision_id>::const_iterator i = children.begin();
                   i != children.end(); ++i)
                revs.push_back(*i);
            }

          if (null_id(r))
            continue;

          if (cb.process_this_rev(r))
            {
              P(F("step expanding contents of rev '%d'\n") % r);

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
                      enumerator_item item;
                      item.tag = enumerator_item::fdata;
                      item.ident_a = fa->second.inner();
                      items.push_back(item);
                    }
                    
                  // Queue up all the file-deltas
                  for (map<split_path, std::pair<file_id, file_id> >::const_iterator fd
                         = cs.deltas_applied.begin();
                       fd != cs.deltas_applied.end(); ++fd)
                    {
                      enumerator_item item;
                      item.tag = enumerator_item::fdelta;
                      item.ident_a = fd->second.first.inner();
                      item.ident_b = fd->second.second.inner();
                      items.push_back(item);
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
          P(F("step extracting item\n"));

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
