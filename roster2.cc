// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "roster2.hh"

// FIXME: move this to a header somewhere
template <typename T, typename K>
safe_erase(T & container, K const & key)
{
  I(container.erase(key));
}
template <typename T, typename V>
safe_insert(T & container, V const & val)
{
  I(container.insert(val).second);
}

// FIXME: we assume split and join paths always start with a null component

esoul
roster_t::lookup(file_path const & fp) const
{
  split_path sp;
  fp.split(sp);
  return lookup(sp);
}

esoul
roster_t::lookup(split_path const & sp) const
{
  esoul es = the_null_soul;
  for (split_path::const_iterator i = sp.begin(); i != sp.end(); ++i)
    es = lookup(es, *i);
  return es;
}

esoul
roster_t::lookup(esoul parent, path_component child) const
{
  if (null_soul(parent))
    {
      I(null_name(child));
      I(!null_soul(root_dir));
      return root_dir;
    }
  dir_map & dir = children(parent);
  dir_map::const_iterator i = dir.find(child);
  I(i != dir.end());
  return i->second;
}

esoul
roster_t::get_name(esoul es, file_path & fp)
{
  split_path sp;
  get_name(es, sp);
  fp = file_path(sp);
}

esoul
roster_t::get_name(esoul es, split_path & sp)
{
  sp.clear();
  while (!null_soul(es))
    {
      element_t & e = element(es);
      sp.push_back(es.name);
      es = es.parent;
    }
  std::reverse(sp.begin(), sp.end());
}

dir_map &
roster_t::children(esoul es)
{
  std::map<esoul, dir_map>::iterator i = children_map.find(es);
  I(i != children_map.end());
  return i->second;
}

element_t &
roster_t::element(esoul es)
{
  std::map<esoul, element_t>::iterator i = elements.find(es);
  I(i != elements.end());
  return i->second;
}

void
roster_t::resoul(esoul from, esoul to, element_t & element)
{
  // first move the element_t
  {
    std::map<esoul, element_t>::iterator i = elements.find(from);
    I(i != elements.end());
    safe_insert(elements, std::make_pair(to, i->second));
    elements.erase(i);
  }
  // then munge the containing directory
  if (root_dir == from)
    {
      root_dir = to;
      I(element.type == etype_dir);
    }
  else
    {
      dir_map & dir = children(element.parent);
      dir_map::iterator i = dir.find(element.name);
      I(i != dir.end());
      I(i->second == from);
      i->second = to;
    }
  // then, for directories, munge the tree representation and the child layout
  if (element.type == etype_dir)
    {
      std::map<esoul, dir_map>::iterator i = children_map.find(from);
      I(i != children_map.end());
      for (dir_map::iterator j = i->second.begin(); j != i->second.end(); ++j)
        {
          element_t & child_e = element(j->second);
          I(child_e.parent == from);
          child_e.parent = to;
        }
      safe_insert(children_map, std::make_pair(to, i->second));
      children_map.erase(i);
    }
}

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

// FIXME: we assume split and join paths always start with a null component
// split_path [] means the_null_component (which is the parent of the root dir)
// [""] means the root dir
// ["", "foo"] means root dir's sub-element "foo"
// etc.

void
roster_t::attach(esoul es, split_path const & sp, element_t & e)
{
  split_path dirname;
  path_component basename;
  dirname_basename(sp, dirname, basename);
  esoul parent = lookup(dirname);
  e.parent = parent;
  e.name = basename;
  if (null_soul(parent))
    {
      // this is the root dir
      root_dir = es;
      I(e.type == etype_dir);
    }
  else
    safe_insert(children(parent), std::make_pair(basename, es));
  if (e.type == etype_dir)
    safe_insert(children_map, std::make_pair(es, dir_map()));
}

void
roster_t::detach(esoul es, etype type)
{
  // for now, the root dir can be created, but cannot be removed
  I(es != root_dir);
  element_t & e = element(es);
  I(e.type == type);
  esoul parent = e.parent;
  safe_erase(children(parent), es);
  e.parent = the_null_soul;
  e.name = the_null_component;
  if (e.type == etype_dir)
    {
      std::map<esoul, dir_map>::iterator i = children_map.find(es);
      I(i != children_map.end());
      I(i->second.empty());
      children_map.erase(i);
    }
}

void
roster_t::remove(esoul es, etype type)
{
  detach(es, type);
  safe_erase(elements, es);
}

void
roster_t::add(esoul es, split_path const & sp, element_t const & element)
{
  safe_insert(elements, std::make_pair(es, element));
  attach(es, sp, element(es));
}

