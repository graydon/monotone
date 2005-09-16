/*************************************************
* Discrete Logarithm Parameters Source File      *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#include <botan/dl_param.h>
#include <botan/numthry.h>
#include <botan/asn1.h>
#include <botan/pipe.h>
#include <botan/pem.h>

namespace Botan {

/*************************************************
* DL_Group Constructor                           *
*************************************************/
DL_Group::DL_Group()
   {
   initialized = false;
   }

/*************************************************
* DL_Group Constructor                           *
*************************************************/
DL_Group::DL_Group(u32bit pbits, PrimeType type)
   {
   if(pbits < 512)
      throw Invalid_Argument("DL_Group: prime size " + to_string(pbits) +
                             " is too small");

   if(type == Strong)
      {
      p = random_safe_prime(pbits, PublicValue);
      q = (p - 1) / 2;
      g = 2;
      }
   else if(type == Prime_Subgroup || type == DSA_Kosherizer)
      {
      if(type == Prime_Subgroup)
         {
         const u32bit qbits = 2 * dl_work_factor(pbits);
         q = random_prime(qbits, PublicValue);
         BigInt X;
         while(p.bits() != pbits || !is_prime(p))
            {
            X = random_integer(pbits, PublicValue);
            p = X - (X % (2*q) - 1);
            }
         }
      else
         generate_dsa_primes(p, q, pbits);

      g = make_dsa_generator(p, q);
      }

   initialized = true;
   }

/*************************************************
* DL_Group Constructor                           *
*************************************************/
DL_Group::DL_Group(const MemoryRegion<byte>& seed, u32bit pbits, u32bit start)
   {
   if(!generate_dsa_primes(p, q, seed.begin(), seed.size(), pbits, start))
      throw Invalid_Argument("DL_Group: The seed/counter given does not "
                             "generate a DSA group");

   g = make_dsa_generator(p, q);

   initialized = true;
   }

/*************************************************
* DL_Group Constructor                           *
*************************************************/
DL_Group::DL_Group(const BigInt& p1, const BigInt& g1)
   {
   initialize(p1, 0, g1);
   }

/*************************************************
* DL_Group Constructor                           *
*************************************************/
DL_Group::DL_Group(const BigInt& p1, const BigInt& q1, const BigInt& g1)
   {
   initialize(p1, q1, g1);
   }

/*************************************************
* DL_Group Initializer                           *
*************************************************/
void DL_Group::initialize(const BigInt& p1, const BigInt& q1, const BigInt& g1)
   {
   if(p1 < 3)
      throw Invalid_Argument("DL_Group: Prime invalid");
   if(g1 < 2 || g1 >= p1)
      throw Invalid_Argument("DL_Group: Generator invalid");
   if(q1 < 0 || q1 >= p1)
      throw Invalid_Argument("DL_Group: Subgroup invalid");

   p = p1;
   g = g1;
   q = q1;

   if(q == 0 && check_prime((p - 1) / 2))
      q = (p - 1) / 2;

   initialized = true;
   }

/*************************************************
* Verify that the group has been set             *
*************************************************/
void DL_Group::init_check() const
   {
   if(!initialized)
      throw Invalid_State("DLP group cannot be used uninitialized");
   }

/*************************************************
* Verify the parameters                          *
*************************************************/
bool DL_Group::verify_group(bool strong) const
   {
   init_check();

   if(g < 2 || p < 3 || q < 0)
      return false;
   if((q != 0) && ((p - 1) % q != 0))
      return false;

   if(!strong)
      return true;

   if(!check_prime(p))
      return false;
   if((q > 0) && !check_prime(q))
      return false;
   return true;
   }

/*************************************************
* Return the prime                               *
*************************************************/
const BigInt& DL_Group::get_p() const
   {
   init_check();
   return p;
   }

/*************************************************
* Return the generator                           *
*************************************************/
const BigInt& DL_Group::get_g() const
   {
   init_check();
   return g;
   }

