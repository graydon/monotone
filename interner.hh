#ifndef __INTERNER_HH__
#define __INTERNER_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <map>
#include <string>

#include "sanity.hh"

template <typename T>
struct 
interner 
{
  std::map<std::string, T> fwd;
  std::map<T, std::string> rev;    
  T max;
  interner() : max(0) {}
  std::string lookup (T in) const
  {
    typename std::map<T, std::string>::const_iterator i = rev.find(in);
    I(i != rev.end());
    return i->second;
  }
  T intern(std::string const & s)
  {
    bool is_new;
    return intern(s, is_new);
  }
  T intern(std::string const & s, bool & is_new) 
  {
    is_new = false;
    typename std::map<std::string, T>::const_iterator i = fwd.find(s);
    if (i == fwd.end())
      {
        is_new = true;
	++max;
	I(rev.find(max) == rev.end());
	fwd.insert(make_pair(s, max));
	rev.insert(make_pair(max, s));
	return max;
      }
    else
      return i->second;
  }
};

#endif // __INTERNER_HH__
