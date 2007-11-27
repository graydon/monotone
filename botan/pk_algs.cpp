/*************************************************
* PK Key Source File                             *
* (C) 1999-2007 The Botan Project                *
*************************************************/

#include <botan/pk_algs.h>
#include <botan/rsa.h>

namespace Botan {

/*************************************************
* Get an PK public key object                    *
*************************************************/
X509_PublicKey* get_public_key(const std::string& alg_name)
   {
   if(alg_name == "RSA")      return new RSA_PublicKey;
   else
      return 0;
   }

/*************************************************
* Get an PK private key object                   *
*************************************************/
PKCS8_PrivateKey* get_private_key(const std::string& alg_name)
   {
   if(alg_name == "RSA")      return new RSA_PrivateKey;
   else
      return 0;
   }

}
