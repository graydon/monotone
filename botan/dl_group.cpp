/*************************************************
* DL Groups Source File                          *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#include <botan/dl_param.h>

namespace Botan {

namespace {

/*************************************************
* IETF 768-bit DL modulus                        *
*************************************************/
const char* IETF_768_PRIME =
   "FFFFFFFF FFFFFFFF C90FDAA2 2168C234 C4C6628B 80DC1CD1"
   "29024E08 8A67CC74 020BBEA6 3B139B22 514A0879 8E3404DD"
   "EF9519B3 CD3A431B 302B0A6D F25F1437 4FE1356D 6D51C245"
   "E485B576 625E7EC6 F44C42E9 A63A3620 FFFFFFFF FFFFFFFF";

/*************************************************
* IETF 1024-bit DL modulus                       *
*************************************************/
const char* IETF_1024_PRIME =
   "FFFFFFFF FFFFFFFF C90FDAA2 2168C234 C4C6628B 80DC1CD1"
   "29024E08 8A67CC74 020BBEA6 3B139B22 514A0879 8E3404DD"
   "EF9519B3 CD3A431B 302B0A6D F25F1437 4FE1356D 6D51C245"
   "E485B576 625E7EC6 F44C42E9 A637ED6B 0BFF5CB6 F406B7ED"
   "EE386BFB 5A899FA5 AE9F2411 7C4B1FE6 49286651 ECE65381"
   "FFFFFFFF FFFFFFFF";

/*************************************************
* IETF 1536-bit DL modulus                       *
*************************************************/
const char* IETF_1536_PRIME =
   "FFFFFFFF FFFFFFFF C90FDAA2 2168C234 C4C6628B 80DC1CD1"
   "29024E08 8A67CC74 020BBEA6 3B139B22 514A0879 8E3404DD"
   "EF9519B3 CD3A431B 302B0A6D F25F1437 4FE1356D 6D51C245"
   "E485B576 625E7EC6 F44C42E9 A637ED6B 0BFF5CB6 F406B7ED"
   "EE386BFB 5A899FA5 AE9F2411 7C4B1FE6 49286651 ECE45B3D"
   "C2007CB8 A163BF05 98DA4836 1C55D39A 69163FA8 FD24CF5F"
   "83655D23 DCA3AD96 1C62F356 208552BB 9ED52907 7096966D"
   "670C354E 4ABC9804 F1746C08 CA237327 FFFFFFFF FFFFFFFF";

/*************************************************
* IETF 2048-bit DL modulus                       *
*************************************************/
const char* IETF_2048_PRIME =
   "FFFFFFFF FFFFFFFF C90FDAA2 2168C234 C4C6628B 80DC1CD1"
   "29024E08 8A67CC74 020BBEA6 3B139B22 514A0879 8E3404DD"
   "EF9519B3 CD3A431B 302B0A6D F25F1437 4FE1356D 6D51C245"
   "E485B576 625E7EC6 F44C42E9 A637ED6B 0BFF5CB6 F406B7ED"
   "EE386BFB 5A899FA5 AE9F2411 7C4B1FE6 49286651 ECE45B3D"
   "C2007CB8 A163BF05 98DA4836 1C55D39A 69163FA8 FD24CF5F"
   "83655D23 DCA3AD96 1C62F356 208552BB 9ED52907 7096966D"
   "670C354E 4ABC9804 F1746C08 CA18217C 32905E46 2E36CE3B"
   "E39E772C 180E8603 9B2783A2 EC07A28F B5C55DF0 6F4C52C9"
   "DE2BCBF6 95581718 3995497C EA956AE5 15D22618 98FA0510"
   "15728E5A 8AACAA68 FFFFFFFF FFFFFFFF";

/*************************************************
* IETF 3072-bit DL modulus                       *
*************************************************/
const char* IETF_3072_PRIME =
  "FFFFFFFF FFFFFFFF C90FDAA2 2168C234 C4C6628B 80DC1CD1"
  "29024E08 8A67CC74 020BBEA6 3B139B22 514A0879 8E3404DD"
  "EF9519B3 CD3A431B 302B0A6D F25F1437 4FE1356D 6D51C245"
  "E485B576 625E7EC6 F44C42E9 A637ED6B 0BFF5CB6 F406B7ED"
  "EE386BFB 5A899FA5 AE9F2411 7C4B1FE6 49286651 ECE45B3D"
  "C2007CB8 A163BF05 98DA4836 1C55D39A 69163FA8 FD24CF5F"
  "83655D23 DCA3AD96 1C62F356 208552BB 9ED52907 7096966D"
  "670C354E 4ABC9804 F1746C08 CA18217C 32905E46 2E36CE3B"
  "E39E772C 180E8603 9B2783A2 EC07A28F B5C55DF0 6F4C52C9"
  "DE2BCBF6 95581718 3995497C EA956AE5 15D22618 98FA0510"
  "15728E5A 8AAAC42D AD33170D 04507A33 A85521AB DF1CBA64"
  "ECFB8504 58DBEF0A 8AEA7157 5D060C7D B3970F85 A6E1E4C7"
  "ABF5AE8C DB0933D7 1E8C94E0 4A25619D CEE3D226 1AD2EE6B"
  "F12FFA06 D98A0864 D8760273 3EC86A64 521F2B18 177B200C"
  "BBE11757 7A615D6C 770988C0 BAD946E2 08E24FA0 74E5AB31"
  "43DB5BFC E0FD108E 4B82D120 A93AD2CA FFFFFFFF FFFFFFFF";

