/*************************************************
* ANSI X9.17 RNG Header File                     *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#ifndef BOTAN_ANSI_X917_RNG_H__
#define BOTAN_ANSI_X917_RNG_H__

#include <botan/base.h>

namespace Botan {

/*************************************************
* ANSI X9.17 RNG                                 *
*************************************************/
class ANSI_X917_RNG : public RandomNumberGenerator
   {
   public:
      void randomize(byte[], u32bit) throw(PRNG_Unseeded);
      bool is_seeded() const;
      void clear() throw();
      std::string name() const;
      ANSI_X917_RNG();
      ~ANSI_X917_RNG() { delete cipher; }
   private:
      void add_randomness(const byte[], u32bit) throw();
      void generate(u64bit) throw();
      void reseed() throw();
      const u32bit ITERATIONS_BEFORE_RESEED;
      BlockCipher* cipher;
      SecureVector<byte> output, state, tstamp;
      u32bit iteration;
   };

}

#endif
