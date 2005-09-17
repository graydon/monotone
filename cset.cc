// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// copyright (C) 2005 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "cset.hh"
#include "sanity.hh"

using std::set;
using std::map;
using std::pair;

struct
detach
{
  detach(split_path const & src) 
    : src_path(src), 
      reattach(false) 
  {}
  
  detach(split_path const & src, 
         split_path const & dst) 
    : src_path(src), 
      reattach(true), 
      dst_path(dst) 
  {}
  
  split_path src_path;
  bool reattach;
  split_path dst_path;

  bool operator<(struct detach const & other) const
  {
    // We sort detach operations bottom-up by src path
    return src_path > other.src_path;
  }
};

struct
attach
{
  attach(node_id n, 
         split_path const & p) 
    : node(n), path(p)
  {}

  node_id node;
  split_path path;

  bool operator<(struct attach const & other) const
  {
    // We sort attach operations top-down by path
    // SPEEDUP?: simply sort by path.size() rather than full lexicographical
    // comparison?
    return path < other.path;
  }
};

static void
check_normalized(cset const & cs)
{
  // FIXME -- normalize:
  //
  //   add_file foo@id1 + apply_delta id1->id2
  //   clear_attr foo:bar + set_attr foo:bar=baz
  //   rename foo -> foo
  //
  // possibly more?

  // no file appears in both the "added" list and the "patched" list
  {
    map<split_path, file_id>::const_iterator a = cs.files_added.begin();
    map<split_path, std::pair<file_id, file_id> >::const_iterator
      d = cs.deltas_applied.begin();
    while (a != cs.files_added.end() && d != cs.deltas_applied.end())
      {
        // SPEEDUP?: should this use lower_bound instead of ++?  it should
        // make this loop iterate about as many times as the shorter list,
        // rather the sum of the two lists...
        if (a->first < d->first)
          ++a;
        else if (d->first < a->first)
          ++d;
        else
          I(false);
      }
  }
  
  // no file+attr pair appears in both the "set" list and the "cleared" list
  {
    set<pair<split_path, attr_key> >::const_iterator c = cs.attrs_cleared.begin();
    map<pair<split_path, attr_key>, attr_value>::const_iterator
      s = cs.attrs_set.begin();
    while (c != cs.attrs_cleared.end() && s != cs.attrs_set.end())
      {
        if (*c < s->first)
          ++c;
        else if (s->first < *c)
          ++s;
        else
          I(false);
      }
  }

  // no no-op renames
  for (std::map<split_path, split_path>::const_iterator i = cs.nodes_renamed.begin();
       i != cs.nodes_renamed.end(); ++i)
    I(i->first != i->second);
}

bool
cset::empty() const
{
  return 
    nodes_deleted.empty() 
    && dirs_added.empty()
    && files_added.empty()
    && nodes_renamed.empty()
    && deltas_applied.empty()
    && attrs_cleared.empty()
    && attrs_set.empty();
}

void 
cset::apply_to(editable_tree & t) const
{
  // SPEEDUP?: use vectors and sort them once, instead of maintaining sorted
  // sets?
  set<detach> detaches;
  set<attach> attaches;
  set<node_id> drops;

  check_normalized(*this);

  // Decompose all additions into a set of pending attachments to be
  // executed top-down. We might as well do this first, to be sure we
  // can form the new nodes -- such as in a filesystem -- before we do
  // anything else potentially destructive. This should all be
  // happening in a temp directory anyways.

  for (set<split_path>::const_iterator i = dirs_added.begin();
       i != dirs_added.end(); ++i)
    attaches.insert(attach(t.create_dir_node(), *i));

  for (map<split_path, file_id>::const_iterator i = files_added.begin();
       i != files_added.end(); ++i)
    attaches.insert(attach(t.create_file_node(i->second), i->first));


  // Decompose all path deletion and the first-half of renamings on
  // existing paths into the set of pending detaches, to be executed
  // bottom-up.

  for (set<split_path>::const_iterator i = nodes_deleted.begin();
       i != nodes_deleted.end(); ++i)    
    detaches.insert(detach(*i));
  
  for (map<split_path, split_path>::const_iterator i = nodes_renamed.begin();
       i != nodes_renamed.end(); ++i)
    detaches.insert(detach(i->first, i->second));


  // Execute all the detaches, rescheduling the results of each detach
  // for either attaching or dropping.

  for (set<detach>::const_iterator i = detaches.begin(); 
       i != detaches.end(); ++i)
    {
      node_id n = t.detach_node(i->src_path);
      if (i->reattach)
        attaches.insert(attach(n, i->dst_path));
      else
        drops.insert(n);
    }


  // Execute all the attaches.

  for (set<attach>::const_iterator i = attaches.begin(); i != attaches.end(); ++i)
    t.attach_node(i->node, i->path);


  // Execute all the drops.

  for (set<node_id>::const_iterator i = drops.begin(); i != drops.end(); ++i)
    t.drop_detached_node (*i);


  // Execute all the in-place edits
  for (map<split_path, pair<file_id, file_id> >::const_iterator i = deltas_applied.begin();
       i != deltas_applied.end(); ++i)
    t.apply_delta(i->first, i->second.first, i->second.second);

  for (set<pair<split_path, attr_key> >::const_iterator i = attrs_cleared.begin();
       i != attrs_cleared.end(); ++i)
    t.clear_attr(i->first, i->second);

  for (map<pair<split_path, attr_key>, attr_value>::const_iterator i = attrs_set.begin();
       i != attrs_set.end(); ++i)
    t.set_attr(i->first.first, i->first.second, i->second);
}


void 
print_cset(basic_io::printer & printer,
	   cset const & cs)
{
  // FIXME: implement
  I(false);
}

void 
parse_cset(basic_io::parser & parser,
	   cset & cs)
{
  // FIXME: implement
  I(false);
}


#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

void
add_cset_tests(test_suite * suite)
{
  I(suite);
}

#endif // BUILD_UNIT_TESTS


