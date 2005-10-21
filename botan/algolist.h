/*************************************************
* Algorithm Lookup Table Header File             *
* (C) 1999-2005 The Botan Project                *
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
S2K*                       get_s2k(const std::string&);
BlockCipherModePaddingMethod* get_bc_pad(const std::string&);

}

}

#endif
