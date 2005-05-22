/*************************************************
* Global RNG Header File                         *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#ifndef BOTAN_GLOBAL_RNG_H__
#define BOTAN_GLOBAL_RNG_H__

#include <botan/base.h>

namespace Botan {

/*************************************************
* RNG Access and Seeding Functions               *
*************************************************/
namespace Global_RNG {

void randomize(byte[], u32bit, RNG_Quality = SessionKey);
byte random(RNG_Quality = SessionKey);

void add_entropy(const byte[], u32bit);
void add_entropy(EntropySource&, bool = true);

u32bit seed(bool = true, u32bit = 256);

void add_es(EntropySource*, bool = true);

}

/*************************************************
* RNG Control Functions                          *
*************************************************/
namespace Init {

void set_global_rngs(RandomNumberGenerator*, RandomNumberGenerator*);
void init_rng_subsystem();
void shutdown_rng_subsystem();

}

}

#endif