/*************************************************
* Return the subgroup                            *
*************************************************/
const BigInt& DL_Group::get_q() const
   {
   init_check();
   if(q == 0)
      throw Format_Error("DLP group has no q prime specified");
   return q;
   }

/*************************************************
* DER encode the parameters                      *
*************************************************/
SecureVector<byte> DL_Group::DER_encode(Format format) const
   {
   init_check();

   if((q == 0) && (format != PKCS_3))
      throw Encoding_Error("The ANSI DL parameter formats require a subgroup");

   DER_Encoder encoder;
   encoder.start_sequence();
   if(format == ANSI_X9_57)
      {
      DER::encode(encoder, p);
      DER::encode(encoder, q);
      DER::encode(encoder, g);
      }
   else if(format == ANSI_X9_42)
      {
      DER::encode(encoder, p);
      DER::encode(encoder, g);
      DER::encode(encoder, q);
      }
   else if(format == PKCS_3)
      {
      DER::encode(encoder, p);
      DER::encode(encoder, g);
      }
   else
      throw Invalid_Argument("Unknown DL_Group encoding " + to_string(format));
   encoder.end_sequence();

   return encoder.get_contents();
   }

/*************************************************
* PEM encode the parameters                      *
*************************************************/
std::string DL_Group::PEM_encode(Format format) const
   {
   SecureVector<byte> encoding = DER_encode(format);
   if(format == PKCS_3)
      return PEM_Code::encode(encoding, "DH PARAMETERS");
   else if(format == ANSI_X9_57)
      return PEM_Code::encode(encoding, "DSA PARAMETERS");
   else if(format == ANSI_X9_42)
      return PEM_Code::encode(encoding, "X942 DH PARAMETERS");
   else
      throw Invalid_Argument("Unknown DL_Group encoding " + to_string(format));
   }

/*************************************************
* Decode BER encoded parameters                  *
*************************************************/
void DL_Group::BER_decode(DataSource& source, Format format)
   {
   BigInt new_p, new_q, new_g;

   BER_Decoder decoder(source);
   BER_Decoder sequence = BER::get_subsequence(decoder);
   if(format == ANSI_X9_57)
      {
      BER::decode(sequence, new_p);
      BER::decode(sequence, new_q);
      BER::decode(sequence, new_g);
      }
   else if(format == ANSI_X9_42)
      {
      BER::decode(sequence, new_p);
      BER::decode(sequence, new_g);
      BER::decode(sequence, new_q);
      sequence.discard_remaining();
      }
   else if(format == PKCS_3)
      {
      BER::decode(sequence, new_p);
      BER::decode(sequence, new_g);
      sequence.discard_remaining();
      }
   else
      throw Invalid_Argument("Unknown DL_Group encoding " + to_string(format));
   sequence.verify_end();

   initialize(new_p, new_q, new_g);
   }

/*************************************************
* Decode PEM encoded parameters                  *
*************************************************/
void DL_Group::PEM_decode(DataSource& source)
   {
   std::string label;
   DataSource_Memory ber(PEM_Code::decode(source, label));

   if(label == "DH PARAMETERS")
      BER_decode(ber, PKCS_3);
   else if(label == "DSA PARAMETERS")
      BER_decode(ber, ANSI_X9_57);
   else if(label == "X942 DH PARAMETERS")
      BER_decode(ber, ANSI_X9_42);
   else
      throw Decoding_Error("DL_Group: Invalid PEM label " + label);
   }

/*************************************************
* Create a random DSA-style generator            *
*************************************************/
BigInt DL_Group::make_dsa_generator(const BigInt& p, const BigInt& q)
   {
   BigInt g, e = (p - 1) / q;

   for(u32bit j = 0; j != PRIME_TABLE_SIZE; j++)
      {
      g = power_mod(PRIMES[j], e, p);
      if(g != 1)
         break;
      }

   if(g == 1)
      throw Exception("DL_Group: Couldn't create a suitable generator");

   return g;
   }

}
