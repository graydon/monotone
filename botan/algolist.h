/*************************************************
* Algorithm Lookup Table Header File             *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#ifndef BOTAN_ALGOLIST_H__
#define BOTAN_ALGOLIST_H__

#include <botan/base.h>
#include <botan/mode_pad.h>
#include <botan/s2k.h>

namespace Botan {

namespace Algolist {

/*************************************************
* Lookup an algorithm in the table               *
*************************************************/
BlockCipher*               get_block_cipher(const std::string&);
StreamCipher*              get_stream_cipher(const std::string&);
HashFunction*              get_hash(const std::string&);
MessageAuthenticationCode* get_mac(const std::string&);
S2K*                       get_s2k(const std::string&);
BlockCipherModePaddingMethod* get_bc_pad(const std::string&);

}

}

#endif
