/*************************************************
* Engine Source File                             *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#include <botan/engine.h>
#include <botan/def_eng.h>
#include <botan/init.h>
#include <botan/rng.h>

namespace Botan {

namespace {

std::vector<Engine*> engines;

}

namespace Init {

/*************************************************
* Initialize the list of Engines                 *
*************************************************/
void startup_engines()
   {
   engines.push_back(new Default_Engine);
   }

/*************************************************
* Delete the list of Engines                     *
*************************************************/
void shutdown_engines()
   {
   for(u32bit j = 0; j != engines.size(); j++)
      delete engines[j];
   engines.clear();
   }

}

namespace Engine_Core {

/*************************************************
* Add an Engine to the list                      *
*************************************************/
void add_engine(Engine* engine)
   {
   engines.insert(engines.end() - 1, engine);
   }

/*************************************************
* Acquire an IF op                               *
*************************************************/
IF_Operation* if_op(const BigInt& e, const BigInt& n, const BigInt& d,
                    const BigInt& p, const BigInt& q, const BigInt& d1,
                    const BigInt& d2, const BigInt& c)
   {
   for(u32bit j = 0; j != engines.size(); j++)
      {
      IF_Operation* op = engines[j]->if_op(e, n, d, p, q, d1, d2, c);
      if(op) return op;
      }
   throw Lookup_Error("Engine_Core::if_op: Unable to find a working engine");
   }

/*************************************************
* Acquire a DSA op                               *
*************************************************/
DSA_Operation* dsa_op(const DL_Group& group, const BigInt& y, const BigInt& x)
   {
   for(u32bit j = 0; j != engines.size(); j++)
      {
      DSA_Operation* op = engines[j]->dsa_op(group, y, x);
      if(op) return op;
      }
   throw Lookup_Error("Engine_Core::dsa_op: Unable to find a working engine");
   }

/*************************************************
* Acquire a NR op                                *
*************************************************/
NR_Operation* nr_op(const DL_Group& group, const BigInt& y, const BigInt& x)
   {
   for(u32bit j = 0; j != engines.size(); j++)
      {
      NR_Operation* op = engines[j]->nr_op(group, y, x);
      if(op) return op;
      }
   throw Lookup_Error("Engine_Core::nr_op: Unable to find a working engine");
   }

/*************************************************
* Acquire an ElGamal op                          *
*************************************************/
ELG_Operation* elg_op(const DL_Group& group, const BigInt& y, const BigInt& x)
   {
   for(u32bit j = 0; j != engines.size(); j++)
      {
      ELG_Operation* op = engines[j]->elg_op(group, y, x);
      if(op) return op;
      }
   throw Lookup_Error("Engine_Core::elg_op: Unable to find a working engine");
   }

/*************************************************
* Acquire a DH op                                *
*************************************************/
DH_Operation* dh_op(const DL_Group& group, const BigInt& x)
   {
   for(u32bit j = 0; j != engines.size(); j++)
      {
      DH_Operation* op = engines[j]->dh_op(group, x);
      if(op) return op;
      }
   throw Lookup_Error("Engine_Core::dh_op: Unable to find a working engine");
   }

}

/*************************************************
* Acquire a modular reducer                      *
*************************************************/
ModularReducer* get_reducer(const BigInt& n, bool convert_ok)
   {
   for(u32bit j = 0; j != engines.size(); j++)
      {
      ModularReducer* op = engines[j]->reducer(n, convert_ok);
      if(op) return op;
      }
   throw Lookup_Error("get_reducer: Unable to find a working engine");
   }

/*************************************************
* Acquire a block cipher                         *
*************************************************/
const BlockCipher* retrieve_block_cipher(const std::string& name)
   {
   for(u32bit j = 0; j != engines.size(); j++)
      {
      const BlockCipher* algo = engines[j]->block_cipher(name);
      if(algo) return algo;
      }
   return 0;
   }

