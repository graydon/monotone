#ifndef __MAC_HH__
#define __MAC_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// message authentication code (HMAC) calculation. 

#include <string>

#include "app_state.hh"
#include "constants.hh"
#include "transforms.hh"

#include "botan/botan.h"

void make_random_seed(app_state & app,
		      std::string & hexseed)
{
  using namespace Botan;

  byte seed[constants::vchecklen];

  Global_RNG::randomize(seed, constants::vchecklen);
  Pipe p(new Hex_Encoder());
  p.process_msg(seed, constants::vchecklen);
  hexseed = lowercase(p.read_all_as_string());
}

void calculate_mac(std::string const & mackey, 
		   std::string const & data,
		   std::string & mac)
{
  using namespace Botan;

  Pipe p(new MAC_Filter("HMAC(SHA-1)", mackey), new Hex_Encoder());
  p.process_msg(data);
  mac = lowercase(p.read_all_as_string());
}

#endif // __MAC_HH__
