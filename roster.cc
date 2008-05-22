// Copyright (C) 2005 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <algorithm>
#include <stack>
#include <set>
#include "vector.hh"
#include <sstream>

#include "basic_io.hh"
#include "cset.hh"
#include "database.hh"
#include "platform-wrapped.hh"
#include "roster.hh"
#include "revision.hh"
#include "vocab.hh"
#include "transforms.hh"
#include "simplestring_xform.hh"
#include "lexical_cast.hh"
#include "file_io.hh"
#include "parallel_iter.hh"
#include "restrictions.hh"
#include "safe_map.hh"
#include "ui.hh"

using std::inserter;
using std::make_pair;
using std::map;
using std::ostringstream;
using std::pair;
using std::reverse;
using std::set;
using std::set_union;
using std::stack;
using std::string;
using std::vector;

using boost::lexical_cast;

///////////////////////////////////////////////////////////////////

template <> void
dump(node_id const & val, string & out)
{
  out = lexical_cast<string>(val);
}

template <> void
dump(full_attr_map_t const & val, string & out)
{
  ostringstream oss;
  for (full_attr_map_t::const_iterator i = val.begin(); i != val.end(); ++i)
    oss << "attr key: '" << i->first << "'\n"
        << "  status: " << (i->second.first ? "live" : "dead") << '\n'
        << "   value: '" << i->second.second << "'\n";
  out = oss.str();
}

template <> void
dump(set<revision_id> const & revids, string & out)
{
  out.clear();
  bool first = true;
  for (set<revision_id>::const_iterator i = revids.begin();
       i != revids.end(); ++i)
    {
      if (!first)
        out += ", ";
      first = false;
      out += encode_hexenc(i->inner()());
    }
}

template <> void
dump(marking_t const & marking, string & out)
{
  ostringstream oss;
  string tmp;
  oss << "birth_revision: " << marking.birth_revision << '\n';
  dump(marking.parent_name, tmp);
  oss << "parent_name: " << tmp << '\n';
  dump(marking.file_content, tmp);
  oss << "file_content: " << tmp << '\n';
  oss << "attrs (number: " << marking.attrs.size() << "):\n";
  for (map<attr_key, set<revision_id> >::const_iterator
         i = marking.attrs.begin(); i != marking.attrs.end(); ++i)
    {
      dump(i->second, tmp);
      oss << "  " << i->first << ": " << tmp << '\n';
    }
  out = oss.str();
}

template <> void
dump(marking_map const & markings, string & out)
{
  ostringstream oss;
  for (marking_map::const_iterator i = markings.begin();
       i != markings.end();
       ++i)
    {
      oss << "Marking for " << i->first << ":\n";
      string marking_str, indented_marking_str;
      dump(i->second, marking_str);
      prefix_lines_with("    ", marking_str, indented_marking_str);
      oss << indented_marking_str << '\n';
    }
  out = oss.str();
}

namespace
{
  //
  // We have a few concepts of "nullness" here:
  //
  // - the_null_node is a node_id. It does not correspond to a real node;
  //   it's an id you use for the parent of the root, or of any node which
  //   is detached.
  //
  // - the root node has a real node id, just like any other directory.
  //
  // - the path_component whose string representation is "", the empty
  //   string, is the *name* of the root node.  write it as
  //   path_component() and test for it with component.empty().
  //
  // - similarly, the file_path whose string representation is "" also
  //   names the root node.  write it as file_path() and test for it
  //   with path.empty().
  //
  // - there is no file_path or path_component corresponding to the_null_node.
  //
  // We do this in order to support the notion of moving the root directory
  // around, or applying attributes to the root directory.  Note that the
  // only supported way to move the root is with the 'pivot_root' operation,
  // which atomically turns the root directory into a subdirectory and some
  // existing subdirectory into the root directory.  This is an UI constraint,
  // not a constraint at this level.

  const node_id first_node = 1;
  const node_id first_temp_node = widen<node_id, int>(1) << (sizeof(node_id) * 8 - 1);
  inline bool temp_node(node_id n)
  {
    return n & first_temp_node;
  }
}


node::node(node_id i)
  : self(i),
    parent(the_null_node),
    name()
{
}


node::node()
  : self(the_null_node),
    parent(the_null_node),
    name()
{
}


dir_node::dir_node(node_id i)
  : node(i)
{
}


dir_node::dir_node()
  : node()
{
}


bool
dir_node::has_child(path_component const & pc) const
{
  return children.find(pc) != children.end();
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
  I(child->name.empty());
  safe_insert(children, make_pair(pc, child));
  child->parent = this->self;
  child->name = pc;
}


node_t
dir_node::detach_child(path_component const & pc)
{
  node_t n = get_child(pc);
  n->parent = the_null_node;
  n->name = path_component();
  safe_erase(children, pc);
  return n;
}


node_t
dir_node::clone()
{
  dir_t d = dir_t(new dir_node(self));
  d->parent = parent;
  d->name = name;
  d->attrs = attrs;
  d->children = children;
  return d;
}


file_node::file_node(node_id i, file_id const & f)
  : node(i),
    content(f)
{
}


file_node::file_node()
  : node()
{
}


node_t
file_node::clone()
{
  file_t f = file_t(new file_node(self, content));
  f->parent = parent;
  f->name = name;
  f->attrs = attrs;
  return f;
}

template <> void
dump(node_t const & n, string & out)
{
  ostringstream oss;
  string name;
  dump(n->name, name);
  oss << "address: " << n << " (uses: " << n.use_count() << ")\n"
      << "self: " << n->self << '\n'
      << "parent: " << n->parent << '\n'
      << "name: " << name << '\n';
  string attr_map_s;
  dump(n->attrs, attr_map_s);
  oss << "attrs:\n" << attr_map_s;
  oss << "type: ";
  if (is_file_t(n))
    {
      oss << "file\ncontent: "
          << downcast_to_file_t(n)->content
          << '\n';
    }
  else
    {
      oss << "dir\n";
      dir_map const & c = downcast_to_dir_t(n)->children;
      oss << "children: " << c.size() << '\n';
      for (dir_map::const_iterator i = c.begin(); i != c.end(); ++i)
        {
          dump(i->first, name);
          oss << "  " << name << " -> " << i->second << '\n';
        }
    }
  out = oss.str();
}

// helper
void
roster_t::do_deep_copy_from(roster_t const & other)
{
  MM(*this);
  MM(other);
  I(!root_dir);
  I(nodes.empty());
  for (node_map::const_iterator i = other.nodes.begin(); i != other.nodes.end();
       ++i)
    hinted_safe_insert(nodes, nodes.end(), make_pair(i->first, i->second->clone()));
  for (node_map::iterator i = nodes.begin(); i != nodes.end(); ++i)
    if (is_dir_t(i->second))
      {
        dir_map & children = downcast_to_dir_t(i->second)->children;
        for (dir_map::iterator j = children.begin(); j != children.end(); ++j)
          j->second = safe_get(nodes, j->second->self);
      }
  if (other.root_dir)
    root_dir = downcast_to_dir_t(safe_get(nodes, other.root_dir->self));
}

roster_t::roster_t(roster_t const & other)
{
  do_deep_copy_from(other);
}

roster_t &
roster_t::operator=(roster_t const & other)
{
  root_dir.reset();
  nodes.clear();
  do_deep_copy_from(other);
  return *this;
}


struct
dfs_iter
{

  dir_t root;
  string curr_path;
  bool return_root;
  bool track_path;
  stack< pair<dir_t, dir_map::const_iterator> > stk;


  dfs_iter(dir_t r, bool t = false)
    : root(r), return_root(root), track_path(t)
  {
    if (root && !root->children.empty())
      stk.push(make_pair(root, root->children.begin()));
  }


  bool finished() const
  {
    return (!return_root) && stk.empty();
  }


  string const & path() const
  {
    I(track_path);
    return curr_path;
  }


  node_t operator*() const
  {
    I(!finished());
    if (return_root)
      return root;
    else
      {
        I(!stk.empty());
        return stk.top().second->second;
      }
  }

private:
  void advance_top()
  {
    int prevsize = 0;
    int nextsize = 0;
    if (track_path)
      {
        prevsize = stk.top().second->first().size();
      }

    ++stk.top().second;

    if (track_path)
      {
        if (stk.top().second != stk.top().first->children.end())
          nextsize = stk.top().second->first().size();

        int tmpsize = curr_path.size()-prevsize;
        I(tmpsize >= 0);
        curr_path.resize(tmpsize);
        if (nextsize != 0)
          curr_path.insert(curr_path.end(),
                           stk.top().second->first().begin(),
                           stk.top().second->first().end());
      }
  }
public:

  void operator++()
  {
    I(!finished());

    if (return_root)
      {
        return_root = false;
        if (!stk.empty())
          curr_path = stk.top().second->first();
        return;
      }

    // we're not finished, so we need to set up so operator* will return the
    // right thing.
    node_t ntmp = stk.top().second->second;
    if (is_dir_t(ntmp))
      {
        dir_t dtmp = downcast_to_dir_t(ntmp);
        stk.push(make_pair(dtmp, dtmp->children.begin()));

        if (track_path)
          {
            if (!curr_path.empty())
              curr_path += "/";
            if (!dtmp->children.empty())
              curr_path += dtmp->children.begin()->first();
          }
      }
    else
      {
        advance_top();
      }

    while (!stk.empty()
           && stk.top().second == stk.top().first->children.end())
      {
        stk.pop();
        if (!stk.empty())
          {
            if (track_path)
              {
                curr_path.resize(curr_path.size()-1);
              }
            advance_top();
          }
      }
  }
};


bool
roster_t::has_root() const
{
  return static_cast<bool>(root_dir);
}


inline bool
same_type(node_t a, node_t b)
{
  return is_file_t(a) == is_file_t(b);
}


inline bool
shallow_equal(node_t a, node_t b,
              bool shallow_compare_dir_children,
              bool compare_file_contents)
{
  if (a->self != b->self)
    return false;

  if (a->parent != b->parent)
    return false;

  if (a->name != b->name)
    return false;

  if (a->attrs != b->attrs)
    return false;

  if (! same_type(a,b))
    return false;

  if (is_file_t(a))
    {
      if (compare_file_contents)
        {
          file_t fa = downcast_to_file_t(a);
          file_t fb = downcast_to_file_t(b);
          if (!(fa->content == fb->content))
            return false;
        }
    }
  else
    {
      dir_t da = downcast_to_dir_t(a);
      dir_t db = downcast_to_dir_t(b);

      if (shallow_compare_dir_children)
        {
          if (da->children.size() != db->children.size())
            return false;

          dir_map::const_iterator
            i = da->children.begin(),
            j = db->children.begin();

          while (i != da->children.end() && j != db->children.end())
            {
              if (i->first != j->first)
                return false;
              if (i->second->self != j->second->self)
                return false;
              ++i;
              ++j;
            }
          I(i == da->children.end() && j == db->children.end());
        }
    }
  return true;
}


// FIXME_ROSTERS: why does this do two loops?  why does it pass 'true' to
// shallow_equal?
// -- njs
bool
roster_t::operator==(roster_t const & other) const
{
  node_map::const_iterator i = nodes.begin(), j = other.nodes.begin();
  while (i != nodes.end() && j != other.nodes.end())
    {
      if (i->first != j->first)
        return false;
      if (!shallow_equal(i->second, j->second, true))
        return false;
      ++i;
      ++j;
    }

  if (i != nodes.end() || j != other.nodes.end())
    return false;

  dfs_iter p(root_dir), q(other.root_dir);
  while (! (p.finished() || q.finished()))
    {
      if (!shallow_equal(*p, *q, true))
        return false;
      ++p;
      ++q;
    }

  if (!(p.finished() && q.finished()))
    return false;

  return true;
}

// This is exactly the same as roster_t::operator== (and the same FIXME
// above applies) except that it does not compare file contents.
bool
equal_shapes(roster_t const & a, roster_t const & b)
{
  node_map::const_iterator i = a.nodes.begin(), j = b.nodes.begin();
  while (i != a.nodes.end() && j != b.nodes.end())
    {
      if (i->first != j->first)
        return false;
      if (!shallow_equal(i->second, j->second, true, false))
        return false;
      ++i;
      ++j;
    }

  if (i != a.nodes.end() || j != b.nodes.end())
    return false;

  dfs_iter p(a.root_dir), q(b.root_dir);
  while (! (p.finished() || q.finished()))
    {
      if (!shallow_equal(*p, *q, true, false))
        return false;
      ++p;
      ++q;
    }

  if (!(p.finished() && q.finished()))
    return false;

  return true;
}

node_t
roster_t::get_node(file_path const & p) const
{
  MM(*this);
  MM(p);

  I(has_root());
  if (p.empty())
    return root_dir;

  dir_t nd = root_dir;
  string const & pi = p.as_internal();
  string::size_type start = 0, stop;
  for (;;)
    {
      stop = pi.find('/', start);
      path_component pc(pi, start, (stop == string::npos
                                    ? stop : stop - start));
      dir_map::const_iterator child = nd->children.find(pc);

      I(child != nd->children.end());
      if (stop == string::npos)
        return child->second;

      start = stop + 1;
      nd = downcast_to_dir_t(child->second);
    }
}

bool
roster_t::has_node(node_id n) const
{
  return nodes.find(n) != nodes.end();
}

bool
roster_t::is_root(node_id n) const
{
  return has_root() && root_dir->self == n;
}

bool
roster_t::is_attached(node_id n) const
{
  if (!has_root())
    return false;
  if (n == root_dir->self)
    return true;
  if (!has_node(n))
    return false;

  node_t node = get_node(n);

  return !null_node(node->parent);
}

bool
roster_t::has_node(file_path const & p) const
{
  MM(*this);
  MM(p);

  if (!has_root())
    return false;
  if (p.empty())
    return true;

  dir_t nd = root_dir;
  string const & pi = p.as_internal();
  string::size_type start = 0, stop;
  for (;;)
    {
      stop = pi.find('/', start);
      path_component pc(pi, start, (stop == string::npos
                                    ? stop : stop - start));
      dir_map::const_iterator child = nd->children.find(pc);

      if (child == nd->children.end())
        return false;
      if (stop == string::npos)
        return true;
      if (!is_dir_t(child->second))
        return false;

      start = stop + 1;
      nd = downcast_to_dir_t(child->second);
    }
}

node_t
roster_t::get_node(node_id nid) const
{
  return safe_get(nodes, nid);
}


