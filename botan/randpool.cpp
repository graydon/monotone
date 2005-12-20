/*************************************************
* Randpool Source File                           *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#include <botan/randpool.h>
#include <botan/lookup.h>
#include <botan/bit_ops.h>
#include <botan/util.h>

namespace Botan {

/*************************************************
* Generate a buffer of random bytes              *
*************************************************/
void Randpool::randomize(byte out[], u32bit length) throw(PRNG_Unseeded)
   {
   if(!is_seeded())
      throw PRNG_Unseeded(name());

   update_buffer();
   while(length)
      {
      const u32bit copied = std::min(length, buffer.size());
      copy_mem(out, buffer.begin(), copied);
      out += copied;
      length -= copied;
      update_buffer();
      }
   }

/*************************************************
* Refill the output buffer                       *
*************************************************/
void Randpool::update_buffer()
   {
   const u64bit timestamp = system_clock();
   counter++;

   for(u32bit j = 0; j != 4; j++)
      mac->update(get_byte(j, counter));
   for(u32bit j = 0; j != 8; j++)
      mac->update(get_byte(j, timestamp));

   SecureVector<byte> mac_val = mac->final();

   for(u32bit j = 0; j != mac_val.size(); j++)
      buffer[j % buffer.size()] ^= mac_val[j];
   cipher->encrypt(buffer);

   if(counter % ITERATIONS_BEFORE_RESEED == 0)
      {
      mix_pool();
      update_buffer();
      }
   }

/*************************************************
* Mix the entropy pool                           *
*************************************************/
void Randpool::mix_pool()
   {
   const u32bit BLOCK_SIZE = cipher->BLOCK_SIZE;

   mac->set_key(mac->process(pool));
   cipher->set_key(mac->process(pool));

   xor_buf(pool, buffer, BLOCK_SIZE);
   cipher->encrypt(pool);
   for(u32bit j = 1; j != POOL_BLOCKS; j++)
      {
      const byte* previous_block = pool + BLOCK_SIZE*(j-1);
      byte* this_block = pool + BLOCK_SIZE*j;
      xor_buf(this_block, previous_block, BLOCK_SIZE);
      cipher->encrypt(this_block);
      }
   }

/*************************************************
* Add entropy to the internal state              *
*************************************************/
void Randpool::add_randomness(const byte data[], u32bit length)
   {
   u32bit this_entropy = entropy_estimate(data, length);
   entropy += std::min(this_entropy, 8*mac->OUTPUT_LENGTH);
   entropy = std::min(entropy, 8 * pool.size());

   SecureVector<byte> mac_val = mac->process(data, length);
   xor_buf(pool, mac_val, mac_val.size());
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
   mac->clear();
   pool.clear();
   buffer.clear();
   entropy = counter = 0;
   }

/*************************************************
* Return the name of this type                   *
*************************************************/
std::string Randpool::name() const
   {
   return "Randpool(" + cipher->name() + "," + mac->name() + ")";
   }

/*************************************************
* Randpool Constructor                           *
*************************************************/
Randpool::Randpool() : ITERATIONS_BEFORE_RESEED(8), POOL_BLOCKS(32)
   {
   const std::string CIPHER_NAME = "AES-256";
   const std::string MAC_NAME = "HMAC(SHA-256)";

   cipher = get_block_cipher(CIPHER_NAME);
   mac = get_mac(MAC_NAME);

   const u32bit BLOCK_SIZE = cipher->BLOCK_SIZE;
   const u32bit OUTPUT_LENGTH = mac->OUTPUT_LENGTH;

   if(OUTPUT_LENGTH < BLOCK_SIZE ||
      !cipher->valid_keylength(OUTPUT_LENGTH) ||
      !mac->valid_keylength(OUTPUT_LENGTH))
      {
      delete cipher;
      delete mac;
      throw Internal_Error("Randpool: Invalid algorithm combination " +
                           CIPHER_NAME + "/" + MAC_NAME);
      }

   buffer.create(BLOCK_SIZE);
   pool.create(POOL_BLOCKS * BLOCK_SIZE);
   entropy = counter = 0;

   mix_pool();
   }

/*************************************************
* Randpool Destructor                            *
*************************************************/
Randpool::~Randpool()
   {
   delete cipher;
   delete mac;
   entropy = counter = 0;
   }

}
