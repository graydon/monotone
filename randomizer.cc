// Copyright (C) 2006 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "randomizer.hh"
#include <boost/random.hpp>

namespace randomizer
{
  static boost::mt19937 *rng = NULL;

  boost::mt19937 &get_rng() 
  {
    if (!rng)
      rng = new boost::mt19937;
    return *rng;
  }
  
  bool flip(size_t n)
  { 
    return bernoulli(1.0 / static_cast<double>(n)); 
  }

  size_t uniform(size_t n)
  {        
    return boost::random_number_generator<boost::mt19937>(get_rng())(n);
  }  

  bool bernoulli(double p)
  {
    typedef boost::mt19937& rng_t;
    typedef boost::bernoulli_distribution<double> dist_t;
    return boost::variate_generator<rng_t, dist_t>(get_rng(), dist_t(p))();
  }  

}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
