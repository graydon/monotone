/*************************************************
* ASN.1 Header File                              *
* (C) 1999-2004 The Botan Project                *
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
   BER_Decoding_Error(const std::string& str) :
      Decoding_Error("BER: " + str) {}
   };

/*************************************************
* BER Bad Tag Error                              *
*************************************************/
struct BER_Bad_Tag : public BER_Decoding_Error
   {
   BER_Bad_Tag(const std::string& str, ASN1_Tag tag) :
      BER_Decoding_Error(str + ": " + to_string(tag)) {}
   BER_Bad_Tag(const std::string& str, ASN1_Tag tag1, ASN1_Tag tag2) :
      BER_Decoding_Error(str + ": " + to_string(tag1) + "/" +
                                      to_string(tag2)) {}
   };

}

#endif
