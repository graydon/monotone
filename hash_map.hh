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

#ifdef _MSC_VER
#define HASHMAP_PRESENT
#include <hash_map>
#include <hash_set>

namespace hashmap 
{
	using stdext::hash_map;
	using stdext::hash_set;
	using stdext::hash_multimap;

	struct string_hash_traits
	{
		static const size_t bucket_size = 4;
		static const size_t min_buckets = 8;

		size_t operator( )(std::string const & s) const
		{
			const char* s2=s.c_str();
			unsigned long h = 0;
			for ( ; *s2; ++s2)
				h = 5*h + *s2;
			return size_t(h);			
		}
		bool operator( )(std::string const & a,
						 std::string const & b) const
		{
			return a < b;
		}
	};
  template <typename V> struct string_hashmap 
	 : public hash_map<std::string, V, string_hash_traits>
  {};
  template <typename V> struct string_hashmultimap 
	 : public hash_multimap<std::string, V, string_hash_traits>
  {};
  struct string_hashset
	  : public hash_set<std::string, string_hash_traits>
  {};
}
#endif

#ifndef HASHMAP_PRESENT
#error need wrapper for hash_map for your compiler
#endif

#undef HASHMAP_PRESENT

#endif
