/*************************************************
* Attribute Source File                          *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#include <botan/asn1_obj.h>
#include <botan/oids.h>

namespace Botan {

/*************************************************
* Create an Attribute                            *
*************************************************/
Attribute::Attribute(const OID& attr_oid, const MemoryRegion<byte>& attr_value)
   {
   oid = attr_oid;
   parameters = attr_value;
   }

/*************************************************
* Create an Attribute                            *
*************************************************/
Attribute::Attribute(const std::string& attr_oid,
                     const MemoryRegion<byte>& attr_value)
   {
   oid = OIDS::lookup(attr_oid);
   parameters = attr_value;
   }

namespace DER {

/*************************************************
* DER encode a Attribute                         *
*************************************************/
void encode(DER_Encoder& encoder, const Attribute& attr)
   {
   encoder.start_sequence();
     DER::encode(encoder, attr.oid);
     encoder.start_set();
       encoder.add_raw_octets(attr.parameters);
     encoder.end_set();
   encoder.end_sequence();
   }

}

namespace BER {

/*************************************************
* Decode a BER encoded Attribute                 *
*************************************************/
void decode(BER_Decoder& source, Attribute& attr)
   {
   BER_Decoder decoder = BER::get_subsequence(source);
   BER::decode(decoder, attr.oid);

   BER_Decoder attributes = BER::get_subset(decoder);
   attr.parameters = attributes.get_remaining();
   attributes.verify_end();

   decoder.verify_end();
   }

}

}