/*************************************************
* Acquire a stream cipher                        *
*************************************************/
const StreamCipher* retrieve_stream_cipher(const std::string& name)
   {
   for(u32bit j = 0; j != engines.size(); j++)
      {
      const StreamCipher* algo = engines[j]->stream_cipher(name);
      if(algo) return algo;
      }
   return 0;
   }

/*************************************************
* Acquire a hash function                        *
*************************************************/
const HashFunction* retrieve_hash(const std::string& name)
   {
   for(u32bit j = 0; j != engines.size(); j++)
      {
      const HashFunction* algo = engines[j]->hash(name);
      if(algo) return algo;
      }
   return 0;
   }

/*************************************************
* Acquire an authentication code                 *
*************************************************/
const MessageAuthenticationCode* retrieve_mac(const std::string& name)
   {
   for(u32bit j = 0; j != engines.size(); j++)
      {
      const MessageAuthenticationCode* algo = engines[j]->mac(name);
      if(algo) return algo;
      }
   return 0;
   }

/*************************************************
* Add a new block cipher                         *
*************************************************/
void add_algorithm(BlockCipher* algo)
   {
   for(u32bit j = 0; j != engines.size(); j++)
      {
      Default_Engine* engine = dynamic_cast<Default_Engine*>(engines[j]);
      if(engine)
         {
         engine->add_algorithm(algo);
         return;
         }
      }
   throw Invalid_State("add_algorithm: Couldn't find the Default_Engine");
   }

/*************************************************
* Add a new stream cipher                        *
*************************************************/
void add_algorithm(StreamCipher* algo)
   {
   for(u32bit j = 0; j != engines.size(); j++)
      {
      Default_Engine* engine = dynamic_cast<Default_Engine*>(engines[j]);
      if(engine)
         {
         engine->add_algorithm(algo);
         return;
         }
      }
   throw Invalid_State("add_algorithm: Couldn't find the Default_Engine");
   }

/*************************************************
* Add a new hash function                        *
*************************************************/
void add_algorithm(HashFunction* algo)
   {
   for(u32bit j = 0; j != engines.size(); j++)
      {
      Default_Engine* engine = dynamic_cast<Default_Engine*>(engines[j]);
      if(engine)
         {
         engine->add_algorithm(algo);
         return;
         }
      }
   throw Invalid_State("add_algorithm: Couldn't find the Default_Engine");
   }

/*************************************************
* Add a new authentication code                  *
*************************************************/
void add_algorithm(MessageAuthenticationCode* algo)
   {
   for(u32bit j = 0; j != engines.size(); j++)
      {
      Default_Engine* engine = dynamic_cast<Default_Engine*>(engines[j]);
      if(engine)
         {
         engine->add_algorithm(algo);
         return;
         }
      }
   throw Invalid_State("add_algorithm: Couldn't find the Default_Engine");
   }

/*************************************************
* Get a cipher object                            *
*************************************************/
Keyed_Filter* get_cipher(const std::string& algo_spec, Cipher_Dir direction)
   {
   for(u32bit j = 0; j != engines.size(); j++)
      {
      Keyed_Filter* algo = engines[j]->get_cipher(algo_spec, direction);
      if(algo) return algo;
      }
   throw Algorithm_Not_Found(algo_spec);
   }

/*************************************************
* Get a cipher object                            *
*************************************************/
Keyed_Filter* get_cipher(const std::string& algo_spec, const SymmetricKey& key,
                         const InitializationVector& iv, Cipher_Dir direction)
   {
   Keyed_Filter* cipher = get_cipher(algo_spec, direction);
   cipher->set_key(key);
   cipher->set_iv(iv);
   return cipher;
   }

/*************************************************
* Get a cipher object                            *
*************************************************/
Keyed_Filter* get_cipher(const std::string& algo_spec, const SymmetricKey& key,
                         Cipher_Dir direction)
   {
   return get_cipher(algo_spec, key, InitializationVector(), direction);
   }

}
