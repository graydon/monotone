#ifndef __XDELTA_HH__
#define __XDELTA_HH__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <boost/shared_ptr.hpp>

void
compute_delta(std::string const & a,
              std::string const & b,
              std::string & delta);

void
apply_delta(std::string const & a,
            std::string const & delta,
            std::string & b);


struct delta_applicator
{
  virtual ~delta_applicator () {}
  virtual void begin(std::string const & base) = 0;
  virtual void next() = 0;
  virtual void finish(std::string & out) = 0;

  virtual void copy(std::string::size_type pos, std::string::size_type len) = 0;
  virtual void insert(std::string const & str) = 0;
};

boost::shared_ptr<delta_applicator> new_simple_applicator();
boost::shared_ptr<delta_applicator> new_piecewise_applicator();

void apply_delta(boost::shared_ptr<delta_applicator> da,
                 std::string const & delta);

u64 measure_delta_target_size(std::string const & delta);
void invert_xdelta(std::string const & old_str,
		   std::string const & delta,
		   std::string & delta_inverse);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __XDELTA_HH__
