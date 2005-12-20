/*************************************************
* ASN.1 Header File                              *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#ifndef BOTAN_ASN1_H__
#define BOTAN_ASN1_H__

#include <botan/enums.h>
#include <botan/der_enc.h>
#include <botan/ber_dec.h>

namespace Botan {

/*************************************************
* BER Decoding Error                             *
*************************************************/
struct BER_Decoding_Error : public Decoding_Error
   {
   BER_Decoding_Error(const std::string&);
   };

/*************************************************
* BER Bad Tag Error                              *
*************************************************/
struct BER_Bad_Tag : public BER_Decoding_Error
   {
   BER_Bad_Tag(const std::string&, ASN1_Tag);
   BER_Bad_Tag(const std::string&, ASN1_Tag, ASN1_Tag);
   };

}

#endif
