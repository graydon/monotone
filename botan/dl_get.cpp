/*************************************************
* DL Group Lookup Source File                    *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#include <botan/dl_param.h>
#include <botan/mutex.h>
#include <botan/init.h>
#include <map>

namespace Botan {

namespace {

/*************************************************
* Name to group mapping table                    *
*************************************************/
std::map<std::string, DL_Group> dl_groups;
Mutex* dl_groups_lock = 0;

}

/*************************************************
* Try to obtain a particular DL group            *
*************************************************/
extern DL_Group try_to_get_dl_group(const std::string&);

/*************************************************
* Retrieve a DL group by name                    *
*************************************************/
const DL_Group& get_dl_group(const std::string& name)
   {
   initialize_mutex(dl_groups_lock);
   Mutex_Holder lock(dl_groups_lock);

   std::map<std::string, DL_Group>::const_iterator group;
   group = dl_groups.find(name);
   if(group != dl_groups.end())
      return group->second;

   dl_groups.insert(std::make_pair(name, try_to_get_dl_group(name)));

   group = dl_groups.find(name);
   if(group != dl_groups.end())
      return group->second;

   throw Lookup_Error("DL group \"" + name + "\" not found");
   }

/*************************************************
* Register a named DL group                      *
*************************************************/
void add_dl_group(const std::string& name, const DL_Group& group)
   {
   initialize_mutex(dl_groups_lock);
   Mutex_Holder lock(dl_groups_lock);
   dl_groups.insert(std::make_pair(name, group));
   }

namespace Init {

/*************************************************
* Destroy the table                              *
*************************************************/
void destroy_dl_groups()
   {
   dl_groups.clear();
   delete dl_groups_lock;
   dl_groups_lock = 0;
   }

}

}
