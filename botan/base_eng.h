/*************************************************
* Default Engine Header File                     *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#ifndef BOTAN_DEFAULT_ENGINE_H__
#define BOTAN_DEFAULT_ENGINE_H__

#include <botan/engine.h>
#include <map>

namespace Botan {

/*************************************************
* Default Engine                                 *
*************************************************/
class Default_Engine : public Engine
   {
   public:
      IF_Operation* if_op(const BigInt&, const BigInt&, const BigInt&,
                          const BigInt&, const BigInt&, const BigInt&,
                          const BigInt&, const BigInt&) const;
      DSA_Operation* dsa_op(const DL_Group&, const BigInt&,
                            const BigInt&) const;
      NR_Operation* nr_op(const DL_Group&, const BigInt&, const BigInt&) const;
      ELG_Operation* elg_op(const DL_Group&, const BigInt&,
                            const BigInt&) const;
      DH_Operation* dh_op(const DL_Group&, const BigInt&) const;
      ModularReducer* reducer(const BigInt&, bool) const;

      const BlockCipher* block_cipher(const std::string&) const;
      const StreamCipher* stream_cipher(const std::string&) const;
      const HashFunction* hash(const std::string&) const;
      const MessageAuthenticationCode* mac(const std::string&) const;

      Default_Engine();
      ~Default_Engine();
   private:
      void add_algorithm(BlockCipher*) const;
      void add_algorithm(StreamCipher*) const;
      void add_algorithm(HashFunction*) const;
      void add_algorithm(MessageAuthenticationCode*) const;

      mutable std::map<std::string, BlockCipher*> bc_map;
      mutable std::map<std::string, StreamCipher*> sc_map;
      mutable std::map<std::string, HashFunction*> hf_map;
      mutable std::map<std::string, MessageAuthenticationCode*> mac_map;

      Mutex* bc_map_lock;
      Mutex* sc_map_lock;
      Mutex* hf_map_lock;
      Mutex* mac_map_lock;
   };

}

#endif
