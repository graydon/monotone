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

// FIXME: what semantics, exactly, does this expect of file_path's split/join
// logic, wrt handling of null paths?

element_soul
lookup(roster_t const & roster, file_path const & fp)
{
  std::vector<path_component> pieces;
  fp.split(pieces);
  element_soul es = root_dir;
  I(!null_soul(es));
  for (std::vector<path_component>::const_iterator i = pieces.begin();
       i != pieces.end(); ++i)
    es = get_existing_value(get_existing_value(roster.children, es), *i);
  return es;
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
  if (element.is_dir)
    {
      I(null_name(element.name) == null_soul(element.parent));
      I(null_id(element.content));
    }
  else
    {
      I(!null_name(element.name));
      I(!null_soul(element.parent));
      I(!null_id(element.content));
    }
}

bool is_root_dir(element_t const & element)
{
  return null_soul(element.parent);
}

roster_t::roster_t(element_map const & elements)
  : elements(elements)
{
  // first pass: find all directories and the unique root directory
  bool found_root_dir = false;
  for (element_map::const_iterator i = elements.begin(); i != elements.end(); ++i)
    {
      element_soul es = i->first;
      element_t const & element = i->second;
      check_sane_element(element);
      if (element.is_dir)
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
  // third pass: sanity check result
  check_sane_roster(*this);
}

void
check_sane_roster(roster_t const & roster)
{
  // no loops
  // no MT entry in top-level dir
  // (though this would be caught before we could write out a manifest or
  // anything, of course...)
}
