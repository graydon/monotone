/*************************************************
* DL Group Cache Source File                     *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#include <botan/dl_param.h>
#include <botan/mutex.h>
#include <botan/init.h>
#include <map>

namespace Botan {

/*************************************************
* Try to obtain a particular DL group            *
*************************************************/
extern DL_Group try_to_get_dl_group(const std::string&);

namespace {

/*************************************************
* Cache for DL_Group objects                     *
*************************************************/
class DL_Group_Cache
   {
   public:
      const DL_Group& get(const std::string&);
      void add(const std::string&, const DL_Group&);

      DL_Group_Cache() { groups_mutex = get_mutex(); }
      ~DL_Group_Cache() { groups.clear(); delete groups_mutex; }
   private:
      std::map<std::string, DL_Group> groups;
      Mutex* groups_mutex;
   };

/*************************************************
* Get a DL_Group                                 *
*************************************************/
const DL_Group& DL_Group_Cache::get(const std::string& name)
   {
   Mutex_Holder lock(groups_mutex);

   std::map<std::string, DL_Group>::const_iterator group;
   group = groups.find(name);
   if(group != groups.end())
      return group->second;

   groups.insert(std::make_pair(name, try_to_get_dl_group(name)));

   group = groups.find(name);
   if(group != groups.end())
      return group->second;

   throw Lookup_Error("DL group \"" + name + "\" not found");
   }

/*************************************************
* Add a new DL_Group                             *
*************************************************/
void DL_Group_Cache::add(const std::string& name, const DL_Group& group)
   {
   Mutex_Holder lock(groups_mutex);
   groups.insert(std::make_pair(name, group));
   }

/*************************************************
* Global state for DL_Group cache                *
*************************************************/
DL_Group_Cache* dl_groups = 0;

}

/*************************************************
* Retrieve a DL group by name                    *
*************************************************/
const DL_Group& get_dl_group(const std::string& name)
   {
   return dl_groups->get(name);
   }

/*************************************************
* Register a named DL group                      *
*************************************************/
void add_dl_group(const std::string& name, const DL_Group& group)
   {
   dl_groups->add(name, group);
   }

namespace Init {

/*************************************************
* Create the cache                               *
*************************************************/
void startup_dl_cache()
   {
   dl_groups = new DL_Group_Cache;
   }

/*************************************************
* Destroy the cache                              *
*************************************************/
void shutdown_dl_cache()
   {
   delete dl_groups;
   dl_groups = 0;
   }

}

}
