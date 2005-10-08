/*************************************************
* Global RNG Source File                         *
* (C) 1999-2005 The Botan Project                *
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
class RNG_State
   {
   public:
      void set_rngs(RandomNumberGenerator*, RandomNumberGenerator*);
      void add_es(EntropySource*, bool);
      void add_entropy(const byte[], u32bit);
      u32bit poll_es(EntropySource*, bool);

      u32bit seed(bool, u32bit);

      void randomize(byte[], u32bit, RNG_Quality);

      RNG_State();
      ~RNG_State();
   private:
      void seed_nonce_rng();
      RandomNumberGenerator* global_rng;
      RandomNumberGenerator* nonce_rng;
      Mutex* rng_mutex;
      Mutex* sources_mutex;
      std::vector<EntropySource*> sources;
   };

/*************************************************
* Create the RNG state                           *
*************************************************/
RNG_State::RNG_State()
   {
   global_rng = nonce_rng = 0;
   rng_mutex = get_mutex();
   sources_mutex = get_mutex();
   }

/*************************************************
* Destroy the RNG state                          *
*************************************************/
RNG_State::~RNG_State()
   {
   delete global_rng;
   delete nonce_rng;
   for(u32bit j = 0; j != sources.size(); j++)
      delete sources[j];

   delete rng_mutex;
   delete sources_mutex;
   }

/*************************************************
* Set the RNG algorithms                         *
*************************************************/
void RNG_State::set_rngs(RandomNumberGenerator* rng1,
                         RandomNumberGenerator* rng2)
   {
   if(rng1)
      {
      if(global_rng)
         delete global_rng;
      global_rng = rng1;
      }

   if(rng2)
      {
      if(nonce_rng)
         delete nonce_rng;
      nonce_rng = rng2;
      }
   }

/*************************************************
* Get entropy from the global RNG                *
*************************************************/
void RNG_State::randomize(byte output[], u32bit size, RNG_Quality level)
   {
   const std::string LTERM_CIPHER = "WiderWake4+1";

   Mutex_Holder lock(rng_mutex);

   if(!global_rng || !nonce_rng)
      throw Invalid_State("Global_RNG::randomize: The global RNG is unset");

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
* Add entropy to the RNG                         *
*************************************************/
void RNG_State::add_entropy(const byte buf[], u32bit length)
   {
   Mutex_Holder lock(rng_mutex);

   if(!global_rng || !nonce_rng)
      throw Invalid_State("Global_RNG::add_entropy: The global RNG is unset");

   global_rng->add_entropy(buf, length);
   seed_nonce_rng();
   }

/*************************************************
* Add an EntropySource to the list               *
*************************************************/
void RNG_State::add_es(EntropySource* src, bool last)
   {
   Mutex_Holder lock(sources_mutex);
   if(last)
      sources.push_back(src);
   else
      sources.insert(sources.begin(), src);
   }

/*************************************************
* Seed the nonce RNG                             *
*************************************************/
void RNG_State::seed_nonce_rng()
   {
   if(!global_rng->is_seeded())
      return;

   for(u32bit j = 0; j != 3; j++)
      {
      if(nonce_rng->is_seeded())
         break;

      SecureVector<byte> entropy(64);
      global_rng->randomize(entropy.begin(), entropy.size());
      nonce_rng->add_entropy(entropy.begin(), entropy.size());
      }
   }

/*************************************************
* Try to do a poll on an EntropySource           *
*************************************************/
u32bit RNG_State::poll_es(EntropySource* source, bool slow_poll)
   {
   SecureVector<byte> buffer(256);
   u32bit got = 0;

   if(slow_poll) got = source->slow_poll(buffer.begin(), buffer.size());
   else          got = source->fast_poll(buffer.begin(), buffer.size());

   add_entropy(buffer.begin(), got);
   return entropy_estimate(buffer.begin(), got);
   }

/*************************************************
* Attempt to seed the RNGs                       *
*************************************************/
u32bit RNG_State::seed(bool slow_poll, u32bit bits_to_get)
   {
   Mutex_Holder lock(sources_mutex);

   u32bit bits = 0;
   for(u32bit j = 0; j != sources.size(); j++)
      {
      bits += poll_es(sources[j], slow_poll);
      if(bits_to_get && bits >= bits_to_get)
         return bits;
      }
   return bits;
   }

/*************************************************
* The global RNG state                           *
*************************************************/
RNG_State* rng_state = 0;

}

namespace Global_RNG {

/*************************************************
* Get entropy from the global RNG                *
*************************************************/
void randomize(byte output[], u32bit size, RNG_Quality level)
   {
   if(!rng_state)
      throw Internal_Error("Global_RNG::randomize: RNG state never created");
   rng_state->randomize(output, size, level);
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
   if(!rng_state)
      throw Internal_Error("Global_RNG::add_entropy: RNG state never created");
   rng_state->add_entropy(entropy, size);
   }

/*************************************************
* Add entropy to the global RNG                  *
*************************************************/
void add_entropy(EntropySource& src, bool slow_poll)
   {
   if(!rng_state)
      throw Internal_Error("Global_RNG::poll_es: RNG state never created");
   rng_state->poll_es(&src, slow_poll);
   }

/*************************************************
* Add an EntropySource to the list               *
*************************************************/
void add_es(EntropySource* src, bool last)
   {
   if(!rng_state)
      throw Internal_Error("Global_RNG::add_es: RNG state never created");
   rng_state->add_es(src, last);
   }

/*************************************************
* Seed the global RNG                            *
*************************************************/
u32bit seed(bool slow_poll, u32bit bits_to_get)
   {
   if(!rng_state)
      throw Internal_Error("Global_RNG::seed: RNG state never created");
   return rng_state->seed(slow_poll, bits_to_get);
   }

}

namespace Init {

/*************************************************
* Initialize the RNG system                      *
*************************************************/
void init_rng_subsystem()
   {
   rng_state = new RNG_State;
   }

/*************************************************
* Deinitialize the RNG system                    *
*************************************************/
void shutdown_rng_subsystem()
   {
   delete rng_state;
   rng_state = 0;
   }

/*************************************************
* Setup the global RNG                           *
*************************************************/
void set_global_rngs(RandomNumberGenerator* rng1, RandomNumberGenerator* rng2)
   {
   if(!rng_state)
      throw Internal_Error("set_global_rngs: RNG state never created");
   rng_state->set_rngs(rng1, rng2);
   }

}

}
