/*************************************************
* OID Registry Source File                       *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#include <botan/oids.h>
#include <botan/exceptn.h>
#include <botan/mutex.h>
#include <botan/init.h>
#include <map>

namespace Botan {

namespace {

/*************************************************
* OID<->String Mappings                          *
*************************************************/
class OID_Mapper
   {
   public:
      void add_oid(const OID&, const std::string&);

      bool have_oid(const std::string&);
      std::string lookup(const OID&);
      OID lookup(const std::string&);

      OID_Mapper() { oid_mutex = get_mutex(); }
      ~OID_Mapper() { delete oid_mutex; }
   private:
      std::map<OID, std::string> oid_to_str;
      std::map<std::string, OID> str_to_oid;
      Mutex* oid_mutex;
   };

/*************************************************
* Register an OID to string mapping              *
*************************************************/
void OID_Mapper::add_oid(const OID& oid, const std::string& name)
   {
   Mutex_Holder lock(oid_mutex);

   if(oid_to_str.find(oid) == oid_to_str.end())
      oid_to_str[oid] = name;
   if(str_to_oid.find(name) == str_to_oid.end())
      str_to_oid[name] = oid;
   }

/*************************************************
* Do an OID to string lookup                     *
*************************************************/
std::string OID_Mapper::lookup(const OID& oid)
   {
   Mutex_Holder lock(oid_mutex);

   std::map<OID, std::string>::const_iterator info = oid_to_str.find(oid);
   if(info == oid_to_str.end())
      return oid.as_string();
   return info->second;
   }

/*************************************************
* Do a string to OID lookup                      *
*************************************************/
OID OID_Mapper::lookup(const std::string& name)
   {
   Mutex_Holder lock(oid_mutex);

   std::map<std::string, OID>::const_iterator info = str_to_oid.find(name);
   if(info == str_to_oid.end())
      throw Lookup_Error("No known OID for " + name);
   return info->second;
   }

/*************************************************
* Check to see if an OID exists in the table     *
*************************************************/
bool OID_Mapper::have_oid(const std::string& name)
   {
   Mutex_Holder lock(oid_mutex);

   return (str_to_oid.find(name) != str_to_oid.end());
   }

/*************************************************
* Global OID map                                 *
*************************************************/
OID_Mapper* mapping = 0;

}

namespace Init {

/*************************************************
* Startup the OID mapping system                 *
*************************************************/
void startup_oids()
   {
   mapping = new OID_Mapper;
   }

/*************************************************
* Shutdown the OID mapping system                *
*************************************************/
void shutdown_oids()
   {
   delete mapping;
   mapping = 0;
   }

}

namespace OIDS {

/*************************************************
* Register an OID to string mapping              *
*************************************************/
void add_oid(const OID& oid, const std::string& name)
   {
   if(!mapping)
      throw Internal_Error("OIDS::add_oid: Mapping not initialized");
   mapping->add_oid(oid, name);
   }

/*************************************************
* Do an OID to string lookup                     *
*************************************************/
std::string lookup(const OID& oid)
   {
   if(!mapping)
      throw Internal_Error("OIDS::lookup: Mapping not initialized");
   return mapping->lookup(oid);
   }

/*************************************************
* Do a string to OID lookup                      *
*************************************************/
OID lookup(const std::string& name)
   {
   if(!mapping)
      throw Internal_Error("OIDS::lookup: Mapping not initialized");
   return mapping->lookup(name);
   }

/*************************************************
* Check to see if an OID exists in the table     *
*************************************************/
bool have_oid(const std::string& name)
   {
   if(!mapping)
      throw Internal_Error("OIDS::lookup: Mapping not initialized");
   return mapping->have_oid(name);
   }

}

}
