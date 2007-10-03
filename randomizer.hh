#ifndef __RANDOMIZER_HH__
#define __RANDOMIZER_HH__

// Copyright (C) 2006 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// This is just a set of utility methods built on top of boost::random.
//
// Our purpose is to create a global randomization utility for unit
// tests. Nothing fancy.

#include <cstddef>
#include <boost/random/mersenne_twister.hpp>

struct randomizer
{
  boost::mt19937 rng;

  // uniform process in [0,n]
  std::size_t uniform(std::size_t n);

  // boolean process with prob(true) = p, prob(false) = 1-p.
  bool bernoulli(double p); 

  bool flip(std::size_t n = 2);
};

#endif


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

