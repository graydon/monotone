/*************************************************
* Randpool Header File                           *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#ifndef BOTAN_RANDPOOL_H__
#define BOTAN_RANDPOOL_H__

#include <botan/base.h>

namespace Botan {

/*************************************************
* Randpool                                       *
*************************************************/
class Randpool : public RandomNumberGenerator
   {
   public:
      void randomize(byte[], u32bit) throw(PRNG_Unseeded);
      bool is_seeded() const;
      void clear() throw();
      std::string name() const;
      Randpool();
      ~Randpool();
   private:
      void add_randomness(const byte[], u32bit) throw();
      void generate(u64bit) throw();
      void mix_pool() throw();
      const u32bit ITERATIONS_BEFORE_RESEED, POOL_BLOCKS;
      BlockCipher* cipher;
      HashFunction* hash;
      SecureVector<byte> pool, output;
      u32bit counter;
   };

}

#endif
