// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "roster.hh"

template <typename K, typename V>
V &
get_existing_value(std::map<K, V> & m, K const & key)
{
  std::map<K, V>::iterator i = m.find(key);
  I(i != m.end());
  return i->second;
}

template <typename K, typename V>
void
erase_existing_key(std::map<K, V> & m, K const & key)
{
  std::map<K, V>::iterator i = m.find(key);
  I(i != m.end());
  m.erase(i);
}

// FIXME: what semantics, exactly, does this expect of file_path's split/join
// logic, wrt handling of null paths?

element_soul
dir_tree::lookup(element_soul parent, path_component child)
{
  return get_existing_value(get_existing_value(children, parent), child);
}
element_soul
dir_tree::lookup(split_path const & sp)
{
  element_soul es = roster.tree.root_dir;
  I(!null_soul(es));
  for (split_path::const_iterator i = sp.begin(); i != sp.end(); ++i)
    es = lookup(es, *i);
  return es;
}
element_soul
dir_tree::lookup(file_path const & fp)
{
  split_path sp;
  fp.split(sp);
  return lookup(sp);
}

void
get_name(roster_t const & roster, element_soul es, file_path & fp)
{
  std::vector<path_component> pieces;
  while (es != roster.root_dir)
    {
      element_t const & element = get_existing_value(roster.elements, es);
      pieces.push_back(element.name);
      es = element.parent;
    }
  std::reverse(pieces.begin(), pieces.end());
  fp = file_path(pieces);
}

void
check_sane_element(element_t const & element)
{
  I(!null_id(element.birth_revision));
  switch (element.type)
    {
    case etype_dir:
      I(null_name(element.name) == null_soul(element.parent));
      I(null_id(element.content));
      break;
    case etype_file:
      I(!null_name(element.name));
      I(!null_soul(element.parent));
      I(!null_id(element.content));
      break;
    }
}

bool is_root_dir(element_t const & element)
{
  return null_soul(element.parent);
}

dir_tree::dir_tree(element_map const & elements)
{
  // first pass: find all directories and the unique root directory
  bool found_root_dir = false;
  for (element_map::const_iterator i = elements.begin(); i != elements.end(); ++i)
    {
      element_soul es = i->first;
      element_t const & element = i->second;
      check_sane_element(element);
      if (element.type == etype_dir)
        {
          if (is_root_dir(element))
            {
              I(!found_root_dir);
              found_root_dir = true;
              root_dir = es;
            }
          children.insert(std::make_pair(es,
                                         std::map<path_component, element_soul>()));
        }
    }
  I(found_root_dir);
  // second pass: fill in each directory
  for (element_map::const_iterator i = elements.begin(); i != elements.end(); ++i)
    {
      element_soul es = i->first;
      element_t const & element = i->second;
      if (es == root_dir)
        continue;
      dir_map & dir = get_existing_value(roster.children, element.parent);
      // this means that dir_map should not be an smap -- unless we want to
      // more-offial-ize smap's assert-on-duplicate behavior, leave this out,
      // and at the end do a 3rd pass to call "assert_no_dups" on each
      // dir_map?
      I(dir.find(element.name) == dir.end());
      dir.insert(std::make_pair(element.name, es));
    }
}

void
check_sane_roster(roster_t const & roster)
{
  // no loops
  // no MT entry in top-level dir
  // (though this would be caught before we could write out a manifest or
  // anything, of course...)
}


// FIXME: this code assumes that split("") = []

static inline void
dirname_basename(split_path const & sp,
                 split_path & dirname, path_component & basename)
{
  I(!sp.empty());
  split_path::const_iterator penultimate = sp.end();
  --penultimate;
  dirname = split_path(sp.begin(), penultimate);
  basename = *penultimate;
}

element_soul
dir_tree::remove(split_path const & sp)
{
  split_path dirname;
  path_component basename;
  dirname_basename(sp, dirname, basename);
  element_soul parent = lookup(dirname);
  dir_map & parent_dir = get_existing_value(children, parent);
  erase_existing_key(parent_dir, basename);
  // in case is a directory itself, remove that directory
  // doing repeated lookups here is kinda inefficient, could do a single
  // traversal to get parent and child both
  element_soul child = lookup(sp);
  std::map<element_soul, dir_map>::iterator i = children.find(child);
  if (i != children.end())
    {
      I(i->second.empty());
      children.erase(i);
    }
  return child;
}

void
dir_tree::add(split_path const & sp, element_soul es, etype type)
{
  split_path dirname;
  path_component basename;
  dirname_basename(sp, dirname, basename);
  element_soul parent = lookup(dirname);
  add(parent, basename, es, type);
}

