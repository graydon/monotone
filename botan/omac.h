/*************************************************
* OMAC Header File                               *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#ifndef BOTAN_OMAC_H__
#define BOTAN_OMAC_H__

#include <botan/base.h>

namespace Botan {

/*************************************************
* OMAC                                           *
*************************************************/
class OMAC : public MessageAuthenticationCode
   {
   public:
      void clear() throw();
      std::string name() const;
      MessageAuthenticationCode* clone() const;
      OMAC(const std::string&);
      ~OMAC() { delete e; }
   private:
      void add_data(const byte[], u32bit);
      void final_result(byte[]);
      void key(const byte[], u32bit);

      BlockCipher* e;
      SecureVector<byte> buffer, state, B, P;
      u32bit position;
      byte polynomial;
   };

}

#endif
