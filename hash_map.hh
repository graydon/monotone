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
}
#endif

#ifndef HASHMAP_PRESENT
#error need wrapper for hash_map for your compiler
#endif

#undef HASHMAP_PRESENT

#endif
