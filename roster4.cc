// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// copyright (C) 2005 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <algorithm>
#include <stack>
#include <set>
#include <vector>

#include "app_state.hh"
#include "change_set.hh" // Remove this when fully defunct
#include "cset.hh"
#include "roster4.hh"
#include "vocab.hh"

using std::inserter;
using std::make_pair;
using std::map;
using std::pair;
using std::reverse;
using std::set;
using std::set_union;
using std::stack;
using std::vector;


///////////////////////////////////////////////////////////////////

template <typename T>
void
safe_erase(T & container, typename T::key_type const & key)
{
  I(container.erase(key));
}

template <typename T>
typename T::iterator
safe_insert(T & container, typename T::value_type const & val)
{
  std::pair<typename T::iterator, bool> r = container.insert(val);
  I(r.second);
  return r.first;
}

template <typename T>
typename T::mapped_type const &
safe_get(T & container, typename T::key_type const & key)
{
  typename T::const_iterator i = container.find(key);
  I(i != container.end());
  return i->second;
}

///////////////////////////////////////////////////////////////////

namespace 
{
  //
  // We have a few concepts of "nullness" here:
  //
  // - the_null_node is a node_id. It does not correspond to a real node;
  //   it's an id you use for the parent of the root, or of any node which
  //   is detached.
  //
  // - the_null_component is a path_component. It is the *name* of the root
  //   node. Its string representation is "", the empty string. 
  //
  // - The split_path corresponding to the_null_node is [], the empty vector.
  //
  // - The split_path corresponding to the root node is [""], the 1-element
  //   vector containing the_null_component.
  //
  // - The split_path corresponding to foo/bar is ["", "foo", "bar"].
  //
  // - The only legal one-element split_path is [""], referring to the
  //   root node.
  //
  // We do this in order to support the notion of moving the root directory
  // around, or applying attributes to the root directory (though we will
  // not support moving the root at this time, since we haven't worked out
  // all the UI implications yet). 
  //


  const node_id the_null_node = 0;
  const node_id first_node = 1;
  inline bool null_node(node_id n)
  {
    return n == the_null_node;
  }

  const node_id first_temp_node = widen<node_id, int>(1) << (sizeof(node_id) * 8 - 1);
  inline bool temp_node(node_id n)
  {
    return n & first_temp_node;
  }
}

node::node()
  : self(the_null_node),
    parent(the_null_node), 
    name(the_null_component)
{
}

node_t 
dir_node::get_child(path_component const & pc) const
{
  return safe_get(children, pc);
}
  

void 
dir_node::attach_child(path_component const & pc, node_t child)
{
  I(null_node(child->parent));
  I(null_name(child->name));
  safe_insert(children, make_pair(pc, child));
  child->parent = this->self;
  child->name = pc;
}


node_t 
dir_node::detach_child(path_component const & pc)
{
  node_t n = get_child(pc);
  n->parent = the_null_node;
  n->name = the_null_component;
  safe_erase(children, pc);
  return n;
}

node_t
dir_node::clone()
{
  dir_t d = dir_t(new dir_node());
  d->birth_revision = birth_revision;
  d->self = self;
  d->parent = parent;
  d->name = name;
  d->attrs = attrs;
  d->children = children;
  return d;
}

node_t
file_node::clone()
{
  file_t f = file_t(new file_node());
  f->birth_revision = birth_revision;
  f->self = self;
  f->parent = parent;
  f->name = name;
  f->attrs = attrs;
  f->content = content;
  return f;
}


static inline void
dirname_basename(split_path const & sp,
                 split_path & dirname, path_component & basename)
{
  I(!sp.empty());
  // L(F("dirname_basename('%s' [%d components],...)\n") % file_path(sp) % sp.size());
  split_path::const_iterator penultimate = sp.begin() + (sp.size()-1);
  dirname = split_path(sp.begin(), penultimate);
  basename = *penultimate;
  if (dirname.empty())
    {
      // L(F("basename %d vs. null component %d\n") % basename % the_null_component);
      I(null_name(basename));
    }
}


bool
roster_t::has_root() const
{
  return static_cast<bool>(root_dir);
}


node_t
roster_t::get_node(split_path const & sp) const
{
  split_path dirname;
  path_component basename;
  dirname_basename(sp, dirname, basename);

  if (dirname.empty())
    {
      I(null_name(basename));
      return root_dir;
    }

  I(has_root());
  dir_t d = root_dir;  
  for (split_path::const_iterator i = dirname.begin()+1; i != dirname.end(); ++i)
    d = downcast_to_dir_t(d->get_child(*i));
  return d->get_child(basename);
}


node_t
roster_t::get_node(node_id nid) const
{
  return safe_get(nodes, nid);
}


void
roster_t::get_name(node_id nid, split_path & sp) const
{
  I(!null_node(nid));
  sp.clear();
  while (!null_node(nid))
    {
      node_t n = get_node(nid);
      sp.push_back(n->name);
      nid = n->parent;
    }
  reverse(sp.begin(), sp.end());
}


