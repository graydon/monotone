/*************************************************
* Allocator Factory Source File                  *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#include <botan/allocate.h>
#include <botan/defalloc.h>
#include <botan/mutex.h>
#include <botan/util.h>
#include <botan/init.h>
#include <map>

namespace Botan {

namespace {

/*************************************************
* A factory for creating Allocators              *
*************************************************/
class AllocatorFactory
   {
   public:
      Allocator* get(const std::string&) const;
      void add(const std::string&, Allocator*);
      std::string set_default_allocator(const std::string&);

      AllocatorFactory() { factory_lock = get_mutex(); }
      ~AllocatorFactory();
   private:
      std::map<std::string, Allocator*> alloc;
      std::string default_allocator;
      Mutex* factory_lock;
   };

/*************************************************
* Get an allocator from the factory              *
*************************************************/
Allocator* AllocatorFactory::get(const std::string& type) const
   {
   Mutex_Holder lock(factory_lock);

   std::map<std::string, Allocator*>::const_iterator iter;
   if(type == "default") iter = alloc.find(default_allocator);
   else                  iter = alloc.find(type);

   if(iter == alloc.end())
      return 0;
   return iter->second;
   }

/*************************************************
* Make a new type available to the factory       *
*************************************************/
void AllocatorFactory::add(const std::string& type, Allocator* allocator)
   {
   Mutex_Holder lock(factory_lock);
   allocator->init();
   alloc[type] = allocator;
   }

/*************************************************
* Set the default allocator type                 *
*************************************************/
std::string AllocatorFactory::set_default_allocator(const std::string& alloc)
   {
   Mutex_Holder lock(factory_lock);

   std::string old_default = default_allocator;
   default_allocator = alloc;
   return old_default;
   }

/*************************************************
* Destroy an allocator factory                   *
*************************************************/
AllocatorFactory::~AllocatorFactory()
   {
   std::map<std::string, Allocator*>::iterator iter;
   for(iter = alloc.begin(); iter != alloc.end(); iter++)
      {
      iter->second->destroy();
      delete iter->second;
      }
   delete factory_lock;
   }

/*************************************************
* Global State                                   *
*************************************************/
AllocatorFactory* factory = 0;

}

/*************************************************
* Get an allocator                               *
*************************************************/
Allocator* get_allocator(const std::string& type)
   {
   if(!factory)
      throw Invalid_State("LibraryInitializer not created, or it failed");

   Allocator* alloc = 0;

   if(type != "")
      {
      alloc = factory->get(type);
      if(alloc) return alloc;
      }

   alloc = factory->get("default");
   if(alloc) return alloc;

   alloc = factory->get("locking");
   if(alloc) return alloc;

   throw Exception("Couldn't find an allocator to use in get_allocator");
   }

/*************************************************
* Set the default allocator type                 *
*************************************************/
std::string set_default_allocator(const std::string& type)
   {
   return factory->set_default_allocator(type);
   }

/*************************************************
* Add new allocator type                         *
*************************************************/
bool add_allocator_type(const std::string& type, Allocator* alloc)
   {
   if(type == "" || factory->get(type))
      return false;
   factory->add(type, alloc);
   return true;
   }

namespace Init {

/*************************************************
* Initialize the memory subsystem                *
*************************************************/
void startup_memory_subsystem()
   {
   factory = new AllocatorFactory;

   add_allocator_type("malloc", new Malloc_Allocator);
   add_allocator_type("locking", new Locking_Allocator);
   }

/*************************************************
* Shut down the memory subsystem                 *
*************************************************/
void shutdown_memory_subsystem()
   {
   delete factory;
   factory = 0;
   }

}

}
