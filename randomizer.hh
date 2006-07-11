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

#include <stdlib.h>

namespace randomizer
{
  // uniform process in [0,n]
  size_t uniform(size_t n);

  // boolean process with prob(true) = p, prob(false) = 1-p.
  bool bernoulli(double p); 

  bool flip(size_t n = 2);
};

#endif