void 
roster_t::replace_node_id(node_id from, node_id to)
{
  I(!null_node(from));
  I(!null_node(to));
  node_t n = get_node(from);
  safe_erase(nodes, from);
  safe_insert(nodes, make_pair(to, n));
  n->self = to;
  
  if (is_dir_t(n))
    {
      dir_t d = downcast_to_dir_t(n);
      for (dir_map::iterator i = d->children.begin(); i != d->children.end(); ++i)
	{
	  I(i->second->parent == from);
	  i->second->parent = to;
	}
    }
}


node_id
roster_t::detach_node(split_path const & pth)
{
  split_path dirname;
  path_component basename;
  dirname_basename(pth, dirname, basename);

  if (dirname.empty())
    {
      // detaching the root dir
      I(null_name(basename));
      node_id root_id = root_dir->self;
      // cleare set the root_dir shared_pointer
      root_dir.reset();
      return root_id;
    }

  dir_t parent = downcast_to_dir_t(get_node(dirname));
  node_id n = parent->detach_child(basename)->self;
  I(!null_node(n));
  return n;
}


void
roster_t::drop_detached_node(node_id nid)
{
  // ensure the node is already detached (as best one can)
  node_t n = get_node(nid);
  I(null_node(n->parent));
  I(null_name(n->name));
  safe_erase(nodes, nid);
}


node_id
roster_t::create_dir_node(node_id_source & nis)
{
  node_id nid = nis.next();
  dir_t d = dir_t(new dir_node());
  d->self = nid;
  safe_insert(nodes, make_pair(nid, d));
  return nid;
}


node_id
roster_t::create_file_node(file_id const & content, node_id_source & nis)
{
  node_id nid = nis.next();
  file_t f = file_t(new file_node());
  f->self = nid;
  f->content = content;
  safe_insert(nodes, make_pair(nid, f));
  return nid;
}


void
roster_t::attach_node(node_id nid, split_path const & dst)
{
  split_path dirname;
  path_component basename;
  dirname_basename(dst, dirname, basename);

  node_t n = get_node(nid);

  // ensure the node is already detached (as best one can)
  I(null_node(n->parent));
  I(null_name(n->name));
  I(!null_node(n->self));

  if (dirname.empty())
    {
      // attaching the root dir
      I(null_name(basename));
      I(!has_root());     
      root_dir = downcast_to_dir_t(n);
    }
  else
    {
      // L(F("attaching into dir '%s'\n") % file_path(dirname));
      dir_t parent = downcast_to_dir_t(get_node(dirname));
      parent->attach_child(basename, n);
    }
}


void
roster_t::apply_delta(split_path const & pth,
                      file_id const & old_id,
                      file_id const & new_id)
{
  file_t f = downcast_to_file_t(get_node(pth));
  I(f->content == old_id);
  I(!null_node(f->self));
  I(!(f->content == new_id));
  f->content = new_id;
}


void
roster_t::clear_attr(split_path const & pth,
                     attr_key const & name)
{
  set_attr(pth, name, make_pair(false, attr_value()));
}


void
roster_t::set_attr(split_path const & pth,
                   attr_key const & name,
                   attr_value const & val)
{
  set_attr(pth, name, make_pair(true, val));
}


void
roster_t::set_attr(split_path const & pth,
                   attr_key const & name,
                   pair<bool, attr_value> const & val)
{
  I(val.first || val.second().empty());
  node_t n = get_node(pth);
  I(!null_node(n->self));
  full_attr_map_t::iterator i = n->attrs.find(name);
  if (i == n->attrs.end())
    i = safe_insert(n->attrs, make_pair(name,
					make_pair(false, attr_value())));
  I(i->second != val);
  i->second = val;
}

marking_t::marking_t(revision_id const & birth_rid, node_t n)
{
  set<revision_id> singleton;
  singleton.insert(birth_rid);
  parent_name = singleton;
  file_content = singleton;
  for (full_attr_map_t::const_iterator i = n->attrs.begin();
       i != n->attrs.end(); ++i)
    attrs.insert(make_pair(i->first, singleton));
}


struct
dfs_iter
{
  
  dir_t root;
  bool return_root;
  stack< pair<dir_t, dir_map::const_iterator> > stk;
  split_path dirname;

  dfs_iter(dir_t r) 
    : root(r), return_root(true)
  {
    if (!root->children.empty())
      stk.push(make_pair(root, root->children.begin()));
  }

  void path(split_path & pv)
  {
    I(!finished());
    if (return_root)
      {
	pv.clear();
	pv.push_back(the_null_component);
      }
    else
      {
	I(!stk.empty());
	pv = dirname;
	pv.push_back(stk.top().second->first);
      }
  }

  bool finished()
  {
    return (!return_root) && stk.empty();
  }

  node_t operator*()
  {
    if (return_root)
      {
	return_root = false;
	return root;
      }
    else
      {
	I(!stk.empty());
	return stk.top().second->second;
      }
  }

  void operator++()
  {
    if (finished())
      return;
    
    return_root = false;

    node_t ntmp = stk.top().second->second;
    if (is_dir_t(ntmp))
      {
        dirname.push_back(stk.top().second->first);
	dir_t dtmp = downcast_to_dir_t(ntmp);
	stk.push(make_pair(dtmp, dtmp->children.begin()));
      }
    else
      ++(stk.top().second);

    while (!stk.empty()
	   && stk.top().second == stk.top().first->children.end())
      {
	stk.pop();
	if (!dirname.empty())
	  dirname.pop_back();
	if (!stk.empty())
	  ++stk.top().second;
      }
  }
};


