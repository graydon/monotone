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

#include "cryptopp/filters.h"
#include "cryptopp/sha.h"
#include "cryptopp/hex.h"
#include "cryptopp/hmac.h"
#include "cryptopp/osrng.h"

void make_random_seed(app_state & app,
		      std::string & hexseed)
{
  using namespace CryptoPP;
  bool request_blocking_rng = false;
  if (!app.lua.hook_non_blocking_rng_ok())
    {
#ifndef BLOCKING_RNG_AVAILABLE 
      throw oops("no blocking RNG available and non-blocking RNG rejected");
#else
      request_blocking_rng = true;
#endif
    }

  byte seed[constants::vchecklen];
  AutoSeededRandomPool rng(request_blocking_rng);
  rng.GenerateBlock(seed, constants::vchecklen);
  std::string out(reinterpret_cast<char *>(seed), constants::vchecklen);
  StringSource str(out, true, 
		   new HexEncoder(new StringSink(hexseed)));  
  hexseed = lowercase(hexseed);
}

void calculate_mac(std::string const & mackey, 
		   std::string const & data,
		   std::string & mac)
{
  using namespace CryptoPP;
  char buf[HMAC<SHA>::DIGESTSIZE];
  HMAC<SHA> mac_calc(reinterpret_cast<byte const *>(mackey.data()), 
		     static_cast<unsigned int>(mackey.size()));
  mac_calc.CalculateDigest(reinterpret_cast<byte *>(buf), 
			   reinterpret_cast<byte const *>(data.data()), 
			   static_cast<unsigned int>(data.size()));
  std::string out(buf, HMAC<SHA>::DIGESTSIZE);
  mac.clear();
  StringSource str(out, true, 
		   new HexEncoder(new StringSink(mac)));
  mac = lowercase(mac);
}

#endif // __MAC_HH__
