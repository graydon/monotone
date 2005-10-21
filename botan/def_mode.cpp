/*************************************************
* Default Engine Source File                     *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#include <botan/def_eng.h>
#include <botan/lookup.h>
#include <botan/filters.h>
#include <botan/ecb.h>
#include <botan/cbc.h>
#include <botan/cts.h>
#include <botan/cfb.h>
#include <botan/ofb.h>
#include <botan/ctr.h>
#include <botan/eax.h>
#include <botan/util.h>

namespace Botan {

namespace {

/*************************************************
* Simply return a mode object of choice          *
*************************************************/
Keyed_Filter* get_mode(Cipher_Dir direction, const std::string& cipher,
                       const std::string& mode, const std::string& pad = "",
                       u32bit bits = 0)
   {
   if(mode == "ECB")
      {
      if(direction == ENCRYPTION) return new ECB_Encryption(cipher, pad);
      else                        return new ECB_Decryption(cipher, pad);
      }
   else if(mode == "CFB")
      {
      if(direction == ENCRYPTION) return new CFB_Encryption(cipher, bits);
      else                        return new CFB_Decryption(cipher, bits);
      }
   else if(mode == "CBC")
      {
      if(pad == "CTS")
         {
         if(direction == ENCRYPTION) return new CTS_Encryption(cipher);
         else                        return new CTS_Decryption(cipher);
         }
      if(direction == ENCRYPTION) return new CBC_Encryption(cipher, pad);
      else                        return new CBC_Decryption(cipher, pad);
      }
   else if(mode == "EAX")
      {
      if(direction == ENCRYPTION) return new EAX_Encryption(cipher, bits);
      else                        return new EAX_Decryption(cipher, bits);
      }
   else
      throw Internal_Error("get_mode: " + cipher + "/" + mode + "/" + pad);
   }

}
/*************************************************
* Get a cipher object                            *
*************************************************/
Keyed_Filter* Default_Engine::get_cipher(const std::string& algo_spec,
                                         Cipher_Dir direction)
   {
   std::vector<std::string> algo_parts = split_on(algo_spec, '/');
   if(algo_parts.size() == 0)
      throw Invalid_Algorithm_Name(algo_spec);

   const std::string cipher = algo_parts[0];

   if(have_stream_cipher(cipher))
      {
      if(algo_parts.size() == 1)
         return new StreamCipher_Filter(cipher);
      return 0;
      }
   else if(have_block_cipher(cipher))
      {
      if(algo_parts.size() != 2 && algo_parts.size() != 3)
         return 0;

      std::string mode = algo_parts[1];
      u32bit bits = 0;

      if(mode.find("CFB") != std::string::npos ||
         mode.find("EAX") != std::string::npos)
         {
         std::vector<std::string> algo_info = parse_algorithm_name(mode);
         mode = algo_info[0];
         if(algo_info.size() == 1)
            bits = 8*block_size_of(cipher);
         else if(algo_info.size() == 2)
            bits = to_u32bit(algo_info[1]);
         else
            throw Invalid_Algorithm_Name(algo_spec);
         }

      std::string padding;
      if(algo_parts.size() == 3)
         padding = algo_parts[2];
      else
         padding = (mode == "CBC") ? "PKCS7" : "NoPadding";

      if(mode == "ECB" && padding == "CTS")
         return 0;
      else if((mode != "CBC" && mode != "ECB") && padding != "NoPadding")
         throw Invalid_Algorithm_Name(algo_spec);

      if(mode == "OFB")         return new OFB(cipher);
      else if(mode == "CTR-BE") return new CTR_BE(cipher);
      else if(mode == "ECB" || mode == "CBC" || mode == "CTS" ||
              mode == "CFB" || mode == "EAX")
         return get_mode(direction, cipher, mode, padding, bits);
      else
         return 0;
      }

   return 0;
   }

}
