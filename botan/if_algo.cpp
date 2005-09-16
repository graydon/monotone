/*************************************************
* IF Scheme Source File                          *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#include <botan/if_algo.h>
#include <botan/numthry.h>
#include <botan/asn1.h>

namespace Botan {

/*************************************************
* Return the X.509 public key encoding           *
*************************************************/
MemoryVector<byte> IF_Scheme_PublicKey::DER_encode_pub() const
   {
   DER_Encoder encoder;
   encoder.start_sequence();
   DER::encode(encoder, n);
   DER::encode(encoder, e);
   encoder.end_sequence();
   return encoder.get_contents();
   }

/*************************************************
* Return the X.509 parameters encoding           *
*************************************************/
MemoryVector<byte> IF_Scheme_PublicKey::DER_encode_params() const
   {
   DER_Encoder encoder;
   DER::encode_null(encoder);
   return encoder.get_contents();
   }

/*************************************************
* Decode X.509 public key encoding               *
*************************************************/
void IF_Scheme_PublicKey::BER_decode_pub(DataSource& source)
   {
   BER_Decoder decoder(source);
   BER_Decoder sequence = BER::get_subsequence(decoder);
   BER::decode(sequence, n);
   BER::decode(sequence, e);
   sequence.verify_end();

   X509_load_hook();
   }

/*************************************************
* Decode X.509 algorithm parameters              *
*************************************************/
void IF_Scheme_PublicKey::BER_decode_params(DataSource& source)
   {
   byte dummy = 0;
   while(!source.end_of_data())
      source.read_byte(dummy);
   }

/*************************************************
* Return the PKCS #1 private key encoding        *
*************************************************/
SecureVector<byte> IF_Scheme_PrivateKey::DER_encode_priv() const
   {
   DER_Encoder encoder;
   encoder.start_sequence();
   DER::encode(encoder, 0);
   DER::encode(encoder, n);
   DER::encode(encoder, e);
   DER::encode(encoder, d);
   DER::encode(encoder, p);
   DER::encode(encoder, q);
   DER::encode(encoder, d1);
   DER::encode(encoder, d2);
   DER::encode(encoder, c);
   encoder.end_sequence();
   return encoder.get_contents();
   }

/*************************************************
* Decode a PKCS #1 private key encoding          *
*************************************************/
void IF_Scheme_PrivateKey::BER_decode_priv(DataSource& source)
   {
   u32bit version;

   BER_Decoder decoder(source);
   BER_Decoder sequence = BER::get_subsequence(decoder);
   BER::decode(sequence, version);
   if(version != 0)
      throw Decoding_Error(algo_name() + ": Unknown PKCS #1 key version");
   BER::decode(sequence, n);
   BER::decode(sequence, e);
   BER::decode(sequence, d);
   BER::decode(sequence, p);
   BER::decode(sequence, q);
   BER::decode(sequence, d1);
   BER::decode(sequence, d2);
   BER::decode(sequence, c);
   sequence.verify_end();

   PKCS8_load_hook();
   check_loaded_private();
   }

/*************************************************
* Algorithm Specific X.509 Initialization Code   *
*************************************************/
void IF_Scheme_PublicKey::X509_load_hook()
   {
   core = IF_Core(e, n);
   check_loaded_public();
   }

/*************************************************
* Algorithm Specific PKCS #8 Initialization Code *
*************************************************/
void IF_Scheme_PrivateKey::PKCS8_load_hook()
   {
   if(n == 0)  n = p * q;
   if(d1 == 0) d1 = d % (p - 1);
   if(d2 == 0) d2 = d % (q - 1);
   if(c == 0)  c = inverse_mod(q, p);

   core = IF_Core(e, n, d, p, q, d1, d2, c);
   }

/*************************************************
* Check IF Scheme Public Parameters              *
*************************************************/
bool IF_Scheme_PublicKey::check_key(bool) const
   {
   if(n < 35 || n.is_even() || e < 2)
      return false;
   return true;
   }

/*************************************************
* Check IF Scheme Private Parameters             *
*************************************************/
bool IF_Scheme_PrivateKey::check_key(bool strong) const
   {
   if(n < 35 || n.is_even() || e < 2 || d < 2 || p < 3 || q < 3 || p*q != n)
      return false;

   if(!strong)
      return true;

   if(d1 != d % (p - 1) || d2 != d % (q - 1) || c != inverse_mod(q, p))
      return false;
   if(!check_prime(p) || !check_prime(q))
      return false;
   return true;
   }

}
