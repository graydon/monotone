#ifndef __INTERNER_HH__
#define __INTERNER_HH__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.


#include "hash_map.hh"
#include "sanity.hh"

template <typename T>
struct
interner
{
  typedef typename hashmap::hash_map<std::string, T> hmap;

  hmap fwd;
  std::vector<std::string> rev;
  interner() {}
  interner(std::string const & init_str, T init_value)
  {
    I(intern(init_str) == init_value);
  }
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
    T t = rev.size();
    // if fwd already contains an entry with key s, this just finds
    // that and returns it
    std::pair<typename hmap::iterator, bool> res(fwd.insert(make_pair(s, t)));
    is_new = res.second;
    if (is_new)
      rev.push_back(s);
    return res.first->second;
  }
};

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __INTERNER_HH__
