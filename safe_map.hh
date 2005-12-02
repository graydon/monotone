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
safe_erase(T & container, typename T::key_type const & key)
{
  I(container.erase(key));
}

// errors out if the key already exists
template <typename T>
typename T::iterator
safe_insert(T & container, typename T::value_type const & val)
{
  std::pair<typename T::iterator, bool> r = container.insert(val);
  I(r.second);
  return r.first;
}

// errors out if the key does not exist
template <typename T>
typename T::mapped_type const &
safe_get(T & container, typename T::key_type const & key)
{
  typename T::const_iterator i = container.find(key);
  I(i != container.end());
  return i->second;
}

#endif