void
roster_t::get_name(node_id nid, file_path & p) const
{
  I(!null_node(nid));

  stack<node_t> sp;
  size_t size = 0;

  while (nid != root_dir->self)
    {
      node_t n = get_node(nid);
      sp.push(n);
      size += n->name().length() + 1;
      nid = n->parent;
    }

  if (size == 0)
    {
      p.clear();
      return;
    }

  I(!bookkeeping_path::internal_string_is_bookkeeping_path(utf8(sp.top()->name())));

  string tmp;
  tmp.reserve(size);

  for (;;)
    {
      tmp += sp.top()->name();
      sp.pop();
      if (sp.empty())
        break;
      tmp += "/";
    }

  p = file_path(tmp, 0, string::npos);  // short circuit constructor
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


// this records the old location into the old_locations member, to prevent the
// same node from being re-attached at the same place.
node_id
roster_t::detach_node(file_path const & p)
{
  file_path dirname;
  path_component basename;
  p.dirname_basename(dirname, basename);

  I(has_root());
  if (basename.empty())
    {
      // detaching the root dir
      I(dirname.empty());
      node_id root_id = root_dir->self;
      safe_insert(old_locations, make_pair(root_id, make_pair(root_dir->parent,
                                                              root_dir->name)));
      // clear ("reset") the root_dir shared_pointer
      root_dir.reset();
      I(!has_root());
      return root_id;
    }

  dir_t parent = downcast_to_dir_t(get_node(dirname));
  node_id nid = parent->detach_child(basename)->self;
  safe_insert(old_locations,
              make_pair(nid, make_pair(parent->self, basename)));
  I(!null_node(nid));
  return nid;
}

void
roster_t::detach_node(node_id nid)
{
  node_t n = get_node(nid);
  if (null_node(n->parent))
    {
      // detaching the root dir
      I(n->name.empty());
      safe_insert(old_locations,
                  make_pair(nid, make_pair(n->parent, n->name)));
      root_dir.reset();
      I(!has_root());
    }
  else
    {
      path_component name = n->name;
      dir_t parent = downcast_to_dir_t(get_node(n->parent));
      I(parent->detach_child(name) == n);
      safe_insert(old_locations,
                  make_pair(nid, make_pair(n->parent, name)));
    }
}

void
roster_t::drop_detached_node(node_id nid)
{
  // ensure the node is already detached
  node_t n = get_node(nid);
  I(null_node(n->parent));
  I(n->name.empty());
  // if it's a dir, make sure it's empty
  if (is_dir_t(n))
    I(downcast_to_dir_t(n)->children.empty());
  // all right, kill it
  safe_erase(nodes, nid);
  // can use safe_erase here, because while not every detached node appears in
  // old_locations, all those that used to be in the tree do.  and you should
  // only ever be dropping nodes that were detached, not nodes that you just
  // created and that have never been attached.
  safe_erase(old_locations, nid);
}


// this creates a node in a detached state, but it does _not_ insert an entry
// for it into the old_locations member, because there is no old_location to
// forbid
node_id
roster_t::create_dir_node(node_id_source & nis)
{
  node_id nid = nis.next();
  create_dir_node(nid);
  return nid;
}
void
roster_t::create_dir_node(node_id nid)
{
  dir_t d = dir_t(new dir_node());
  d->self = nid;
  safe_insert(nodes, make_pair(nid, d));
}


// this creates a node in a detached state, but it does _not_ insert an entry
// for it into the old_locations member, because there is no old_location to
// forbid
node_id
roster_t::create_file_node(file_id const & content, node_id_source & nis)
{
  node_id nid = nis.next();
  create_file_node(content, nid);
  return nid;
}
void
roster_t::create_file_node(file_id const & content, node_id nid)
{
  file_t f = file_t(new file_node());
  f->self = nid;
  f->content = content;
  safe_insert(nodes, make_pair(nid, f));
}

void
roster_t::attach_node(node_id nid, file_path const & p)
{
  MM(p);
  if (p.empty())
    // attaching the root node
    attach_node(nid, the_null_node, path_component());
  else
    {
      file_path dir;
      path_component base;
      p.dirname_basename(dir, base);
      attach_node(nid, get_node(dir)->self, base);
    }
}

void
roster_t::attach_node(node_id nid, node_id parent, path_component name)
{
  node_t n = get_node(nid);

  I(!null_node(n->self));
  // ensure the node is already detached (as best one can)
  I(null_node(n->parent));
  I(n->name.empty());

  // this iterator might point to old_locations.end(), because old_locations
  // only includes entries for renames, not new nodes
  map<node_id, pair<node_id, path_component> >::iterator
    i = old_locations.find(nid);

  if (null_node(parent) || name.empty())
    {
      I(null_node(parent) && name.empty());
      I(null_node(n->parent));
      I(n->name.empty());
      I(!has_root());
      root_dir = downcast_to_dir_t(n);
      I(i == old_locations.end() || i->second != make_pair(root_dir->parent,
                                                           root_dir->name));
    }
  else
    {
      dir_t parent_n = downcast_to_dir_t(get_node(parent));
      parent_n->attach_child(name, n);
      I(i == old_locations.end() || i->second != make_pair(n->parent, n->name));
    }

  if (i != old_locations.end())
    old_locations.erase(i);
}

void
roster_t::apply_delta(file_path const & pth,
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
roster_t::set_content(node_id nid, file_id const & new_id)
{
  file_t f = downcast_to_file_t(get_node(nid));
  I(!(f->content == new_id));
  f->content = new_id;
}


void
roster_t::clear_attr(file_path const & pth,
                     attr_key const & name)
{
  set_attr(pth, name, make_pair(false, attr_value()));
}

void
roster_t::erase_attr(node_id nid,
                     attr_key const & name)
{
  node_t n = get_node(nid);
  safe_erase(n->attrs, name);
}

void
roster_t::set_attr(file_path const & pth,
                   attr_key const & name,
                   attr_value const & val)
{
  set_attr(pth, name, make_pair(true, val));
}


void
roster_t::set_attr(file_path const & pth,
                   attr_key const & name,
                   pair<bool, attr_value> const & val)
{
  node_t n = get_node(pth);
  I(val.first || val.second().empty());
  I(!null_node(n->self));
  full_attr_map_t::iterator i = n->attrs.find(name);
  if (i == n->attrs.end())
    i = safe_insert(n->attrs, make_pair(name,
                                        make_pair(false, attr_value())));
  I(i->second != val);
  i->second = val;
}

// same as above, but allowing <unknown> -> <dead> transition
void
roster_t::set_attr_unknown_to_dead_ok(node_id nid,
                                      attr_key const & name,
                                      pair<bool, attr_value> const & val)
{
  node_t n = get_node(nid);
  I(val.first || val.second().empty());
  full_attr_map_t::iterator i = n->attrs.find(name);
  if (i != n->attrs.end())
    I(i->second != val);
  n->attrs[name] = val;
}

bool
roster_t::get_attr(file_path const & pth,
                   attr_key const & name,
                   attr_value & val) const
{
  I(has_node(pth));

  node_t n = get_node(pth);
  full_attr_map_t::const_iterator i = n->attrs.find(name);
  if (i != n->attrs.end() && i->second.first)
    {
      val = i->second.second;
      return true;
    }
  return false;
}


template <> void
dump(roster_t const & val, string & out)
{
  ostringstream oss;
  if (val.root_dir)
    oss << "Root node: " << val.root_dir->self << '\n'
        << "   at " << val.root_dir << ", uses: " << val.root_dir.use_count() << '\n';
  else
    oss << "root dir is NULL\n";
  for (node_map::const_iterator i = val.nodes.begin(); i != val.nodes.end(); ++i)
    {
      oss << "\nNode " << i->first << '\n';
      string node_s;
      dump(i->second, node_s);
      oss << node_s;
    }
  out = oss.str();
}

void
roster_t::check_sane(bool temp_nodes_ok) const
{
  I(has_root());
  node_map::const_iterator ri;

  I(old_locations.empty());

  for (ri = nodes.begin();
       ri != nodes.end();
       ++ri)
    {
      node_id nid = ri->first;
      I(!null_node(nid));
      if (!temp_nodes_ok)
        I(!temp_node(nid));
      node_t n = ri->second;
      I(n->self == nid);
      if (is_dir_t(n))
        {
          if (n->name.empty() || null_node(n->parent))
            I(n->name.empty() && null_node(n->parent));
          else
            I(!n->name.empty() && !null_node(n->parent));
        }
      else
        {
          I(!n->name.empty() && !null_node(n->parent));
          I(!null_id(downcast_to_file_t(n)->content));
        }
      for (full_attr_map_t::const_iterator i = n->attrs.begin(); i != n->attrs.end(); ++i)
        I(i->second.first || i->second.second().empty());
      if (n != root_dir)
        {
          I(!null_node(n->parent));
          I(downcast_to_dir_t(get_node(n->parent))->get_child(n->name) == n);
        }

    }

  I(has_root());
  size_t maxdepth = nodes.size();
  for (dfs_iter i(root_dir); !i.finished(); ++i)
    {
      I(*i == get_node((*i)->self));
      I(maxdepth-- > 0);
    }
  I(maxdepth == 0);
}

void
roster_t::check_sane_against(marking_map const & markings, bool temp_nodes_ok) const
{

  check_sane(temp_nodes_ok);

  node_map::const_iterator ri;
  marking_map::const_iterator mi;

  for (ri = nodes.begin(), mi = markings.begin();
       ri != nodes.end() && mi != markings.end();
       ++ri, ++mi)
    {
      I(!null_id(mi->second.birth_revision));
      I(!mi->second.parent_name.empty());

      if (is_file_t(ri->second))
        I(!mi->second.file_content.empty());
      else
        I(mi->second.file_content.empty());

      full_attr_map_t::const_iterator rai;
      map<attr_key, set<revision_id> >::const_iterator mai;
      for (rai = ri->second->attrs.begin(), mai = mi->second.attrs.begin();
           rai != ri->second->attrs.end() && mai != mi->second.attrs.end();
           ++rai, ++mai)
        {
          I(rai->first == mai->first);
          I(!mai->second.empty());
        }
      I(rai == ri->second->attrs.end() && mai == mi->second.attrs.end());
      // TODO: attrs
    }

  I(ri == nodes.end() && mi == markings.end());
}


temp_node_id_source::temp_node_id_source()
  : curr(first_temp_node)
{}

node_id
temp_node_id_source::next()
{
    node_id n = curr++;
    I(temp_node(n));
    return n;
}

editable_roster_base::editable_roster_base(roster_t & r, node_id_source & nis)
  : r(r), nis(nis)
{}

node_id
editable_roster_base::detach_node(file_path const & src)
{
  // L(FL("detach_node('%s')") % src);
  return r.detach_node(src);
}

void
editable_roster_base::drop_detached_node(node_id nid)
{
  // L(FL("drop_detached_node(%d)") % nid);
  r.drop_detached_node(nid);
}

node_id
editable_roster_base::create_dir_node()
{
  // L(FL("create_dir_node()"));
  node_id n = r.create_dir_node(nis);
  // L(FL("create_dir_node() -> %d") % n);
  return n;
}

node_id
editable_roster_base::create_file_node(file_id const & content)
{
  // L(FL("create_file_node('%s')") % content);
  node_id n = r.create_file_node(content, nis);
  // L(FL("create_file_node('%s') -> %d") % content % n);
  return n;
}

void
editable_roster_base::attach_node(node_id nid, file_path const & dst)
{
  // L(FL("attach_node(%d, '%s')") % nid % dst);
  MM(dst);
  MM(this->r);
  r.attach_node(nid, dst);
}

void
editable_roster_base::apply_delta(file_path const & pth,
                                  file_id const & old_id,
                                  file_id const & new_id)
{
  // L(FL("apply_delta('%s', '%s', '%s')") % pth % old_id % new_id);
  r.apply_delta(pth, old_id, new_id);
}

void
editable_roster_base::clear_attr(file_path const & pth,
                                 attr_key const & name)
{
  // L(FL("clear_attr('%s', '%s')") % pth % name);
  r.clear_attr(pth, name);
}

void
editable_roster_base::set_attr(file_path const & pth,
                               attr_key const & name,
                               attr_value const & val)
{
  // L(FL("set_attr('%s', '%s', '%s')") % pth % name % val);
  r.set_attr(pth, name, val);
}

void
editable_roster_base::commit()
{
}

namespace
{
  struct true_node_id_source
    : public node_id_source
  {
    true_node_id_source(database & db) : db(db) {}
    virtual node_id next()
    {
      node_id n = db.next_node_id();
      I(!temp_node(n));
      return n;
    }
    database & db;
  };


  class editable_roster_for_merge
    : public editable_roster_base
  {
  public:
    set<node_id> new_nodes;
    editable_roster_for_merge(roster_t & r, node_id_source & nis)
      : editable_roster_base(r, nis)
    {}
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


  void union_new_nodes(roster_t & a, set<node_id> & a_new,
                       roster_t & b, set<node_id> & b_new,
                       node_id_source & nis)
  {
    // We must not replace a node whose id is in both a_new and b_new
    // with a new temp id that is already in either set.  b_new is
    // destructively modified, so record the union of both sets now.
    set<node_id> all_new_nids;
    std::set_union(a_new.begin(), a_new.end(),
                   b_new.begin(), b_new.end(),
                   std::inserter(all_new_nids, all_new_nids.begin()));

    // First identify nodes that are new in A but not in B, or new in both.
    for (set<node_id>::const_iterator i = a_new.begin(); i != a_new.end(); ++i)
      {
        node_id const aid = *i;
        file_path p;
        // SPEEDUP?: climb out only so far as is necessary to find a shared
        // id?  possibly faster (since usually will get a hit immediately),
        // but may not be worth the effort (since it doesn't take that long to
        // get out in any case)
        a.get_name(aid, p);
        node_id bid = b.get_node(p)->self;
        if (b_new.find(bid) != b_new.end())
          {
            I(temp_node(bid));
            node_id new_nid;
            do
              new_nid = nis.next();
            while (all_new_nids.find(new_nid) != all_new_nids.end());

            a.replace_node_id(aid, new_nid);
            b.replace_node_id(bid, new_nid);
            b_new.erase(bid);
          }
        else
          {
            a.replace_node_id(aid, bid);
          }
      }

    // Now identify nodes that are new in B but not A.
    for (set<node_id>::const_iterator i = b_new.begin(); i != b_new.end(); i++)
      {
        node_id const bid = *i;
        file_path p;
        // SPEEDUP?: climb out only so far as is necessary to find a shared
        // id?  possibly faster (since usually will get a hit immediately),
        // but may not be worth the effort (since it doesn't take that long to
        // get out in any case)
        b.get_name(bid, p);
        node_id aid = a.get_node(p)->self;
        I(a_new.find(aid) == a_new.end());
        b.replace_node_id(bid, aid);
      }
  }

  void
  union_corpses(roster_t & left, roster_t & right)
  {
    // left and right should be equal, except that each may have some attr
    // corpses that the other does not
    node_map::const_iterator left_i = left.all_nodes().begin();
    node_map::const_iterator right_i = right.all_nodes().begin();
    while (left_i != left.all_nodes().end() || right_i != right.all_nodes().end())
      {
        I(left_i->second->self == right_i->second->self);
        parallel::iter<full_attr_map_t> j(left_i->second->attrs,
                                          right_i->second->attrs);
        // we batch up the modifications until the end, so as not to be
        // changing things around under the parallel::iter's feet
        set<attr_key> left_missing, right_missing;
        while (j.next())
          {
            MM(j);
            switch (j.state())
              {
              case parallel::invalid:
                I(false);

              case parallel::in_left:
                // this is a corpse
                I(!j.left_data().first);
                right_missing.insert(j.left_key());
                break;

              case parallel::in_right:
                // this is a corpse
                I(!j.right_data().first);
                left_missing.insert(j.right_key());
                break;

              case parallel::in_both:
                break;
              }
          }
        for (set<attr_key>::const_iterator j = left_missing.begin();
             j != left_missing.end(); ++j)
          safe_insert(left_i->second->attrs,
                      make_pair(*j, make_pair(false, attr_value())));
        for (set<attr_key>::const_iterator j = right_missing.begin();
             j != right_missing.end(); ++j)
          safe_insert(right_i->second->attrs,
                      make_pair(*j, make_pair(false, attr_value())));
        ++left_i;
        ++right_i;
      }
    I(left_i == left.all_nodes().end());
    I(right_i == right.all_nodes().end());
  }

  // After this, left should == right, and there should be no temporary ids.
  // Destroys sets, because that's handy (it has to scan over both, but it can
  // skip some double-scanning)
  void
  unify_rosters(roster_t & left, set<node_id> & left_new,
                roster_t & right, set<node_id> & right_new,
                node_id_source & nis)
  {
    // Our basic problem is this: when interpreting a revision with multiple
    // csets, those csets all give enough information for us to get the same
    // manifest, and even a bit more than that.  However, there is some
    // information that is not exposed at the manifest level, and csets alone
    // do not give us all we need.  This function is responsible taking the
    // two rosters that we get from pure cset application, and fixing them up
    // so that they are wholly identical.

    // The first thing that is missing is identification of files.  If one
    // cset says "add_file" and the other says nothing, then the add_file is
    // not really an add_file.  One of our rosters will have a temp id for
    // this file, and the other will not.  In this case, we replace the temp
    // id with the other side's permanent id.  However, if both csets say
    // "add_file", then this really is a new id; both rosters will have temp
    // ids, and we replace both of them with a newly allocated id.  After
    // this, the two rosters will have identical node_ids at every path.
    union_new_nodes(left, left_new, right, right_new, nis);

    // The other thing we need to fix up is attr corpses.  Live attrs are made
    // identical by the csets; but if, say, on one side of a fork an attr is
    // added and then deleted, then one of our incoming merge rosters will
    // have a corpse for that attr, and the other will not.  We need to make
    // sure at both of them end up with the corpse.  This function fixes up
    // that.
    union_corpses(left, right);
  }

  template <typename T> void
  mark_unmerged_scalar(set<revision_id> const & parent_marks,
                       T const & parent_val,
                       revision_id const & new_rid,
                       T const & new_val,
                       set<revision_id> & new_marks)
  {
    I(new_marks.empty());
    if (parent_val == new_val)
      new_marks = parent_marks;
    else
      new_marks.insert(new_rid);
  }

  // This function implements the case.
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

  template <typename T> void
  mark_merged_scalar(set<revision_id> const & left_marks,
                     set<revision_id> const & left_uncommon_ancestors,
                     T const & left_val,
                     set<revision_id> const & right_marks,
                     set<revision_id> const & right_uncommon_ancestors,
                     T const & right_val,
                     revision_id const & new_rid,
                     T const & new_val,
                     set<revision_id> & new_marks)
  {
    I(new_marks.empty());

    // let's not depend on T::operator!= being defined, only on T::operator==
    // being defined.
    bool diff_from_left = !(new_val == left_val);
    bool diff_from_right = !(new_val == right_val);

    // some quick sanity checks
    for (set<revision_id>::const_iterator i = left_marks.begin();
         i != left_marks.end(); ++i)
      I(right_uncommon_ancestors.find(*i) == right_uncommon_ancestors.end());
    for (set<revision_id>::const_iterator i = right_marks.begin();
         i != right_marks.end(); ++i)
      I(left_uncommon_ancestors.find(*i) == left_uncommon_ancestors.end());

    if (diff_from_left && diff_from_right)
      new_marks.insert(new_rid);

    else if (diff_from_left && !diff_from_right)
      mark_won_merge(left_marks, left_uncommon_ancestors, right_marks,
                     new_rid, new_marks);

    else if (!diff_from_left && diff_from_right)
      mark_won_merge(right_marks, right_uncommon_ancestors, left_marks,
                     new_rid, new_marks);

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
        set_union(left_marks.begin(), left_marks.end(),
                  right_marks.begin(), right_marks.end(),
                  inserter(new_marks, new_marks.begin()));
      }
  }

  void
  mark_new_node(revision_id const & new_rid, node_t n, marking_t & new_marking)
  {
    new_marking.birth_revision = new_rid;
    I(new_marking.parent_name.empty());
    new_marking.parent_name.insert(new_rid);
    I(new_marking.file_content.empty());
    if (is_file_t(n))
      new_marking.file_content.insert(new_rid);
    I(new_marking.attrs.empty());
    set<revision_id> singleton;
    singleton.insert(new_rid);
    for (full_attr_map_t::const_iterator i = n->attrs.begin();
         i != n->attrs.end(); ++i)
      new_marking.attrs.insert(make_pair(i->first, singleton));
  }

  void
  mark_unmerged_node(marking_t const & parent_marking, node_t parent_n,
                     revision_id const & new_rid, node_t n,
                     marking_t & new_marking)
  {
    // SPEEDUP?: the common case here is that the parent and child nodes are
    // exactly identical, in which case the markings are also exactly
    // identical.  There might be a win in first doing an overall
    // comparison/copy, in case it can be better optimized as a block
    // comparison and a block copy...

    I(same_type(parent_n, n) && parent_n->self == n->self);

    new_marking.birth_revision = parent_marking.birth_revision;

    mark_unmerged_scalar(parent_marking.parent_name,
                         make_pair(parent_n->parent, parent_n->name),
                         new_rid,
                         make_pair(n->parent, n->name),
                         new_marking.parent_name);

    if (is_file_t(n))
      mark_unmerged_scalar(parent_marking.file_content,
                           downcast_to_file_t(parent_n)->content,
                           new_rid,
                           downcast_to_file_t(n)->content,
                           new_marking.file_content);

    for (full_attr_map_t::const_iterator i = n->attrs.begin();
           i != n->attrs.end(); ++i)
      {
        set<revision_id> & new_marks = new_marking.attrs[i->first];
        I(new_marks.empty());
        full_attr_map_t::const_iterator j = parent_n->attrs.find(i->first);
        if (j == parent_n->attrs.end())
          new_marks.insert(new_rid);
        else
          mark_unmerged_scalar(safe_get(parent_marking.attrs, i->first),
                               j->second,
                               new_rid, i->second, new_marks);
      }
  }

  void
  mark_merged_node(marking_t const & left_marking,
                   set<revision_id> const & left_uncommon_ancestors,
                   node_t ln,
                   marking_t const & right_marking,
                   set<revision_id> const & right_uncommon_ancestors,
                   node_t rn,
                   revision_id const & new_rid,
                   node_t n,
                   marking_t & new_marking)
  {
    I(same_type(ln, n) && same_type(rn, n));
    I(left_marking.birth_revision == right_marking.birth_revision);
    new_marking.birth_revision = left_marking.birth_revision;

    // name
    mark_merged_scalar(left_marking.parent_name, left_uncommon_ancestors,
                       make_pair(ln->parent, ln->name),
                       right_marking.parent_name, right_uncommon_ancestors,
                       make_pair(rn->parent, rn->name),
                       new_rid,
                       make_pair(n->parent, n->name),
                       new_marking.parent_name);
    // content
    if (is_file_t(n))
      {
        file_t f = downcast_to_file_t(n);
        file_t lf = downcast_to_file_t(ln);
        file_t rf = downcast_to_file_t(rn);
        mark_merged_scalar(left_marking.file_content, left_uncommon_ancestors,
                           lf->content,
                           right_marking.file_content, right_uncommon_ancestors,
                           rf->content,
                           new_rid, f->content, new_marking.file_content);
      }
    // attrs
    for (full_attr_map_t::const_iterator i = n->attrs.begin();
         i != n->attrs.end(); ++i)
      {
        attr_key const & key = i->first;
        full_attr_map_t::const_iterator li = ln->attrs.find(key);
        full_attr_map_t::const_iterator ri = rn->attrs.find(key);
        I(new_marking.attrs.find(key) == new_marking.attrs.end());
        // [], when used to refer to a non-existent element, default
        // constructs that element and returns a reference to it.  We make use
        // of this here.
        set<revision_id> & new_marks = new_marking.attrs[key];

        if (li == ln->attrs.end() && ri == rn->attrs.end())
          // this is a brand new attribute, never before seen
          safe_insert(new_marks, new_rid);

        else if (li != ln->attrs.end() && ri == rn->attrs.end())
          // only the left side has seen this attr before
          mark_unmerged_scalar(safe_get(left_marking.attrs, key),
                               li->second,
                               new_rid, i->second, new_marks);

        else if (li == ln->attrs.end() && ri != rn->attrs.end())
          // only the right side has seen this attr before
          mark_unmerged_scalar(safe_get(right_marking.attrs, key),
                               ri->second,
                               new_rid, i->second, new_marks);

        else
          // both sides have seen this attr before
          mark_merged_scalar(safe_get(left_marking.attrs, key),
                             left_uncommon_ancestors,
                             li->second,
                             safe_get(right_marking.attrs, key),
                             right_uncommon_ancestors,
                             ri->second,
                             new_rid, i->second, new_marks);
      }

    // some extra sanity checking -- attributes are not allowed to be deleted,
    // so we double check that they haven't.
    // SPEEDUP?: this code could probably be made more efficient -- but very
    // rarely will any node have more than, say, one attribute, so it probably
    // doesn't matter.
    for (full_attr_map_t::const_iterator i = ln->attrs.begin();
         i != ln->attrs.end(); ++i)
      I(n->attrs.find(i->first) != n->attrs.end());
    for (full_attr_map_t::const_iterator i = rn->attrs.begin();
         i != rn->attrs.end(); ++i)
      I(n->attrs.find(i->first) != n->attrs.end());
  }

} // anonymous namespace


// This function is also responsible for verifying ancestry invariants --
// those invariants on a roster that involve the structure of the roster's
// parents, rather than just the structure of the roster itself.
void
mark_merge_roster(roster_t const & left_roster,
                  marking_map const & left_markings,
                  set<revision_id> const & left_uncommon_ancestors,
                  roster_t const & right_roster,
                  marking_map const & right_markings,
                  set<revision_id> const & right_uncommon_ancestors,
                  revision_id const & new_rid,
                  roster_t const & merge,
                  marking_map & new_markings)
{
  for (node_map::const_iterator i = merge.all_nodes().begin();
       i != merge.all_nodes().end(); ++i)
    {
      node_t const & n = i->second;
      node_map::const_iterator lni = left_roster.all_nodes().find(i->first);
      node_map::const_iterator rni = right_roster.all_nodes().find(i->first);

      bool exists_in_left = (lni != left_roster.all_nodes().end());
      bool exists_in_right = (rni != right_roster.all_nodes().end());

      marking_t new_marking;

      if (!exists_in_left && !exists_in_right)
        mark_new_node(new_rid, n, new_marking);

      else if (!exists_in_left && exists_in_right)
        {
          node_t const & right_node = rni->second;
          marking_t const & right_marking = safe_get(right_markings, n->self);
          // must be unborn on the left (as opposed to dead)
          I(right_uncommon_ancestors.find(right_marking.birth_revision)
            != right_uncommon_ancestors.end());
          mark_unmerged_node(right_marking, right_node,
                             new_rid, n, new_marking);
        }
      else if (exists_in_left && !exists_in_right)
        {
          node_t const & left_node = lni->second;
          marking_t const & left_marking = safe_get(left_markings, n->self);
          // must be unborn on the right (as opposed to dead)
          I(left_uncommon_ancestors.find(left_marking.birth_revision)
            != left_uncommon_ancestors.end());
          mark_unmerged_node(left_marking, left_node,
                             new_rid, n, new_marking);
        }
      else
        {
          node_t const & left_node = lni->second;
          node_t const & right_node = rni->second;
          mark_merged_node(safe_get(left_markings, n->self),
                           left_uncommon_ancestors, left_node,
                           safe_get(right_markings, n->self),
                           right_uncommon_ancestors, right_node,
                           new_rid, n, new_marking);
        }

      safe_insert(new_markings, make_pair(i->first, new_marking));
    }
}

namespace {

  class editable_roster_for_nonmerge
    : public editable_roster_base
  {
  public:
    editable_roster_for_nonmerge(roster_t & r, node_id_source & nis,
                                 revision_id const & rid,
                                 marking_map & markings)
      : editable_roster_base(r, nis),
        rid(rid), markings(markings)
    {}

    virtual node_id detach_node(file_path const & src)
    {
      node_id nid = this->editable_roster_base::detach_node(src);
      marking_map::iterator marking = markings.find(nid);
      I(marking != markings.end());
      marking->second.parent_name.clear();
      marking->second.parent_name.insert(rid);
      return nid;
    }

    virtual void drop_detached_node(node_id nid)
    {
      this->editable_roster_base::drop_detached_node(nid);
      safe_erase(markings, nid);
    }

    virtual node_id create_dir_node()
    {
      return handle_new(this->editable_roster_base::create_dir_node());
    }

    virtual node_id create_file_node(file_id const & content)
    {
      return handle_new(this->editable_roster_base::create_file_node(content));
    }

    virtual void apply_delta(file_path const & pth,
                             file_id const & old_id, file_id const & new_id)
    {
      this->editable_roster_base::apply_delta(pth, old_id, new_id);
      node_id nid = r.get_node(pth)->self;
      marking_map::iterator marking = markings.find(nid);
      I(marking != markings.end());
      marking->second.file_content.clear();
      marking->second.file_content.insert(rid);
    }

    virtual void clear_attr(file_path const & pth, attr_key const & name)
    {
      this->editable_roster_base::clear_attr(pth, name);
      handle_attr(pth, name);
    }

    virtual void set_attr(file_path const & pth, attr_key const & name,
                          attr_value const & val)
    {
      this->editable_roster_base::set_attr(pth, name, val);
      handle_attr(pth, name);
    }

    node_id handle_new(node_id nid)
    {
      node_t n = r.get_node(nid);
      marking_t new_marking;
      mark_new_node(rid, n, new_marking);
      safe_insert(markings, make_pair(nid, new_marking));
      return nid;
    }

    void handle_attr(file_path const & pth, attr_key const & name)
    {
      node_id nid = r.get_node(pth)->self;
      marking_map::iterator marking = markings.find(nid);
      map<attr_key, set<revision_id> >::iterator am = marking->second.attrs.find(name);
      if (am == marking->second.attrs.end())
        {
          marking->second.attrs.insert(make_pair(name, set<revision_id>()));
          am = marking->second.attrs.find(name);
        }

      I(am != marking->second.attrs.end());
      am->second.clear();
      am->second.insert(rid);
    }

  private:
    revision_id const & rid;
    // markings starts out as the parent's markings
    marking_map & markings;
  };

  // Interface note: make_roster_for_merge and make_roster_for_nonmerge
  // each exist in two variants:
  //
  // 1. A variant that does all of the actual work, taking every single
  //    relevant base-level data object as a separate argument.  This
  //    variant is called directly by the unit tests, and also by variant 2.
  //
  // 2. A variant that takes a revision object, a revision ID, a database,
  //    and a node_id_source.  This variant uses those four things to look
  //    up all of the low-level data required by variant 1, then calls
  //    variant 1 to get the real work done.  This is the one called by
  //    (one variant of) make_roster_for_revision.

  // yes, this function takes 14 arguments.  I'm very sorry.
  void
  make_roster_for_merge(revision_id const & left_rid,
                        roster_t const & left_roster,
                        marking_map const & left_markings,
                        cset const & left_cs,
                        set<revision_id> const & left_uncommon_ancestors,

                        revision_id const & right_rid,
                        roster_t const & right_roster,
                        marking_map const & right_markings,
                        cset const & right_cs,
                        set<revision_id> const & right_uncommon_ancestors,

                        revision_id const & new_rid,
                        roster_t & new_roster,
                        marking_map & new_markings,
                        node_id_source & nis)
  {
    I(!null_id(left_rid) && !null_id(right_rid));
    I(left_uncommon_ancestors.find(left_rid) != left_uncommon_ancestors.end());
    I(left_uncommon_ancestors.find(right_rid) == left_uncommon_ancestors.end());
    I(right_uncommon_ancestors.find(right_rid) != right_uncommon_ancestors.end());
    I(right_uncommon_ancestors.find(left_rid) == right_uncommon_ancestors.end());
    MM(left_rid);
    MM(left_roster);
    MM(left_markings);
    MM(left_cs);
    MM(left_uncommon_ancestors);
    MM(right_rid);
    MM(right_roster);
    MM(right_markings);
    MM(right_cs);
    MM(right_uncommon_ancestors);
    MM(new_rid);
    MM(new_roster);
    MM(new_markings);
    {
      temp_node_id_source temp_nis;
      new_roster = left_roster;
      roster_t from_right_r(right_roster);
      MM(from_right_r);

      editable_roster_for_merge from_left_er(new_roster, temp_nis);
      editable_roster_for_merge from_right_er(from_right_r, temp_nis);

      left_cs.apply_to(from_left_er);
      right_cs.apply_to(from_right_er);

      set<node_id> new_ids;
      unify_rosters(new_roster, from_left_er.new_nodes,
                    from_right_r, from_right_er.new_nodes,
                    nis);

      // Kluge: If both csets have no content changes, and the node_id_source
      // passed to this function is a temp_node_id_source, then we are being
      // called from get_current_roster_shape, and we should not attempt to
      // verify that these rosters match as far as content IDs.
      if (left_cs.deltas_applied.size() == 0
          && right_cs.deltas_applied.size() == 0
          && typeid(nis) == typeid(temp_node_id_source))
        I(equal_shapes(new_roster, from_right_r));
      else
        I(new_roster == from_right_r);
    }

    // SPEEDUP?: instead of constructing new marking from scratch, track which
    // nodes were modified, and scan only them
    // load one of the parent markings directly into the new marking map
    new_markings.clear();
    mark_merge_roster(left_roster, left_markings, left_uncommon_ancestors,
                      right_roster, right_markings, right_uncommon_ancestors,
                      new_rid, new_roster, new_markings);
  }


  // WARNING: this function is not tested directly (no unit tests).  Do not
  // put real logic in it.
  void
  make_roster_for_merge(revision_t const & rev, revision_id const & new_rid,
                        roster_t & new_roster, marking_map & new_markings,
                        database & db, node_id_source & nis)
  {
    edge_map::const_iterator i = rev.edges.begin();
    revision_id const & left_rid = edge_old_revision(i);
    cset const & left_cs = edge_changes(i);
    ++i;
    revision_id const & right_rid = edge_old_revision(i);
    cset const & right_cs = edge_changes(i);

    I(!null_id(left_rid) && !null_id(right_rid));
    cached_roster left_cached, right_cached;
    db.get_roster(left_rid, left_cached);
    db.get_roster(right_rid, right_cached);

    set<revision_id> left_uncommon_ancestors, right_uncommon_ancestors;
    db.get_uncommon_ancestors(left_rid, right_rid,
                                  left_uncommon_ancestors,
                                  right_uncommon_ancestors);

    make_roster_for_merge(left_rid, *left_cached.first, *left_cached.second,
                          left_cs, left_uncommon_ancestors,
                          right_rid, *right_cached.first, *right_cached.second,
                          right_cs, right_uncommon_ancestors,
                          new_rid,
                          new_roster, new_markings,
                          nis);
  }

  // Warning: this function expects the parent's roster and markings in the
  // 'new_roster' and 'new_markings' parameters, and they are modified
  // destructively!
  // This function performs an almost identical task to
  // mark_roster_with_one_parent; however, for efficiency, it is implemented
  // in a different, destructive way.
  void
  make_roster_for_nonmerge(cset const & cs,
                           revision_id const & new_rid,
                           roster_t & new_roster, marking_map & new_markings,
                           node_id_source & nis)
  {
    editable_roster_for_nonmerge er(new_roster, nis, new_rid, new_markings);
    cs.apply_to(er);
  }

  // WARNING: this function is not tested directly (no unit tests).  Do not
  // put real logic in it.
  void
  make_roster_for_nonmerge(revision_t const & rev,
                           revision_id const & new_rid,
                           roster_t & new_roster, marking_map & new_markings,
                           database & db, node_id_source & nis)
  {
    revision_id const & parent_rid = edge_old_revision(rev.edges.begin());
    cset const & parent_cs = edge_changes(rev.edges.begin());
    db.get_roster(parent_rid, new_roster, new_markings);
    make_roster_for_nonmerge(parent_cs, new_rid, new_roster, new_markings, nis);
  }
}

void
mark_roster_with_no_parents(revision_id const & rid,
                            roster_t const & roster,
                            marking_map & markings)
{
  roster_t mock_parent;
  marking_map mock_parent_markings;
  mark_roster_with_one_parent(mock_parent, mock_parent_markings,
                              rid, roster, markings);
}

void
mark_roster_with_one_parent(roster_t const & parent,
                            marking_map const & parent_markings,
                            revision_id const & child_rid,
                            roster_t const & child,
                            marking_map & child_markings)
{
  MM(parent);
  MM(parent_markings);
  MM(child_rid);
  MM(child);
  MM(child_markings);

  I(!null_id(child_rid));
  child_markings.clear();

  for (node_map::const_iterator i = child.all_nodes().begin();
       i != child.all_nodes().end(); ++i)
    {
      marking_t new_marking;
      if (parent.has_node(i->first))
        mark_unmerged_node(safe_get(parent_markings, i->first),
                           parent.get_node(i->first),
                           child_rid, i->second, new_marking);
      else
        mark_new_node(child_rid, i->second, new_marking);
      safe_insert(child_markings, make_pair(i->first, new_marking));
    }

  child.check_sane_against(child_markings, true);
}

// WARNING: this function is not tested directly (no unit tests).  Do not put
// real logic in it.
void
make_roster_for_revision(database & db, node_id_source & nis,
                         revision_t const & rev, revision_id const & new_rid,
                         roster_t & new_roster, marking_map & new_markings)
{
  MM(rev);
  MM(new_rid);
  MM(new_roster);
  MM(new_markings);
  if (rev.edges.size() == 1)
    make_roster_for_nonmerge(rev, new_rid, new_roster, new_markings, db, nis);
  else if (rev.edges.size() == 2)
    make_roster_for_merge(rev, new_rid, new_roster, new_markings, db, nis);
  else
    I(false);

  // If nis is not a true_node_id_source, we have to assume we can get temp
  // node ids out of it.  ??? Provide a predicate method on node_id_sources
  // instead of doing a typeinfo comparison.
  new_roster.check_sane_against(new_markings,
                                typeid(nis) != typeid(true_node_id_source));
}

void
make_roster_for_revision(database & db,
                         revision_t const & rev, revision_id const & new_rid,
                         roster_t & new_roster, marking_map & new_markings)
{
  true_node_id_source nis(db);
  make_roster_for_revision(db, nis, rev, new_rid, new_roster, new_markings);
}


////////////////////////////////////////////////////////////////////
//   Calculation of a cset
////////////////////////////////////////////////////////////////////


namespace
{

  void delta_only_in_from(roster_t const & from,
                          node_id nid, node_t n,
                          cset & cs)
  {
    file_path pth;
    from.get_name(nid, pth);
    safe_insert(cs.nodes_deleted, pth);
  }


  void delta_only_in_to(roster_t const & to, node_id nid, node_t n,
                        cset & cs)
  {
    file_path pth;
    to.get_name(nid, pth);
    if (is_file_t(n))
      {
        safe_insert(cs.files_added,
                    make_pair(pth, downcast_to_file_t(n)->content));
      }
    else
      {
        safe_insert(cs.dirs_added, pth);
      }
    for (full_attr_map_t::const_iterator i = n->attrs.begin();
         i != n->attrs.end(); ++i)
      if (i->second.first)
        safe_insert(cs.attrs_set,
                    make_pair(make_pair(pth, i->first), i->second.second));
  }

  void delta_in_both(node_id nid,
                     roster_t const & from, node_t from_n,
                     roster_t const & to, node_t to_n,
                     cset & cs)
  {
    I(same_type(from_n, to_n));
    I(from_n->self == to_n->self);

    if (shallow_equal(from_n, to_n, false))
      return;

    file_path from_p, to_p;
    from.get_name(nid, from_p);
    to.get_name(nid, to_p);

    // Compare name and path.
    if (from_n->name != to_n->name || from_n->parent != to_n->parent)
      safe_insert(cs.nodes_renamed, make_pair(from_p, to_p));

    // Compare file content.
    if (is_file_t(from_n))
      {
        file_t from_f = downcast_to_file_t(from_n);
        file_t to_f = downcast_to_file_t(to_n);
        if (!(from_f->content == to_f->content))
          {
            safe_insert(cs.deltas_applied,
                        make_pair(to_p, make_pair(from_f->content,
                                                   to_f->content)));
          }
      }

    // Compare attrs.
    {
      parallel::iter<full_attr_map_t> i(from_n->attrs, to_n->attrs);
      while (i.next())
        {
          MM(i);
          if ((i.state() == parallel::in_left
               || (i.state() == parallel::in_both && !i.right_data().first))
              && i.left_data().first)
            {
              safe_insert(cs.attrs_cleared,
                          make_pair(to_p, i.left_key()));
            }
          else if ((i.state() == parallel::in_right
                    || (i.state() == parallel::in_both && !i.left_data().first))
                   && i.right_data().first)
            {
              safe_insert(cs.attrs_set,
                          make_pair(make_pair(to_p, i.right_key()),
                                    i.right_data().second));
            }
          else if (i.state() == parallel::in_both
                   && i.right_data().first
                   && i.left_data().first
                   && i.right_data().second != i.left_data().second)
            {
              safe_insert(cs.attrs_set,
                          make_pair(make_pair(to_p, i.right_key()),
                                    i.right_data().second));
            }
        }
    }
  }
}

void
make_cset(roster_t const & from, roster_t const & to, cset & cs)
{
  cs.clear();
  parallel::iter<node_map> i(from.all_nodes(), to.all_nodes());
  while (i.next())
    {
      MM(i);
      switch (i.state())
        {
        case parallel::invalid:
          I(false);

        case parallel::in_left:
          // deleted
          delta_only_in_from(from, i.left_key(), i.left_data(), cs);
          break;

        case parallel::in_right:
          // added
          delta_only_in_to(to, i.right_key(), i.right_data(), cs);
          break;

        case parallel::in_both:
          // moved/renamed/patched/attribute changes
          delta_in_both(i.left_key(), from, i.left_data(), to, i.right_data(), cs);
          break;
        }
    }
}


// we assume our input is sane
bool
equal_up_to_renumbering(roster_t const & a, marking_map const & a_markings,
                        roster_t const & b, marking_map const & b_markings)
{
  if (a.all_nodes().size() != b.all_nodes().size())
    return false;

  for (node_map::const_iterator i = a.all_nodes().begin();
       i != a.all_nodes().end(); ++i)
    {
      file_path p;
      a.get_name(i->first, p);
      if (!b.has_node(p))
        return false;
      node_t b_n = b.get_node(p);
      // we already know names are the same
      if (!same_type(i->second, b_n))
        return false;
      if (i->second->attrs != b_n->attrs)
        return false;
      if (is_file_t(i->second))
        {
          if (!(downcast_to_file_t(i->second)->content
                == downcast_to_file_t(b_n)->content))
            return false;
        }
      // nodes match, check the markings too
      if (!(safe_get(a_markings, i->first) == safe_get(b_markings, b_n->self)))
        return false;
    }
  return true;
}

static void
select_restricted_nodes(roster_t const & from, roster_t const & to,
                        node_restriction const & mask,
                        map<node_id, node_t> & selected)
{
  selected.clear();
  parallel::iter<node_map> i(from.all_nodes(), to.all_nodes());
  while (i.next())
    {
      MM(i);

      switch (i.state())
        {
        case parallel::invalid:
          I(false);

        case parallel::in_left:
          // deleted
          if (!mask.includes(from, i.left_key()))
            selected.insert(make_pair(i.left_key(), i.left_data()));
          break;

        case parallel::in_right:
          // added
          if (mask.includes(to, i.right_key()))
            selected.insert(make_pair(i.right_key(), i.right_data()));
          break;

        case parallel::in_both:
          // moved/renamed/patched/attribute changes
          if (mask.includes(from, i.left_key()) ||
              mask.includes(to, i.right_key()))
            selected.insert(make_pair(i.right_key(), i.right_data()));
          else
            selected.insert(make_pair(i.left_key(), i.left_data()));
          break;
        }
    }
}

void
make_restricted_roster(roster_t const & from, roster_t const & to,
                       roster_t & restricted,
                       node_restriction const & mask)
{
  MM(from);
  MM(to);
  MM(restricted);

  I(restricted.all_nodes().empty());

  map<node_id, node_t> selected;

  select_restricted_nodes(from, to, mask, selected);

  int problems = 0;

  while (!selected.empty())
    {
      map<node_id, node_t>::const_iterator n = selected.begin();

      L(FL("selected node %d %s parent %d")
            % n->second->self
            % n->second->name
            % n->second->parent);

      bool missing_parent = false;

      while (!null_node(n->second->parent) &&
             !restricted.has_node(n->second->parent))
        {
          // we can't add this node until its parent has been added

          L(FL("deferred node %d %s parent %d")
            % n->second->self
            % n->second->name
            % n->second->parent);

          map<node_id, node_t>::const_iterator
            p = selected.find(n->second->parent);

          if (p != selected.end())
            {
              n = p; // see if we can add the parent
              I(is_dir_t(n->second));
            }
          else
            {
              missing_parent = true;
              break;
            }
        }

      if (!missing_parent)
        {
          L(FL("adding node %d %s parent %d")
            % n->second->self
            % n->second->name
            % n->second->parent);

          if (is_file_t(n->second))
            {
              file_t const f = downcast_to_file_t(n->second);
              restricted.create_file_node(f->content, f->self);
            }
          else
            restricted.create_dir_node(n->second->self);

          node_t added = restricted.get_node(n->second->self);
          added->attrs = n->second->attrs;

          restricted.attach_node(n->second->self, n->second->parent, n->second->name);
        }
      else if (from.has_node(n->second->parent) && !to.has_node(n->second->parent))
        {
          // included a delete that must be excluded
          file_path self, parent;
          from.get_name(n->second->self, self);
          from.get_name(n->second->parent, parent);
          W(F("restriction includes deletion of '%s' "
              "but excludes deletion of '%s'")
            % parent % self);
          problems++;
        }
      else if (!from.has_node(n->second->parent) && to.has_node(n->second->parent))
        {
          // excluded an add that must be included
          file_path self, parent;
          to.get_name(n->second->self, self);
          to.get_name(n->second->parent, parent);
          W(F("restriction excludes addition of '%s' "
              "but includes addition of '%s'")
            % parent % self);
          problems++;
        }
      else
        I(false); // something we missed?!?

      selected.erase(n->first);
    }


  // we cannot call restricted.check_sane(true) unconditionally because the
  // restricted roster is very possibly *not* sane. for example, if we run
  // the following in a new unversioned directory the from, to and
  // restricted rosters will all be empty and thus not sane.
  //
  // mtn setup .
  // mtn status
  //
  // several tests do this and it seems entirely reasonable. we first check
  // that the restricted roster is not empty and only then require it to be
  // sane.

  if (!restricted.all_nodes().empty() && !restricted.has_root())
   {
     W(F("restriction excludes addition of root directory"));
     problems++;
   }

  N(problems == 0, F("invalid restriction"));

  if (!restricted.all_nodes().empty())
    restricted.check_sane(true);

}

void
select_nodes_modified_by_cset(cset const & cs,
                              roster_t const & old_roster,
                              roster_t const & new_roster,
                              set<node_id> & nodes_modified)
{
  nodes_modified.clear();

  set<file_path> modified_prestate_nodes;
  set<file_path> modified_poststate_nodes;

  // Pre-state damage

  copy(cs.nodes_deleted.begin(), cs.nodes_deleted.end(),
       inserter(modified_prestate_nodes, modified_prestate_nodes.begin()));

  for (map<file_path, file_path>::const_iterator i = cs.nodes_renamed.begin();
       i != cs.nodes_renamed.end(); ++i)
    modified_prestate_nodes.insert(i->first);

  // Post-state damage

  copy(cs.dirs_added.begin(), cs.dirs_added.end(),
       inserter(modified_poststate_nodes, modified_poststate_nodes.begin()));

  for (map<file_path, file_id>::const_iterator i = cs.files_added.begin();
       i != cs.files_added.end(); ++i)
    modified_poststate_nodes.insert(i->first);

  for (map<file_path, file_path>::const_iterator i = cs.nodes_renamed.begin();
       i != cs.nodes_renamed.end(); ++i)
    modified_poststate_nodes.insert(i->second);

  for (map<file_path, pair<file_id, file_id> >::const_iterator i = cs.deltas_applied.begin();
       i != cs.deltas_applied.end(); ++i)
    modified_poststate_nodes.insert(i->first);

  for (set<pair<file_path, attr_key> >::const_iterator i = cs.attrs_cleared.begin();
       i != cs.attrs_cleared.end(); ++i)
    modified_poststate_nodes.insert(i->first);

  for (map<pair<file_path, attr_key>, attr_value>::const_iterator i = cs.attrs_set.begin();
       i != cs.attrs_set.end(); ++i)
    modified_poststate_nodes.insert(i->first.first);

  // Finale

  for (set<file_path>::const_iterator i = modified_prestate_nodes.begin();
       i != modified_prestate_nodes.end(); ++i)
    {
      I(old_roster.has_node(*i));
      nodes_modified.insert(old_roster.get_node(*i)->self);
    }

  for (set<file_path>::const_iterator i = modified_poststate_nodes.begin();
       i != modified_poststate_nodes.end(); ++i)
    {
      I(new_roster.has_node(*i));
      nodes_modified.insert(new_roster.get_node(*i)->self);
    }

}

void
roster_t::extract_path_set(set<file_path> & paths) const
{
  paths.clear();
  if (has_root())
    {
      for (dfs_iter i(root_dir, true); !i.finished(); ++i)
        {
          file_path pth = file_path_internal(i.path());
          if (!pth.empty())
            paths.insert(pth);
        }
    }
}

// ??? make more similar to the above (member function, use dfs_iter)
void
get_content_paths(roster_t const & roster, map<file_id, file_path> & paths)
{
  node_map const & nodes = roster.all_nodes();
  for (node_map::const_iterator i = nodes.begin(); i != nodes.end(); ++i)
    {
      node_t node = roster.get_node(i->first);
      if (is_file_t(node))
        {
          file_path p;
          roster.get_name(i->first, p);
          file_t file = downcast_to_file_t(node);
          paths.insert(make_pair(file->content, p));
        }
    }
}

////////////////////////////////////////////////////////////////////
//   I/O routines
////////////////////////////////////////////////////////////////////

void
push_marking(basic_io::stanza & st,
             bool is_file,
             marking_t const & mark)
{

  I(!null_id(mark.birth_revision));
  st.push_binary_pair(basic_io::syms::birth, mark.birth_revision.inner());

  for (set<revision_id>::const_iterator i = mark.parent_name.begin();
       i != mark.parent_name.end(); ++i)
    st.push_binary_pair(basic_io::syms::path_mark, i->inner());

  if (is_file)
    {
      for (set<revision_id>::const_iterator i = mark.file_content.begin();
           i != mark.file_content.end(); ++i)
        st.push_binary_pair(basic_io::syms::content_mark, i->inner());
    }
  else
    I(mark.file_content.empty());

  for (map<attr_key, set<revision_id> >::const_iterator i = mark.attrs.begin();
       i != mark.attrs.end(); ++i)
    {
      for (set<revision_id>::const_iterator j = i->second.begin();
           j != i->second.end(); ++j)
        st.push_binary_triple(basic_io::syms::attr_mark, i->first(), j->inner());
    }
}


void
parse_marking(basic_io::parser & pa,
              marking_t & marking)
{
  while (pa.symp())
    {
      string rev;
      if (pa.symp(basic_io::syms::birth))
        {
          pa.sym();
          pa.hex(rev);
          marking.birth_revision = revision_id(decode_hexenc(rev));
        }
      else if (pa.symp(basic_io::syms::path_mark))
        {
          pa.sym();
          pa.hex(rev);
          safe_insert(marking.parent_name, revision_id(decode_hexenc(rev)));
        }
      else if (pa.symp(basic_io::syms::content_mark))
        {
          pa.sym();
          pa.hex(rev);
          safe_insert(marking.file_content, revision_id(decode_hexenc(rev)));
        }
      else if (pa.symp(basic_io::syms::attr_mark))
        {
          string k;
          pa.sym();
          pa.str(k);
          pa.hex(rev);
          attr_key key = attr_key(k);
          safe_insert(marking.attrs[key], revision_id(decode_hexenc(rev)));
        }
      else break;
    }
}

// SPEEDUP?: hand-writing a parser for manifests was a measurable speed win,
// and the original parser was much simpler than basic_io.  After benchmarking
// consider replacing the roster disk format with something that can be
// processed more efficiently.

void
roster_t::print_to(basic_io::printer & pr,
                   marking_map const & mm,
                   bool print_local_parts) const
{
  I(has_root());
  {
    basic_io::stanza st;
    st.push_str_pair(basic_io::syms::format_version, "1");
    pr.print_stanza(st);
  }
  for (dfs_iter i(root_dir, true); !i.finished(); ++i)
    {
      node_t curr = *i;
      basic_io::stanza st;

      {
        if (is_dir_t(curr))
          {
            st.push_str_pair(basic_io::syms::dir, i.path());
          }
        else
          {
            file_t ftmp = downcast_to_file_t(curr);
            st.push_str_pair(basic_io::syms::file, i.path());
            st.push_binary_pair(basic_io::syms::content, ftmp->content.inner());
          }
      }

      if (print_local_parts)
        {
          I(curr->self != the_null_node);
          st.push_str_pair(basic_io::syms::ident, lexical_cast<string>(curr->self));
        }

      // Push the non-dormant part of the attr map
      for (full_attr_map_t::const_iterator j = curr->attrs.begin();
           j != curr->attrs.end(); ++j)
        {
          if (j->second.first)
            {
              // L(FL("printing attr %s : %s = %s") % fp % j->first % j->second);
              st.push_str_triple(basic_io::syms::attr, j->first(), j->second.second());
            }
        }

      if (print_local_parts)
        {
          // Push the dormant part of the attr map
          for (full_attr_map_t::const_iterator j = curr->attrs.begin();
               j != curr->attrs.end(); ++j)
            {
              if (!j->second.first)
                {
                  I(j->second.second().empty());
                  st.push_str_pair(basic_io::syms::dormant_attr, j->first());
                }
            }

          marking_map::const_iterator m = mm.find(curr->self);
          I(m != mm.end());
          push_marking(st, is_file_t(curr), m->second);
        }

      pr.print_stanza(st);
    }
}

inline size_t
read_num(string const & s)
{
  size_t n = 0;

  for (string::const_iterator i = s.begin(); i != s.end(); i++)
    {
      I(*i >= '0' && *i <= '9');
      n *= 10;
      n += static_cast<size_t>(*i - '0');
    }
  return n;
}

void
roster_t::parse_from(basic_io::parser & pa,
                     marking_map & mm)
{
  // Instantiate some lookaside caches to ensure this roster reuses
  // string storage across ATOMIC elements.
  id::symtab id_syms;
  utf8::symtab path_syms;
  attr_key::symtab attr_key_syms;
  attr_value::symtab attr_value_syms;


  // We *always* parse the local part of a roster, because we do not
  // actually send the non-local part over the network; the only times
  // we serialize a manifest (non-local roster) is when we're printing
  // it out for a user, or when we're hashing it for a manifest ID.
  nodes.clear();
  root_dir.reset();
  mm.clear();

  {
    pa.esym(basic_io::syms::format_version);
    string vers;
    pa.str(vers);
    I(vers == "1");
  }

  while(pa.symp())
    {
      string pth, ident, rev;
      node_t n;

      if (pa.symp(basic_io::syms::file))
        {
          string content;
          pa.sym();
          pa.str(pth);
          pa.esym(basic_io::syms::content);
          pa.hex(content);
          pa.esym(basic_io::syms::ident);
          pa.str(ident);
          n = file_t(new file_node(read_num(ident),
                                   file_id(decode_hexenc(content))));
        }
      else if (pa.symp(basic_io::syms::dir))
        {
          pa.sym();
          pa.str(pth);
          pa.esym(basic_io::syms::ident);
          pa.str(ident);
          n = dir_t(new dir_node(read_num(ident)));
        }
      else
        break;

      I(static_cast<bool>(n));

      safe_insert(nodes, make_pair(n->self, n));
      if (is_dir_t(n) && pth.empty())
        {
          I(! has_root());
          root_dir = downcast_to_dir_t(n);
        }
      else
        {
          I(!pth.empty());
          attach_node(n->self, file_path_internal(pth));
        }

      // Non-dormant attrs
      while(pa.symp(basic_io::syms::attr))
        {
          pa.sym();
          string k, v;
          pa.str(k);
          pa.str(v);
          safe_insert(n->attrs, make_pair(attr_key(k),
                                          make_pair(true, attr_value(v))));
        }

      // Dormant attrs
      while(pa.symp(basic_io::syms::dormant_attr))
        {
          pa.sym();
          string k;
          pa.str(k);
          safe_insert(n->attrs, make_pair(attr_key(k),
                                          make_pair(false, attr_value())));
        }

      {
        marking_t & m(safe_insert(mm, make_pair(n->self, marking_t()))->second);
        parse_marking(pa, m);
      }
    }
}


void
read_roster_and_marking(roster_data const & dat,
                        roster_t & ros,
                        marking_map & mm)
{
  basic_io::input_source src(dat.inner()(), "roster");
  basic_io::tokenizer tok(src);
  basic_io::parser pars(tok);
  ros.parse_from(pars, mm);
  I(src.lookahead == EOF);
  ros.check_sane_against(mm);
}


static void
write_roster_and_marking(roster_t const & ros,
                         marking_map const & mm,
                         data & dat,
                         bool print_local_parts)
{
  if (print_local_parts)
    ros.check_sane_against(mm);
  else
    ros.check_sane(true);
  basic_io::printer pr;
  ros.print_to(pr, mm, print_local_parts);
  dat = data(pr.buf);
}


void
write_roster_and_marking(roster_t const & ros,
                         marking_map const & mm,
                         roster_data & dat)
{
  data tmp;
  write_roster_and_marking(ros, mm, tmp, true);
  dat = roster_data(tmp);
}


void
write_manifest_of_roster(roster_t const & ros,
                         manifest_data & dat)
{
  data tmp;
  marking_map mm;
  write_roster_and_marking(ros, mm, tmp, false);
  dat = manifest_data(tmp);
}

void calculate_ident(roster_t const & ros,
                     manifest_id & ident)
{
  manifest_data tmp;
  if (!ros.all_nodes().empty())
    {
      write_manifest_of_roster(ros, tmp);
      calculate_ident(tmp, ident);
    }
}

////////////////////////////////////////////////////////////////////
//   testing
////////////////////////////////////////////////////////////////////

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"
#include "sanity.hh"
#include "constants.hh"
#include "randomizer.hh"

#include "roster_delta.hh"

#include <cstdlib>
#include "lexical_cast.hh"

using std::logic_error;
using std::search;

using boost::shared_ptr;

static void
make_fake_marking_for(roster_t const & r, marking_map & mm)
{
  mm.clear();
  revision_id rid(decode_hexenc("0123456789abcdef0123456789abcdef01234567"));
  for (node_map::const_iterator i = r.all_nodes().begin(); i != r.all_nodes().end();
       ++i)
    {
      marking_t fake_marks;
      mark_new_node(rid, i->second, fake_marks);
      mm.insert(make_pair(i->first, fake_marks));
    }
}

static void
do_testing_on_one_roster(roster_t const & r)
{
  if (!r.has_root())
    {
      I(r.all_nodes().size() == 0);
      // not much testing to be done on an empty roster -- can't iterate over
      // it or read/write it.
      return;
    }

  MM(r);
  // test dfs_iter by making sure it returns the same number of items as there
  // are items in all_nodes()
  int n; MM(n);
  n = r.all_nodes().size();
  int dfs_counted = 0; MM(dfs_counted);
  for (dfs_iter i(downcast_to_dir_t(r.get_node(file_path())));
       !i.finished(); ++i)
    ++dfs_counted;
  I(n == dfs_counted);

  // Test dfs_iter's path calculations.
  for (dfs_iter i(downcast_to_dir_t(r.get_node(file_path())), true);
       !i.finished(); ++i)
    {
      file_path from_iter = file_path_internal(i.path());
      file_path from_getname;
      node_t curr = *i;
      r.get_name(curr->self, from_getname);
      I(from_iter == from_getname);
    }

  // do a read/write spin
  roster_data r_dat; MM(r_dat);
  marking_map fm;
  make_fake_marking_for(r, fm);
  write_roster_and_marking(r, fm, r_dat);
  roster_t r2; MM(r2);
  marking_map fm2;
  read_roster_and_marking(r_dat, r2, fm2);
  I(r == r2);
  I(fm == fm2);
  roster_data r2_dat; MM(r2_dat);
  write_roster_and_marking(r2, fm2, r2_dat);
  I(r_dat == r2_dat);
}

static void
do_testing_on_two_equivalent_csets(cset const & a, cset const & b)
{
  // we do all this reading/writing/comparing of both strings and objects to
  // cross-check the reading, writing, and comparison logic against each
  // other.  (if, say, there is a field in cset that == forgets to check but
  // that write remembers to include, this should catch it).
  MM(a);
  MM(b);
  I(a == b);

  data a_dat, b_dat, a2_dat, b2_dat;
  MM(a_dat);
  MM(b_dat);
  MM(a2_dat);
  MM(b2_dat);

  write_cset(a, a_dat);
  write_cset(b, b_dat);
  I(a_dat == b_dat);
  cset a2, b2;
  MM(a2);
  MM(b2);
  read_cset(a_dat, a2);
  read_cset(b_dat, b2);
  I(a2 == a);
  I(b2 == b);
  I(b2 == a);
  I(a2 == b);
  I(a2 == b2);
  write_cset(a2, a2_dat);
  write_cset(b2, b2_dat);
  I(a_dat == a2_dat);
  I(b_dat == b2_dat);
}

static void
apply_cset_and_do_testing(roster_t & r, cset const & cs, node_id_source & nis)
{
  MM(r);
  MM(cs);
  roster_t original = r;
  MM(original);
  I(original == r);

  editable_roster_base e(r, nis);
  cs.apply_to(e);

  cset derived;
  MM(derived);
  make_cset(original, r, derived);

  do_testing_on_two_equivalent_csets(cs, derived);
  do_testing_on_one_roster(r);
}

static void
tests_on_two_rosters(roster_t const & a, roster_t const & b, node_id_source & nis)
{
  MM(a);
  MM(b);

  do_testing_on_one_roster(a);
  do_testing_on_one_roster(b);

  cset a_to_b; MM(a_to_b);
  cset b_to_a; MM(b_to_a);
  make_cset(a, b, a_to_b);
  make_cset(b, a, b_to_a);
  roster_t a2(b); MM(a2);
  roster_t b2(a); MM(b2);
  // we can't use a cset to entirely empty out a roster, so don't bother doing
  // the apply_to tests towards an empty roster
  // (NOTE: if you notice this special case in a time when root dirs can be
  // renamed or deleted, remove it, it will no longer be necessary.
  if (!a.all_nodes().empty())
    {
      editable_roster_base eb(a2, nis);
      b_to_a.apply_to(eb);
    }
  else
    a2 = a;
  if (!b.all_nodes().empty())
    {
      editable_roster_base ea(b2, nis);
      a_to_b.apply_to(ea);
    }
  else
    b2 = b;
  // We'd like to assert that a2 == a and b2 == b, but we can't, because they
  // will have new ids assigned.
  // But they _will_ have the same manifests, assuming things are working
  // correctly.
  manifest_data a_dat; MM(a_dat);
  manifest_data a2_dat; MM(a2_dat);
  manifest_data b_dat; MM(b_dat);
  manifest_data b2_dat; MM(b2_dat);
  if (a.has_root())
    write_manifest_of_roster(a, a_dat);
  if (a2.has_root())
    write_manifest_of_roster(a2, a2_dat);
  if (b.has_root())
    write_manifest_of_roster(b, b_dat);
  if (b2.has_root())
    write_manifest_of_roster(b2, b2_dat);
  I(a_dat == a2_dat);
  I(b_dat == b2_dat);

  cset a2_to_b2; MM(a2_to_b2);
  cset b2_to_a2; MM(b2_to_a2);
  make_cset(a2, b2, a2_to_b2);
  make_cset(b2, a2, b2_to_a2);
  do_testing_on_two_equivalent_csets(a_to_b, a2_to_b2);
  do_testing_on_two_equivalent_csets(b_to_a, b2_to_a2);

  {
    marking_map a_marking;
    make_fake_marking_for(a, a_marking);
    marking_map b_marking;
    make_fake_marking_for(b, b_marking);
    test_roster_delta_on(a, a_marking, b, b_marking);
  }
}

template<typename M>
typename M::const_iterator
random_element(M const & m, randomizer & rng)
{
  size_t i = rng.uniform(m.size());
  typename M::const_iterator j = m.begin();
  while (i > 0)
    {
      I(j != m.end());
      --i;
      ++j;
    }
  return j;
}

string new_word(randomizer & rng)
{
  static string wordchars = "abcdefghijlkmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  static unsigned tick = 0;
  string tmp;
  do
    {
      tmp += wordchars[rng.uniform(wordchars.size())];
    }
  while (tmp.size() < 10 && !rng.flip(10));
  return tmp + lexical_cast<string>(tick++);
}

file_id new_ident(randomizer & rng)
{
  static string tab = "0123456789abcdef";
  string tmp;
  tmp.reserve(constants::idlen);
  for (unsigned i = 0; i < constants::idlen; ++i)
    tmp += tab[rng.uniform(tab.size())];
  return file_id(decode_hexenc(tmp));
}

path_component new_component(randomizer & rng)
{
  return path_component(new_word(rng));
}


attr_key pick_attr(full_attr_map_t const & attrs, randomizer & rng)
{
  return random_element(attrs, rng)->first;
}

attr_key pick_attr(attr_map_t const & attrs, randomizer & rng)
{
  return random_element(attrs, rng)->first;
}

bool parent_of(file_path const & p,
               file_path const & c)
{
  bool is_parent = false;

  // the null path is the parent of all paths.
  if (p.depth() == 0)
    is_parent = true;

  else if (p.depth() <= c.depth())
    {
      string const & ci = c.as_internal();
      string const & pi = p.as_internal();

      string::const_iterator c_anchor =
        search(ci.begin(), ci.end(),
               pi.begin(), pi.end());

      is_parent = (c_anchor == ci.begin() && (ci.size() == pi.size()
                                              || *(ci.begin() + pi.size())
                                              == '/'));
    }

  //     L(FL("path '%s' is%s parent of '%s'")
  //       % p
  //       % (is_parent ? "" : " not")
  //       % c);

  return is_parent;
}

void perform_random_action(roster_t & r, node_id_source & nis, randomizer & rng)
{
  cset c;
  I(r.has_root());
  while (c.empty())
    {
      node_t n = random_element(r.all_nodes(), rng)->second;
      file_path pth;
      r.get_name(n->self, pth);
      // L(FL("considering acting on '%s'") % pth);

      switch (rng.uniform(7))
        {
        default:
        case 0:
        case 1:
        case 2:
          if (is_file_t(n) || (pth.depth() > 1 && rng.flip()))
            // Add a sibling of an existing entry.
            pth = pth.dirname() / new_component(rng);

          else
            // Add a child of an existing entry.
            pth = pth / new_component(rng);

          if (rng.flip())
            {
              // L(FL("adding dir '%s'") % pth);
              safe_insert(c.dirs_added, pth);
            }
          else
            {
              // L(FL("adding file '%s'") % pth);
              safe_insert(c.files_added, make_pair(pth, new_ident(rng)));
            }
          break;

        case 3:
          if (is_file_t(n))
            {
              // L(FL("altering content of file '%s'") % pth);
              safe_insert(c.deltas_applied,
                          make_pair(pth,
                                    make_pair(downcast_to_file_t(n)->content,
                                              new_ident(rng))));
            }
          break;

        case 4:
          {
            node_t n2 = random_element(r.all_nodes(), rng)->second;
            if (n == n2)
              continue;

            file_path pth2;
            r.get_name(n2->self, pth2);

            if (is_file_t(n2) || (pth2.depth() > 1 && rng.flip()))
              {
                // L(FL("renaming to a sibling of an existing entry '%s'")
                //   % pth2);
                // Move to a sibling of an existing entry.
                pth2 = pth2.dirname() / new_component(rng);
              }

            else
              {
                // L(FL("renaming to a child of an existing entry '%s'")
                //   % pth2);
                // Move to a child of an existing entry.
                pth2 = pth2 / new_component(rng);
              }

            if (!parent_of(pth, pth2))
              {
                // L(FL("renaming '%s' -> '%s") % pth % pth2);
                safe_insert(c.nodes_renamed, make_pair(pth, pth2));
              }
          }
          break;

        case 5:
          if (!null_node(n->parent)
              && (is_file_t(n) || downcast_to_dir_t(n)->children.empty())
              && r.all_nodes().size() > 1) // do not delete the root
            {
              // L(FL("deleting '%s'") % pth);
              safe_insert(c.nodes_deleted, pth);
            }
          break;

        case 6:
          if (!n->attrs.empty() && rng.flip())
            {
              attr_key k = pick_attr(n->attrs, rng);
              if (safe_get(n->attrs, k).first)
                {
                  if (rng.flip())
                    {
                      // L(FL("clearing attr on '%s'") % pth);
                      safe_insert(c.attrs_cleared, make_pair(pth, k));
                    }
                  else
                    {
                      // L(FL("changing attr on '%s'\n") % pth);
                      safe_insert(c.attrs_set,
                                  make_pair(make_pair(pth, k), new_word(rng)));
                    }
                }
              else
                {
                  // L(FL("setting previously set attr on '%s'") % pth);
                  safe_insert(c.attrs_set,
                              make_pair(make_pair(pth, k), new_word(rng)));
                }
            }
          else
            {
              // L(FL("setting attr on '%s'") % pth);
              safe_insert(c.attrs_set,
                          make_pair(make_pair(pth, new_word(rng)),
                                    new_word(rng)));
            }
          break;
        }
    }
  // now do it
  apply_cset_and_do_testing(r, c, nis);
}

testing_node_id_source::testing_node_id_source()
  : curr(first_node)
{}

node_id
testing_node_id_source::next()
{
  // L(FL("creating node %x") % curr);
  node_id n = curr++;
  I(!temp_node(n));
  return n;
}

template <> void
dump(int const & i, string & out)
{
  out = lexical_cast<string>(i) + "\n";
}

UNIT_TEST(roster, random_actions)
{
  randomizer rng;
  roster_t r;
  testing_node_id_source nis;

  roster_t empty, prev, recent, ancient;

  {
    // give all the rosters a root
    cset c;
    c.dirs_added.insert(file_path());
    apply_cset_and_do_testing(r, c, nis);
  }

  empty = ancient = recent = prev = r;
  for (int i = 0; i < 2000; )
    {
      int manychanges = 100 + rng.uniform(300);
      // P(F("random roster actions: outer step at %d, making %d changes")
      //   % i % manychanges);

      for (int outer_limit = i + manychanges; i < outer_limit; )
        {
          int fewchanges = 5 + rng.uniform(10);
          // P(F("random roster actions: inner step at %d, making %d changes")
          //   % i % fewchanges);

          for (int inner_limit = i + fewchanges; i < inner_limit; i++)
            {
              // P(F("random roster actions: change %d") % i);
              perform_random_action(r, nis, rng);
              I(!(prev == r));
              prev = r;
            }
          tests_on_two_rosters(recent, r, nis);
          tests_on_two_rosters(empty, r, nis);
          recent = r;
        }
      tests_on_two_rosters(ancient, r, nis);
      ancient = r;
    }
}

// some of our raising operations leave our state corrupted.  so rather than
// trying to do all the illegal things in one pass, we re-run this function a
// bunch of times, and each time we do only one of these potentially
// corrupting tests.  Test numbers are in the range [0, total).

#define MAYBE(code) if (total == to_run) { L(FL(#code)); code; return; } ++total

static void
check_sane_roster_do_tests(int to_run, int& total)
{
  total = 0;
  testing_node_id_source nis;
  roster_t r;
  MM(r);

  // roster must have a root dir
  MAYBE(UNIT_TEST_CHECK_THROW(r.check_sane(false), logic_error));
  MAYBE(UNIT_TEST_CHECK_THROW(r.check_sane(true), logic_error));

  file_path fp_;
  file_path fp_foo = file_path_internal("foo");
  file_path fp_foo_bar = file_path_internal("foo/bar");
  file_path fp_foo_baz = file_path_internal("foo/baz");

  node_id nid_f = r.create_file_node(file_id(decode_hexenc("0000000000000000000000000000000000000000")),
                                     nis);
  // root must be a directory, not a file
  MAYBE(UNIT_TEST_CHECK_THROW(r.attach_node(nid_f, fp_), logic_error));

  node_id root_dir = r.create_dir_node(nis);
  r.attach_node(root_dir, fp_);
  // has a root dir, but a detached file
  MAYBE(UNIT_TEST_CHECK_THROW(r.check_sane(false), logic_error));
  MAYBE(UNIT_TEST_CHECK_THROW(r.check_sane(true), logic_error));

  r.attach_node(nid_f, fp_foo);
  // now should be sane
  UNIT_TEST_CHECK_NOT_THROW(r.check_sane(false), logic_error);
  UNIT_TEST_CHECK_NOT_THROW(r.check_sane(true), logic_error);

  node_id nid_d = r.create_dir_node(nis);
  // if "foo" exists, can't attach another node at "foo"
  MAYBE(UNIT_TEST_CHECK_THROW(r.attach_node(nid_d, fp_foo), logic_error));
  // if "foo" is a file, can't attach a node at "foo/bar"
  MAYBE(UNIT_TEST_CHECK_THROW(r.attach_node(nid_d, fp_foo_bar), logic_error));

  UNIT_TEST_CHECK(r.detach_node(fp_foo) == nid_f);
  r.attach_node(nid_d, fp_foo);
  r.attach_node(nid_f, fp_foo_bar);
  UNIT_TEST_CHECK_NOT_THROW(r.check_sane(false), logic_error);
  UNIT_TEST_CHECK_NOT_THROW(r.check_sane(true), logic_error);

  temp_node_id_source nis_tmp;
  node_id nid_tmp = r.create_dir_node(nis_tmp);
  // has a detached node
  MAYBE(UNIT_TEST_CHECK_THROW(r.check_sane(false), logic_error));
  MAYBE(UNIT_TEST_CHECK_THROW(r.check_sane(true), logic_error));
  r.attach_node(nid_tmp, fp_foo_baz);
  // now has no detached nodes, but one temp node
  MAYBE(UNIT_TEST_CHECK_THROW(r.check_sane(false), logic_error));
  UNIT_TEST_CHECK_NOT_THROW(r.check_sane(true), logic_error);
}

#undef MAYBE

UNIT_TEST(roster, check_sane_roster)
{
  int total;
  check_sane_roster_do_tests(-1, total);
  for (int to_run = 0; to_run < total; ++to_run)
    {
      L(FL("check_sane_roster_test: loop = %i (of %i)") % to_run % (total - 1));
      int tmp;
      check_sane_roster_do_tests(to_run, tmp);
    }
}

UNIT_TEST(roster, check_sane_roster_loop)
{
  testing_node_id_source nis;
  roster_t r; MM(r);
  file_path root;
  r.attach_node(r.create_dir_node(nis), root);
  node_id nid_foo = r.create_dir_node(nis);
  node_id nid_bar = r.create_dir_node(nis);
  r.attach_node(nid_foo, nid_bar, path_component("foo"));
  r.attach_node(nid_bar, nid_foo, path_component("bar"));
  UNIT_TEST_CHECK_THROW(r.check_sane(true), logic_error);
}

UNIT_TEST(roster, check_sane_roster_screwy_dir_map)
{
  testing_node_id_source nis;
  roster_t r; MM(r);
  file_path root;
  r.attach_node(r.create_dir_node(nis), root);
  roster_t other; MM(other);
  node_id other_nid = other.create_dir_node(nis);
  dir_t root_n = downcast_to_dir_t(r.get_node(root));
  root_n->children.insert(make_pair(path_component("foo"),
                                    other.get_node(other_nid)));
  UNIT_TEST_CHECK_THROW(r.check_sane(), logic_error);
  // well, but that one was easy, actually, because a dir traversal will hit
  // more nodes than actually exist... so let's make it harder, by making sure
  // that a dir traversal will hit exactly as many nodes as actually exist.
  node_id distractor_nid = r.create_dir_node(nis);
  UNIT_TEST_CHECK_THROW(r.check_sane(), logic_error);
  // and even harder, by making that node superficially valid too
  dir_t distractor_n = downcast_to_dir_t(r.get_node(distractor_nid));
  distractor_n->parent = distractor_nid;
  distractor_n->name = path_component("foo");
  distractor_n->children.insert(make_pair(distractor_n->name, distractor_n));
  UNIT_TEST_CHECK_THROW(r.check_sane(), logic_error);
}

UNIT_TEST(roster, bad_attr)
{
  testing_node_id_source nis;
  roster_t r; MM(r);
  file_path root;
  r.attach_node(r.create_dir_node(nis), root);
  UNIT_TEST_CHECK_THROW(r.set_attr(root, attr_key("test_key1"),
                               make_pair(false, attr_value("invalid"))),
                    logic_error);
  UNIT_TEST_CHECK_NOT_THROW(r.check_sane(true), logic_error);
  safe_insert(r.get_node(root)->attrs,
              make_pair(attr_key("test_key2"),
                        make_pair(false, attr_value("invalid"))));
  UNIT_TEST_CHECK_THROW(r.check_sane(true), logic_error);
}

////////////////////////////////////////////////////////////////////////
// exhaustive marking tests
////////////////////////////////////////////////////////////////////////

// The marking/roster generation code is extremely critical.  It is the very
// core of monotone's versioning technology, very complex, and bugs can result
// in corrupt and nonsensical histories (not to mention erroneous merges and
// the like).  Furthermore, the code that implements it is littered with
// case-by-case analysis, where copy-paste errors could easily occur.  So the
// purpose of this section is to systematically and exhaustively test every
// possible case.
//
// Our underlying merger, *-merge, works on scalars, case-by-case.
// The cases are:
//   0 parent:
//       a*
//   1 parent:
//       a     a
//       |     |
//       a     b*
//   2 parents:
//       a   a  a   a  a   b  a   b
//        \ /    \ /    \ /    \ /
//         a      b*     c*     a?
//
// Each node has a number of scalars associated with it:
//   * basename+parent
//   * file content (iff a file)
//   * attributes
//
// So for each scalar, we want to test each way it can appear in each of the
// above shapes.  This is made more complex by lifecycles.  We can achieve a 0
// parent node as:
//   * a node in a 0-parent roster (root revision)
//   * a newly added node in a 1-parent roster
//   * a newly added node in a 2-parent roster
// a 1 parent node as:
//   * a pre-existing node in a 1-parent roster
//   * a node in a 2-parent roster that only existed in one of the parents
// a 2 parent node as:
//   * a pre-existing node in a 2-parent roster
//
// Because the basename+parent and file_content scalars have lifetimes that
// exactly match the lifetime of the node they are on, those are all the cases
// for these scalars.  However, attrs make things a bit more complicated,
// because they can be added.  An attr can have 0 parents:
//   * in any of the above cases, with an attribute newly added on the node
// And one parent:
//   * in any of the cases above with one node parent and the attr pre-existing
//   * in a 2-parent node where the attr exists in only one of the parents
//
// Plus, just to be sure, in the merge cases we check both the given example
// and the mirror-reversed one, since the code implementing this could
// conceivably mark merge(A, B) right but get merge(B, A) wrong.  And for the
// scalars that can appear on either files or dirs, we check both.

// The following somewhat elaborate code implements all these checks.  The
// most important background assumption to know, is that it always assumes
// (and this assumption is hard-coded in various places) that it is looking at
// one of the following topologies:
//
//     old
//
//     old
//      |
//     new
//
//     old
//     / \.
// left   right
//     \ /
//     new
//
// There is various tricksiness in making sure that the root directory always
// has the right birth_revision, that nodes are created with good birth
// revisions and sane markings on the scalars we are not interested in, etc.
// This code is ugly and messy and could use refactoring, but it seems to
// work.

////////////////
// These are some basic utility pieces handy for the exhaustive mark tests

namespace
{
  template <typename T> set<T>
  singleton(T const & t)
  {
    set<T> s;
    s.insert(t);
    return s;
  }

  template <typename T> set<T>
  doubleton(T const & t1, T const & t2)
  {
    set<T> s;
    s.insert(t1);
    s.insert(t2);
    return s;
  }

  revision_id old_rid(string(constants::idlen_bytes, '\x00'));
  revision_id left_rid(string(constants::idlen_bytes, '\x11'));
  revision_id right_rid(string(constants::idlen_bytes, '\x22'));
  revision_id new_rid(string(constants::idlen_bytes, '\x44'));

////////////////
// These classes encapsulate information about all the different scalars
// that *-merge applies to.

  typedef enum { scalar_a, scalar_b, scalar_c,
                 scalar_none, scalar_none_2 } scalar_val;

  void
  dump(scalar_val const & val, string & out)
  {
    switch (val)
      {
      case scalar_a: out = "scalar_a"; break;
      case scalar_b: out = "scalar_b"; break;
      case scalar_c: out = "scalar_c"; break;
      case scalar_none: out = "scalar_none"; break;
      case scalar_none_2: out = "scalar_none_2"; break;
      }
    out += "\n";
  }

  struct a_scalar
  {
    // Must use std::set in arguments to avoid "changes meaning" errors.
    virtual void set(revision_id const & scalar_origin_rid,
                     scalar_val val,
                     std::set<revision_id> const & this_scalar_mark,
                     roster_t & roster, marking_map & markings)
      = 0;
    virtual ~a_scalar() {};

    node_id_source & nis;
    node_id const root_nid;
    node_id const obj_under_test_nid;
    a_scalar(node_id_source & nis)
      : nis(nis), root_nid(nis.next()), obj_under_test_nid(nis.next())
    {}

    void setup(roster_t & roster, marking_map & markings)
    {
      roster.create_dir_node(root_nid);
      roster.attach_node(root_nid, file_path_internal(""));
      marking_t marking;
      marking.birth_revision = old_rid;
      marking.parent_name.insert(old_rid);
      safe_insert(markings, make_pair(root_nid, marking));
    }

    virtual string my_type() const = 0;

    virtual void dump(string & out) const
    {
      ostringstream oss;
      oss << "type: " << my_type() << '\n'
          << "root_nid: " << root_nid << '\n'
          << "obj_under_test_nid: " << obj_under_test_nid << '\n';
      out = oss.str();
    }
  };

  void
  dump(a_scalar const & s, string & out)
  {
    s.dump(out);
  }

  struct file_maker
  {
    static void make_obj(revision_id const & scalar_origin_rid, node_id nid,
                         roster_t & roster, marking_map & markings)
    {
      make_file(scalar_origin_rid, nid,
                file_id(string(constants::idlen_bytes, '\xaa')),
                roster, markings);
    }
    static void make_file(revision_id const & scalar_origin_rid, node_id nid,
                          file_id const & fid,
                          roster_t & roster, marking_map & markings)
    {
      roster.create_file_node(fid, nid);
      marking_t marking;
      marking.birth_revision = scalar_origin_rid;
      marking.parent_name = marking.file_content = singleton(scalar_origin_rid);
      safe_insert(markings, make_pair(nid, marking));
    }
  };

  struct dir_maker
  {
    static void make_obj(revision_id const & scalar_origin_rid, node_id nid,
                         roster_t & roster, marking_map & markings)
    {
      roster.create_dir_node(nid);
      marking_t marking;
      marking.birth_revision = scalar_origin_rid;
      marking.parent_name = singleton(scalar_origin_rid);
      safe_insert(markings, make_pair(nid, marking));
    }
  };

  struct file_content_scalar : public a_scalar
  {
    virtual string my_type() const { return "file_content_scalar"; }

    map<scalar_val, file_id> values;
    file_content_scalar(node_id_source & nis)
      : a_scalar(nis)
    {
      safe_insert(values,
                  make_pair(scalar_a,
                            file_id(string(constants::idlen_bytes, '\xaa'))));
      safe_insert(values,
                  make_pair(scalar_b,
                            file_id(string(constants::idlen_bytes, '\xbb'))));
      safe_insert(values,
                  make_pair(scalar_c,
                            file_id(string(constants::idlen_bytes, '\xcc'))));
    }
    virtual void
    set(revision_id const & scalar_origin_rid, scalar_val val,
        std::set<revision_id> const & this_scalar_mark,
        roster_t & roster, marking_map & markings)
    {
      setup(roster, markings);
      if (val != scalar_none)
        {
          file_maker::make_file(scalar_origin_rid, obj_under_test_nid,
                                safe_get(values, val),
                                roster, markings);
          roster.attach_node(obj_under_test_nid, file_path_internal("foo"));
          markings[obj_under_test_nid].file_content = this_scalar_mark;
        }
      roster.check_sane_against(markings);
    }
  };

  template <typename T>
  struct X_basename_scalar : public a_scalar
  {
    virtual string my_type() const { return "X_basename_scalar"; }

    map<scalar_val, file_path> values;
    X_basename_scalar(node_id_source & nis)
      : a_scalar(nis)
    {
      safe_insert(values, make_pair(scalar_a, file_path_internal("a")));
      safe_insert(values, make_pair(scalar_b, file_path_internal("b")));
      safe_insert(values, make_pair(scalar_c, file_path_internal("c")));
    }
    virtual void
    set(revision_id const & scalar_origin_rid, scalar_val val,
        std::set<revision_id> const & this_scalar_mark,
        roster_t & roster, marking_map & markings)
    {
      setup(roster, markings);
      if (val != scalar_none)
        {
          T::make_obj(scalar_origin_rid, obj_under_test_nid, roster, markings);
          roster.attach_node(obj_under_test_nid, safe_get(values, val));
          markings[obj_under_test_nid].parent_name = this_scalar_mark;
        }
      roster.check_sane_against(markings);
    }
  };

  template <typename T>
  struct X_parent_scalar : public a_scalar
  {
    virtual string my_type() const { return "X_parent_scalar"; }

    map<scalar_val, file_path> values;
    node_id const a_nid, b_nid, c_nid;
    X_parent_scalar(node_id_source & nis)
      : a_scalar(nis), a_nid(nis.next()), b_nid(nis.next()), c_nid(nis.next())
    {
      safe_insert(values, make_pair(scalar_a, file_path_internal("dir_a/foo")));
      safe_insert(values, make_pair(scalar_b, file_path_internal("dir_b/foo")));
      safe_insert(values, make_pair(scalar_c, file_path_internal("dir_c/foo")));
    }
    void
    setup_dirs(roster_t & roster, marking_map & markings)
    {
      roster.create_dir_node(a_nid);
      roster.attach_node(a_nid, file_path_internal("dir_a"));
      roster.create_dir_node(b_nid);
      roster.attach_node(b_nid, file_path_internal("dir_b"));
      roster.create_dir_node(c_nid);
      roster.attach_node(c_nid, file_path_internal("dir_c"));
      marking_t marking;
      marking.birth_revision = old_rid;
      marking.parent_name.insert(old_rid);
      safe_insert(markings, make_pair(a_nid, marking));
      safe_insert(markings, make_pair(b_nid, marking));
      safe_insert(markings, make_pair(c_nid, marking));
    }
    virtual void
    set(revision_id const & scalar_origin_rid, scalar_val val,
        std::set<revision_id> const & this_scalar_mark,
        roster_t & roster, marking_map & markings)
    {
      setup(roster, markings);
      setup_dirs(roster, markings);
      if (val != scalar_none)
        {
          T::make_obj(scalar_origin_rid, obj_under_test_nid, roster, markings);
          roster.attach_node(obj_under_test_nid, safe_get(values, val));
          markings[obj_under_test_nid].parent_name = this_scalar_mark;
        }
      roster.check_sane_against(markings);
    }
  };

  // this scalar represents an attr whose node already exists, and we put an
  // attr on it.
  template <typename T>
  struct X_attr_existing_node_scalar : public a_scalar
  {
    virtual string my_type() const { return "X_attr_scalar"; }

    map<scalar_val, pair<bool, attr_value> > values;
    X_attr_existing_node_scalar(node_id_source & nis)
      : a_scalar(nis)
    {
      safe_insert(values, make_pair(scalar_a, make_pair(true, attr_value("a"))));
      safe_insert(values, make_pair(scalar_b, make_pair(true, attr_value("b"))));
      safe_insert(values, make_pair(scalar_c, make_pair(true, attr_value("c"))));
    }
    virtual void
    set(revision_id const & scalar_origin_rid, scalar_val val,
        std::set<revision_id> const & this_scalar_mark,
        roster_t & roster, marking_map & markings)
    {
      setup(roster, markings);
      // _not_ scalar_origin_rid, because our object exists everywhere, regardless of
      // when the attr shows up
      T::make_obj(old_rid, obj_under_test_nid, roster, markings);
      roster.attach_node(obj_under_test_nid, file_path_internal("foo"));
      if (val != scalar_none)
        {
          safe_insert(roster.get_node(obj_under_test_nid)->attrs,
                      make_pair(attr_key("test_key"), safe_get(values, val)));
          markings[obj_under_test_nid].attrs[attr_key("test_key")] = this_scalar_mark;
        }
      roster.check_sane_against(markings);
    }
  };

  // this scalar represents an attr whose node does not exist; we create the
  // node when we create the attr.
  template <typename T>
  struct X_attr_new_node_scalar : public a_scalar
  {
    virtual string my_type() const { return "X_attr_scalar"; }

    map<scalar_val, pair<bool, attr_value> > values;
    X_attr_new_node_scalar(node_id_source & nis)
      : a_scalar(nis)
    {
      safe_insert(values, make_pair(scalar_a, make_pair(true, attr_value("a"))));
      safe_insert(values, make_pair(scalar_b, make_pair(true, attr_value("b"))));
      safe_insert(values, make_pair(scalar_c, make_pair(true, attr_value("c"))));
    }
    virtual void
    set(revision_id const & scalar_origin_rid, scalar_val val,
        std::set<revision_id> const & this_scalar_mark,
        roster_t & roster, marking_map & markings)
    {
      setup(roster, markings);
      if (val != scalar_none)
        {
          T::make_obj(scalar_origin_rid, obj_under_test_nid, roster, markings);
          roster.attach_node(obj_under_test_nid, file_path_internal("foo"));
          safe_insert(roster.get_node(obj_under_test_nid)->attrs,
                      make_pair(attr_key("test_key"), safe_get(values, val)));
          markings[obj_under_test_nid].attrs[attr_key("test_key")] = this_scalar_mark;
        }
      roster.check_sane_against(markings);
    }
  };

  typedef vector<shared_ptr<a_scalar> > scalars;
  scalars
  all_scalars(node_id_source & nis)
  {
    scalars ss;
    ss.push_back(shared_ptr<a_scalar>(new file_content_scalar(nis)));
    ss.push_back(shared_ptr<a_scalar>(new X_basename_scalar<file_maker>(nis)));
    ss.push_back(shared_ptr<a_scalar>(new X_basename_scalar<dir_maker>(nis)));
    ss.push_back(shared_ptr<a_scalar>(new X_parent_scalar<file_maker>(nis)));
    ss.push_back(shared_ptr<a_scalar>(new X_parent_scalar<dir_maker>(nis)));
    ss.push_back(shared_ptr<a_scalar>(new X_attr_existing_node_scalar<file_maker>(nis)));
    ss.push_back(shared_ptr<a_scalar>(new X_attr_existing_node_scalar<dir_maker>(nis)));
    ss.push_back(shared_ptr<a_scalar>(new X_attr_new_node_scalar<file_maker>(nis)));
    ss.push_back(shared_ptr<a_scalar>(new X_attr_new_node_scalar<dir_maker>(nis)));
    return ss;
  }
}

////////////////
// These functions encapsulate the logic for running a particular mark
// scenario with a particular scalar with 0, 1, or 2 roster parents.

static void
run_with_0_roster_parents(a_scalar & s, revision_id scalar_origin_rid,
                          scalar_val new_val,
                          set<revision_id> const & new_mark_set,
                          node_id_source & nis)
{
  MM(s);
  MM(scalar_origin_rid);
  MM(new_val);
  MM(new_mark_set);
  roster_t expected_roster; MM(expected_roster);
  marking_map expected_markings; MM(expected_markings);

  s.set(scalar_origin_rid, new_val, new_mark_set, expected_roster, expected_markings);

  roster_t empty_roster;
  cset cs; MM(cs);
  make_cset(empty_roster, expected_roster, cs);

  roster_t new_roster; MM(new_roster);
  marking_map new_markings; MM(new_markings);
  // this function takes the old parent roster/marking and modifies them; in
  // our case, the parent roster/marking are empty, and so are our
  // roster/marking, so we don't need to do anything special.
  make_roster_for_nonmerge(cs, old_rid, new_roster, new_markings, nis);

  I(equal_up_to_renumbering(expected_roster, expected_markings,
                            new_roster, new_markings));

  marking_map new_markings2; MM(new_markings2);
  mark_roster_with_no_parents(old_rid, new_roster, new_markings2);
  I(new_markings == new_markings2);

  marking_map new_markings3; MM(new_markings3);
  roster_t parent3;
  marking_map old_markings3;
  mark_roster_with_one_parent(parent3, old_markings3, old_rid, new_roster,
                              new_markings3);
  I(new_markings == new_markings3);
}

static void
run_with_1_roster_parent(a_scalar & s,
                         revision_id scalar_origin_rid,
                         scalar_val parent_val,
                         set<revision_id> const & parent_mark_set,
                         scalar_val new_val,
                         set<revision_id> const & new_mark_set,
                         node_id_source & nis)
{
  MM(s);
  MM(scalar_origin_rid);
  MM(parent_val);
  MM(parent_mark_set);
  MM(new_val);
  MM(new_mark_set);
  roster_t parent_roster; MM(parent_roster);
  marking_map parent_markings; MM(parent_markings);
  roster_t expected_roster; MM(expected_roster);
  marking_map expected_markings; MM(expected_markings);

  s.set(scalar_origin_rid, parent_val, parent_mark_set, parent_roster, parent_markings);
  s.set(scalar_origin_rid, new_val, new_mark_set, expected_roster, expected_markings);

  cset cs; MM(cs);
  make_cset(parent_roster, expected_roster, cs);

  roster_t new_roster; MM(new_roster);
  marking_map new_markings; MM(new_markings);
  new_roster = parent_roster;
  new_markings = parent_markings;
  make_roster_for_nonmerge(cs, new_rid, new_roster, new_markings, nis);

  I(equal_up_to_renumbering(expected_roster, expected_markings,
                            new_roster, new_markings));

  marking_map new_markings2; MM(new_markings2);
  mark_roster_with_one_parent(parent_roster, parent_markings,
                              new_rid, new_roster, new_markings2);
  I(new_markings == new_markings2);
}

static void
run_with_2_roster_parents(a_scalar & s,
                          revision_id scalar_origin_rid,
                          scalar_val left_val,
                          set<revision_id> const & left_mark_set,
                          scalar_val right_val,
                          set<revision_id> const & right_mark_set,
                          scalar_val new_val,
                          set<revision_id> const & new_mark_set,
                          node_id_source & nis)
{
  MM(s);
  MM(scalar_origin_rid);
  MM(left_val);
  MM(left_mark_set);
  MM(right_val);
  MM(right_mark_set);
  MM(new_val);
  MM(new_mark_set);
  roster_t left_roster; MM(left_roster);
  roster_t right_roster; MM(right_roster);
  roster_t expected_roster; MM(expected_roster);
  marking_map left_markings; MM(left_markings);
  marking_map right_markings; MM(right_markings);
  marking_map expected_markings; MM(expected_markings);

  s.set(scalar_origin_rid, left_val, left_mark_set, left_roster, left_markings);
  s.set(scalar_origin_rid, right_val, right_mark_set, right_roster, right_markings);
  s.set(scalar_origin_rid, new_val, new_mark_set, expected_roster, expected_markings);

  cset left_cs; MM(left_cs);
  cset right_cs; MM(right_cs);
  make_cset(left_roster, expected_roster, left_cs);
  make_cset(right_roster, expected_roster, right_cs);

  set<revision_id> left_uncommon_ancestors; MM(left_uncommon_ancestors);
  left_uncommon_ancestors.insert(left_rid);
  set<revision_id> right_uncommon_ancestors; MM(right_uncommon_ancestors);
  right_uncommon_ancestors.insert(right_rid);

  roster_t new_roster; MM(new_roster);
  marking_map new_markings; MM(new_markings);
  make_roster_for_merge(left_rid, left_roster, left_markings, left_cs,
                        left_uncommon_ancestors,
                        right_rid, right_roster, right_markings, right_cs,
                        right_uncommon_ancestors,
                        new_rid, new_roster, new_markings,
                        nis);

  I(equal_up_to_renumbering(expected_roster, expected_markings,
                            new_roster, new_markings));
}

////////////////
// These functions encapsulate all the different ways to get a 0 parent node,
// a 1 parent node, and a 2 parent node.

////////////////
// These functions encapsulate all the different ways to get a 0 parent
// scalar, a 1 parent scalar, and a 2 parent scalar.

// FIXME: have clients just use s.nis instead of passing it separately...?

static void
run_a_2_scalar_parent_mark_scenario_exact(revision_id const & scalar_origin_rid,
                                          scalar_val left_val,
                                          set<revision_id> const & left_mark_set,
                                          scalar_val right_val,
                                          set<revision_id> const & right_mark_set,
                                          scalar_val new_val,
                                          set<revision_id> const & new_mark_set)
{
  testing_node_id_source nis;
  scalars ss = all_scalars(nis);
  for (scalars::const_iterator i = ss.begin(); i != ss.end(); ++i)
    {
      run_with_2_roster_parents(**i, scalar_origin_rid,
                                left_val, left_mark_set,
                                right_val, right_mark_set,
                                new_val, new_mark_set,
                                nis);
    }
}

static revision_id
flip_revision_id(revision_id const & rid)
{
  if (rid == old_rid || rid == new_rid)
    return rid;
  else if (rid == left_rid)
    return right_rid;
  else if (rid == right_rid)
    return left_rid;
  else
    I(false);
}

static set<revision_id>
flip_revision(set<revision_id> const & rids)
{
  set<revision_id> flipped_rids;
  for (set<revision_id>::const_iterator i = rids.begin(); i != rids.end(); ++i)
    flipped_rids.insert(flip_revision_id(*i));
  return flipped_rids;
}

static void
run_a_2_scalar_parent_mark_scenario(revision_id const & scalar_origin_rid,
                                    scalar_val left_val,
                                    set<revision_id> const & left_mark_set,
                                    scalar_val right_val,
                                    set<revision_id> const & right_mark_set,
                                    scalar_val new_val,
                                    set<revision_id> const & new_mark_set)
{
  // run both what we're given...
  run_a_2_scalar_parent_mark_scenario_exact(scalar_origin_rid,
                                            left_val, left_mark_set,
                                            right_val, right_mark_set,
                                            new_val, new_mark_set);
  // ...and its symmetric reflection.  but we have to flip the mark set,
  // because the exact stuff has hard-coded the names of the various
  // revisions and their uncommon ancestor sets.
  {
    set<revision_id> flipped_left_mark_set = flip_revision(left_mark_set);
    set<revision_id> flipped_right_mark_set = flip_revision(right_mark_set);
    set<revision_id> flipped_new_mark_set = flip_revision(new_mark_set);

    run_a_2_scalar_parent_mark_scenario_exact(flip_revision_id(scalar_origin_rid),
                                              right_val, flipped_right_mark_set,
                                              left_val, flipped_left_mark_set,
                                              new_val, flipped_new_mark_set);
  }
}

static void
run_a_2_scalar_parent_mark_scenario(scalar_val left_val,
                                    set<revision_id> const & left_mark_set,
                                    scalar_val right_val,
                                    set<revision_id> const & right_mark_set,
                                    scalar_val new_val,
                                    set<revision_id> const & new_mark_set)
{
  run_a_2_scalar_parent_mark_scenario(old_rid,
                                      left_val, left_mark_set,
                                      right_val, right_mark_set,
                                      new_val, new_mark_set);
}

static void
run_a_1_scalar_parent_mark_scenario(scalar_val parent_val,
                                    set<revision_id> const & parent_mark_set,
                                    scalar_val new_val,
                                    set<revision_id> const & new_mark_set)
{
  {
    testing_node_id_source nis;
    scalars ss = all_scalars(nis);
    for (scalars::const_iterator i = ss.begin(); i != ss.end(); ++i)
      run_with_1_roster_parent(**i, old_rid,
                               parent_val, parent_mark_set,
                               new_val, new_mark_set,
                               nis);
  }
  // this is an asymmetric, test, so run it via the code that will test it
  // both ways
  run_a_2_scalar_parent_mark_scenario(left_rid,
                                      parent_val, parent_mark_set,
                                      scalar_none, set<revision_id>(),
                                      new_val, new_mark_set);
}

static void
run_a_0_scalar_parent_mark_scenario()
{
  {
    testing_node_id_source nis;
    scalars ss = all_scalars(nis);
    for (scalars::const_iterator i = ss.begin(); i != ss.end(); ++i)
      {
        run_with_0_roster_parents(**i, old_rid, scalar_a, singleton(old_rid), nis);
        run_with_1_roster_parent(**i, new_rid,
                                 scalar_none, set<revision_id>(),
                                 scalar_a, singleton(new_rid),
                                 nis);
        run_with_2_roster_parents(**i, new_rid,
                                  scalar_none, set<revision_id>(),
                                  scalar_none, set<revision_id>(),
                                  scalar_a, singleton(new_rid),
                                  nis);
      }
  }
}

////////////////
// These functions contain the actual list of *-merge cases that we would like
// to test.

UNIT_TEST(roster, all_0_scalar_parent_mark_scenarios)
{
  L(FL("TEST: begin checking 0-parent marking"));
  // a*
  run_a_0_scalar_parent_mark_scenario();
  L(FL("TEST: end checking 0-parent marking"));
}

UNIT_TEST(roster, all_1_scalar_parent_mark_scenarios)
{
  L(FL("TEST: begin checking 1-parent marking"));
  //  a
  //  |
  //  a
  run_a_1_scalar_parent_mark_scenario(scalar_a, singleton(old_rid),
                                      scalar_a, singleton(old_rid));
  //  a*
  //  |
  //  a
  run_a_1_scalar_parent_mark_scenario(scalar_a, singleton(left_rid),
                                      scalar_a, singleton(left_rid));
  // a*  a*
  //  \ /
  //   a
  //   |
  //   a
  run_a_1_scalar_parent_mark_scenario(scalar_a, doubleton(left_rid, right_rid),
                                      scalar_a, doubleton(left_rid, right_rid));
  //  a
  //  |
  //  b*
  run_a_1_scalar_parent_mark_scenario(scalar_a, singleton(old_rid),
                                      scalar_b, singleton(new_rid));
  //  a*
  //  |
  //  b*
  run_a_1_scalar_parent_mark_scenario(scalar_a, singleton(left_rid),
                                      scalar_b, singleton(new_rid));
  // a*  a*
  //  \ /
  //   a
  //   |
  //   b*
  run_a_1_scalar_parent_mark_scenario(scalar_a, doubleton(left_rid, right_rid),
                                      scalar_b, singleton(new_rid));
  L(FL("TEST: end checking 1-parent marking"));
}

UNIT_TEST(roster, all_2_scalar_parent_mark_scenarios)
{
  L(FL("TEST: begin checking 2-parent marking"));
  ///////////////////////////////////////////////////////////////////
  // a   a
  //  \ /
  //   a
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(old_rid),
                                      scalar_a, singleton(old_rid),
                                      scalar_a, singleton(old_rid));
  // a   a*
  //  \ /
  //   a
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(old_rid),
                                      scalar_a, singleton(right_rid),
                                      scalar_a, doubleton(old_rid, right_rid));
  // a*  a*
  //  \ /
  //   a
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(left_rid),
                                      scalar_a, singleton(right_rid),
                                      scalar_a, doubleton(left_rid, right_rid));

  ///////////////////////////////////////////////////////////////////
  // a   a
  //  \ /
  //   b*
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(old_rid),
                                      scalar_a, singleton(old_rid),
                                      scalar_b, singleton(new_rid));
  // a   a*
  //  \ /
  //   b*
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(old_rid),
                                      scalar_a, singleton(right_rid),
                                      scalar_b, singleton(new_rid));
  // a*  a*
  //  \ /
  //   b*
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(left_rid),
                                      scalar_a, singleton(right_rid),
                                      scalar_b, singleton(new_rid));

  ///////////////////////////////////////////////////////////////////
  //  a*  b*
  //   \ /
  //    c*
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(left_rid),
                                      scalar_b, singleton(right_rid),
                                      scalar_c, singleton(new_rid));
  //  a   b*
  //   \ /
  //    c*
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(old_rid),
                                      scalar_b, singleton(right_rid),
                                      scalar_c, singleton(new_rid));
  // this case cannot actually arise, because if *(a) = *(b) then val(a) =
  // val(b).  but hey.
  //  a   b
  //   \ /
  //    c*
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(old_rid),
                                      scalar_b, singleton(old_rid),
                                      scalar_c, singleton(new_rid));

  ///////////////////////////////////////////////////////////////////
  //  a*  b*
  //   \ /
  //    a*
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(left_rid),
                                      scalar_b, singleton(right_rid),
                                      scalar_a, singleton(new_rid));
  //  a   b*
  //   \ /
  //    a*
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(old_rid),
                                      scalar_b, singleton(right_rid),
                                      scalar_a, singleton(new_rid));
  //  a*  b
  //   \ /
  //    a
  run_a_2_scalar_parent_mark_scenario(scalar_a, singleton(left_rid),
                                      scalar_b, singleton(old_rid),
                                      scalar_a, singleton(left_rid));

  // FIXME: be nice to test:
  //  a*  a*  b
  //   \ /   /
  //    a   /
  //     \ /
  //      a
  L(FL("TEST: end checking 2-parent marking"));
}

