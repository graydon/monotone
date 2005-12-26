#ifndef __SAFE_MAP_HH__
#define __SAFE_MAP_HH__

// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// some helpers to safely use maps

// errors out if the key does not exist
template <typename T>
void
do_safe_erase(T & container, typename T::key_type const & key,
              char const * container_name, char const * file, int line)
{
  if (!container.erase(key))
    global_sanity.invariant_failure((F("erasing nonexistent key from %s")
                                     % container_name).str(), 
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
                                     % container_name).str(), 
                                    file, line);
  return r.first;
}
#define safe_insert(CONT, VAL) \
  do_safe_insert((CONT), (VAL), #CONT, __FILE__, __LINE__)


// errors out if the key does not exist
template <typename T>
typename T::mapped_type const &
do_safe_get(T & container, typename T::key_type const & key,
            char const * container_name, char const * file, int line)
{
  typename T::const_iterator i = container.find(key);
  if (i == container.end())
    global_sanity.invariant_failure((F("fetching nonexistent entry from %s")
                                     % container_name).str(), 
                                    file, line);
  return i->second;
}
#define safe_get(CONT, VAL) \
  do_safe_get((CONT), (VAL), #CONT, __FILE__, __LINE__)

#endif
