/*************************************************
* AlternativeName Source File                    *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#include <botan/asn1_obj.h>

namespace Botan {

/*************************************************
* Create an AlternativeName                      *
*************************************************/
AlternativeName::AlternativeName(const std::string& email_addr,
                                 const std::string& uri,
                                 const std::string& dns)
   {
   add_attribute("RFC822", email_addr);
   add_attribute("DNS", dns);
   add_attribute("URI", uri);
   }

/*************************************************
* Add an attribute to an alternative name        *
*************************************************/
void AlternativeName::add_attribute(const std::string& type,
                                    const std::string& str)
   {
   if(type == "" || str == "")
      return;

   typedef std::multimap<std::string, std::string>::iterator iter;
   std::pair<iter, iter> range = alt_info.equal_range(type);
   for(iter j = range.first; j != range.second; j++)
      if(j->second == str)
         return;

   multimap_insert(alt_info, type, str);
   }

/*************************************************
* Get the attributes of this alternative name    *
*************************************************/
std::multimap<std::string, std::string> AlternativeName::get_attributes() const
   {
   return alt_info;
   }

/*************************************************
* Return if this object has anything useful      *
*************************************************/
bool AlternativeName::has_items() const
   {
   return (alt_info.size() > 0);
   }

namespace DER {

/*************************************************
* DER encode a AlternativeName entry             *
*************************************************/
void encode_entries(DER_Encoder& encoder, const AlternativeName& alt_name,
                    const std::string& type, ASN1_Tag tagging)
   {
   std::multimap<std::string, std::string> attr = alt_name.get_attributes();
   typedef std::multimap<std::string, std::string>::iterator iter;

   std::pair<iter, iter> range = attr.equal_range(type);
   for(iter j = range.first; j != range.second; j++)
      {
      ASN1_String asn1_string(j->second, IA5_STRING);
      DER::encode(encoder, asn1_string, tagging, CONTEXT_SPECIFIC);
      }
   }

/*************************************************
* DER encode a AlternativeName                   *
*************************************************/
void encode(DER_Encoder& encoder, const AlternativeName& alt_name)
   {
   encoder.start_sequence();
   encode_entries(encoder, alt_name, "RFC822", ASN1_Tag(1));
   encode_entries(encoder, alt_name, "DNS", ASN1_Tag(2));
   encode_entries(encoder, alt_name, "URI", ASN1_Tag(6));
   encoder.end_sequence();
   }

}

namespace BER {

/*************************************************
* Decode a BER encoded AlternativeName           *
*************************************************/
void decode(BER_Decoder& source, AlternativeName& alt_name)
   {
   BER_Decoder names = BER::get_subsequence(source);
   while(names.more_items())
      {
      BER_Object obj = names.get_next_object();
      if(obj.class_tag != CONTEXT_SPECIFIC)
         continue;

      ASN1_Tag tag = obj.type_tag;
      const std::string value = iso2local(BER::to_string(obj));

      if(tag == 1)      alt_name.add_attribute("RFC822", value);
      else if(tag == 2) alt_name.add_attribute("DNS", value);
      else if(tag == 6) alt_name.add_attribute("URI", value);
      }
   }

}

}