void
roster_t::check_finite_depth() const
{
  I(has_root());
  size_t maxdepth = nodes.size(); 
  for (dfs_iter i(root_dir); !i.finished(); ++i)
    I(maxdepth-- > 0);
}


void
roster_t::check_sane(marking_map const & marking) const
{
  node_map::const_iterator ri;
  marking_map::const_iterator mi;

  I(has_root());

  for (ri = nodes.begin(), mi = marking.begin();
       ri != nodes.end() && mi != marking.end();
       ++ri, ++mi)
  {
    I(ri->first == mi->first);
    node_id nid = ri->first;
    I(!null_node(nid) && !temp_node(nid));
    node_t n = ri->second;
    I(n->self == nid);
    if (is_dir_t(n))
      {
        if (null_name(n->name) || null_node(n->parent))
	  I(null_name(n->name) && null_node(n->parent));
	else
	  I(!null_name(n->name) && !null_node(n->parent));
      }
    else
      {
	I(!null_name(n->name) && !null_node(n->parent));
	!null_id(downcast_to_file_t(n)->content);
      }
    I(!null_id(n->birth_revision));
    for (full_attr_map_t::const_iterator i = n->attrs.begin(); i != n->attrs.end(); ++i)
      I(i->second.first || !i->second.second().empty());
    if (n != root_dir)
      I(downcast_to_dir_t(get_node(n->parent))->get_child(n->name) == n);
  }

  I(ri == nodes.end() && mi == marking.end());
  check_finite_depth();
}


namespace 
{

  // adaptor class to enable cset application on rosters.
  class editable_roster_base 
    : public editable_tree
  {
  public:
    editable_roster_base(roster_t & r, node_id_source & nis)
      : r(r), nis(nis)
      {}

    virtual node_id detach_node(split_path const & src)
    {
      // L(F("detach_node('%s')") % file_path(src)); 
      return r.detach_node(src);
    }
    virtual void drop_detached_node(node_id nid)
    {
      // L(F("drop_detached_node(%d)") % nid); 
      r.drop_detached_node(nid);
    }
    virtual node_id create_dir_node()
    {
      // L(F("create_dir_node()\n")); 
      node_id n = r.create_dir_node(nis);
      // L(F("create_dir_node() -> %d\n") % n); 
      return n;
    }
    virtual node_id create_file_node(file_id const & content)
    {
      // L(F("create_file_node('%s')\n") % content); 
      node_id n = r.create_file_node(content, nis);
      // L(F("create_file_node('%s') -> %d\n") % content % n); 
      return n;
    }
    virtual void attach_node(node_id nid, split_path const & dst)
    {
      // L(F("attach_node(%d, '%s')") % nid % file_path(dst));
      r.attach_node(nid, dst);
    }
    virtual void apply_delta(split_path const & pth, 
                             file_id const & old_id, 
                             file_id const & new_id)
    {
      // L(F("clear_attr('%s', '%s', '%s')") % file_path(pth) % old_id % new_id);
      r.apply_delta(pth, old_id, new_id);
    }
    virtual void clear_attr(split_path const & pth,
                            attr_key const & name)
    {
      // L(F("clear_attr('%s', '%s')") % file_path(pth) % name);
      r.clear_attr(pth, name);
    }
    virtual void set_attr(split_path const & pth,
                          attr_key const & name,
                          attr_value const & val)
    {
      // L(F("set_attr('%s', '%s', '%s')") % file_path(pth) % name % val);
      r.set_attr(pth, name, val);
    }
  protected:
    roster_t & r;
    node_id_source & nis;
  };

  struct testing_node_id_source : public node_id_source
  {
    testing_node_id_source() : curr(first_node) {}
    virtual node_id next()
    {
      // L(F("creating node %x\n") % curr);
      node_id n = curr++;
      I(!temp_node(n));
      return n;
    }
    node_id curr;
  };

  struct temp_node_id_source : public node_id_source
  {
    temp_node_id_source() : curr(first_temp_node) {}
    virtual node_id next()
    {
      node_id n = curr++;
      I(temp_node(n));
      return n;
    }
    node_id curr;
  };

  struct true_node_id_source : public node_id_source
  {
    true_node_id_source(app_state & app) : app(app) {}
    virtual node_id next()
    {
      node_id n = app.db.next_node_id();
      I(!temp_node(n));
      return n;
    }
    app_state & app;
  };