// there is _one_ remaining case that the above tests miss, because they
// couple scalar lifetimes and node lifetimes.  Maybe they shouldn't do that,
// but anyway... until someone decides to refactor, we need this.  The basic
// issue is that for content and name scalars, the scalar lifetime and the
// node lifetime are identical.  For attrs, this isn't necessarily true.  This
// is why we have two different attr scalars.  Let's say that "." means a node
// that doesn't exist, and "+" means a node that exists but has no roster.
// The first scalar checks cases like
//     +
//     |
//     a
//
//   +   +
//    \ /
//     a*
//
//   a*  +
//    \ /
//     a
// and the second one checks cases like
//     .
//     |
//     a
//
//   .   .
//    \ /
//     a*
//
//   a*  .
//    \ /
//     a
// Between them, they cover _almost_ all possibilities.  The one that they
// miss is:
//   .   +
//    \ /
//     a*
// (and its reflection).
// That is what this test checks.
// Sorry it's so code-duplication-iferous.  Refactors would be good...

namespace
{
  // this scalar represents an attr whose node may or may not already exist
  template <typename T>
  struct X_attr_mixed_scalar : public a_scalar
  {
    virtual string my_type() const { return "X_attr_scalar"; }

    map<scalar_val, pair<bool, attr_value> > values;
    X_attr_mixed_scalar(node_id_source & nis)
      : a_scalar(nis)
    {
      safe_insert(values, make_pair(scalar_a, make_pair(true, attr_value("a"))));
      safe_insert(values, make_pair(scalar_b, make_pair(true, attr_value("b"))));
      safe_insert(values, make_pair(scalar_c, make_pair(true, attr_value("c"))));
    }
    virtual void
    set(revision_id const & scalar_origin_rid, scalar_val val,
        std::set<revision_id> const & this_scalar_mark,
        roster_t & roster, marking_map & markings)
    {
      setup(roster, markings);
      // scalar_none is . in the above notation
      // and scalar_none_2 is +
      if (val != scalar_none)
        {
          T::make_obj(scalar_origin_rid, obj_under_test_nid, roster, markings);
          roster.attach_node(obj_under_test_nid, file_path_internal("foo"));
        }
      if (val != scalar_none && val != scalar_none_2)
        {
          safe_insert(roster.get_node(obj_under_test_nid)->attrs,
                      make_pair(attr_key("test_key"), safe_get(values, val)));
          markings[obj_under_test_nid].attrs[attr_key("test_key")] = this_scalar_mark;
        }
      roster.check_sane_against(markings);
    }
  };
}