void
dir_tree::add(element_soul parent, path_component name,
              element_soul child, etype type)
{
  dir_map parent_dir = get_existing_value(children, parent);
  I(parent_dir.find(basename) == parent_dir.end());
  parent_dir.insert(std::make_pair(basename, es));
  if (type == etype_dir)
    {
      I(children.find(es) == children.end());
      children.insert(std::make_pair(es, dir_map()));
    }
}

static void delete_element(roster_t & r, etype type, split_path const & sp)
{
  element_soul es = r.tree.remove(sp);
  element_map::iterator i = r.elements.find(es);
  I(i != r.elements.end());
  I(i->second.type == type);
  r.elements.erase(i);
}

// ...this is all absurd:
struct greater_length
{
  bool operator()(std::vector<path_component> v1,
                  std::vector<path_component> v2)
  {
    return v1.size() > v2.size();
  }
};
                         
typedef std::pair<split_path, split_path> rename_pair;

template <typename T>
struct first_first_greater_length
{
  bool operator()(std::pair<rename_pair, T> const & p1,
                  std::pair<rename_pair, T> const & p2)
  {
    return p1.first.first.size() > p2.first.first.size();
  }
};

template <typename T>
struct first_second_less_length
{
  bool operator()(std::pair<rename_pair, T> const & p1,
                  std::pair<rename_pair, T> const & p2)
  {
    return p1.first.second.size() < p2.first.second.size();
  }
};

static void
create_element(roster_t & roster, split_path const & sp, etype type,
               temp_soul_source & tss)
{
  split_path dirname;
  path_component basename;
  dirname_basename(sp, dirname, basename);
  element_soul parent = roster.tree.lookup(dirname);
  element_t element;
  element.type = type;
  element.parent = parent;
  element_soul es = temp_soul_source.next();
  I(roster.elements.find(es) == roster.elements.end());
  roster.elements.insert(std::make_pair(es, element));
  roster.tree.add(parent, basename, pieces, type);
}

