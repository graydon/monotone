/*************************************************
* FIPS 186-2 RNG Header File                     *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#ifndef BOTAN_FIPS_186_RNG_H__
#define BOTAN_FIPS_186_RNG_H__

#include <botan/sha160.h>

namespace Botan {

/*************************************************
* FIPS 186-2 RNG                                 *
*************************************************/
class FIPS_186_RNG : public RandomNumberGenerator
   {
   public:
      void randomize(byte[], u32bit) throw(PRNG_Unseeded);
      bool is_seeded() const;
      void clear() throw();
      std::string name() const;

      FIPS_186_RNG();
      ~FIPS_186_RNG();
   private:
      void add_randomness(const byte[], u32bit) throw();
      void update_buffer() throw();
      void do_add(MemoryRegion<byte>&, const MemoryRegion<byte>&);
      SecureVector<byte> gen_xval();
      SecureVector<byte> do_hash(const MemoryRegion<byte>&);

      SHA_160 sha1;
      SecureVector<byte> xkey, buffer;
      RandomNumberGenerator* randpool;
      u32bit position;
   };

}

#endif