UNIT_TEST(roster, residual_attr_mark_scenario)
{
  L(FL("TEST: begin checking residual attr marking case"));
  {
    testing_node_id_source nis;
    X_attr_mixed_scalar<file_maker> s(nis);
    run_with_2_roster_parents(s, left_rid,
                              scalar_none_2, set<revision_id>(),
                              scalar_none, set<revision_id>(),
                              scalar_a, singleton(new_rid),
                              nis);
  }
  {
    testing_node_id_source nis;
    X_attr_mixed_scalar<dir_maker> s(nis);
    run_with_2_roster_parents(s, left_rid,
                              scalar_none_2, set<revision_id>(),
                              scalar_none, set<revision_id>(),
                              scalar_a, singleton(new_rid),
                              nis);
  }
  {
    testing_node_id_source nis;
    X_attr_mixed_scalar<file_maker> s(nis);
    run_with_2_roster_parents(s, right_rid,
                              scalar_none, set<revision_id>(),
                              scalar_none_2, set<revision_id>(),
                              scalar_a, singleton(new_rid),
                              nis);
  }
  {
    testing_node_id_source nis;
    X_attr_mixed_scalar<dir_maker> s(nis);
    run_with_2_roster_parents(s, right_rid,
                              scalar_none, set<revision_id>(),
                              scalar_none_2, set<revision_id>(),
                              scalar_a, singleton(new_rid),
                              nis);
  }
  L(FL("TEST: end checking residual attr marking case"));
}

