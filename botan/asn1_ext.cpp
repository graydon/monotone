/*************************************************
* Extension Source File                          *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#include <botan/asn1_obj.h>
#include <botan/oids.h>

namespace Botan {

/*************************************************
* Create an Extension                            *
*************************************************/
Extension::Extension(const OID& extn_oid, const MemoryRegion<byte>& extn_value)
   {
   oid = extn_oid;
   value = extn_value;
   critical = false;
   }

/*************************************************
* Create an Extension                            *
*************************************************/
Extension::Extension(const std::string& extn_oid,
                     const MemoryRegion<byte>& extn_value)
   {
   oid = OIDS::lookup(extn_oid);
   value = extn_value;
   critical = false;
   }

namespace DER {

/*************************************************
* DER encode a Extension                         *
*************************************************/
void encode(DER_Encoder& encoder, const Extension& extn)
   {
   encoder.start_sequence();
   DER::encode(encoder, extn.oid);
   if(extn.critical)
      DER::encode(encoder, true);
   DER::encode(encoder, extn.value, OCTET_STRING);
   encoder.end_sequence();
   }

}

namespace BER {

/*************************************************
* Decode a BER encoded Extension                 *
*************************************************/
void decode(BER_Decoder& source, Extension& extn)
   {
   BER_Decoder extension = BER::get_subsequence(source);
   BER::decode(extension, extn.oid);
   BER::decode_optional(extension, extn.critical, BOOLEAN, UNIVERSAL, false);
   BER::decode(extension, extn.value, OCTET_STRING);
   extension.verify_end();
   }

}

}
