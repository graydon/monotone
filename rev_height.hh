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

#include "numeric_vocab.hh"

class rev_height
{
  std::string d;

public:
  rev_height() : d() {}
  rev_height(rev_height const & other) : d(other.d) {}
  explicit rev_height(std::string const & s) : d(s) {}
  std::string const & operator()() const { return d; }

  rev_height child_height(u32 nr) const;
  static rev_height root_height();

  bool valid() const { return d.size() > 0; }

  bool operator ==(rev_height const & other) const
  {
    return this->d == other.d;
  }
  bool operator < (rev_height const & other) const
  {
    return this->d < other.d;
  }
  bool operator !=(rev_height const & other) const
  {
    return this->d != other.d;
  }
  bool operator > (rev_height const & other) const
  {
    return this->d > other.d;
  }
  bool operator <=(rev_height const & other) const
  {
    return this->d <= other.d;
  }
  bool operator >=(rev_height const & other) const
  {
    return this->d >= other.d;
  }
};

std::ostream & operator <<(std::ostream & os, rev_height const & h);
void dump(rev_height const & h, std::string & out);

#endif // __REV_HEIGHT_HH_

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