  class editable_roster_for_merge : editable_roster_base
  {
  public:
    set<node_id> new_nodes;
    virtual node_id create_dir_node()
    {
      node_id nid = this->editable_roster_base::create_dir_node();
      new_nodes.insert(nid);
      return nid;
    }
    virtual node_id create_file_node(file_id const & content)
    {
      node_id nid = this->editable_roster_base::create_file_node(content);
      new_nodes.insert(nid);
      return nid;
    }
  };

  // this handles all the stuff in a_new
  void unify_roster_oneway(roster_t & a, set<node_id> & a_new,
                           roster_t & b, set<node_id> & b_new,
                           set<node_id> & new_ids,
                           node_id_source & nis)
  {
    for (set<node_id>::const_iterator i = a_new.begin(); i != a_new.end(); ++i)
      {
        node_id const aid = *i;
        split_path sp;
        // SPEEDUP?: climb out only so far as is necessary to find a shared
        // id?  possibly faster (since usually will get a hit immediately),
        // but may not be worth the effort (since it doesn't take that long to
        // get out in any case)
        a.get_name(aid, sp);
        node_id bid = b.get_node(aid)->self;
        if (temp_node(bid))
          {
            node_id new_nid = nis.next();
            a.replace_node_id(aid, new_nid);
            b.replace_node_id(bid, new_nid);
            new_ids.insert(new_nid);
            b_new.erase(bid);
          }
        else
          {
            a.replace_node_id(aid, bid);
            a.get_node(bid)->birth_revision = b.get_node(bid)->birth_revision;
          }
      }
  }

  // After this, left should == right, and there should be no temporary ids.
  // Destroys sets, because that's handy (it has to scan over both, but it can
  // skip some double-scanning)
  void
  unify_rosters(roster_t & left, set<node_id> & left_new,
                roster_t & right, set<node_id> & right_new,
                // these new_ids all come from the given node_id_source
                set<node_id> & new_ids,
                node_id_source & nis)
  {
    unify_roster_oneway(left, left_new, right, right_new, new_ids, nis);
    unify_roster_oneway(right, right_new, left, left_new, new_ids, nis);
  }

  // this function implements the case
  //   a   b1
  //    \ /
  //     b2
  void
  mark_won_merge(set<revision_id> const & a_marks,
                 set<revision_id> const & a_uncommon_ancestors,
                 set<revision_id> const & b1_marks,
                 revision_id const & new_rid,
                 set<revision_id> & new_marks)
  {
    for (set<revision_id>::const_iterator i = a_marks.begin();
         i != a_marks.end(); ++i)
      {
        if (a_uncommon_ancestors.find(*i) != a_uncommon_ancestors.end())
          {
            // at least one element of *(a) is not an ancestor of b1
            new_marks.clear();
            new_marks.insert(new_rid);
            return;
          }
      }
    // all elements of *(a) are ancestors of b1; this was a clean merge to b,
    // so copy forward the marks.
    new_marks = b1_marks;
  }


  void
  mark_attrs(full_attr_map_t const & lattrs, 
             full_attr_map_t const & rattrs,
             marking_t const & lmarks, 
             marking_t const & rmarks,
             set<revision_id> const & left_uncommon_ancestors,
             set<revision_id> const & right_uncommon_ancestors,

             // We are in the process of marking a new revision, so we take
             // its rev id and its new attrs, plus the new marking for the
             // attrs (which we write to in this function).
             revision_id const & new_rid,
             full_attr_map_t const & attrs, 
             marking_t & marks)
    {
      I(marks.attrs.empty());
      for (full_attr_map_t::const_iterator j = attrs.begin(); j != attrs.end();
           ++j)
        {
          full_attr_map_t::const_iterator lai = lattrs.find(j->first);
          full_attr_map_t::const_iterator rai = rattrs.find(j->first);

          I(marks.attrs.find(j->first) == marks.attrs.end());

          // Neither left nor right have ever seen this attr, so it was
          // new in this rev. We make a new marking set for it and add the
          // current rev to the marking set.
          if (lai == lattrs.end() && rai == rattrs.end())
            safe_insert(marks.attrs[j->first], new_rid);

          // Only the right side has ever seen this attr, so the right side
          // won merging.
          else if (lai == lattrs.end() && rai != rattrs.end())
            {
              // Two sub-possibilities:               
              if (j->second == rai->second)
                {
                  // 1. The right edge is of the form a->a, and represents no decision
                  // on the part of the user, just a propagation of an existing state.
                  // In this case we carry the old mark-set forward from the right marking.
                  safe_insert(marks.attrs, make_pair(j->first, 
                                                     safe_get(rmarks.attrs, j->first)));
                }
              else
                
                {
                  // 2. The right edge represents a change to the attr value --
                  // thus a decision on the part of the user -- in which case
                  // we need to set the new mark-set to {new_rid}
                  safe_insert(marks.attrs[j->first], new_rid);
                }
            }

          // Only the left side has ever seen this attr, so the left
          // side won merging.
          else if (lai != lattrs.end() && rai == rattrs.end())
            {
              // Same two sub-cases here as above:
              if (j->second == lai->second)
                safe_insert(marks.attrs, make_pair(j->first, 
                                                   safe_get(lmarks.attrs, j->first)));
              else
                safe_insert(marks.attrs[j->first], new_rid);
            }

          // Otherwise both sides have seen this attr, and we need to look at
          // both old values.
          else
            {
              bool diff_from_left = (j->second != lai->second);
              bool diff_from_right = (j->second != rai->second);

              // If the merged attr value differs from both inputs, the
              // user "expressed a preference" by making a new setting, so
              // we make the marking set for the new attr value contain
              // only the new rev.
              if (diff_from_left && diff_from_right)
                safe_insert(marks.attrs[j->first], new_rid);

              // If the merged attr is equal to one side of the merge input,
              // we must ask for help in determining what to do with the 
              // marks.
              else if (diff_from_left && !diff_from_right)
                mark_won_merge(safe_get(lmarks.attrs, j->first),
                               left_uncommon_ancestors,
                               safe_get(rmarks.attrs, j->first),
                               new_rid, marks.attrs[j->first]);

              else if (!diff_from_left && diff_from_right)
                mark_won_merge(safe_get(rmarks.attrs, j->first),
                               right_uncommon_ancestors,
                               safe_get(lmarks.attrs, j->first),
                               new_rid, marks.attrs[j->first]);

              // Otherwise the merged attr is the same as both ancestors,
              // meaning we have a clean merge in which the user said
              // nothing; we must preserve (union) the mark sets of both
              // inputs.
              else
                {
                  set<revision_id> const & lam = safe_get(lmarks.attrs, j->first);
                  set<revision_id> const & ram = safe_get(rmarks.attrs, j->first);
                  set<revision_id> res;
                  set_union(lam.begin(), lam.end(),
                            ram.begin(), ram.end(),
                            inserter(res, res.begin()));
                  safe_insert(marks.attrs, make_pair(j->first, res));
                }
            }
        }
    }

