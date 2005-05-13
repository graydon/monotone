#ifndef __XDELTA_HH__
#define __XDELTA_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>
#include <boost/shared_ptr.hpp>

#include "manifest.hh"

void 
compute_delta(std::string const & a,
              std::string const & b,
              std::string & delta);

void
compute_delta(manifest_map const & a,
              manifest_map const & b,
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

#endif // __XDELTA_HH__
