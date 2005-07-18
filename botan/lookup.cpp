/*************************************************
* Algorithm Lookup Table Source File             *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#include <botan/lookup.h>
#include <botan/look_add.h>
#include <botan/algolist.h>
#include <botan/mutex.h>
#include <map>

namespace Botan {

namespace {

/*************************************************
* Name to algorithm mapping tables               *
*************************************************/
std::map<std::string, S2K*> s2k_map;
std::map<std::string, BlockCipherModePaddingMethod*> bc_pad_map;

/*************************************************
* Alias to canonical name mapping table          *
*************************************************/
std::map<std::string, std::string> alias_map;

/*************************************************
* Mutexes controlling access to the tables       *
*************************************************/
Mutex* s2k_map_lock = 0;
Mutex* bc_pad_map_lock = 0;
Mutex* alias_map_lock = 0;

}

/*************************************************
* Retrieve a S2K algorithm                       *
*************************************************/
const S2K* retrieve_s2k(const std::string& name)
   {
   S2K* retval = 0;
   s2k_map_lock->lock();
   std::map<std::string, S2K*>::const_iterator algo;
   algo = s2k_map.find(deref_alias(name));
   if(algo != s2k_map.end())
      retval = algo->second;
   s2k_map_lock->unlock();
   if(!retval)
      {
      retval = Algolist::get_s2k(deref_alias(name));
      add_algorithm(retval);
      }
   return retval;
   }

/*************************************************
* Retrieve a block cipher padding method         *
*************************************************/
const BlockCipherModePaddingMethod* retrieve_bc_pad(const std::string& name)
   {
   BlockCipherModePaddingMethod* retval = 0;
   bc_pad_map_lock->lock();
   std::map<std::string, BlockCipherModePaddingMethod*>::const_iterator algo;
   algo = bc_pad_map.find(deref_alias(name));
   if(algo != bc_pad_map.end())
      retval = algo->second;
   bc_pad_map_lock->unlock();
   if(!retval)
      {
      retval = Algolist::get_bc_pad(deref_alias(name));
      add_algorithm(retval);
      }
   return retval;
   }

/*************************************************
* Add a S2K algorithm to the lookup table        *
*************************************************/
void add_algorithm(S2K* algo)
   {
   if(!algo) return;
   s2k_map_lock->lock();
   if(s2k_map.find(algo->name()) != s2k_map.end())
      delete s2k_map[algo->name()];
   s2k_map[algo->name()] = algo;
   s2k_map_lock->unlock();
   }

/*************************************************
* Add a padding method to the lookup table       *
*************************************************/
void add_algorithm(BlockCipherModePaddingMethod* algo)
   {
   if(!algo) return;
   bc_pad_map_lock->lock();
   if(bc_pad_map.find(algo->name()) != bc_pad_map.end())
      delete bc_pad_map[algo->name()];
   bc_pad_map[algo->name()] = algo;
   bc_pad_map_lock->unlock();
   }

/*************************************************
* Add an alias for an algorithm                  *
*************************************************/
void add_alias(const std::string& alias, const std::string& official_name)
   {
   if(alias == "" || official_name == "")
      return;

   Mutex_Holder lock(alias_map_lock);

   if(alias_map.find(alias) != alias_map.end())
      {
      if(deref_alias(alias_map[alias]) != deref_alias(official_name))
         throw Invalid_Argument("add_alias: The alias " + alias +
                                " already exists");
      return;
      }

   alias_map[alias] = official_name;
   }

/*************************************************
* Dereference an alias                           *
*************************************************/
std::string deref_alias(const std::string& name)
   {
   std::map<std::string, std::string>::const_iterator realname;
   realname = alias_map.find(name);
   if(realname == alias_map.end())
      return name;
   return deref_alias(realname->second);
   }

/*************************************************
* Handle startup for the lookup tables           *
*************************************************/
void init_lookup_tables()
   {
   s2k_map_lock = get_mutex();
   bc_pad_map_lock = get_mutex();
   alias_map_lock = get_mutex();
   }

/*************************************************
* Destroy the lookup tables                      *
*************************************************/
void destroy_lookup_tables()
   {
   std::map<std::string, S2K*>::iterator s2k_iter;
   for(s2k_iter = s2k_map.begin(); s2k_iter != s2k_map.end(); s2k_iter++)
      delete s2k_iter->second;

   std::map<std::string, BlockCipherModePaddingMethod*>::iterator pad_iter;
   for(pad_iter = bc_pad_map.begin(); pad_iter != bc_pad_map.end(); pad_iter++)
      delete pad_iter->second;

   s2k_map.clear();
   bc_pad_map.clear();
   alias_map.clear();

   delete s2k_map_lock;
   delete bc_pad_map_lock;
   delete alias_map_lock;

   s2k_map_lock = 0;
   bc_pad_map_lock = 0;
   alias_map_lock = 0;
   }

}
