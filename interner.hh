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
struct interner 
{
  map<string, T> fwd;
  map<T, string> rev;    
  T max;
  interner() : max(0) {}
  string lookup (T in) const
  {
    typename map<T, string>::const_iterator i = rev.find(in);
    I(i != rev.end());
    return i->second;
  }
  T intern(string const & s) 
  {
    typename map<string, T>::const_iterator i = fwd.find(s);
    if (i == fwd.end())
      {
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
