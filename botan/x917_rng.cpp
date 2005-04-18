/*************************************************
* ANSI X9.17 RNG Source File                     *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#include <botan/x917_rng.h>
#include <botan/lookup.h>

namespace Botan {

/*************************************************
* Generate a buffer of random bytes              *
*************************************************/
void ANSI_X917_RNG::randomize(byte out[], u32bit length) throw(PRNG_Unseeded)
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
      iteration++;
      if(iteration == ITERATIONS_BEFORE_RESEED)
         reseed();
      }
   xor_buf(out, output, length);
   generate(system_clock());
   }

/*************************************************
* Refill the internal state                      *
*************************************************/
void ANSI_X917_RNG::generate(u64bit input) throw()
   {
   SecureVector<byte> buffer(cipher->BLOCK_SIZE);

   xor_buf(tstamp, (const byte*)&input, sizeof(u64bit));
   cipher->encrypt(tstamp);
   xor_buf(state, tstamp, cipher->BLOCK_SIZE);
   cipher->encrypt(state, buffer);
   xor_buf(state, buffer, tstamp, cipher->BLOCK_SIZE);
   cipher->encrypt(state);

   for(u32bit j = 0; j != buffer.size(); j++)
      output[j % output.size()] ^= buffer[j];
   }

/*************************************************
* Reseed the internal state                      *
*************************************************/
void ANSI_X917_RNG::reseed() throw()
   {
   SecureVector<byte> key(cipher->BLOCK_SIZE);

   generate(system_clock());
   for(u32bit j = 0; j != key.size(); j++)
      key[j] = state[j];
   cipher->encrypt(key);

   cipher->set_key(key, key.size());
   generate(system_clock());
   iteration = 0;
   }

/*************************************************
* Add entropy to internal state                  *
*************************************************/
void ANSI_X917_RNG::add_randomness(const byte data[], u32bit length) throw()
   {
   update_entropy(data, length, state.size());

   while(length)
      {
      u32bit added = std::min(state.size(), length);
      xor_buf(state, data, added);
      generate(system_clock());
      length -= added;
      data += added;
      }
   reseed();
   }

/*************************************************
* Check if the the PRNG is seeded                *
*************************************************/
bool ANSI_X917_RNG::is_seeded() const
   {
   return (entropy >= 96);
   }

/*************************************************
* Clear memory of sensitive data                 *
*************************************************/
void ANSI_X917_RNG::clear() throw()
   {
   cipher->clear();
   tstamp.clear();
   state.clear();
   output.clear();
   entropy = iteration = 0;
   }

/*************************************************
* Return the name of this type                   *
*************************************************/
std::string ANSI_X917_RNG::name() const
   {
   return "X9.17(" + cipher->name() + ")";
   }

/*************************************************
* ANSI X917 RNG Constructor                      *
*************************************************/
ANSI_X917_RNG::ANSI_X917_RNG() : ITERATIONS_BEFORE_RESEED(16)
   {
   cipher = get_block_cipher("AES");
   output.create(cipher->BLOCK_SIZE / 2);
   state.create(cipher->BLOCK_SIZE);
   tstamp.create(cipher->BLOCK_SIZE);
   entropy = iteration = 0;

   cipher->set_key(state, state.size());
   generate(system_clock());
   reseed();
   }

}
