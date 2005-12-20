/*************************************************
* ASN.1 OID Source File                          *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#include <botan/asn1.h>
#include <botan/bit_ops.h>
#include <botan/parsing.h>

namespace Botan {

/*************************************************
* ASN.1 OID Constructor                          *
*************************************************/
OID::OID(const std::string& oid_str)
   {
   if(oid_str != "")
      {
      id = parse_asn1_oid(oid_str);
      if(id.size() < 2 || id[0] > 2)
         throw Invalid_OID(oid_str);
      if((id[0] == 0 || id[0] == 1) && id[1] > 39)
         throw Invalid_OID(oid_str);
      }
   }

/*************************************************
* Clear the current OID                          *
*************************************************/
void OID::clear()
   {
   id.clear();
   }

/*************************************************
* Return this OID as a string                    *
*************************************************/
std::string OID::as_string() const
   {
   std::string oid_str;
   for(u32bit j = 0; j != id.size(); j++)
      {
      oid_str += to_string(id[j]);
      if(j != id.size() - 1)
         oid_str += '.';
      }
   return oid_str;
   }

/*************************************************
* OID equality comparison                        *
*************************************************/
bool OID::operator==(const OID& oid) const
   {
   if(id.size() != oid.id.size())
      return false;
   for(u32bit j = 0; j != id.size(); j++)
      if(id[j] != oid.id[j])
         return false;
   return true;
   }

/*************************************************
* Append another component to the OID            *
*************************************************/
OID& OID::operator+=(u32bit component)
   {
   id.push_back(component);
   return (*this);
   }

/*************************************************
* Append another component to the OID            *
*************************************************/
OID operator+(const OID& oid, u32bit component)
   {
   OID new_oid(oid);
   new_oid += component;
   return new_oid;
   }

/*************************************************
* OID inequality comparison                      *
*************************************************/
bool operator!=(const OID& a, const OID& b)
   {
   return !(a == b);
   }

/*************************************************
* Compare two OIDs                               *
*************************************************/
bool operator<(const OID& a, const OID& b)
   {
   std::vector<u32bit> oid1 = a.get_id();
   std::vector<u32bit> oid2 = b.get_id();

   if(oid1.size() < oid2.size())
      return true;
   if(oid1.size() > oid2.size())
      return false;
   for(u32bit j = 0; j != oid1.size(); j++)
      {
      if(oid1[j] < oid2[j])
         return true;
      if(oid1[j] > oid2[j])
         return false;
      }
   return false;
   }

namespace DER {

/*************************************************
* DER encode an OBJECT IDENTIFIER                *
*************************************************/
void encode(DER_Encoder& encoder, const OID& oid_obj)
   {
   std::vector<u32bit> oid = oid_obj.get_id();

   if(oid.size() < 2)
      throw Invalid_Argument("DER::encode(OID): OID is invalid");

   MemoryVector<byte> encoding;
   encoding.append(40 * oid[0] + oid[1]);

   for(u32bit j = 2; j != oid.size(); j++)
      {
      if(oid[j] == 0)
         encoding.append(0);
      else
         {
         u32bit blocks = high_bit(oid[j]) + 6;
         blocks = (blocks - (blocks % 7)) / 7;

         for(u32bit k = 0; k != blocks - 1; k++)
            encoding.append(0x80 | ((oid[j] >> 7*(blocks-k-1)) & 0x7F));
         encoding.append(oid[j] & 0x7F);
         }
      }
   encoder.add_object(OBJECT_ID, UNIVERSAL, encoding);
   }

}

namespace BER {

/*************************************************
* Decode a BER encoded OBJECT IDENTIFIER         *
*************************************************/
void decode(BER_Decoder& decoder, OID& oid)
   {
   BER_Object obj = decoder.get_next_object();
   if(obj.type_tag != OBJECT_ID || obj.class_tag != UNIVERSAL)
      throw BER_Bad_Tag("Error decoding OID, unknown tag",
                        obj.type_tag, obj.class_tag);
   if(obj.value.size() < 2)
      throw BER_Decoding_Error("OID encoding is too short");

   oid.clear();
   oid += (obj.value[0] / 40);
   oid += (obj.value[0] % 40);

   u32bit j = 0;
   while(j != obj.value.size() - 1)
      {
      u32bit component = 0;
      while(j != obj.value.size() - 1)
         {
         j++;
         component = (component << 7) + (obj.value[j] & 0x7F);
         if(!(obj.value[j] & 0x80))
            break;
         }
      oid += component;
      }
   }

}

}
