/*************************************************
* DER Encoder Source File                        *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#include <botan/der_enc.h>
#include <botan/bigint.h>

namespace Botan {

namespace {

/*************************************************
* DER encode an ASN.1 type tag                   *
*************************************************/
SecureVector<byte> encode_tag(ASN1_Tag type_tag, ASN1_Tag class_tag)
   {
   if((class_tag | 0xE0) != 0xE0)
      throw Encoding_Error("DER_Encoder: Invalid class tag " +
                           to_string(class_tag));

   SecureVector<byte> encoded_tag;
   if(type_tag <= 30)
      encoded_tag.append((byte)(type_tag | class_tag));
   else
      {
      u32bit blocks = high_bit(type_tag) + 6;
      blocks = (blocks - (blocks % 7)) / 7;

      encoded_tag.append(class_tag | 0x1F);
      for(u32bit k = 0; k != blocks - 1; k++)
         encoded_tag.append(0x80 | ((type_tag >> 7*(blocks-k-1)) & 0x7F));
      encoded_tag.append(type_tag & 0x7F);
      }

   return encoded_tag;
   }

/*************************************************
* DER encode an ASN.1 length field               *
*************************************************/
SecureVector<byte> encode_length(u32bit length)
   {
   SecureVector<byte> encoded_length;
   if(length <= 127)
      encoded_length.append((byte)length);
   else
      {
      const u32bit top_byte = significant_bytes(length);
      encoded_length.append((byte)(0x80 | top_byte));
      for(u32bit j = 4-top_byte; j != 4; j++)
         encoded_length.append(get_byte(j, length));
      }
   return encoded_length;
   }

/*************************************************
* A comparison functor for sorting SET objects   *
*************************************************/
class DER_Cmp
   {
   public:
      bool operator()(const MemoryRegion<byte>&,
                      const MemoryRegion<byte>&) const;
   };

/*************************************************
* Compare two encodings, as specified by X.690   *
*************************************************/
bool DER_Cmp::operator()(const MemoryRegion<byte>& a,
                         const MemoryRegion<byte>& b) const
   {
   if(a.size() < b.size()) return true;
   if(a.size() > b.size()) return false;

   for(u32bit j = 0; j != a.size(); j++)
      {
      if(a[j] < b[j]) return true;
      if(a[j] > b[j]) return false;
      }
   return false;
   }

}

/*************************************************
* Return the encoded SEQUENCE/SET                *
*************************************************/
SecureVector<byte> DER_Encoder::DER_Sequence::get_contents()
   {
   const ASN1_Tag real_class_tag = ASN1_Tag(class_tag | CONSTRUCTED);

   SecureVector<byte> encoded_tag = encode_tag(type_tag, real_class_tag);

   if(is_a_set)
      {
      std::sort(set_contents.begin(), set_contents.end(), DER_Cmp());
      for(u32bit j = 0; j != set_contents.size(); j++)
         contents.append(set_contents[j]);
      set_contents.clear();
      }

   SecureVector<byte> encoded_length = encode_length(contents.size());

   SecureVector<byte> retval;
   retval.append(encoded_tag);
   retval.append(encoded_length);
   retval.append(contents);
   contents.destroy();
   return retval;
   }

/*************************************************
* Add an encoded value to the SEQUENCE/SET       *
*************************************************/
void DER_Encoder::DER_Sequence::add_bytes(const byte data[], u32bit length)
   {
   if(is_a_set)
      {
      set_contents.push_back(SecureVector<byte>(data, length));
      }
   else
      contents.append(data, length);
   }

/*************************************************
* Return the type and class taggings             *
*************************************************/
ASN1_Tag DER_Encoder::DER_Sequence::tag_of() const
   {
   return ASN1_Tag(type_tag | class_tag);
   }

/*************************************************
* DER_Sequence Constructor                       *
*************************************************/
DER_Encoder::DER_Sequence::DER_Sequence(ASN1_Tag t1, ASN1_Tag t2, bool b) :
   type_tag(t1), class_tag(t2), is_a_set(b)
   {
   }

/*************************************************
* Return the encoded contents                    *
*************************************************/
SecureVector<byte> DER_Encoder::get_contents()
   {
   if(sequence_level != 0)
      throw Invalid_State("DER_Encoder: Sequence hasn't been marked done");

   SecureVector<byte> retval;
   retval = contents;
   contents.destroy();
   return retval;
   }

/*************************************************
* Start a new ASN.1 SEQUENCE/SET/EXPLICIT        *
*************************************************/
void DER_Encoder::start_cons(ASN1_Tag type_tag, ASN1_Tag class_tag,
                             bool is_a_set)
   {
   sequence_level++;
   subsequences.push_back(DER_Sequence(type_tag, class_tag, is_a_set));
   }

