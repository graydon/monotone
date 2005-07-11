/*************************************************
* Global RNG Source File                         *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#include <botan/rng.h>
#include <botan/mutex.h>
#include <botan/lookup.h>
#include <botan/init.h>
#include <memory>
#include <vector>

namespace Botan {

namespace {

/*************************************************
* Global RNG/EntropySource state                 *
*************************************************/
RandomNumberGenerator* global_rng = 0;
RandomNumberGenerator* nonce_rng = 0;
std::vector<EntropySource*> sources;
Mutex* global_rng_lock = 0;
Mutex* sources_lock = 0;

/*************************************************
* Try to do a poll on an EntropySource           *
*************************************************/
u32bit poll_es(EntropySource* source, bool slow_poll)
   {
   SecureVector<byte> buffer(256);
   u32bit got = 0;

   if(slow_poll) got = source->slow_poll(buffer.begin(), buffer.size());
   else          got = source->fast_poll(buffer.begin(), buffer.size());

   Global_RNG::add_entropy(buffer.begin(), got);
   return entropy_estimate(buffer.begin(), got);
   }

/*************************************************
* Seed the nonce RNG                             *
*************************************************/
void seed_nonce_rng()
   {
   if(!global_rng->is_seeded())
      return;

   while(!nonce_rng->is_seeded())
      {
      SecureVector<byte> entropy(64);
      global_rng->randomize(entropy.begin(), entropy.size());
      nonce_rng->add_entropy(entropy.begin(), entropy.size());
      }
   }

}

namespace Global_RNG {

/*************************************************
* Get entropy from the global RNG                *
*************************************************/
void randomize(byte output[], u32bit size, RNG_Quality level)
   {
   const std::string LTERM_CIPHER = "WiderWake4+1";

   if(!global_rng || !nonce_rng)
      throw Invalid_State("Global_RNG::randomize: The global RNG is unset");

   Mutex_Holder lock(global_rng_lock);

   if(level == Nonce)
      nonce_rng->randomize(output, size);
   else if(level == SessionKey)
      global_rng->randomize(output, size);
   else if(level == LongTermKey)
      {
      global_rng->randomize(output, size);
      if(have_stream_cipher(LTERM_CIPHER))
         {
         std::auto_ptr<StreamCipher> cipher(get_stream_cipher(LTERM_CIPHER));
         SecureVector<byte> key(cipher->MAXIMUM_KEYLENGTH);
         global_rng->randomize(key.begin(), key.size());
         cipher->set_key(key.begin(), key.size());
         cipher->encrypt(output, size);
         }
      }
   else
      throw Invalid_Argument("Global_RNG::randomize: Invalid RNG_Quality");
   }

/*************************************************
* Get entropy from the global RNG                *
*************************************************/
byte random(RNG_Quality level)
   {
   byte ret = 0;
   randomize(&ret, 1, level);
   return ret;
   }

/*************************************************
* Add entropy to the global RNG                  *
*************************************************/
void add_entropy(const byte entropy[], u32bit size)
   {
   if(!global_rng || !nonce_rng)
      throw Invalid_State("Global_RNG::add_entropy: The global RNG is unset");

   Mutex_Holder lock(global_rng_lock);
   global_rng->add_entropy(entropy, size);
   seed_nonce_rng();
   }

/*************************************************
* Add entropy to the global RNG                  *
*************************************************/
void add_entropy(EntropySource& src, bool slow_poll)
   {
   if(!global_rng || !nonce_rng)
      throw Invalid_State("Global_RNG::add_entropy: The global RNG is unset");

   Mutex_Holder lock(global_rng_lock);
   global_rng->add_entropy(src, slow_poll);
   seed_nonce_rng();
   }

/*************************************************
* Add an EntropySource to the list               *
*************************************************/
void add_es(EntropySource* src, bool last)
   {
   Mutex_Holder lock(sources_lock);
   if(last)
      sources.push_back(src);
   else
      sources.insert(sources.begin(), src);
   }

/*************************************************
* Seed the global RNG                            *
*************************************************/
u32bit seed(bool slow_poll, u32bit bits_to_get)
   {
   u32bit bits = 0;
   for(u32bit j = 0; j != sources.size(); j++)
      {
      bits += poll_es(sources[j], slow_poll);
      if(bits_to_get && bits >= bits_to_get)
         return bits;
      }
   return bits;
   }

}

namespace Init {

/*************************************************
* Initialize the RNG system                      *
*************************************************/
void init_rng_subsystem()
   {
   global_rng_lock = get_mutex();
   sources_lock = get_mutex();
   }

/*************************************************
* Deinitialize the RNG system                    *
*************************************************/
void shutdown_rng_subsystem()
   {
   if(sources_lock && sources.size())
      {
      Mutex_Holder lock(sources_lock);
      for(u32bit j = 0; j != sources.size(); j++)
         delete sources[j];
      sources.clear();
      }
   delete sources_lock;
   sources_lock = 0;
   delete global_rng_lock;
   global_rng_lock = 0;
   }

/*************************************************
* Setup the global RNG                           *
*************************************************/
void set_global_rngs(RandomNumberGenerator* rng1, RandomNumberGenerator* rng2)
   {
   if(global_rng)
      delete global_rng;
   if(nonce_rng)
      delete nonce_rng;

   global_rng = rng1;
   nonce_rng = rng2;
   }

}

}