////////////////////////////////////////////////////////////////////////
// end of exhaustive tests
////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////
// lifecyle tests
////////////////////////////////////////////////////////////////////////

// nodes can't survive dying on one side of a merge
UNIT_TEST(roster, die_die_die_merge)
{
  roster_t left_roster; MM(left_roster);
  marking_map left_markings; MM(left_markings);
  roster_t right_roster; MM(right_roster);
  marking_map right_markings; MM(right_markings);
  testing_node_id_source nis;

  // left roster is empty except for the root
  left_roster.attach_node(left_roster.create_dir_node(nis), file_path());
  marking_t an_old_marking;
  an_old_marking.birth_revision = old_rid;
  an_old_marking.parent_name = singleton(old_rid);
  safe_insert(left_markings, make_pair(left_roster.root()->self,
                                       an_old_marking));
  // right roster is identical, except for a dir created in the old rev
  right_roster = left_roster;
  right_markings = left_markings;
  right_roster.attach_node(right_roster.create_dir_node(nis),
                           file_path_internal("foo"));
  safe_insert(right_markings, make_pair(right_roster.get_node(file_path_internal("foo"))->self,
                                        an_old_marking));

  left_roster.check_sane_against(left_markings);
  right_roster.check_sane_against(right_markings);

  cset left_cs; MM(left_cs);
  // we add the node
  left_cs.dirs_added.insert(file_path_internal("foo"));
  // we do nothing
  cset right_cs; MM(right_cs);

  roster_t new_roster; MM(new_roster);
  marking_map new_markings; MM(new_markings);

  // because the dir was created in the old rev, the left side has logically
  // seen it and killed it, so it needs to be dead in the result.
  UNIT_TEST_CHECK_THROW(
     make_roster_for_merge(left_rid, left_roster, left_markings, left_cs,
                           singleton(left_rid),
                           right_rid, right_roster, right_markings, right_cs,
                           singleton(right_rid),
                           new_rid, new_roster, new_markings,
                           nis),
     logic_error);
  UNIT_TEST_CHECK_THROW(
     make_roster_for_merge(right_rid, right_roster, right_markings, right_cs,
                           singleton(right_rid),
                           left_rid, left_roster, left_markings, left_cs,
                           singleton(left_rid),
                           new_rid, new_roster, new_markings,
                           nis),
     logic_error);
}
// nodes can't change type file->dir or dir->file
//    make_cset fails
//    merging a file and a dir with the same nid and no mention of what should
//      happen to them fails

