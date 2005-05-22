/*************************************************
* Algorithm Identifier Source File               *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#include <botan/asn1_obj.h>
#include <botan/oids.h>

namespace Botan {

/*************************************************
* Create an AlgorithmIdentifier                  *
*************************************************/
AlgorithmIdentifier::AlgorithmIdentifier(const OID& o,
                                         const MemoryRegion<byte>& p) :
   oid(o), parameters(p)
   {
   }

/*************************************************
* Create an AlgorithmIdentifier                  *
*************************************************/
AlgorithmIdentifier::AlgorithmIdentifier(const std::string& alg_id,
                                         bool use_null)
   {
   const byte DER_NULL[] = { 0x05, 0x00 };

   oid = OIDS::lookup(alg_id);
   if(use_null)
      parameters.append(DER_NULL, sizeof(DER_NULL));
   }

/*************************************************
* Compare two AlgorithmIdentifiers               *
*************************************************/
bool operator==(const AlgorithmIdentifier& a1, const AlgorithmIdentifier& a2)
   {
   if(a1.oid != a2.oid)
      return false;
   if(a1.parameters != a2.parameters)
      return false;
   return true;
   }

/*************************************************
* Compare two AlgorithmIdentifiers               *
*************************************************/
bool operator!=(const AlgorithmIdentifier& a1, const AlgorithmIdentifier& a2)
   {
   return !(a1 == a2);
   }

namespace DER {

/*************************************************
* DER encode an AlgorithmIdentifier              *
*************************************************/
void encode(DER_Encoder& encoder, const AlgorithmIdentifier& alg_id)
   {
   encoder.start_sequence();
   DER::encode(encoder, alg_id.oid);
   encoder.add_raw_octets(alg_id.parameters);
   encoder.end_sequence();
   }

}

namespace BER {

/*************************************************
* Decode a BER encoded AlgorithmIdentifier       *
*************************************************/
void decode(BER_Decoder& source, AlgorithmIdentifier& alg_id)
   {
   BER_Decoder sequence = BER::get_subsequence(source);
   BER::decode(sequence, alg_id.oid);
   alg_id.parameters = sequence.get_remaining();
   sequence.verify_end();
   }

}

}