  // take care of marking a single node both of whose parents exist
  void
  mark_nontrivial_node(node_t ln, 
                       node_t rn,
                       marking_t const & lmarks, 
                       marking_t const & rmarks,
                       set<revision_id> left_uncommon_ancestors,
                       set<revision_id> right_uncommon_ancestors,
                       
                       // We are in the process of marking a new revision,
                       // so we take its rev id and the new node, plus the
                       // new marking for the node (which we write to in
                       // this function).
                       revision_id const & new_rid,
                       node_t n, 
                       marking_t & marks)
  {
    // name
    {
      bool diff_from_left = (n->parent != ln->parent || n->name != ln->name);
      bool diff_from_right = (n->parent != rn->parent || n->name != rn->name);

      if (diff_from_left && diff_from_right)
        marks.parent_name.insert(new_rid);

      else if (diff_from_left && !diff_from_right)
        mark_won_merge(lmarks.parent_name, left_uncommon_ancestors,
                       rmarks.parent_name, new_rid,
                       marks.parent_name);

      else if (!diff_from_left && diff_from_right)
        mark_won_merge(rmarks.parent_name, right_uncommon_ancestors,
                       lmarks.parent_name, new_rid,
                       marks.parent_name);
      else
        {
          // this is the case
          //   a   a
          //    \ /
          //     a
          // so we simply union the mark sets.  This is technically not
          // quite the canonical multi-*-merge thing to do; in the case
          //     a1*
          //    / \      (blah blah; avoid multi-line-comment warning)
          //   b   a2
          //   |   |
          //   a3* |
          //    \ /
          //     a4
          // we will set *(a4) = {a1, a3}, even though the minimal
          // common ancestor set is {a3}.  we could fix this by running
          // erase_ancestors.  However, there isn't really any point;
          // the only operation performed on *(a4) is to test *(a4) > R
          // for some revision R.  The truth-value of this test cannot
          // be affected by added new revisions to *(a4) that are
          // ancestors of revisions that are already in *(a4).
          set_union(lmarks.parent_name.begin(), lmarks.parent_name.end(),
                    rmarks.parent_name.begin(), rmarks.parent_name.end(),
                    inserter(marks.parent_name, marks.parent_name.begin()));
        }
    }
    // content
    if (is_file_t(n))
      {
        file_t f = downcast_to_file_t(n);
        file_t lf = downcast_to_file_t(ln);
        file_t rf = downcast_to_file_t(rn);

        bool diff_from_left = (!(f->content == lf->content));
        bool diff_from_right = (!(f->content == rf->content));

        if (diff_from_left && diff_from_right)
          marks.file_content.insert(new_rid);

        else if (diff_from_left && !diff_from_right)
          mark_won_merge(lmarks.file_content, left_uncommon_ancestors,
                         rmarks.file_content, new_rid,
                         marks.file_content);

        else if (!diff_from_left && diff_from_right)
          mark_won_merge(rmarks.file_content, right_uncommon_ancestors,
                         lmarks.file_content, new_rid,
                         marks.file_content);

        else
          set_union(lmarks.file_content.begin(), lmarks.file_content.end(),
                    rmarks.file_content.begin(), rmarks.file_content.end(),
                    inserter(marks.file_content, marks.file_content.begin()));
      }
    // attrs are pain, and thus get their own function
    mark_attrs(ln->attrs, rn->attrs, 
               lmarks, rmarks,
               left_uncommon_ancestors, right_uncommon_ancestors,
               new_rid, n->attrs, marks);
  }
}