/*************************************************
* Finish the current ASN.1 SEQUENCE/SET/EXPLICIT *
*************************************************/
void DER_Encoder::end_cons(ASN1_Tag type_tag, ASN1_Tag class_tag)
   {
   if(sequence_level == 0)
      throw Invalid_State("DER_Encoder::end_cons: No such sequence");
   sequence_level--;
   if(subsequences[sequence_level].tag_of() != ASN1_Tag(type_tag | class_tag))
      throw Invalid_Argument("DER_Encoder::end_cons: Tag mismatch");

   SecureVector<byte> seq = subsequences[sequence_level].get_contents();
   subsequences.pop_back();
   add_raw_octets(seq);
   }

/*************************************************
* Start a new ASN.1 SEQUENCE                     *
*************************************************/
void DER_Encoder::start_sequence(ASN1_Tag type_tag, ASN1_Tag class_tag)
   {
   start_cons(type_tag, class_tag, false);
   }

/*************************************************
* Finish the current ASN.1 SEQUENCE              *
*************************************************/
void DER_Encoder::end_sequence(ASN1_Tag type_tag, ASN1_Tag class_tag)
   {
   end_cons(type_tag, class_tag);
   }

/*************************************************
* Start a new ASN.1 SET                          *
*************************************************/
void DER_Encoder::start_set(ASN1_Tag type_tag, ASN1_Tag class_tag)
   {
   start_cons(type_tag, class_tag, true);
   }

/*************************************************
* Finish the current ASN.1 SET                   *
*************************************************/
void DER_Encoder::end_set(ASN1_Tag type_tag, ASN1_Tag class_tag)
   {
   end_cons(type_tag, class_tag);
   }

/*************************************************
* Start a new ASN.1 SEQUENCE                     *
*************************************************/
void DER_Encoder::start_sequence()
   {
   start_sequence(SEQUENCE, UNIVERSAL);
   }

/*************************************************
* Finish the current ASN.1 SEQUENCE              *
*************************************************/
void DER_Encoder::end_sequence()
   {
   end_sequence(SEQUENCE, UNIVERSAL);
   }

/*************************************************
* Start a new ASN.1 SET                          *
*************************************************/
void DER_Encoder::start_set()
   {
   start_set(SET, UNIVERSAL);
   }

/*************************************************
* Finish the current ASN.1 SET                   *
*************************************************/
void DER_Encoder::end_set()
   {
   end_set(SET, UNIVERSAL);
   }

/*************************************************
* Start a new ASN.1 EXPLICIT encoding            *
*************************************************/
void DER_Encoder::start_explicit(ASN1_Tag type_tag, ASN1_Tag class_tag)
   {
   start_cons(type_tag, class_tag, false);
   }

/*************************************************
* Finish the current ASN.1 EXPLICIT encoding     *
*************************************************/
void DER_Encoder::end_explicit(ASN1_Tag type_tag, ASN1_Tag class_tag)
   {
   end_cons(type_tag, class_tag);
   }

/*************************************************
* Write raw octets into the stream               *
*************************************************/
void DER_Encoder::add_raw_octets(const MemoryRegion<byte>& octets)
   {
   add_raw_octets(octets.begin(), octets.size());
   }

/*************************************************
* Write raw octets into the stream               *
*************************************************/
void DER_Encoder::add_raw_octets(const byte octets[], u32bit length)
   {
   if(sequence_level == 0)
      contents.append(octets, length);
   else
      subsequences[sequence_level-1].add_bytes(octets, length);
   }

/*************************************************
* Write the encoding of the octet(s)             *
*************************************************/
void DER_Encoder::add_object(ASN1_Tag type_tag, ASN1_Tag class_tag,
                             const byte rep[], u32bit length)
   {
   SecureVector<byte> encoded_tag = encode_tag(type_tag, class_tag);
   SecureVector<byte> encoded_length = encode_length(length);

   SecureVector<byte> buffer;
   buffer.append(encoded_tag);
   buffer.append(encoded_length);
   buffer.append(rep, length);

   add_raw_octets(buffer);
   }

/*************************************************
* Write the encoding of the octet(s)             *
*************************************************/
void DER_Encoder::add_object(ASN1_Tag type_tag, ASN1_Tag class_tag,
                             const MemoryRegion<byte>& rep_buf)
   {
   const byte* rep = rep_buf.begin();
   const u32bit rep_len = rep_buf.size();
   add_object(type_tag, class_tag, rep, rep_len);
   }

/*************************************************
* Write the encoding of the octet(s)             *
*************************************************/
void DER_Encoder::add_object(ASN1_Tag type_tag, ASN1_Tag class_tag,
                             const std::string& rep_str)
   {
   const byte* rep = (const byte*)rep_str.c_str();
   const u32bit rep_len = rep_str.size();
   add_object(type_tag, class_tag, rep, rep_len);
   }

/*************************************************
* Write the encoding of the octet                *
*************************************************/
void DER_Encoder::add_object(ASN1_Tag type_tag, ASN1_Tag class_tag, byte rep)
   {
   add_object(type_tag, class_tag, &rep, 1);
   }

/*************************************************
* DER_Encoder Constructor                        *
*************************************************/
DER_Encoder::DER_Encoder()
    {
   sequence_level = 0;
   }

}