UNIT_TEST(roster, same_nid_diff_type)
{
  randomizer rng;
  testing_node_id_source nis;

  roster_t dir_roster; MM(dir_roster);
  marking_map dir_markings; MM(dir_markings);
  dir_roster.attach_node(dir_roster.create_dir_node(nis), file_path());
  marking_t marking;
  marking.birth_revision = old_rid;
  marking.parent_name = singleton(old_rid);
  safe_insert(dir_markings, make_pair(dir_roster.root()->self,
                                      marking));

  roster_t file_roster; MM(file_roster);
  marking_map file_markings; MM(file_markings);
  file_roster = dir_roster;
  file_markings = dir_markings;

  // okay, they both have the root dir
  node_id nid = nis.next();
  dir_roster.create_dir_node(nid);
  dir_roster.attach_node(nid, file_path_internal("foo"));
  safe_insert(dir_markings, make_pair(nid, marking));

  file_roster.create_file_node(new_ident(rng), nid);
  file_roster.attach_node(nid, file_path_internal("foo"));
  marking.file_content = singleton(old_rid);
  safe_insert(file_markings, make_pair(nid, marking));

  dir_roster.check_sane_against(dir_markings);
  file_roster.check_sane_against(file_markings);

  cset cs; MM(cs);
  UNIT_TEST_CHECK_THROW(make_cset(dir_roster, file_roster, cs), logic_error);
  UNIT_TEST_CHECK_THROW(make_cset(file_roster, dir_roster, cs), logic_error);

  cset left_cs; MM(left_cs);
  cset right_cs; MM(right_cs);
  roster_t new_roster; MM(new_roster);
  marking_map new_markings; MM(new_markings);
  UNIT_TEST_CHECK_THROW(
     make_roster_for_merge(left_rid, dir_roster, dir_markings, left_cs,
                           singleton(left_rid),
                           right_rid, file_roster, file_markings, right_cs,
                           singleton(right_rid),
                           new_rid, new_roster, new_markings,
                           nis),
     logic_error);
  UNIT_TEST_CHECK_THROW(
     make_roster_for_merge(left_rid, file_roster, file_markings, left_cs,
                           singleton(left_rid),
                           right_rid, dir_roster, dir_markings, right_cs,
                           singleton(right_rid),
                           new_rid, new_roster, new_markings,
                           nis),
     logic_error);

}

