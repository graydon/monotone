/*************************************************
* Default Engine Algorithms Source File          *
* (C) 1999-2007 The Botan Project                *
*************************************************/

#include <botan/eng_def.h>
#include <botan/lookup.h>
#include <botan/parsing.h>

#include <botan/aes.h>
#include <botan/des.h>

#include <botan/arc4.h>

#include <botan/crc32.h>
#include <botan/sha160.h>
#include <botan/sha256.h>

#include <botan/cbc_mac.h>
#include <botan/hmac.h>

#include <botan/mode_pad.h>
#include <botan/pkcs5.h>

namespace Botan {

/*************************************************
* Some macros to simplify control flow           *
*************************************************/
#define HANDLE_TYPE_NO_ARGS(NAME, TYPE)        \
   if(algo_name == NAME)                       \
      {                                        \
      if(name.size() == 1)                     \
         return new TYPE;                      \
      throw Invalid_Algorithm_Name(algo_spec); \
      }

#define HANDLE_TYPE_ONE_U32BIT(NAME, TYPE, DEFAULT) \
   if(algo_name == NAME)                            \
      {                                             \
      if(name.size() == 1)                          \
         return new TYPE(DEFAULT);                  \
      if(name.size() == 2)                          \
         return new TYPE(to_u32bit(name[1]));       \
      throw Invalid_Algorithm_Name(algo_spec);      \
      }

#define HANDLE_TYPE_TWO_U32BIT(NAME, TYPE, DEFAULT)               \
   if(algo_name == NAME)                                          \
      {                                                           \
      if(name.size() == 1)                                        \
         return new TYPE(DEFAULT);                                \
      if(name.size() == 2)                                        \
         return new TYPE(to_u32bit(name[1]));                     \
      if(name.size() == 3)                                        \
         return new TYPE(to_u32bit(name[1]), to_u32bit(name[2])); \
      throw Invalid_Algorithm_Name(algo_spec);                    \
      }

#define HANDLE_TYPE_ONE_STRING(NAME, TYPE)     \
   if(algo_name == NAME)                       \
      {                                        \
      if(name.size() == 2)                     \
         return new TYPE(name[1]);             \
      throw Invalid_Algorithm_Name(algo_spec); \
      }

/*************************************************
* Look for an algorithm with this name           *
*************************************************/
BlockCipher*
Default_Engine::find_block_cipher(const std::string& algo_spec) const
   {
   std::vector<std::string> name = parse_algorithm_name(algo_spec);
   if(name.empty())
      return 0;
   const std::string algo_name = deref_alias(name[0]);

   HANDLE_TYPE_NO_ARGS("AES", AES);
   HANDLE_TYPE_NO_ARGS("AES-128", AES_128);
   HANDLE_TYPE_NO_ARGS("AES-192", AES_192);
   HANDLE_TYPE_NO_ARGS("AES-256", AES_256);
   HANDLE_TYPE_NO_ARGS("DES", DES);
   HANDLE_TYPE_NO_ARGS("DESX", DESX);
   HANDLE_TYPE_NO_ARGS("TripleDES", TripleDES);

   return 0;
   }

/*************************************************
* Look for an algorithm with this name           *
*************************************************/
StreamCipher*
Default_Engine::find_stream_cipher(const std::string& algo_spec) const
   {
   std::vector<std::string> name = parse_algorithm_name(algo_spec);
   if(name.empty())
      return 0;
   const std::string algo_name = deref_alias(name[0]);

   HANDLE_TYPE_ONE_U32BIT("ARC4", ARC4, 0);
   HANDLE_TYPE_ONE_U32BIT("RC4_drop", ARC4, 768);

   return 0;
   }

/*************************************************
* Look for an algorithm with this name           *
*************************************************/
HashFunction*
Default_Engine::find_hash(const std::string& algo_spec) const
   {
   std::vector<std::string> name = parse_algorithm_name(algo_spec);
   if(name.empty())
      return 0;
   const std::string algo_name = deref_alias(name[0]);

   HANDLE_TYPE_NO_ARGS("CRC32", CRC32);
   HANDLE_TYPE_NO_ARGS("SHA-160", SHA_160);
   HANDLE_TYPE_NO_ARGS("SHA-256", SHA_256);
   return 0;
   }

/*************************************************
* Look for an algorithm with this name           *
*************************************************/
MessageAuthenticationCode*
Default_Engine::find_mac(const std::string& algo_spec) const
   {
   std::vector<std::string> name = parse_algorithm_name(algo_spec);
   if(name.empty())
      return 0;
   const std::string algo_name = deref_alias(name[0]);

   HANDLE_TYPE_ONE_STRING("CBC-MAC", CBC_MAC);
   HANDLE_TYPE_ONE_STRING("HMAC", HMAC);

   return 0;
   }

/*************************************************
* Look for an algorithm with this name           *
*************************************************/
S2K* Default_Engine::find_s2k(const std::string& algo_spec) const
   {
   std::vector<std::string> name = parse_algorithm_name(algo_spec);
   if(name.empty())
      return 0;

   const std::string algo_name = deref_alias(name[0]);

   HANDLE_TYPE_ONE_STRING("PBKDF1", PKCS5_PBKDF1);
   HANDLE_TYPE_ONE_STRING("PBKDF2", PKCS5_PBKDF2);

   return 0;
   }

/*************************************************
* Look for an algorithm with this name           *
*************************************************/
BlockCipherModePaddingMethod*
Default_Engine::find_bc_pad(const std::string& algo_spec) const
   {
   std::vector<std::string> name = parse_algorithm_name(algo_spec);
   if(name.empty())
      return 0;

   const std::string algo_name = deref_alias(name[0]);

   HANDLE_TYPE_NO_ARGS("PKCS7",       PKCS7_Padding);
   HANDLE_TYPE_NO_ARGS("OneAndZeros", OneAndZeros_Padding);
   HANDLE_TYPE_NO_ARGS("X9.23",       ANSI_X923_Padding);
   HANDLE_TYPE_NO_ARGS("NoPadding",   Null_Padding);

   return 0;
   }

}
