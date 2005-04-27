#ifndef __INTERNER_HH__
#define __INTERNER_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>
#include <ext/hash_map>

#include "sanity.hh"

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

template <typename T>
struct 
interner 
{
  typedef typename __gnu_cxx::hash_map<std::string, T, 
                                       string_hash, 
                                       string_eq> hmap;

  hmap fwd;
  std::vector<std::string> rev;
  interner() {}
  std::string lookup (T in) const
  {
    std::vector<std::string>::size_type k = static_cast<std::vector<std::string>::size_type>(in);
    I(k < rev.size());
    return rev[k];
  }
  T intern(std::string const & s)
  {
    bool is_new;
    return intern(s, is_new);
  }
  T intern(std::string const & s, bool & is_new) 
  {
    is_new = false;
    typename hmap::const_iterator i = fwd.find(s);
    if (i == fwd.end())
      {
        is_new = true;
        T t = rev.size();
	fwd.insert(make_pair(s, t));
        rev.push_back(s);
	return t;
      }
    else
      return i->second;
  }
};

#endif // __INTERNER_HH__
