/*************************************************
* Map Utility Functions Header File              *
* (C) 1999-2006 The Botan Project                *
*************************************************/

#ifndef BOTAN_MAP_UTIL_H__
#define BOTAN_MAP_UTIL_H__

#include <map>

namespace Botan {

/*************************************************
* Searching through a std::map                   *
*************************************************/
template<typename K, typename V>
inline V search_map(const std::map<K, V>& mapping,
                    const K& key,
                    const V& null_result = V())
   {
   typename std::map<K, V>::const_iterator i = mapping.find(key);
   if(i == mapping.end())
      return null_result;
   return i->second;
   }

template<typename K, typename V, typename R>
inline R search_map(const std::map<K, V>& mapping, const K& key,
                    const R& null_result, const R& found_result)
   {
   typename std::map<K, V>::const_iterator i = mapping.find(key);
   if(i == mapping.end())
      return null_result;
   return found_result;
   }

/*************************************************
* Insert a key/value pair into a multimap        *
*************************************************/
template<typename K, typename V>
void multimap_insert(std::multimap<K, V>& multimap,
                     const K& key, const V& value)
   {
   multimap.insert(std::make_pair(key, value));
   }

}

#endif