UNIT_TEST(roster, write_roster)
{
  L(FL("TEST: write_roster_test"));
  roster_t r; MM(r);
  marking_map mm; MM(mm);

  testing_node_id_source nis;

  file_path root;
  file_path foo = file_path_internal("foo");
  file_path foo_ang = file_path_internal("foo/ang");
  file_path foo_bar = file_path_internal("foo/bar");
  file_path foo_zoo = file_path_internal("foo/zoo");
  file_path fo = file_path_internal("fo");
  file_path xx = file_path_internal("xx");

  file_id f1(string(constants::idlen_bytes, '\x11'));
  revision_id rid(string(constants::idlen_bytes, '\x44'));
  node_id nid;

  // if adding new nodes, add them at the end to keep the node_id order

  nid = nis.next();
  r.create_dir_node(nid);
  r.attach_node(nid, root);
  mark_new_node(rid, r.get_node(nid), mm[nid]);

  nid = nis.next();
  r.create_dir_node(nid);
  r.attach_node(nid, foo);
  mark_new_node(rid, r.get_node(nid), mm[nid]);

  nid = nis.next();
  r.create_dir_node(nid);
  r.attach_node(nid, xx);
  r.set_attr(xx, attr_key("say"), attr_value("hello"));
  mark_new_node(rid, r.get_node(nid), mm[nid]);

  nid = nis.next();
  r.create_dir_node(nid);
  r.attach_node(nid, fo);
  mark_new_node(rid, r.get_node(nid), mm[nid]);

  // check that files aren't ordered separately to dirs & vice versa
  nid = nis.next();
  r.create_file_node(f1, nid);
  r.attach_node(nid, foo_bar);
  r.set_attr(foo_bar, attr_key("fascist"), attr_value("tidiness"));
  mark_new_node(rid, r.get_node(nid), mm[nid]);

  nid = nis.next();
  r.create_dir_node(nid);
  r.attach_node(nid, foo_ang);
  mark_new_node(rid, r.get_node(nid), mm[nid]);

  nid = nis.next();
  r.create_dir_node(nid);
  r.attach_node(nid, foo_zoo);
  r.set_attr(foo_zoo, attr_key("regime"), attr_value("new"));
  r.clear_attr(foo_zoo, attr_key("regime"));
  mark_new_node(rid, r.get_node(nid), mm[nid]);

  {
    // manifest first
    manifest_data mdat; MM(mdat);
    write_manifest_of_roster(r, mdat);

    manifest_data
      expected(string("format_version \"1\"\n"
                      "\n"
                      "dir \"\"\n"
                      "\n"
                      "dir \"fo\"\n"
                      "\n"
                      "dir \"foo\"\n"
                      "\n"
                      "dir \"foo/ang\"\n"
                      "\n"
                      "   file \"foo/bar\"\n"
                      "content [1111111111111111111111111111111111111111]\n"
                      "   attr \"fascist\" \"tidiness\"\n"
                      "\n"
                      "dir \"foo/zoo\"\n"
                      "\n"
                      " dir \"xx\"\n"
                      "attr \"say\" \"hello\"\n"
                      ));
    MM(expected);

    UNIT_TEST_CHECK_NOT_THROW( I(expected == mdat), logic_error);
  }

  {
    // full roster with local parts
    roster_data rdat; MM(rdat);
    write_roster_and_marking(r, mm, rdat);

    // node_id order is a hassle.
    // root 1, foo 2, xx 3, fo 4, foo_bar 5, foo_ang 6, foo_zoo 7
    roster_data
      expected(string("format_version \"1\"\n"
                      "\n"
                      "      dir \"\"\n"
                      "    ident \"1\"\n"
                      "    birth [4444444444444444444444444444444444444444]\n"
                      "path_mark [4444444444444444444444444444444444444444]\n"
                      "\n"
                      "      dir \"fo\"\n"
                      "    ident \"4\"\n"
                      "    birth [4444444444444444444444444444444444444444]\n"
                      "path_mark [4444444444444444444444444444444444444444]\n"
                      "\n"
                      "      dir \"foo\"\n"
                      "    ident \"2\"\n"
                      "    birth [4444444444444444444444444444444444444444]\n"
                      "path_mark [4444444444444444444444444444444444444444]\n"
                      "\n"
                      "      dir \"foo/ang\"\n"
                      "    ident \"6\"\n"
                      "    birth [4444444444444444444444444444444444444444]\n"
                      "path_mark [4444444444444444444444444444444444444444]\n"
                      "\n"
                      "        file \"foo/bar\"\n"
                      "     content [1111111111111111111111111111111111111111]\n"
                      "       ident \"5\"\n"
                      "        attr \"fascist\" \"tidiness\"\n"
                      "       birth [4444444444444444444444444444444444444444]\n"
                      "   path_mark [4444444444444444444444444444444444444444]\n"
                      "content_mark [4444444444444444444444444444444444444444]\n"
                      "   attr_mark \"fascist\" [4444444444444444444444444444444444444444]\n"
                      "\n"
                      "         dir \"foo/zoo\"\n"
                      "       ident \"7\"\n"
                      "dormant_attr \"regime\"\n"
                      "       birth [4444444444444444444444444444444444444444]\n"
                      "   path_mark [4444444444444444444444444444444444444444]\n"
                      "   attr_mark \"regime\" [4444444444444444444444444444444444444444]\n"
                      "\n"
                      "      dir \"xx\"\n"
                      "    ident \"3\"\n"
                      "     attr \"say\" \"hello\"\n"
                      "    birth [4444444444444444444444444444444444444444]\n"
                      "path_mark [4444444444444444444444444444444444444444]\n"
                      "attr_mark \"say\" [4444444444444444444444444444444444444444]\n"
                      ));
    MM(expected);

    UNIT_TEST_CHECK_NOT_THROW( I(expected == rdat), logic_error);
  }
}