namespace 
{
  struct change_task
  {
    virtual void go(roster_t & r, split_path const & target);
  };
  struct delete_element : public change_task
  {
    etype type;
    delete_element(etype type) : type(type) {}
    virtual void go(roster_t & r, split_path const & target)
    {
      r.remove(r.lookup(target), type);
    }
  };
  struct rename_detach : public change_task
  {
    etype type;
  }
}

// FIXME: make apply_changeset apply to some sort of abstract mutable tree
// interface, rather than rosters -- that way we can use this same code for
// applying changes to the working copy.
// (or does this make sense?)
// (such an interface would be really handy for implementing rollback and undo
// on working copy operations...)

namespace 
{
  typedef enum
    { remove_task, add_task, rename_start_task, rename_end_task }
  task_type;
  struct ctask
  {
    task_type task;
    etype type;
    file_path const & rename_target;
    esoul rename_source;
    ctask(task_type task, etype type) : task(task), type(type)
    { I(task == remove_task || task == add_task); }
    ctask(task_type task, etype type, file_path const & rename_target)
      : task(task), type(type), rename_target(rename_target)
      { I(task == rename_start_task); }
    ctask(task_type task, etype type, esoul rename_source)
      : task(task), type(type), rename_source(rename_source)
      { I(task == rename_end_task); }
  }

  inline void sched(std::vector<std::pair<split_path, ctask> > vec,
                    file_path const & fp, ctask const & ct)
  {
    split_path sp;
    fp.split(sp);
    vec.push_back(std::make_pair(sp, ct));
  }

  struct bu_comparator
  {
    bool operator()(std::pair<split_path, ctask> const & a,
                    std::pair<split_path, ctask> const & b)
    {
      return a.first.size() > b.first.size();
    }
  };
  struct td_comparator
  {
    bool operator()(std::pair<split_path, ctask> const & a,
                    std::pair<split_path, ctask> const & b)
    {
      return a.first.size() < b.first.size();
    }
  };
  
}

void
roster_t::apply_changeset(changeset const & cs,
                          soul_source & ss, revision_id const & new_id,
                          std::set<esoul> & new_souls,
                          std::set<esoul> & touched_souls)
{
  typedef std::vector<std::pair<split_path, ctask> > schedvec;
  schedvec bottom_up_tasks, top_down_tasks;
  bottom_up_tasks.reserve(re.deleted_files.size()
                          + re.deleted_dirs.size()
                          + re.renamed_files.size()
                          + re.renamed_dirs.size());
  top_down_tasks.reserve(re.added_files.size()
                         + re.added_dirs.size()
                         + re.renamed_files.size()
                         + re.renamed_dirs.size());
  // first, we apply deletes and the first half of renames, in bottom-up order
  changeset::path_rearrangement const & re = cs.rearrangement;
  for (std::set<file_path>::const_iterator i = re.deleted_files.begin();
       i != re.deleted_files.end(); ++i)
    sched(bottom_up_tasks, *i, ctask(remove_task, etype_file));
  for (std::set<file_path>::const_iterator i = re.deleted_dirs.begin();
       i != re.deleted_dirs.end(); ++i)
    sched(bottom_up_tasks, *i, ctask(remove_task, etype_dir));
  for (std::map<file_path, file_path>::const_iterator i = re.renamed_files.begin();
       i != re.renamed_files.end(); ++i)
    sched(bottom_up_tasks, i->first,
          ctask(rename_start_task, etype_file, i->second));
  for (std::map<file_path, file_path>::const_iterator i = re.renamed_dirs.begin();
       i != re.renamed_dirs.end(); ++i)
    sched(bottom_up_tasks, i->first,
          ctask(rename_start_task, etype_dir, i->second));

  std::sort(bottom_up_tasks.begin(), bottom_up_tasks.end(), bu_comparator());

  for (schedvec::const_iterator i = bottom_up_tasks.begin();
       i != bottom_up_tasks.end(); ++i)
    {
      split_path const & sp = i->first;
      ctask const & ct = i->second;
      switch (ct.task)
        {
        case remove_task:
          remove(lookup(sp), ct.type);
          break;
        case rename_start_task:
          esoul es = lookup(sp);
          detach(es, ct.type);
          sched(top_down_tasks, ct.rename_target,
                ctask(rename_end_task, ct.type, es));
          touched_souls.insert(es);
          break;
        case add_task:
        case rename_end_task:
          I(false);
        }
    }
  // next, we apply adds and the second half of renames, in top-down order
  // already scheduled renames, still need to schedule adds
  for (std::set<file_path>::const_iterator i = re.added_files.begin();
       i != re.added_files.end(); ++i)
    sched(top_down_tasks, *i, ctask(add_task, etype_file));
  for (std::set<file_path>::const_iterator i = re.added_dirs.begin();
       i != re.added_dirs.end(); ++i)
    sched(top_down_tasks, *i, ctask(add_task, etype_dir));

  std::sort(top_down_tasks.begin(), top_down_tasks.end(), td_comparator());

  for (schedvec::const_iterator i = bottom_up_tasks.begin();
       i != bottom_up_tasks.end(); ++i)
    {
      split_path const & sp = i->first;
      ctask const & ct = i->second;
      switch (ct.task)
        {
        case add_task:
          element_t element;
          element.birth_revision = new_id;
          esoul new_soul = ss.next();
          add(new_soul, sp, element);
          new_souls.insert(new_soul);
          break;
        case rename_end_task:
          attach(ct.rename_source, sp);
          break;
        case remove_task:
        case rename_start_task:
          I(false);
        }
    }
  // finally, we apply content and attr changes
  for (delta_map::const_iterator i = cs.deltas.begin(); i != cs.deltas.end(); ++i)
    {
      esoul es = lookup(delta_entry_path(i));
      element_t & e = element(es);
      I(e.type == etype_file);
      I(e.content == delta_entry_src(i));
      I(delta_entry_src(i) != delta_entry_dst(i));
      e.content = delta_entry_dst(i);
      touched_souls.insert(es);
    }
  smap<std::pair<file_path, attr_key> > modified;
  for (attr_set_map::const_iterator i = cs.attr_sets.begin();
       i != cs.attr_sets.end(); ++i)
    {
      esoul es = lookup(attr_set_entry_path(i));
      element_t & e = element(es);
      attr_map::const_iterator j = e.attrs.find(attr_set_entry_key(i));
      I(j == e.attrs.end() || j->second != attr_set_entry_value(i));
      e.attrs[attr_set_entry_key(i)] = attr_set_entry_value(i);
      touched_souls.insert(es);
      modified.insert(std::make_pair(attr_set_entry_path(i),
                                     attr_set_entry_key(i)));
    }
  for (attr_clear_map::const_iterator i = cs.attr_clears.begin();
       i != cs.attr_clears.end(); ++i)
    {
      esoul es = lookup(attr_set_entry_path(i));
      element_t & e = element(es);
      attr_map::iterator j = e.attrs.find(attr_set_entry_key(i));
      I(j != e.attrs.end());
      e.attrs.erase(j);
      touched_souls.insert(es);
      I(modified.find(std::make_pair(attr_set_entry_path(i), attr_set_entry_key(i)))
        == modified.end());
    }
}

