/*************************************************
* Algorithms List Source File                    *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#include <botan/lookup.h>
#include <botan/parsing.h>
#include <botan/mode_pad.h>
#include <botan/pkcs5.h>

namespace Botan {

namespace Algolist {

/*************************************************
* Attempt to get a string to key object          *
*************************************************/
S2K* get_s2k(const std::string& algo_spec)
   {
   std::vector<std::string> name = parse_algorithm_name(algo_spec);
   if(name.size() == 0)
      return 0;
   if(name.size() != 2)
      throw Invalid_Algorithm_Name(algo_spec);

   const std::string algo_name = deref_alias(name[0]);

   if(algo_name == "PBKDF1") return new PKCS5_PBKDF1(name[1]);
   if(algo_name == "PBKDF2") return new PKCS5_PBKDF2(name[1]);

   return 0;
   }

/*************************************************
* Attempt to get a block cipher padding method   *
*************************************************/
BlockCipherModePaddingMethod* get_bc_pad(const std::string& algo_spec)
   {
   std::vector<std::string> name = parse_algorithm_name(algo_spec);
   if(name.size() == 0)
      return 0;
   if(name.size() != 1)
      throw Invalid_Algorithm_Name(algo_spec);

   const std::string algo_name = deref_alias(name[0]);

   if(algo_name == "PKCS7") return new PKCS7_Padding;
   if(algo_name == "OneAndZeros") return new OneAndZeros_Padding;
   if(algo_name == "X9.23") return new  ANSI_X923_Padding;
   if(algo_name == "NoPadding") return new Null_Padding;

   return 0;
   }

}

}