UNIT_TEST(roster, check_sane_against)
{
  testing_node_id_source nis;
  file_path root;
  file_path foo = file_path_internal("foo");
  file_path bar = file_path_internal("bar");

  file_id f1(decode_hexenc("1111111111111111111111111111111111111111"));
  revision_id rid(decode_hexenc("1234123412341234123412341234123412341234"));
  node_id nid;

  {
    L(FL("TEST: check_sane_against_test, no extra nodes in rosters"));
    roster_t r; MM(r);
    marking_map mm; MM(mm);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, root);
    mark_new_node(rid, r.get_node(nid), mm[nid]);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, foo);
    mark_new_node(rid, r.get_node(nid), mm[nid]);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, bar);
    // missing the marking

    UNIT_TEST_CHECK_THROW(r.check_sane_against(mm), logic_error);
  }

  {
    L(FL("TEST: check_sane_against_test, no extra nodes in markings"));
    roster_t r; MM(r);
    marking_map mm; MM(mm);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, root);
    mark_new_node(rid, r.get_node(nid), mm[nid]);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, foo);
    mark_new_node(rid, r.get_node(nid), mm[nid]);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, bar);
    mark_new_node(rid, r.get_node(nid), mm[nid]);
    r.detach_node(bar);

    UNIT_TEST_CHECK_THROW(r.check_sane_against(mm), logic_error);
  }

  {
    L(FL("TEST: check_sane_against_test, missing birth rev"));
    roster_t r; MM(r);
    marking_map mm; MM(mm);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, root);
    mark_new_node(rid, r.get_node(nid), mm[nid]);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, foo);
    mark_new_node(rid, r.get_node(nid), mm[nid]);
    mm[nid].birth_revision = revision_id();

    UNIT_TEST_CHECK_THROW(r.check_sane_against(mm), logic_error);
  }

  {
    L(FL("TEST: check_sane_against_test, missing path mark"));
    roster_t r; MM(r);
    marking_map mm; MM(mm);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, root);
    mark_new_node(rid, r.get_node(nid), mm[nid]);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, foo);
    mark_new_node(rid, r.get_node(nid), mm[nid]);
    mm[nid].parent_name.clear();

    UNIT_TEST_CHECK_THROW(r.check_sane_against(mm), logic_error);
  }

  {
    L(FL("TEST: check_sane_against_test, missing content mark"));
    roster_t r; MM(r);
    marking_map mm; MM(mm);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, root);
    mark_new_node(rid, r.get_node(nid), mm[nid]);

    nid = nis.next();
    r.create_file_node(f1, nid);
    r.attach_node(nid, foo);
    mark_new_node(rid, r.get_node(nid), mm[nid]);
    mm[nid].file_content.clear();

    UNIT_TEST_CHECK_THROW(r.check_sane_against(mm), logic_error);
  }

  {
    L(FL("TEST: check_sane_against_test, extra content mark"));
    roster_t r; MM(r);
    marking_map mm; MM(mm);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, root);
    mark_new_node(rid, r.get_node(nid), mm[nid]);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, foo);
    mark_new_node(rid, r.get_node(nid), mm[nid]);
    mm[nid].file_content.insert(rid);

    UNIT_TEST_CHECK_THROW(r.check_sane_against(mm), logic_error);
  }

  {
    L(FL("TEST: check_sane_against_test, missing attr mark"));
    roster_t r; MM(r);
    marking_map mm; MM(mm);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, root);
    // NB: mark and _then_ add attr
    mark_new_node(rid, r.get_node(nid), mm[nid]);
    r.set_attr(root, attr_key("my_key"), attr_value("my_value"));

    UNIT_TEST_CHECK_THROW(r.check_sane_against(mm), logic_error);
  }

  {
    L(FL("TEST: check_sane_against_test, empty attr mark"));
    roster_t r; MM(r);
    marking_map mm; MM(mm);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, root);
    r.set_attr(root, attr_key("my_key"), attr_value("my_value"));
    mark_new_node(rid, r.get_node(nid), mm[nid]);
    mm[nid].attrs[attr_key("my_key")].clear();

    UNIT_TEST_CHECK_THROW(r.check_sane_against(mm), logic_error);
  }

  {
    L(FL("TEST: check_sane_against_test, extra attr mark"));
    roster_t r; MM(r);
    marking_map mm; MM(mm);

    nid = nis.next();
    r.create_dir_node(nid);
    r.attach_node(nid, root);
    r.set_attr(root, attr_key("my_key"), attr_value("my_value"));
    mark_new_node(rid, r.get_node(nid), mm[nid]);
    mm[nid].attrs[attr_key("my_second_key")].insert(rid);

    UNIT_TEST_CHECK_THROW(r.check_sane_against(mm), logic_error);
  }
}

static void
check_post_roster_unification_ok(roster_t const & left,
                                 roster_t const & right,
                                 bool temp_nodes_ok)
{
  MM(left);
  MM(right);
  I(left == right);
  left.check_sane(temp_nodes_ok);
  right.check_sane(temp_nodes_ok);
}

static void
create_random_unification_task(roster_t & left,
                               roster_t & right,
                               editable_roster_base & left_erb,
                               editable_roster_base & right_erb,
                               editable_roster_for_merge & left_erm,
                               editable_roster_for_merge & right_erm,
			       randomizer & rng)
{
  size_t n_nodes = 20 + rng.uniform(60);

  // Stick in a root if there isn't one.
  if (!left.has_root())
    {
      I(!right.has_root());

      node_id left_nid = left_erm.create_dir_node();
      left_erm.attach_node(left_nid, file_path());

      node_id right_nid = right_erm.create_dir_node();
      right_erm.attach_node(right_nid, file_path());
    }

  // Now throw in a bunch of others
  for (size_t i = 0; i < n_nodes; ++i)
    {
      node_t left_n = random_element(left.all_nodes(), rng)->second;

      // With equal probability, choose to make the new node appear to
      // be new in just the left, just the right, or both.
      editable_roster_base * left_er;
      editable_roster_base * right_er;
      switch (rng.uniform(2))
        {
        case 0: left_er = &left_erm; right_er = &right_erm; break;
        case 1: left_er = &left_erb; right_er = &right_erm; break;
        case 2: left_er = &left_erm; right_er = &right_erb; break;
        default: I(false);
        }

      node_id left_nid, right_nid;
      if (rng.flip())
        {
          left_nid = left_er->create_dir_node();
          right_nid = right_er->create_dir_node();
        }
      else
        {
          file_id fid = new_ident(rng);
          left_nid = left_er->create_file_node(fid);
          right_nid = right_er->create_file_node(fid);
        }

      file_path pth;
      left.get_name(left_n->self, pth);
      I(right.has_node(pth));

      if (is_file_t(left_n) || (pth.depth() > 1 && rng.flip()))
        // Add a sibling of an existing entry.
        pth = pth.dirname() / new_component(rng);
      else
        // Add a child of an existing entry.
        pth = pth / new_component(rng);

      left_er->attach_node(left_nid, pth);
      right_er->attach_node(right_nid, pth);
    }
}

static void
unify_rosters_randomized_core(node_id_source & tmp_nis,
                              node_id_source & test_nis,
                              bool temp_nodes_ok)
{
  roster_t left, right;
  randomizer rng;
  for (size_t i = 0; i < 30; ++i)
    {
      editable_roster_base left_erb(left, test_nis);
      editable_roster_base right_erb(right, test_nis);
      editable_roster_for_merge left_erm(left, tmp_nis);
      editable_roster_for_merge right_erm(right, tmp_nis);

      create_random_unification_task(left, right,
                                     left_erb, right_erb,
                                     left_erm, right_erm, rng);
      unify_rosters(left, left_erm.new_nodes,
                    right, right_erm.new_nodes,
                    test_nis);
      check_post_roster_unification_ok(left, right, temp_nodes_ok);
    }
}

UNIT_TEST(roster, unify_rosters_randomized_trueids)
{
  L(FL("TEST: begin checking unification of rosters (randomly, true IDs)"));
  temp_node_id_source tmp_nis;
  testing_node_id_source test_nis;
  unify_rosters_randomized_core(tmp_nis, test_nis, false);
  L(FL("TEST: end checking unification of rosters (randomly, true IDs)"));
}

UNIT_TEST(roster, unify_rosters_randomized_tempids)
{
  L(FL("TEST: begin checking unification of rosters (randomly, temp IDs)"));
  temp_node_id_source tmp_nis;
  unify_rosters_randomized_core(tmp_nis, tmp_nis, true);
  L(FL("TEST: end checking unification of rosters (randomly, temp IDs)"));
}

UNIT_TEST(roster, unify_rosters_end_to_end_ids)
{
  L(FL("TEST: begin checking unification of rosters (end to end, ids)"));
  revision_id has_rid = left_rid;
  revision_id has_not_rid = right_rid;
  file_id my_fid(decode_hexenc("9012901290129012901290129012901290129012"));

  testing_node_id_source nis;

  roster_t has_not_roster; MM(has_not_roster);
  marking_map has_not_markings; MM(has_not_markings);
  {
    has_not_roster.attach_node(has_not_roster.create_dir_node(nis),
                               file_path());
    marking_t root_marking;
    root_marking.birth_revision = old_rid;
    root_marking.parent_name = singleton(old_rid);
    safe_insert(has_not_markings, make_pair(has_not_roster.root()->self,
                                            root_marking));
  }

  roster_t has_roster = has_not_roster; MM(has_roster);
  marking_map has_markings = has_not_markings; MM(has_markings);
  node_id new_id;
  {
    new_id = has_roster.create_file_node(my_fid, nis);
    has_roster.attach_node(new_id, file_path_internal("foo"));
    marking_t file_marking;
    file_marking.birth_revision = has_rid;
    file_marking.parent_name = file_marking.file_content = singleton(has_rid);
    safe_insert(has_markings, make_pair(new_id, file_marking));
  }

  cset add_cs; MM(add_cs);
  safe_insert(add_cs.files_added, make_pair(file_path_internal("foo"), my_fid));
  cset no_add_cs; MM(no_add_cs);

  // added in left, then merged
  {
    roster_t new_roster; MM(new_roster);
    marking_map new_markings; MM(new_markings);
    make_roster_for_merge(has_rid, has_roster, has_markings, no_add_cs,
                          singleton(has_rid),
                          has_not_rid, has_not_roster, has_not_markings, add_cs,
                          singleton(has_not_rid),
                          new_rid, new_roster, new_markings,
                          nis);
    I(new_roster.get_node(file_path_internal("foo"))->self == new_id);
  }
  // added in right, then merged
  {
    roster_t new_roster; MM(new_roster);
    marking_map new_markings; MM(new_markings);
    make_roster_for_merge(has_not_rid, has_not_roster, has_not_markings, add_cs,
                          singleton(has_not_rid),
                          has_rid, has_roster, has_markings, no_add_cs,
                          singleton(has_rid),
                          new_rid, new_roster, new_markings,
                          nis);
    I(new_roster.get_node(file_path_internal("foo"))->self == new_id);
  }
  // added in merge
  // this is a little "clever", it uses the same has_not_roster twice, but the
  // second time it passes the has_rid, to make it a possible graph.
  {
    roster_t new_roster; MM(new_roster);
    marking_map new_markings; MM(new_markings);
    make_roster_for_merge(has_not_rid, has_not_roster, has_not_markings, add_cs,
                          singleton(has_not_rid),
                          has_rid, has_not_roster, has_not_markings, add_cs,
                          singleton(has_rid),
                          new_rid, new_roster, new_markings,
                          nis);
    I(new_roster.get_node(file_path_internal("foo"))->self
      != has_roster.get_node(file_path_internal("foo"))->self);
  }
  L(FL("TEST: end checking unification of rosters (end to end, ids)"));
}

UNIT_TEST(roster, unify_rosters_end_to_end_attr_corpses)
{
  L(FL("TEST: begin checking unification of rosters (end to end, attr corpses)"));
  revision_id first_rid = left_rid;
  revision_id second_rid = right_rid;
  file_id my_fid(decode_hexenc("9012901290129012901290129012901290129012"));

  testing_node_id_source nis;

  // Both rosters have the file "foo"; in one roster, it has the attr corpse
  // "testfoo1", and in the other, it has the attr corpse "testfoo2".  Only
  // the second roster has the file "bar"; it has the attr corpse "testbar".

  roster_t first_roster; MM(first_roster);
  marking_map first_markings; MM(first_markings);
  node_id foo_id;
  {
    first_roster.attach_node(first_roster.create_dir_node(nis), file_path());
    marking_t marking;
    marking.birth_revision = old_rid;
    marking.parent_name = singleton(old_rid);
    safe_insert(first_markings, make_pair(first_roster.root()->self, marking));

    foo_id = first_roster.create_file_node(my_fid, nis);
    first_roster.attach_node(foo_id, file_path_internal("foo"));
    marking.file_content = singleton(old_rid);
    safe_insert(first_markings,
                make_pair(first_roster.get_node(file_path_internal("foo"))->self, marking));
  }

  roster_t second_roster = first_roster; MM(second_roster);
  marking_map second_markings = first_markings; MM(second_markings);
  {
    second_roster.attach_node(second_roster.create_file_node(my_fid, nis),
                              file_path_internal("bar"));
    safe_insert(second_roster.get_node(file_path_internal("bar"))->attrs,
                make_pair(attr_key("testbar"), make_pair(false, attr_value())));
    marking_t marking;
    marking.birth_revision = second_rid;
    marking.parent_name = marking.file_content = singleton(second_rid);
    safe_insert(marking.attrs,
                make_pair(attr_key("testbar"), singleton(second_rid)));
    safe_insert(second_markings,
                make_pair(second_roster.get_node(file_path_internal("bar"))->self, marking));
  }

  // put in the attrs on foo
  {
    safe_insert(first_roster.get_node(foo_id)->attrs,
                make_pair(attr_key("testfoo1"), make_pair(false, attr_value())));
    safe_insert(first_markings.find(foo_id)->second.attrs,
                make_pair(attr_key("testfoo1"), singleton(first_rid)));
    safe_insert(second_roster.get_node(foo_id)->attrs,
                make_pair(attr_key("testfoo2"), make_pair(false, attr_value())));
    safe_insert(second_markings.find(foo_id)->second.attrs,
                make_pair(attr_key("testfoo2"), singleton(second_rid)));
  }

  cset add_cs; MM(add_cs);
  safe_insert(add_cs.files_added, make_pair(file_path_internal("bar"), my_fid));
  cset no_add_cs; MM(no_add_cs);

  {
    roster_t new_roster; MM(new_roster);
    marking_map new_markings; MM(new_markings);
    make_roster_for_merge(first_rid, first_roster, first_markings, add_cs,
                          singleton(first_rid),
                          second_rid, second_roster, second_markings, no_add_cs,
                          singleton(second_rid),
                          new_rid, new_roster, new_markings,
                          nis);
    I(new_roster.get_node(file_path_internal("foo"))->attrs.size() == 2);
    I(new_roster.get_node(file_path_internal("bar"))->attrs
      == second_roster.get_node(file_path_internal("bar"))->attrs);
    I(new_roster.get_node(file_path_internal("bar"))->attrs.size() == 1);
  }
  {
    roster_t new_roster; MM(new_roster);
    marking_map new_markings; MM(new_markings);
    make_roster_for_merge(second_rid, second_roster, second_markings, no_add_cs,
                          singleton(second_rid),
                          first_rid, first_roster, first_markings, add_cs,
                          singleton(first_rid),
                          new_rid, new_roster, new_markings,
                          nis);
    I(new_roster.get_node(file_path_internal("foo"))->attrs.size() == 2);
    I(new_roster.get_node(file_path_internal("bar"))->attrs
      == second_roster.get_node(file_path_internal("bar"))->attrs);
    I(new_roster.get_node(file_path_internal("bar"))->attrs.size() == 1);
  }

  L(FL("TEST: end checking unification of rosters (end to end, attr corpses)"));
}

#endif // BUILD_UNIT_TESTS

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