namespace 
{
  // this handles all the stuff in a_new
  void unify_roster_oneway(roster_t & a, std::set<esoul> & a_new,
                           roster_t & b, std::set<esoul> & b_new,
                           std::set<esoul> new_souls,
                           soul_source & ss)
  {
    for (std::set<esoul>::const_iterator i = a_new.begin(); i != a_new.end(); ++i)
      {
        element_soul const as = *i;
        split_path sp;
        // FIXME: climb out only so far as is necessary to find a shared soul?
        // possibly faster (since usually will get a hit immediately), but may
        // not be worth the effort (since it doesn't take that long to get out
        // in any case)
        a.get_name(as, sp);
        element_soul const bs = b.lookup(as);
        if (temp_soul(bs))
          {
            esoul new_soul = ss.next();
            a.resoul(ls, new_soul);
            b.resoul(rs, new_soul);
            new_souls.insert(new_soul);
            b_new.erase(bs);
          }
        else
          {
            a.resoul(as, bs);
            a.element(bs).birth_revision = b.element(bs).birth_revision;
          }
      }
  }

  // after this, left should == right, and there should be no temporary ids
  // destroys sets, because that's handy (it has to scan over both, but it can
  // skip some double-scanning)
  void
  unify_rosters(roster_t & left, std::set<esoul> & left_new,
                roster_t & right, std::set<esoul> & right_new,
                // these new_souls all come from the given soul source
                std::set<esoul> new_souls,
                soul_source & ss)
  {
    unify_roster_oneway(left, left_new, right, right_new, new_souls, ss);
    unify_roster_oneway(right, right_new, left, left_new, new_souls, ss);
  }

}


void roster_for_revision(revision_id const & rid, revision_set const & rev,
                         roster_t & r,
                         app_state & app)
{
  // if the revision has 1 edge: get parent roster in r (or blank roster, if
  // parent is null), apply the changeset to it, using a real soul source
  // use the new_souls and touched_souls things to scan part of resulting
  // roster and generate markings

  // if the revision has 2 edges: get two parent rosters.  copy each, and
  // apply relevant changeset to each copy.
  // unify the two copies
  // make sure the two unified copies are identical
  // throw out one of the two identical copies
  // get uncommon ancestors
  // scan over new and touched pieces of new roster to generate markings

  // NB: merge parents cannot have null ids
  // note: remove old_manifest lines, pure redundancy

  // otherwise: I(false)
}