/// Remainder is not yet compiling, so commented out
/*

namespace 
{
  // this function is also responsible for verifying ancestry invariants --
  // those invariants on a roster that involve the structure of the roster's
  // parents, rather than just the structure of the roster itself.
  void
  mark_merge_roster(roster_t const & left_r, roster_t const & right_r,
                    marking_map const & left_marking,
                    marking_map const & right_marking,
                    set<revision_id> const & left_uncommon_ancestors,
                    set<revision_id> const & right_uncommon_ancestors,
                    revision_id const & new_rid,
                    roster_t const & merge, 
                    marking_map & marking)
  {
    for (map<node_id, node_t>::const_iterator i = merge.all_nodes().begin();
         i != merge.all_nodes().end(); ++i)
      {
        node_t const & n = i->second;
        // SPEEDUP?: instead of using find repeatedly, iterate everything in
        // parallel
        map<node_id, node_t>::const_iterator lni = left_r.all_nodes().find(i->first);
        map<node_id, node_t>::const_iterator rni = right_r.all_nodes().find(i->first);
        marking_map::const_iterator lmi = left_marking.find(i->first);
        marking_map::const_iterator rmi = right_marking.find(i->first);
        bool exists_in_left = (lni != left_r.all_nodes().end());
        bool exists_in_right = (rni != right_r.all_nodes().end());
        if (!exists_in_left && !exists_in_right)
          {
            marking.insert(make_pair(i->first, marking_t(new_rid)));
            I(n.birth_revision == new_rid);
          }
        else if (!exists_in_left && exists_in_right)
          {
            marking.insert(*rni);
            node_t const & rn = rni->second;
            I(n.type == rn.type && n.birth_revision == rn.birth_revision);
            I(right_uncommon_ancestors.find(n.birth_revision)
              != right_uncommon_ancestors.end());
          }
        else if (exists_in_left && !exists_in_right)
          {
            marking.insert(*lni);
            node_t const & ln = lni->second;
            I(n.type == ln.type && n.birth_revision == ln.birth_revision);
            I(left_uncommon_ancestors.find(n.birth_revision)
              != left_uncommon_ancestors.end());
          }
        else
          {
            node_t const & ln = lni->second;
            node_t const & rn = rni->second;
            I(n.type == rn.type && n.birth_revision == rn.birth_revision);
            I(n.type == ln.type && n.birth_revision == ln.birth_revision);
            marking_t marks;
            marking_t const & lmarks = lmi->second;
            marking_t const & rmarks = rmi->second;
            mark_nontrivial_node(ln, rn, lmi->second, rmi->second,
                                 left_uncommon_ancestors, right_uncommon_ancestors,
                                 n, marks);
            // attributes can never be deleted.
            // this is kinda inefficent, but very rarely will any node have
            // more than 1 attribute.
            for (full_attr_map_t::const_iterator j = ln.attrs.begin();
                 j != ln.attrs.end(); ++j)
              I(n.attrs.find(j->first) != n.attrs.end());
            for (full_attr_map_t::const_iterator j = rn.attrs.begin();
                 j != rn.attrs.end(); ++j)
              I(n.attrs.find(j->first) != n.attrs.end());
          }
      }
  }             
}

void
make_roster_for_merge(cset const & left_cs, revision_id const & left_rid,
                      cset const & right_cs, revision_id const & right_rid,
                      revision_id const & new_rid,
                      roster_t & result, marking_map & marking, app_state & app)
{
  I(!null_id(left_rid) && !null_id(right_rid));
  roster_t left_r, right_r;
  marking_map left_marking, right_marking;
  app.db.get_roster(left_rid, left_r, left_marking);
  app.db.get_roster(right_rid, right_r, right_marking);
  {
    temp_node_id_source nis;
    // SPEEDUP?: the copies on the next two lines are probably the main
    // bottleneck in this code
    result = left_r;
    roster_t from_right_r(right_r);
    editable_roster from_left_er(result, nis), from_right_er(from_right_r, nis);
    left_cs.apply_to(from_left_er);
    right_cs.apply_to(from_right_er);
    set<node_id> new_ids;
    unify_rosters(result, from_left_er.new_nodes,
                  from_right_r, from_right_er.new_nodes,
                  new_ids, true_node_id_source(app));
    I(result == new_from_right);
  }
  // SPEEDUP?: instead of constructing new marking from scratch, track which
  // nodes were modified, and scan only them
  // load one of the parent markings directly into the new marking map
  marking.clear();
  set<revision_id> left_uncommon_ancestors, right_uncommon_ancestors;
  app.db.get_uncommon_ancestors(left_rid, right_rid,
                                left_uncommon_ancestors, right_uncommon_ancestors);
  mark_merge_roster(left_r, right_r, left_marking, right_marking,
                    left_uncommon_ancestors, right_uncommon_ancestors,
                    result, marking);
}

namespace
{
  class editable_roster_for_nonmerge : editable_roster_base
  {
  public:
    editable_roster_for_nonmerge(roster_t & r, node_id_source & nis,
                                 revision_id const & rid,
                                 marking_map & marking)
      : editable_roster_base(r, nis),
        rid(rid), marking(marking)
    {}
    virtual node_id detach_node(split_path const & src)
    {
      node_id nid = this->editable_roster_base::detach_node(nid);
      marking_t & marks = safe_get(marking, nid);
      marks.parent_name.clear();
      marks.parent_name.insert(rid);
      return nid;
    }
    virtual void drop_detached_node(node_id nid)
    {
      this->editable_roster_base::drop_detached_node(nid);
      safe_erase(marking, nid);
    }
    node_id handle_new(node_id nid)
    {
      node_t & n = r.node(nid);
      n.birth_revision = rid;
      marking.insert(make_pair(nid, marking_t(rid, n)));
      return nid;
    }
    virtual node_id create_dir_node()
    {
      return handle_new(this->editable_roster_base::create_dir_node());
    }
    virtual node_id create_file_node()
    {
      return handle_new(this->editable_roster_base::create_file_node());
    }
    virtual void apply_delta(split_path const & pth,
                             file_id const & old_id, file_id const & new_id)
    {
      this->editable_roster_base::apply_delta(pth, old_id, new_id);
      marking_t & marks = safe_get(marking, r.lookup(pth));
      marks.file_content.clear();
      marks.file_content.insert(rid);
    }
    void handle_attr(split_path const & pth, attr_key const & name)
    {
      marking_t & marks = safe_get(marking, r.lookup(pth));
      if (marks.attrs.find(name) == marks.attrs.end())
        marks.attrs.insert(make_pair(name, set<revision_id>()));
      set<revision_id> & markset = safe_get(marks.attrs, name);
      markset.clear();
      markset.insert(rid);
    }
    virtual void clear_attr(split_path const & pth, attr_key const & name)
    {
      this->editable_roster_base::clear_attr(pth, name);
      handle_attr(pth, name);
    }
    virtual void set_attr(split_path const & pth, attr_key const & name,
                          attr_value const & val);
    {
      this->editable_roster_base::set_attr(pth, name, val);
      handle_attr(pth, name);
    }
  private:
    revision_id const & rid;
    // marking starts out as the parent's marking
    marking_map & marking;
  };
}

void
make_roster_for_nonmerge(cset const & cs, revision_id const & parent_rid,
                         revision_id const & new_rid,
                         roster_t & result, marking_map & marking, app_state & app)
{
  roster_t parent_r;
  app.db.get_roster(parent_rid, result, marking);
  true_node_id_source nis(app.db);
  editable_roster_for_nonmerge er(result, nis, new_rid, marking);
  cs.apply_to(er);
}

void
make_roster_for_revision(revision_set const & rev, revision_id const & rid,
                         roster_t & result, marking_map & marking, app_state & app)
{
  if (rev.edges.size() == 1)
    make_roster_for_nonmerge(rev.edges.begin()->second,
                             rev.edges.begin()->first,
                             rid, result, marking, app);
  else if (rev.edges.size() == 2)
    {
      edge_map::iterator i = rev.edges.begin();
      revision_id const & left_rid = i->first;
      cset const & left_cs = i->second;
      ++i;
      revision_id const & right_rid = i->first;
      cset const & left_cs = i->second;
      make_roster_for_merge(left_cs, left_rid, right_cs, right_rid,
                            rid, result, marking, app);
    }
  else
    I(false);
  result.check_sane(marking);
}
*/





