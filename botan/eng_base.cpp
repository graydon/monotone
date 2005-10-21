/*************************************************
* Basic No-Op Engine Source File                 *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#include <botan/engine.h>
#include <botan/lookup.h>

namespace Botan {

/*************************************************
* Basic No-Op Engine Implementation              *
*************************************************/
IF_Operation* Engine::if_op(const BigInt&, const BigInt&, const BigInt&,
                            const BigInt&, const BigInt&, const BigInt&,
                            const BigInt&, const BigInt&) const
   {
   return 0;
   }

/*************************************************
* Basic No-Op Engine Implementation              *
*************************************************/
DSA_Operation* Engine::dsa_op(const DL_Group&, const BigInt&,
                              const BigInt&) const
   {
   return 0;
   }

/*************************************************
* Basic No-Op Engine Implementation              *
*************************************************/
NR_Operation* Engine::nr_op(const DL_Group&, const BigInt&,
                            const BigInt&) const
   {
   return 0;
   }

/*************************************************
* Basic No-Op Engine Implementation              *
*************************************************/
ELG_Operation* Engine::elg_op(const DL_Group&, const BigInt&,
                              const BigInt&) const
   {
   return 0;
   }

/*************************************************
* Basic No-Op Engine Implementation              *
*************************************************/
DH_Operation* Engine::dh_op(const DL_Group&, const BigInt&) const
   {
   return 0;
   }

/*************************************************
* Basic No-Op Engine Implementation              *
*************************************************/
ModularReducer* Engine::reducer(const BigInt&, bool) const
   {
   return 0;
   }

/*************************************************
* Acquire a BlockCipher                          *
*************************************************/
const BlockCipher* Engine::block_cipher(const std::string& name) const
   {
   BlockCipher* retval = 0;
   bc_map_lock->lock();
   std::map<std::string, BlockCipher*>::const_iterator algo;
   algo = bc_map.find(deref_alias(name));
   if(algo != bc_map.end())
      retval = algo->second;
   bc_map_lock->unlock();
   if(!retval)
      {
      retval = find_block_cipher(deref_alias(name));
      add_algorithm(retval);
      }
   return retval;
   }

/*************************************************
* Acquire a StreamCipher                         *
*************************************************/
const StreamCipher* Engine::stream_cipher(const std::string& name) const
   {
   StreamCipher* retval = 0;
   sc_map_lock->lock();
   std::map<std::string, StreamCipher*>::const_iterator algo;
   algo = sc_map.find(deref_alias(name));
   if(algo != sc_map.end())
      retval = algo->second;
   sc_map_lock->unlock();
   if(!retval)
      {
      retval = find_stream_cipher(deref_alias(name));
      add_algorithm(retval);
      }
   return retval;
   }

/*************************************************
* Acquire a HashFunction                         *
*************************************************/
const HashFunction* Engine::hash(const std::string& name) const
   {
   HashFunction* retval = 0;
   hf_map_lock->lock();
   std::map<std::string, HashFunction*>::const_iterator algo;
   algo = hf_map.find(deref_alias(name));
   if(algo != hf_map.end())
      retval = algo->second;
   hf_map_lock->unlock();
   if(!retval)
      {
      retval = find_hash(deref_alias(name));
      add_algorithm(retval);
      }
   return retval;
   }

/*************************************************
* Acquire a MessageAuthenticationCode            *
*************************************************/
const MessageAuthenticationCode* Engine::mac(const std::string& name) const
   {
   MessageAuthenticationCode* retval = 0;
   mac_map_lock->lock();
   std::map<std::string, MessageAuthenticationCode*>::const_iterator algo;
   algo = mac_map.find(deref_alias(name));
   if(algo != mac_map.end())
      retval = algo->second;
   mac_map_lock->unlock();
   if(!retval)
      {
      retval = find_mac(deref_alias(name));
      add_algorithm(retval);
      }
   return retval;
   }

/*************************************************
* Add a block cipher to the lookup table         *
*************************************************/
void Engine::add_algorithm(BlockCipher* algo) const
   {
   if(!algo) return;
   bc_map_lock->lock();
   if(bc_map.find(algo->name()) != bc_map.end())
      delete bc_map[algo->name()];
   bc_map[algo->name()] = algo;
   bc_map_lock->unlock();
   }

/*************************************************
* Add a stream cipher to the lookup table        *
*************************************************/
void Engine::add_algorithm(StreamCipher* algo) const
   {
   if(!algo) return;
   sc_map_lock->lock();
   if(sc_map.find(algo->name()) != sc_map.end())
      delete sc_map[algo->name()];
   sc_map[algo->name()] = algo;
   sc_map_lock->unlock();
   }

/*************************************************
* Add a hash function to the lookup table        *
*************************************************/
void Engine::add_algorithm(HashFunction* algo) const
   {
   if(!algo) return;
   hf_map_lock->lock();
   if(hf_map.find(algo->name()) != hf_map.end())
      delete hf_map[algo->name()];
   hf_map[algo->name()] = algo;
   hf_map_lock->unlock();
   }

/*************************************************
* Add a MAC to the lookup table                  *
*************************************************/
void Engine::add_algorithm(MessageAuthenticationCode* algo) const
   {
   if(!algo) return;
   mac_map_lock->lock();
   if(mac_map.find(algo->name()) != mac_map.end())
      delete mac_map[algo->name()];
   mac_map[algo->name()] = algo;
   mac_map_lock->unlock();
   }

/*************************************************
* Create an Engine                               *
*************************************************/
Engine::Engine()
   {
   bc_map_lock = get_mutex();
   sc_map_lock = get_mutex();
   hf_map_lock = get_mutex();
   mac_map_lock = get_mutex();
   }

/*************************************************
* Destroy an Engine                              *
*************************************************/
Engine::~Engine()
   {
   std::map<std::string, BlockCipher*>::iterator bc_iter;
   for(bc_iter = bc_map.begin(); bc_iter != bc_map.end(); bc_iter++)
      delete bc_iter->second;

   std::map<std::string, StreamCipher*>::iterator sc_iter;
   for(sc_iter = sc_map.begin(); sc_iter != sc_map.end(); sc_iter++)
      delete sc_iter->second;

   std::map<std::string, HashFunction*>::iterator hf_iter;
   for(hf_iter = hf_map.begin(); hf_iter != hf_map.end(); hf_iter++)
      delete hf_iter->second;

   std::map<std::string, MessageAuthenticationCode*>::iterator mac_iter;
   for(mac_iter = mac_map.begin(); mac_iter != mac_map.end(); mac_iter++)
      delete mac_iter->second;

   delete bc_map_lock;
   delete sc_map_lock;
   delete hf_map_lock;
   delete mac_map_lock;
   }

/*************************************************
* Basic No-Op Engine Implementation              *
*************************************************/
BlockCipher* Engine::find_block_cipher(const std::string&) const
   {
   return 0;
   }

/*************************************************
* Basic No-Op Engine Implementation              *
*************************************************/
StreamCipher* Engine::find_stream_cipher(const std::string&) const
   {
   return 0;
   }

/*************************************************
* Basic No-Op Engine Implementation              *
*************************************************/
HashFunction* Engine::find_hash(const std::string&) const
   {
   return 0;
   }

/*************************************************
* Basic No-Op Engine Implementation              *
*************************************************/
MessageAuthenticationCode* Engine::find_mac(const std::string&) const
   {
   return 0;
   }

/*************************************************
* Basic No-Op Engine Implementation              *
*************************************************/
Keyed_Filter* Engine::get_cipher(const std::string&, Cipher_Dir)
   {
   return 0;
   }

}