void
apply_change_set(change_set const & cs, roster_t & roster, temp_soul_source & tss)
{
  path_rearrangement const & re = cs.rearrangement;
  // process in order: deleted files, deleted dirs, read out renamed files and
  // renamed dirs, write out renamed dirs (shortest first), write out renamed
  // files, add dirs (shortest first), add files, apply deltas, apply attr
  // changes
  // deleted files:
  for (std::set<file_path>::const_iterator i = re.deleted_files.begin();
       i != re.deleted_files.end(); ++i)
    {
      std::vector<path_component> pieces;
      i->split(pieces);
      delete_element(roster, etype_file, pieces);
    }
  // deleted dirs
  {
    std::vector<std::vector<path_component> > schedule;
    for (std::set<file_path>::const_iterator i = re.deleted_dirs.begin();
         i != re.deleted_dirs.end(); ++i)
      {
        std::vector<path_component> pieces;
        i->split(pieces);
        schedule.push_back(pieces);
      }
    // order pieces so that long names come first
    std::sort(schedule.begin(), schedule.end(), greater_length());
    for (std::vector<std::vector<path_component> >::const_iterator i = schedule.begin();
         i != schedule.end(); ++i)
      delete_element(roster, etype_dir, *i);
  }
  // renames
  {
    typedef std::pair<etype type, element_soul> rename_info;
    std::vector<rename_pair, rename_info> renames;
    for (std::map<file_path, file_path>::const_iterator i = re.renamed_files.begin();
         i != re.renamed_files.end(); ++i)
      {
        std::vector<path_component> src, dst;
        i->first.split(src);
        i->second.split(dst);
        renames.push_back(std::make_pair(std::make_pair(src, dst),
                                         std::make_pair(etype_file, the_null_soul)));
      }
    for (std::map<file_path, file_path>::const_iterator i = re.renamed_dirs.begin();
         i != re.renamed_dirs.end(); ++i)
      {
        std::vector<path_component> src, dst;
        i->first.split(src);
        i->second.split(dst);
        renames.push_back(std::make_pair(std::make_pair(src, dst),
                                         std::make_pair(etype_dir, the_null_soul)));
      }
    std::sort(renames.begin(), renames.end(),
              first_first_greater_length<rename_info>());
    for (std::vector<rename_pair, rename_info>::iterator i = renames.begin();
         i != renames.end(); ++i)
      {
        element_soul es = roster.tree.remove(i->first.first);
        I(get_existing_value(roster.elements, es).type == i->second.first);
        i->second.second = es;
      }
    std::sort(renames.begin(), renames.end(),
              first_second_less_length<rename_info>());
    for (std::vector<rename_pair, rename_info>::iterator i = renames.begin();
         i != renames.end(); ++i)
      {
        split_path const & target = i->first.second;
        element_soul es = i->second.second;
        element_t & element = get_existing_value(roster.elements, es);
        split_path dirname;
        path_component basename;
        dirname_basename(target, dirname, basename);
        element.parent = roster.tree.lookup(dirname);
        element.name = basename;
        roster.tree.add(element.parent, element.name, es, element.type);
      }
  }
  // added dirs
  // lexicographic order on dirs works fine
  for (std::set<file_path>::const_iterator i = re.added_dirs.begin();
       i != re.added_dirs.end(); ++i)
    {
      split_path sp;
      i->split(sp);
      create_element(roster, sp, etype_dir, tss);
    }
  // added files
  for (std::set<file_path>::const_iterator i = re.added_files.begin();
       i != re.added_files.end(); ++i)
    {
      split_path sp;
      i->split(sp);
      create_element(roster, sp, etype_file, tss);
    }
  // modified files
  for (delta_map::const_iterator i = cs.deltas.begin(); i != cs.deltas.end(); ++i)
    {
      split_path sp;
      delta_entry_path.split(sp);
      element_soul es = roster.tree.lookup(sp);
      roster_t & roster = get_existing_value(roster.elements, es);
      I(roster.type = etype_file);
      I(roster.content == delta_entry_src(i));
      I(roster.content != delta_entry_dst(i));
      roster.content = delta_entry_dst(i);
    }
  // attributes
  {
    std::set<std::pair<file_path, attr_key> > modified_attrs;
    for (attrs_set_map::const_iterator i = cs.attr_sets.begin();
         i != cs.attr_sets.end(); ++i)
      {
        file_path const & fp = i->first;
        std::map<attr_key, attr_value> const & new_attrs = i->second;
        element_soul es = roster.tree.lookup(fp);
        element_t & element = get_existing_value(roster.elements, es);
        for (std::map<attr_key, attr_value>::const_iterator j = new_attrs.begin();
             j != new_attrs.end(); ++j)
          {
            modified_attrs.insert(std::make_pair(fp, j->first));
            std::map<attr_key, attr_value>::const_iterator k = element.attrs.find(j->first);
            if (k == elements.attrs.end())
              element.attrs.insert(*j);
            else
              {
                I(k->second != j->second);
                k->second = j->second;
              }
          }
      }
    for (attrs_clear_map::const_iterator i = cs.attr_clears.begin();
         i != cs.attr_sets.end(); ++i)
      {
        file_path const & fp = i->first;
        std::set<attr_key> const & cleared_attrs = i->second;
        element_soul es = roster.tree.lookup(fp);
        element_t & element = get_existing_value(roster.elements, es);
        for (std::set<attr_key>::const_iterator j = cleared_attrs.begin();
             j != cleared_attrs.end(); ++j)
          {
            I(modified_attrs.find(std::make_pair(fp, *j)) == modified_attrs.end());
            std::map<attr_key, attr_value>::iterator k = element.attrs.find(*j);
            I(k != element.attrs.end());
            element.attrs.erase(k);
          }
      }
  }
}



struct parallel_roster_walker
{
  virtual void visit_left(roster_t & left, element_soul left_soul) = 0;
  virtual void visit_right(roster_t & right, element_soul right_soul) = 0;
  virtual void visit_both(roster_t & left, element_soul left_soul,
                          roster_t & right, element_soul right_soul);
};

void
parallel_walk_rosters_recurse_left(roster_t & left, element_soul & start,
                                   parallel_roster_walker & walker)
{
  walker.visit_left(left, start);
  std::map<element_soul, dir_map>::const_iterator i = left.tree.children.find(start);
  if (i != left.tree.children.end())
    for (dir_map::const_iterator j = i->second.begin(); j != i->second.end(); ++j)
      parallel_walk_rosters_recurse_left(left, j->second, walker);
}

void
parallel_walk_rosters_recurse_right(roster_t & right, element_soul start,
                                    parallel_roster_walker & walker)
{
  walker.visit_right(right, start);
  std::map<element_soul, dir_map>::const_iterator i = right.tree.children.find(start);
  if (i != right.tree.children.end())
    for (dir_map::const_iterator j = i->second.begin(); j != i->second.end(); ++j)
      parallel_walk_rosters_recurse_right(right, j->second, walker);
}

