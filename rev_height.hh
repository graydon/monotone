#ifndef __REV_HEIGHT_HH_
#define __REV_HEIGHT_HH_

// Copyright (C) 2006 Thomas Moschny <thomas.moschny@gmx.de>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <iostream>
#include <string>
#include "numeric_vocab.hh"

using std::ostream;
using std::string;


class rev_height
{
  string d;
  static size_t const width = sizeof(u32);
  u32 read_at(size_t pos) const;
  void write_at(size_t pos, u32 val);
  void append(u32 val);
  size_t size() const;
  void clear();
public:
  rev_height();
  rev_height(rev_height const & other);
  void from_string(string const & s);
  string const & operator()() const;
  void child_height(rev_height & child, u32 nr) const;
  static void root_height(rev_height & root);

  bool operator ==(rev_height const & other) const;
  bool operator < (rev_height const & other) const;

  inline bool operator !=(rev_height const & other) const
  {
    return !(*this == other);
  }
  inline bool operator > (rev_height const & other) const
  {
    return other < *this;
  }
  inline bool operator <=(rev_height const & other) const
  {
    return !(other < *this);
  }
  inline bool operator >=(rev_height const & other) const
  {
    return !(*this < other);
  }
  friend ostream & operator <<(ostream & os, rev_height const & h);
};

void dump(rev_height const & h, string & out);

#endif // __REV_HEIGHT_HH_


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