/*************************************************
* IETF 4096-bit DL modulus                       *
*************************************************/
const char* IETF_4096_PRIME =
  "FFFFFFFF FFFFFFFF C90FDAA2 2168C234 C4C6628B 80DC1CD1"
  "29024E08 8A67CC74 020BBEA6 3B139B22 514A0879 8E3404DD"
  "EF9519B3 CD3A431B 302B0A6D F25F1437 4FE1356D 6D51C245"
  "E485B576 625E7EC6 F44C42E9 A637ED6B 0BFF5CB6 F406B7ED"
  "EE386BFB 5A899FA5 AE9F2411 7C4B1FE6 49286651 ECE45B3D"
  "C2007CB8 A163BF05 98DA4836 1C55D39A 69163FA8 FD24CF5F"
  "83655D23 DCA3AD96 1C62F356 208552BB 9ED52907 7096966D"
  "670C354E 4ABC9804 F1746C08 CA18217C 32905E46 2E36CE3B"
  "E39E772C 180E8603 9B2783A2 EC07A28F B5C55DF0 6F4C52C9"
  "DE2BCBF6 95581718 3995497C EA956AE5 15D22618 98FA0510"
  "15728E5A 8AAAC42D AD33170D 04507A33 A85521AB DF1CBA64"
  "ECFB8504 58DBEF0A 8AEA7157 5D060C7D B3970F85 A6E1E4C7"
  "ABF5AE8C DB0933D7 1E8C94E0 4A25619D CEE3D226 1AD2EE6B"
  "F12FFA06 D98A0864 D8760273 3EC86A64 521F2B18 177B200C"
  "BBE11757 7A615D6C 770988C0 BAD946E2 08E24FA0 74E5AB31"
  "43DB5BFC E0FD108E 4B82D120 A9210801 1A723C12 A787E6D7"
  "88719A10 BDBA5B26 99C32718 6AF4E23C 1A946834 B6150BDA"
  "2583E9CA 2AD44CE8 DBBBC2DB 04DE8EF9 2E8EFC14 1FBECAA6"
  "287C5947 4E6BC05D 99B2964F A090C3A2 233BA186 515BE7ED"
  "1F612970 CEE2D7AF B81BDD76 2170481C D0069127 D5B05AA9"
  "93B4EA98 8D8FDDC1 86FFB7DC 90A6C08F 4DF435C9 34063199"
  "FFFFFFFF FFFFFFFF";

/*************************************************
* JCE seed/counter for 512-bit DSA modulus       *
*************************************************/
const char* JCE_512_SEED = "B869C82B 35D70E1B 1FF91B28 E37A62EC DC34409B";
const u32bit JCE_512_COUNTER = 123;

/*************************************************
* JCE seed/counter for 768-bit DSA modulus       *
*************************************************/
const char* JCE_768_SEED = "77D0F8C4 DAD15EB8 C4F2F8D6 726CEFD9 6D5BB399";
const u32bit JCE_768_COUNTER = 263;

/*************************************************
* JCE seed/counter for 1024-bit DSA modulus      *
*************************************************/
const char* JCE_1024_SEED = "8D515589 4229D5E6 89EE01E6 018A237E 2CAE64CD";
const u32bit JCE_1024_COUNTER = 92;

/*************************************************
* Decode the modulus string                      *
*************************************************/
BigInt decode(const char* prime)
   {
   return BigInt::decode((const byte*)prime, std::strlen(prime),
                         BigInt::Hexadecimal);
   }

/*************************************************
* Decode the seed for DSA prime generation       *
*************************************************/
MemoryVector<byte> decode_seed(const std::string& hex_seed)
   {
   return OctetString(hex_seed).bits_of();
   }

}

/*************************************************
* Try to obtain a particular DL group            *
*************************************************/
DL_Group try_to_get_dl_group(const std::string& name)
   {
   if(name == "DSA-512")
      return DL_Group(decode_seed(JCE_512_SEED), 512, JCE_512_COUNTER);
   if(name == "DSA-768")
      return DL_Group(decode_seed(JCE_768_SEED), 768, JCE_768_COUNTER);
   if(name == "DSA-1024")
      return DL_Group(decode_seed(JCE_1024_SEED), 1024, JCE_1024_COUNTER);

   BigInt p, q, g;

   if(name == "IETF-768")  { g = 2; p = decode(IETF_768_PRIME); }
   if(name == "IETF-1024") { g = 2; p = decode(IETF_1024_PRIME); }
   if(name == "IETF-1536") { g = 2; p = decode(IETF_1536_PRIME); }
   if(name == "IETF-2048") { g = 2; p = decode(IETF_2048_PRIME); }
   if(name == "IETF-3072") { g = 2; p = decode(IETF_3072_PRIME); }
   if(name == "IETF-4096") { g = 2; p = decode(IETF_4096_PRIME); }

   if(p > 0 && g > 0 && !q)
      return DL_Group(p, g);
   if(p > 0 && g > 0 && q > 0)
      return DL_Group(p, q, g);

   throw Lookup_Error("DL group \"" + name + "\" not found");
   }

}
