#ifndef __SAFE_MAP_HH__
#define __SAFE_MAP_HH__

// Copyright (C) 2005 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// some helpers to safely use maps

#include "sanity.hh"

// errors out if the key does not exist
template <typename T>
void
do_safe_erase(T & container, typename T::key_type const & key,
              char const * container_name, char const * file, int line)
{
  if (!container.erase(key))
    global_sanity.invariant_failure((F("erasing nonexistent key from %s")
                                     % container_name).str().c_str(),
                                    file, line);
}
#define safe_erase(CONT, KEY) \
  do_safe_erase((CONT), (KEY), #CONT, __FILE__, __LINE__)


// errors out if the key already exists
template <typename T>
typename T::iterator
do_safe_insert(T & container, typename T::value_type const & val,
               char const * container_name, char const * file, int line)
{
  std::pair<typename T::iterator, bool> r = container.insert(val);
  if (!r.second)
    global_sanity.invariant_failure((F("inserting duplicate entry into %s")
                                     % container_name).str().c_str(),
                                    file, line);
  return r.first;
}
#define safe_insert(CONT, VAL) \
  do_safe_insert((CONT), (VAL), #CONT, __FILE__, __LINE__)


// errors out if the key already exists
template <typename T>
typename T::iterator
do_safe_insert(T & container, typename T::iterator where,
               typename T::value_type const & val,
               char const * container_name, char const * file, int line)
{
  typename T::size_type pre_size = container.size();
  typename T::iterator r = container.insert(where, val);
  if (pre_size == container.size())
    global_sanity.invariant_failure((F("inserting duplicate entry into %s")
                                     % container_name).str().c_str(),
                                    file, line);
  return r;
}
#define hinted_safe_insert(CONT, HINT, VAL)                             \
  do_safe_insert((CONT), (HINT), (VAL), #CONT, __FILE__, __LINE__)


// errors out if the key does not exist
template <typename T>
typename T::mapped_type const &
do_safe_get(T & container, typename T::key_type const & key,
            char const * container_name, char const * file, int line)
{
  typename T::const_iterator i = container.find(key);
  if (i == container.end())
    global_sanity.invariant_failure((F("fetching nonexistent entry from %s")
                                     % container_name).str().c_str(),
                                    file, line);
  return i->second;
}
#define safe_get(CONT, VAL) \
  do_safe_get((CONT), (VAL), #CONT, __FILE__, __LINE__)

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
