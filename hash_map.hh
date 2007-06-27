#ifndef __HASHMAP_HH
#define __HASHMAP_HH


#include <functional>
namespace hashmap {

  template<typename T>
  class equal_to : public std::equal_to<T>
  {
    // bool operator()(T const & b, T const & b) const;
  };

  template<typename T>
  class less : public std::less<T>
  {
    // bool operator()(T const & b, T const & b) const;
  };

  template<typename T>
  struct hash
  {
    // size_t operator()(T const & t) const;
  };

  template<>
  struct hash<unsigned int>
  {
    size_t operator()(unsigned int t) const
    {
      return t;
    }
  };

  template<>
  struct hash<unsigned long>
  {
    size_t operator()(unsigned long t) const
    {
      return t;
    }
  };
}

#if HAVE_TR1_UNORDERED_MAP_AND_SET && HAVE_WORKING_TR1_UNORDERED_MAP_AND_SET
#define HASHMAP_PRESENT
#include <tr1/functional>
#include <tr1/unordered_map>
#include <tr1/unordered_set>

namespace hashmap {
  template<>
  struct hash<std::string>
  {
    size_t operator()(std::string const & s) const
    {
      return std::tr1::hash<std::string>()(s);
    }
  };

  template<typename _Key, typename _Value>
  class hash_map : public std::tr1::unordered_map<_Key,
                                                  _Value,
                                                  hash<_Key>,
                                                  equal_to<_Key> >
  {};

  template<typename _Key>
  class hash_set : public std::tr1::unordered_set<_Key,
                                                  hash<_Key>,
                                                  equal_to<_Key> >
  {};

  template<typename _Key, typename _Value>
  class hash_multimap : public std::tr1::unordered_multimap<_Key,
                                                            _Value,
                                                            hash<_Key>,
                                                            equal_to<_Key> >
  {};
}

#elif defined(HAVE_GNUCXX_HASHMAP)
#define HASHMAP_PRESENT
#include <ext/hash_map>
#include <ext/hash_set>

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

#elif HAVE_STLPORT_HASHMAP
#define HASHMAP_PRESENT
#include <hash_map>
#include <hash_set>

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

#elif _MSC_VER
#define HASHMAP_PRESENT
#include <hash_map>
#include <hash_set>

namespace hashmap
{
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

  template<typename T>
  struct hash_traits
  {
    static const size_t bucket_size = 4;
    static const size_t min_buckets = 8;
    less<T> lt;
    hash<T> h;

    size_t operator( )(T const & s) const
    {
      return h(s);              
    }
    bool operator( )(T const & a,
                     T const & b) const
    {
      return lt(a, b);
    }
  };
  template <typename _Key, typename _Value>
  struct hash_map : public stdext::hash_map<_Key,
                                            _Value,
                                            hash_traits<_Key> >
  {};
  template <typename _Key, typename _Value>
  struct hash_multimap : public stdext::hash_multimap<_Key,
                                                      _Value,
                                                      hash_traits<_Key> >
  {};
  template <typename _Key>
  struct hash_set : public stdext::hash_set<_Key,
                                            hash_traits<_Key> >
  {};
}
#endif

#ifndef HASHMAP_PRESENT
#error need wrapper for hash_map for your compiler
#endif

#undef HASHMAP_PRESENT

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
