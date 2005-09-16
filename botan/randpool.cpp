/*************************************************
* Randpool Source File                           *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#include <botan/randpool.h>
#include <botan/lookup.h>

namespace Botan {

/*************************************************
* Generate a buffer of random bytes              *
*************************************************/
void Randpool::randomize(byte out[], u32bit length) throw(PRNG_Unseeded)
   {
   if(!is_seeded())
      throw PRNG_Unseeded(name());

   generate(system_clock());
   while(length >= output.size())
      {
      xor_buf(out, output, output.size());
      length -= output.size();
      out += output.size();
      generate(system_clock());
      }
   xor_buf(out, output, length);
   }

/*************************************************
* Refill the output buffer                       *
*************************************************/
void Randpool::generate(u64bit input) throw()
   {
   for(u32bit j = 0; j != 4; j++)
      hash->update(get_byte(j, counter));
   for(u32bit j = 0; j != 8; j++)
      hash->update(get_byte(j, input));
   hash->update(pool);

   SecureVector<byte> poolhash = hash->final();

   for(u32bit j = 0; j != poolhash.size(); j++)
      output[j % output.size()] ^= poolhash[j];
   cipher->encrypt(output);

   if(counter % ITERATIONS_BEFORE_RESEED == 0)
      mix_pool();
   counter++;
   }

/*************************************************
* Mix the randomness pool                        *
*************************************************/
void Randpool::mix_pool() throw()
   {
   const u32bit BLOCK_SIZE = cipher->BLOCK_SIZE;

   cipher->set_key(output, output.size());

   xor_buf(pool, pool + BLOCK_SIZE*(POOL_BLOCKS-1), BLOCK_SIZE);
   cipher->encrypt(pool);
   for(u32bit j = 1; j != POOL_BLOCKS; j++)
      {
      const byte* previous_block = pool + BLOCK_SIZE*(j-1);
      byte* this_block = pool + BLOCK_SIZE*j;
      xor_buf(this_block, previous_block, BLOCK_SIZE);
      cipher->encrypt(this_block);
      }

   for(u32bit j = 0; j != output.size(); j++)
      output[j] ^= 0xFF;
   cipher->encrypt(output);
   }

/*************************************************
* Add entropy to the internal state              *
*************************************************/
void Randpool::add_randomness(const byte data[], u32bit length) throw()
   {
   update_entropy(data, length, pool.size());

   while(length)
      {
      u32bit added = std::min(pool.size() / 2, length);
      xor_buf(pool, data, added);
      generate(system_clock());
      mix_pool();
      length -= added;
      data += added;
      }
   generate(system_time());
   mix_pool();
   }

/*************************************************
* Check if the the pool is seeded                *
*************************************************/
bool Randpool::is_seeded() const
   {
   return (entropy >= 256);
   }

/*************************************************
* Clear memory of sensitive data                 *
*************************************************/
void Randpool::clear() throw()
   {
   cipher->clear();
   hash->clear();
   pool.clear();
   output.clear();
   entropy = counter = 0;
   }

/*************************************************
* Return the name of this type                   *
*************************************************/
std::string Randpool::name() const
   {
   return "Randpool";
   }

/*************************************************
* Randpool Constructor                           *
*************************************************/
Randpool::Randpool() : ITERATIONS_BEFORE_RESEED(8), POOL_BLOCKS(64)
   {
   cipher = get_block_cipher("AES-128");
   hash = get_hash("SHA-1");

   const u32bit BLOCK_SIZE = cipher->BLOCK_SIZE;
   output.create(BLOCK_SIZE);
   pool.create(POOL_BLOCKS * BLOCK_SIZE);
   entropy = counter = 0;

   if(hash->OUTPUT_LENGTH < BLOCK_SIZE || !cipher->valid_keylength(BLOCK_SIZE))
      throw Internal_Error("Randpool: Invalid algorithm combination " +
                           cipher->name() + "/" + hash->name());

   cipher->set_key(output, output.size());
   for(u32bit j = 0; j != ITERATIONS_BEFORE_RESEED + 1; j++)
      generate(system_clock());
   }

/*************************************************
* Randpool Destructor                            *
*************************************************/
Randpool::~Randpool()
   {
   delete cipher;
   delete hash;
   }

}