void
parallel_walk_rosters_recurse_both(roster_t & left, element_soul start_left,
                                   roster_t & right, element_soul start_right,
                                   parallel_roster_walker & walker)
{
  walker.visit_both(left, start_left, right, start_right);
  std::map<element_soul, dir_map>::const_iterator
    li = left.tree.children.find(start_left);
  std::map<element_soul, dir_map>::const_iterator
    ri = right.tree.children.find(start_left);
  if (li == left.tree.children.end())
    {
      I(ri == right.tree.children.end());
      return;
    }
  else
    {
      I(ri != right.tree.children.end());
      dir_map & ldir = li->second;
      dir_map & rdir = ri->second;
      dir_map::iterator lc = ldir.begin();
      dir_map::iterator rc = rdir.begin();
      while (lc != ldir.end() || rc != rdir.end())
        {
          if (lc == ldir.end() || rc->first < lc->first)
            {
              parallel_walk_rosters_recurse_right(right, rc->second, walker);
              ++rc;
            }
          else if (rc == rdir.end() || lc->first < rc->first)
            {
              parallel_walk_rosters_recurse_left(left, lc->second, walker);
              ++lc;
            }
          else
            {
              I(lc->first == rc->first);
              parallel_walk_rosters_recurse_both(left, lc->second,
                                                 right, rc->second,
                                                 walker);
              ++lc;
              ++rc;
            }
        }
    }
}

// FIXME: either make sure this is only passed non-loopy rosters, or add depth
// checking to this recursion
void
parallel_walk_rosters(roster_t & left, roster_t & right,
                      parallel_roster_walker & walker)
{
  parallel_walk_rosters_recurse_both(left, left.tree.root_dir,
                                     right, right.tree.root_dir,
                                     walker);
}


struct merge_id_fixer : parallel_roster_walker
{
  revision_id const & left_parent_id;
  revision_id const & right_parent_id;
  revision_id const & my_id;
  std::set<revision_id> const & left_ancestors;
  std::set<revision_id> const & right_ancestors;
  marking_t & marking;
  app_state & app;
  merge_id_fixer(revision_id const & li, revision_id const & ri,
                 revision_id const & my_id,
                 std::set<revision_id> const & la,
                 std::set<revision_id> const & ra,
                 marking_t & marking, app_state & app)
    : left_parent_id(li), right_parent_id(ri), my_id(my_id), left_ancestors(la),
      right_ancestors(ra), marking(marking), app(app)
    {}
  virtual void visit_left(roster_t & left, element_soul left_soul);
  virtual void visit_right(roster_t & right, element_soul right_soul);
  virtual void visit_both(roster_t & left, element_soul left_soul,
                          roster_t & right, element_soul right_soul);
}

void
merge_id_fixer::visit_left(roster_t & left, element_soul left_soul)
{
  I(false);
}
void
merge_id_fixer::visit_right(roster_t & right, element_soul right_soul)
{
  I(false);
}

void
merge_id_fixer::visit_both(roster_t & left, element_soul left_soul,
                           roster_t & right, element_soul right_soul)
{
  element_t & left_element = get_existing_value(left.elements, left_soul);
  element_t & right_elementt = get_existing_value(right.elements, right_soul);
  I(left_element == right_element);
  if (temp_soul(left_soul) && temp_soul(right_soul))
    {
      // brand new element
      element_soul new_soul = app.db.next_soul();
      left.resoul(left_soul, new_soul);
      right.resoul(right_soul, new_soul);
      mark_item mi;
      mi.name_marks.insert(my_id);
      mi.content
        marking.insert();
    }
  
}

// the last step in generating a merge roster
static void
unify_rosters(roster_t & target, element_soul ts,
              roster const & other, element_soul os,
              revision_id const & new_rev,
              app_state & app)
{
  element_t & telement = get_existing_value(target.elements, ts);
  element_t & oelement = get_existing_value(other.elements, os);
  if (temp_soul(ts) && temp_soul(os))
    {
      element_soul new_soul = app.db.next_soul();
      target.resoul(ts, new_soul);
      ts = new_soul;
      telement.birth_revision = new_rev;
    }
  else if (temp_soul(ts) && !temp_soul(os))
    {
      // not a new file -- picked it up from other
      target.resoul(ts, os);
      ts = os;
      telement.birth_revision = oelement.birth_revision;
    }
  else if (temp_soul(ts) && !temp_soul(os))
    {
      // not a new file -- we already have the right stuff
    }
  else if (!temp_soul(ts) && !temp_soul(os))
    {
      // file existed in both parents -- we already have the right stuff
    }
  else
    I(false);
  I(telement == oelement);
  if (telement.type == etype_dir)
    {
      dir_map & tdir = get_existing_value(target.tree.children, ts);
      dir_map & odir = get_existing_value(other.tree.children, os);
      
}
