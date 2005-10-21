/*************************************************
* DER Encoder Header File                        *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#ifndef BOTAN_DER_ENCODER_H__
#define BOTAN_DER_ENCODER_H__

#include <botan/asn1_oid.h>
#include <botan/bigint.h>
#include <vector>

namespace Botan {

/*************************************************
* General DER Encoding Object                    *
*************************************************/
class DER_Encoder
   {
   public:
      SecureVector<byte> get_contents();

      void start_sequence(ASN1_Tag, ASN1_Tag = CONTEXT_SPECIFIC);
      void end_sequence(ASN1_Tag, ASN1_Tag = CONTEXT_SPECIFIC);
      void start_set(ASN1_Tag, ASN1_Tag = CONTEXT_SPECIFIC);
      void end_set(ASN1_Tag, ASN1_Tag = CONTEXT_SPECIFIC);

      void start_sequence();
      void end_sequence();
      void start_set();
      void end_set();

      void start_explicit(ASN1_Tag, ASN1_Tag = CONTEXT_SPECIFIC);
      void end_explicit(ASN1_Tag, ASN1_Tag = CONTEXT_SPECIFIC);

      void add_raw_octets(const byte[], u32bit);
      void add_raw_octets(const MemoryRegion<byte>&);

      void add_object(ASN1_Tag, ASN1_Tag, const byte[], u32bit);
      void add_object(ASN1_Tag, ASN1_Tag, const MemoryRegion<byte>&);
      void add_object(ASN1_Tag, ASN1_Tag, const std::string&);
      void add_object(ASN1_Tag, ASN1_Tag, byte);

      DER_Encoder();
   private:
      void start_cons(ASN1_Tag, ASN1_Tag, bool);
      void end_cons(ASN1_Tag, ASN1_Tag);
      class DER_Sequence
         {
         public:
            ASN1_Tag tag_of() const;
            SecureVector<byte> get_contents();
            void add_bytes(const byte[], u32bit);
            DER_Sequence(ASN1_Tag, ASN1_Tag, bool = false);
         private:
            ASN1_Tag type_tag, class_tag;
            bool is_a_set;
            SecureVector<byte> contents;
            std::vector< SecureVector<byte> > set_contents;
         };
      SecureVector<byte> contents;
      std::vector<DER_Sequence> subsequences;
      u32bit sequence_level;
   };

/*************************************************
* DER Encoding Functions                         *
*************************************************/
namespace DER {

void encode_null(DER_Encoder&);
void encode(DER_Encoder&, const OID&);

void encode(DER_Encoder&, bool);
void encode(DER_Encoder&, int);
void encode(DER_Encoder&, u32bit);
void encode(DER_Encoder&, const BigInt&);
void encode(DER_Encoder&, const MemoryRegion<byte>&, ASN1_Tag);
void encode(DER_Encoder&, const byte[], u32bit, ASN1_Tag);

void encode(DER_Encoder&, bool, ASN1_Tag, ASN1_Tag = CONTEXT_SPECIFIC);
void encode(DER_Encoder&, u32bit, ASN1_Tag, ASN1_Tag = CONTEXT_SPECIFIC);
void encode(DER_Encoder&, const BigInt&, ASN1_Tag,
            ASN1_Tag = CONTEXT_SPECIFIC);
void encode(DER_Encoder&, const MemoryRegion<byte>&,
            ASN1_Tag, ASN1_Tag, ASN1_Tag = CONTEXT_SPECIFIC);
void encode(DER_Encoder&, const byte[], u32bit,
            ASN1_Tag, ASN1_Tag, ASN1_Tag = CONTEXT_SPECIFIC);

SecureVector<byte> put_in_sequence(const MemoryRegion<byte>&);

}

}

#endif