////////////////////////////////////////////////////////////////////
//   testing
////////////////////////////////////////////////////////////////////

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"
#include "sanity.hh"
#include "constants.hh"

#include <string>
#include <boost/lexical_cast.hpp>

using std::string;
using boost::lexical_cast;

template<typename M>
typename M::const_iterator 
random_element(M const & m)
{
  size_t i = rand() % m.size();
  typename M::const_iterator j = m.begin();
  while (i > 0)
    {
      I(j != m.end());
      --i; 
      ++j;
    }
  return j;
}

struct
change_automaton
{

  change_automaton()
  {
    srand(0x12345678);
  }

  string new_word()
  {
    static string wordchars = "abcdefghijlkmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static unsigned tick = 0;
    string tmp;
    do
      {
        tmp += wordchars[rand() % wordchars.size()];
      }
    while (tmp.size() < 10 && !flip(10));
    return tmp + lexical_cast<string>(tick++);
  }

  file_id new_ident()
  {
    static string tab = "0123456789abcdef";
    string tmp;
    tmp.reserve(constants::idlen);
    for (unsigned i = 0; i < constants::idlen; ++i)
      tmp += tab[rand() % tab.size()];
    return file_id(tmp);
  }

  path_component new_component()
  {
    vector<path_component> pieces;
    file_path_internal(new_word()).split(pieces);
    return pieces.back();
  }

  bool flip(unsigned n = 2)
  {
    return (rand() % n) == 0;
  }

  attr_key pick_attr(full_attr_map_t const & attrs)
  {
    return random_element(attrs)->first;
  }

  attr_key pick_attr(attr_map_t const & attrs)
  {
    return random_element(attrs)->first;
  }

  bool parent_of(split_path const & p,
                 split_path const & c)
  {
    bool is_parent = false;

    if (p.size() <= c.size())
      {
        split_path::const_iterator c_anchor = 
          search(c.begin(), c.end(),
                 p.begin(), p.end());
        
        is_parent = (c_anchor == c.begin());
      }

    //     L(F("path '%s' is%s parent of '%s'")
    //       % file_path(p)
    //       % (is_parent ? "" : " not")
    //       % file_path(c));
    
    return is_parent;      
  }

  void perform_random_action(roster_t & r, node_id_source & nis)
  {
    cset c;
    while (c.empty())
       {
        if (r.all_nodes().empty())
          {
            // Must add, couldn't find anything to work with
            split_path root;
            root.push_back(the_null_component);
            c.dirs_added.insert(root);
          }
        else
          {
            node_t n = random_element(r.all_nodes())->second;
            split_path pth;
            r.get_name(n->self, pth);
            // L(F("considering acting on '%s'\n") % file_path(pth));

            switch (rand() % 7)
              {
              default:
              case 0:
              case 1:
              case 2:
                if (is_file_t(n) || (pth.size() > 1 && flip()))
                  // Add a sibling of an existing entry.
                  pth[pth.size() - 1] = new_component();
                
                else 
                  // Add a child of an existing entry.
                  pth.push_back(new_component());
                
                if (flip())
                  {
                    // L(F("adding dir '%s'\n") % file_path(pth));
                    safe_insert(c.dirs_added, pth);
                  }
                else
                  {
                    // L(F("adding file '%s'\n") % file_path(pth));
                    safe_insert(c.files_added, make_pair(pth, new_ident()));
                  }
                break;

              case 3:
                if (is_file_t(n))
                  {
                    safe_insert(c.deltas_applied, 
                                make_pair
                                (pth, make_pair(downcast_to_file_t(n)->content,
                                                new_ident())));
                  }
                break;

              case 4:
                {
                  node_t n2 = random_element(r.all_nodes())->second;
                  split_path pth2;
                  r.get_name(n2->self, pth2);

                  if (n == n2)
                    continue;
                  
                  if (is_file_t(n2) || (pth2.size() > 1 && flip()))
                    {
                      // L(F("renaming to a sibling of an existing entry '%s'\n") % file_path(pth2));
                      // Move to a sibling of an existing entry.
                      pth2[pth2.size() - 1] = new_component();
                    }
                  
                  else
                    {
                      // L(F("renaming to a child of an existing entry '%s'\n") % file_path(pth2));
                      // Move to a child of an existing entry.
                      pth2.push_back(new_component());
                    }
                  
                  if (!parent_of(pth, pth2))
                    {
                      // L(F("renaming '%s' -> '%s\n") % file_path(pth) % file_path(pth2));
                      safe_insert(c.nodes_renamed, make_pair(pth, pth2));
                    }
                }
                break;
                
              case 5:
                if (!null_node(n->parent) && 
                    (is_file_t(n) || downcast_to_dir_t(n)->children.empty()))
                  {
                    // L(F("deleting '%s'\n") % file_path(pth));
                    safe_insert(c.nodes_deleted, pth);
                  }
                break;
                
              case 6:
                if (!n->attrs.empty() && flip())
                  {
                    attr_key k = pick_attr(n->attrs);
                    if (safe_get(n->attrs, k).first)
                      {
                        // L(F("clearing attr on '%s'\n") % file_path(pth));
                        safe_insert(c.attrs_cleared, make_pair(pth, k));
                      }
                  }
                else
                  {
                    // L(F("setting attr on '%s'\n") % file_path(pth));
                    safe_insert(c.attrs_set, make_pair(make_pair(pth, new_word()), new_word()));
                  }
                break;                
              }
          }
      }
    // now do it
    editable_roster_base e = editable_roster_base(r, nis);
    c.apply_to(e);
  }
};


static void
automaton_roster_test()
{
  roster_t r1;
  change_automaton aut;
  testing_node_id_source nis;

  for (int i = 0; i < 100000; ++i)
    {
      if (i < 500 || i % 500 == 0)
        P(F("performing random action %d\n") % i);
      aut.perform_random_action(r1, nis);
    }
}


void
add_roster_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&automaton_roster_test));
}


#endif // BUILD_UNIT_TESTS
