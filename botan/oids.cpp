/*************************************************
* OID Registry Source File                       *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#include <botan/oids.h>
#include <botan/exceptn.h>
#include <botan/mutex.h>
#include <map>

namespace Botan {

namespace OIDS {

namespace {

std::map<OID, std::string> oid_to_str;
std::map<std::string, OID> str_to_oid;
Mutex* oid_mutex = 0;

}

/*************************************************
* Register an OID to string mapping              *
*************************************************/
void add_oid(const OID& oid, const std::string& name)
   {
   initialize_mutex(oid_mutex);
   Mutex_Holder lock(oid_mutex);

   if(oid_to_str.find(oid) == oid_to_str.end())
      oid_to_str[oid] = name;
   if(str_to_oid.find(name) == str_to_oid.end())
      str_to_oid[name] = oid;
   }

/*************************************************
* Do an OID to string lookup                     *
*************************************************/
std::string lookup(const OID& oid)
   {
   initialize_mutex(oid_mutex);
   Mutex_Holder lock(oid_mutex);

   std::map<OID, std::string>::const_iterator info = oid_to_str.find(oid);
   if(info == oid_to_str.end())
      return oid.as_string();
   return info->second;
   }

/*************************************************
* Do a string to OID lookup                      *
*************************************************/
OID lookup(const std::string& name)
   {
   initialize_mutex(oid_mutex);
   Mutex_Holder lock(oid_mutex);

   std::map<std::string, OID>::const_iterator info = str_to_oid.find(name);
   if(info == str_to_oid.end())
      throw Lookup_Error("No known OID for " + name);
   return info->second;
   }

/*************************************************
* Check to see if an OID exists in the table     *
*************************************************/
bool have_oid(const std::string& name)
   {
   initialize_mutex(oid_mutex);
   Mutex_Holder lock(oid_mutex);

   return (str_to_oid.find(name) != str_to_oid.end());
   }

}

}
