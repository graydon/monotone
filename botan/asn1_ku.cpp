/*************************************************
* KeyUsage Source File                           *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#include <botan/asn1_obj.h>

namespace Botan {

namespace DER {

/*************************************************
* DER encode a KeyUsage BIT STRING               *
*************************************************/
void encode(DER_Encoder& encoder, Key_Constraints usage)
   {
   if(usage == NO_CONSTRAINTS)
      throw Encoding_Error("Cannot encode zero usage constraints");

   const u32bit unused_bits = low_bit(usage) - 1;

   SecureVector<byte> der;
   der.append(BIT_STRING);
   der.append(2 + ((unused_bits < 8) ? 1 : 0));
   der.append(unused_bits % 8);
   der.append((usage >> 8) & 0xFF);
   if(usage & 0xFF)
      der.append(usage & 0xFF);

   encoder.add_raw_octets(der);
   }

}

namespace BER {

/*************************************************
* Decode a BER encoded KeyUsage                  *
*************************************************/
void decode(BER_Decoder& source, Key_Constraints& key_usage)
   {
   BER_Object obj = source.get_next_object();

   if(obj.type_tag != BIT_STRING || obj.class_tag != UNIVERSAL)
      throw BER_Bad_Tag("Bad tag for usage constraint",
                        obj.type_tag, obj.class_tag);
   if(obj.value.size() != 2 && obj.value.size() != 3)
      throw BER_Decoding_Error("Bad size for BITSTRING in usage constraint");
   if(obj.value[0] >= 8)
      throw BER_Decoding_Error("Invalid unused bits in usage constraint");

   const byte mask = (0xFF << obj.value[0]);
   obj.value[obj.value.size()-1] &= mask;

   u16bit usage = 0;
   for(u32bit j = 1; j != obj.value.size(); j++)
      usage = (obj.value[j] << 8) | usage;

   key_usage = Key_Constraints(usage);
   }

}

}
