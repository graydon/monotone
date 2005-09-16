/*************************************************
* Engine Header File                             *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#ifndef BOTAN_ENGINE_H__
#define BOTAN_ENGINE_H__

#include <botan/base.h>
#include <botan/pk_ops.h>
#include <botan/reducer.h>
#include <botan/basefilt.h>
#include <botan/mutex.h>
#include <map>

namespace Botan {

/*************************************************
* Engine Base Class                              *
*************************************************/
class Engine
   {
   public:
      virtual IF_Operation* if_op(const BigInt&, const BigInt&, const BigInt&,
                                  const BigInt&, const BigInt&, const BigInt&,
                                  const BigInt&, const BigInt&) const;
      virtual DSA_Operation* dsa_op(const DL_Group&, const BigInt&,
                                    const BigInt&) const;
      virtual NR_Operation* nr_op(const DL_Group&, const BigInt&,
                                  const BigInt&) const;
      virtual ELG_Operation* elg_op(const DL_Group&, const BigInt&,
                                    const BigInt&) const;
      virtual DH_Operation* dh_op(const DL_Group&, const BigInt&) const;
      virtual ModularReducer* reducer(const BigInt&, bool) const;

      const BlockCipher* block_cipher(const std::string&) const;
      const StreamCipher* stream_cipher(const std::string&) const;
      const HashFunction* hash(const std::string&) const;
      const MessageAuthenticationCode* mac(const std::string&) const;

      virtual Keyed_Filter* get_cipher(const std::string&, Cipher_Dir);

      void add_algorithm(BlockCipher*) const;
      void add_algorithm(StreamCipher*) const;
      void add_algorithm(HashFunction*) const;
      void add_algorithm(MessageAuthenticationCode*) const;

      Engine();
      virtual ~Engine();
   private:
      virtual BlockCipher* find_block_cipher(const std::string&) const;
      virtual StreamCipher* find_stream_cipher(const std::string&) const;
      virtual HashFunction* find_hash(const std::string&) const;
      virtual MessageAuthenticationCode* find_mac(const std::string&) const;

      mutable std::map<std::string, BlockCipher*> bc_map;
      mutable std::map<std::string, StreamCipher*> sc_map;
      mutable std::map<std::string, HashFunction*> hf_map;
      mutable std::map<std::string, MessageAuthenticationCode*> mac_map;

      Mutex* bc_map_lock;
      Mutex* sc_map_lock;
      Mutex* hf_map_lock;
      Mutex* mac_map_lock;
   };

namespace Engine_Core {

/*************************************************
* Engine Management                              *
*************************************************/
void add_engine(Engine*);

/*************************************************
* Get an operation from an Engine                *
*************************************************/
IF_Operation* if_op(const BigInt&, const BigInt&, const BigInt&,
                    const BigInt&, const BigInt&, const BigInt&,
                    const BigInt&, const BigInt&);

DSA_Operation* dsa_op(const DL_Group&, const BigInt&, const BigInt&);
NR_Operation* nr_op(const DL_Group&, const BigInt&, const BigInt&);

ELG_Operation* elg_op(const DL_Group&, const BigInt&, const BigInt&);

DH_Operation* dh_op(const DL_Group&, const BigInt&);

}

}

#endif
