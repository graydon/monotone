/*************************************************
* DER Coding Source File                         *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#include <botan/der_enc.h>

namespace Botan {

namespace DER {

/*************************************************
* Put some arbitrary bytes into a SEQUENCE       *
*************************************************/
SecureVector<byte> put_in_sequence(const MemoryRegion<byte>& contents)
   {
   DER_Encoder encoder;
   encoder.start_sequence();
   encoder.add_raw_octets(contents);
   encoder.end_sequence();
   return encoder.get_contents();
   }

/*************************************************
* DER encode NULL                                *
*************************************************/
void encode_null(DER_Encoder& encoder)
   {
   encoder.add_object(NULL_TAG, UNIVERSAL, 0, 0);
   }

/*************************************************
* DER encode a BOOLEAN                           *
*************************************************/
void encode(DER_Encoder& encoder, bool is_true)
   {
   encode(encoder, is_true, BOOLEAN, UNIVERSAL);
   }

/*************************************************
* DER encode a small INTEGER                     *
*************************************************/
void encode(DER_Encoder& encoder, u32bit n)
   {
   encode(encoder, BigInt(n), INTEGER, UNIVERSAL);
   }

/*************************************************
* DER encode a small INTEGER                     *
*************************************************/
void encode(DER_Encoder& encoder, int n)
   {
   if(n < 0)
      throw Invalid_Argument("DER::encode(int): n must be >= 0");
   encode(encoder, BigInt(n), INTEGER, UNIVERSAL);
   }

/*************************************************
* DER encode an INTEGER                          *
*************************************************/
void encode(DER_Encoder& encoder, const BigInt& n)
   {
   encode(encoder, n, INTEGER, UNIVERSAL);
   }

/*************************************************
* DER encode an OCTET STRING or BIT STRING       *
*************************************************/
void encode(DER_Encoder& encoder, const MemoryRegion<byte>& octets,
            ASN1_Tag real_type)
   {
   encode(encoder, octets.begin(), octets.size(),
          real_type, real_type, UNIVERSAL);
   }

/*************************************************
* DER encode an OCTET STRING or BIT STRING       *
*************************************************/
void encode(DER_Encoder& encoder, const byte octets[], u32bit length,
            ASN1_Tag real_type)
   {
   encode(encoder, octets, length, real_type, real_type, UNIVERSAL);
   }

/*************************************************
* DER encode a BOOLEAN                           *
*************************************************/
void encode(DER_Encoder& encoder, bool is_true,
            ASN1_Tag type_tag, ASN1_Tag class_tag)
   {
   if(is_true)
      encoder.add_object(type_tag, class_tag, 0xFF);
   else
      encoder.add_object(type_tag, class_tag, 0x00);
   }

/*************************************************
* DER encode a small INTEGER                     *
*************************************************/
void encode(DER_Encoder& encoder, u32bit n,
            ASN1_Tag type_tag, ASN1_Tag class_tag)
   {
   encode(encoder, BigInt(n), type_tag, class_tag);
   }

/*************************************************
* DER encode a small INTEGER                     *
*************************************************/
void encode(DER_Encoder& encoder, int n,
            ASN1_Tag type_tag, ASN1_Tag class_tag)
   {
   if(n < 0)
      throw Invalid_Argument("DER::encode(int): n must be >= 0");
   encode(encoder, BigInt(n), type_tag, class_tag);
   }

/*************************************************
* DER encode an INTEGER                          *
*************************************************/
void encode(DER_Encoder& encoder, const BigInt& n,
            ASN1_Tag type_tag, ASN1_Tag class_tag)
   {
   if(n == 0)
      encoder.add_object(type_tag, class_tag, 0);
   else
      {
      bool extra_zero = (n.bits() % 8 == 0);
      SecureVector<byte> contents(extra_zero + n.bytes());
      BigInt::encode(contents.begin() + extra_zero, n);
      if(n < 0)
         {
         for(u32bit j = 0; j != contents.size(); j++)
            contents[j] = ~contents[j];
         for(u32bit j = contents.size(); j > 0; j--)
            if(++contents[j-1])
               break;
         }
      encoder.add_object(type_tag, class_tag, contents);
      }
   }

/*************************************************
* DER encode an OCTET STRING or BIT STRING       *
*************************************************/
void encode(DER_Encoder& encoder, const MemoryRegion<byte>& octets,
            ASN1_Tag real_type, ASN1_Tag type_tag, ASN1_Tag class_tag)
   {
   encode(encoder, octets.begin(), octets.size(),
          real_type, type_tag, class_tag);
   }

/*************************************************
* DER encode an OCTET STRING or BIT STRING       *
*************************************************/
void encode(DER_Encoder& encoder, const byte octets[], u32bit length,
            ASN1_Tag real_type, ASN1_Tag type_tag, ASN1_Tag class_tag)
   {
   if(real_type != OCTET_STRING && real_type != BIT_STRING)
      throw Invalid_Argument("DER_Encoder: Invalid tag for byte/bit string");

   if(real_type == OCTET_STRING)
      encoder.add_object(type_tag, class_tag, octets, length);
   else
      {
      SecureVector<byte> encoded;
      encoded.append(0);
      encoded.append(octets, length);
      encoder.add_object(type_tag, class_tag, encoded);
      }
   }

}

}
