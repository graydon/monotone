/*************************************************
* Lookup Table Management Header File            *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#ifndef BOTAN_LOOKUP_MANGEMENT_H__
#define BOTAN_LOOKUP_MANGEMENT_H__

#include <botan/base.h>
#include <botan/mode_pad.h>
#include <botan/s2k.h>

namespace Botan {

/*************************************************
* Add an algorithm to the lookup table           *
*************************************************/
void add_algorithm(BlockCipher*);
void add_algorithm(StreamCipher*);
void add_algorithm(HashFunction*);
void add_algorithm(MessageAuthenticationCode*);
void add_algorithm(S2K*);
void add_algorithm(BlockCipherModePaddingMethod*);

/*************************************************
* Add an alias for an algorithm                  *
*************************************************/
void add_alias(const std::string&, const std::string&);

/*************************************************
* Lookup table startup/shutdown                  *
*************************************************/
void init_lookup_tables();
void destroy_lookup_tables();
void add_default_oids();
void add_default_aliases();

}

#endif
