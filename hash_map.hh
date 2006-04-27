#ifndef __HASHMAP_HH
#define __HASHMAP_HH

#include "config.h"

#include <functional>
namespace hashmap {
  template<typename _T>
  class equal_to : public std::equal_to<_T>
  {};
  
  template<typename _T>
  struct hash
  {
//    size_t operator()(_T const & t) const;
  };
  template<>
  struct hash<unsigned int>
  {
    size_t operator()(unsigned int t) const
    {
      return t;
    }
  };
}

#ifdef HAVE_GNUCXX_HASHMAP
#define HASHMAP_PRESENT
#include <ext/hash_map>
#include <ext/hash_set>
#include <string>

namespace hashmap {
  template<>
  struct hash<std::string>
  {
    size_t operator()(std::string const & s) const
    {
      return __gnu_cxx::__stl_hash_string(s.c_str());
    }
  };
  
  template<typename _Key, typename _Value>
  class hash_map : public __gnu_cxx::hash_map<_Key,
                                              _Value,
                                              hash<_Key>,
                                              equal_to<_Key> >
  {};
  
  template<typename _Key>
  class hash_set : public __gnu_cxx::hash_set<_Key,
                                              hash<_Key>,
                                              equal_to<_Key> >
  {};
  
  template<typename _Key, typename _Value>
  class hash_multimap : public __gnu_cxx::hash_multimap<_Key,
                                                        _Value,
                                                        hash<_Key>,
                                                        equal_to<_Key> >
  {};

	
}
#endif

#ifdef HAVE_STLPORT_HASHMAP
#define HASHMAP_PRESENT
#include <hash_map>
#include <hash_set>
#include <string>

namespace hashmap {
  template<>
  struct hash<std::string>
  {
    size_t operator()(std::string const & s) const
    {
      const char* s2=s.c_str();
      unsigned long h = 0;
      for ( ; *s2; ++s2)
        h = 5*h + *s2;
      return size_t(h);
    }
  };

  template<typename _Key, typename _Value>
  class hash_map : public std::hash_map<_Key,
                                        _Value,
                                        hash<_Key>,
                                        equal_to<_Key> >
  {};
  
  template<typename _Key>
  class hash_set : public std::hash_set<_Key,
                                        hash<_Key>,
                                        equal_to<_Key> >
  {};
  
  template<typename _Key, typename _Value>
  class hash_multimap : public std::hash_multimap<_Key,
                                                  _Value,
                                                  hash<_Key>,
                                                  equal_to<_Key> >
  {};

}
#endif

#ifndef HASHMAP_PRESENT
#error need wrapper for hash_map for your compiler
#endif

#undef HASHMAP_PRESENT

#endif
