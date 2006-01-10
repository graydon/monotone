/*************************************************
* Library Initialization Header File             *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#ifndef BOTAN_INIT_H__
#define BOTAN_INIT_H__

#include <botan/mutex.h>
#include <botan/timers.h>
#include <string>

namespace Botan {

namespace Init {

/*************************************************
* Main Library Initialization/Shutdown Functions *
*************************************************/
void initialize(const std::string& = "");
void deinitialize();

/*************************************************
* Internal Initialization/Shutdown Functions     *
*************************************************/
void set_mutex_type(Mutex*);
void set_timer_type(Timer*);

void startup_memory_subsystem();
void shutdown_memory_subsystem();

void startup_engines();
void shutdown_engines();

void startup_dl_cache();
void shutdown_dl_cache();

void startup_oids();
void shutdown_oids();

void startup_conf();
void shutdown_conf();
void set_default_options();

}

/*************************************************
* Library Initialization/Shutdown Object         *
*************************************************/
class LibraryInitializer
   {
   public:
      LibraryInitializer(const std::string& = "");
      ~LibraryInitializer();
   };

}

#endif
