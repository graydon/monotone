#ifndef __HASHMAP_HH
#define __HASHMAP_HH

#include <config.h>

#ifdef HAVE_GNUCXX_HASHMAP
#define HASHMAP_PRESENT
#include <ext/hash_map>
#include <ext/hash_set>
#include <string>

namespace hashmap {
 using __gnu_cxx::hash_map;
 using __gnu_cxx::hash_set;
 using __gnu_cxx::hash_multimap;

 struct string_hash
 {
   size_t operator()(std::string const & s) const
   {
    return __gnu_cxx::__stl_hash_string(s.c_str());
   }
 };
 struct string_eq
 {
   bool operator()(std::string const & a,
                   std::string const & b) const
   {
     return a == b;
   }
 };
 template <typename V> struct string_hashmap 
	 : public hash_map<std::string, V, string_hash, string_eq>
 {};
 template <typename V> struct string_hashmultimap 
	 : public hash_multimap<std::string, V, string_hash, string_eq>
 {};
 struct string_hashset 
	 : public hash_set<std::string, string_hash, string_eq>
 {};	
}
#endif

#ifdef HAVE_STLPORT_HASHMAP
#define HASHMAP_PRESENT
#include <hash_map>
#include <hash_set>
#include <string>

namespace hashmap {
 using std::hash_map;
 using std::hash_set;
 using std::hash_multimap;

 struct string_hash
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
 struct string_eq
 {
   bool operator()(std::string const & a,
                   std::string const & b) const
   {
     return a == b;
   }
 };
 struct string_hashmap<typename V> 
	 : public hash_map<std::string, V, string_hash, string_eq>
 {};
 template <typename V> struct string_hashmultimap 
	 : public hash_multimap<std::string, V, string_hash, string_eq>
 {};
 struct string_hashset 
	 : public hash_set<std::string, string_hash, string_eq>
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
