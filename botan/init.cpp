/*************************************************
* Initialization Function Source File            *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#include <botan/init.h>

#include <botan/allocate.h>
#include <botan/look_add.h>
#include <botan/mutex.h>
#include <botan/rng.h>
#include <botan/randpool.h>
#include <botan/x917_rng.h>
#include <botan/fips_rng.h>
#include <botan/fips140.h>
#include <botan/es_file.h>
#include <botan/conf.h>
#include <map>

#if defined(BOTAN_EXT_MUTEX_PTHREAD)
  #include <botan/mux_pthr.h>
#elif defined(BOTAN_EXT_MUTEX_WIN32)
  #include <botan/mux_win.h>
#elif defined(BOTAN_EXT_MUTEX_QT)
  #include <botan/mux_qt.h>
#endif

#if defined(BOTAN_EXT_ALLOC_MMAP)
  #include <botan/mmap_mem.h>
#endif

#if defined(BOTAN_EXT_TIMER_HARDWARE)
  #include <botan/tm_hard.h>
#elif defined(BOTAN_EXT_TIMER_POSIX)
  #include <botan/tm_posix.h>
#elif defined(BOTAN_EXT_TIMER_UNIX)
  #include <botan/tm_unix.h>
#elif defined(BOTAN_EXT_TIMER_WIN32)
  #include <botan/tm_win32.h>
#endif

#if defined(BOTAN_EXT_ENGINE_AEP)
  #include <botan/eng_aep.h>
#endif

#if defined(BOTAN_EXT_ENGINE_GNU_MP)
  #include <botan/eng_gmp.h>
#endif

#if defined(BOTAN_EXT_ENGINE_OPENSSL)
  #include <botan/eng_ossl.h>
#endif

#if defined(BOTAN_EXT_ENTROPY_SRC_AEP)
  #include <botan/es_aep.h>
#endif

#if defined(BOTAN_EXT_ENTROPY_SRC_EGD)
  #include <botan/es_egd.h>
#endif

#if defined(BOTAN_EXT_ENTROPY_SRC_UNIX)
  #include <botan/es_unix.h>
#endif

#if defined(BOTAN_EXT_ENTROPY_SRC_BEOS)
  #include <botan/es_beos.h>
#endif

#if defined(BOTAN_EXT_ENTROPY_SRC_CAPI)
  #include <botan/es_capi.h>
#endif

#if defined(BOTAN_EXT_ENTROPY_SRC_WIN32)
  #include <botan/es_win32.h>
#endif

#if defined(BOTAN_EXT_ENTROPY_SRC_FTW)
  #include <botan/es_ftw.h>
#endif

namespace Botan {

/*************************************************
* Library Initialization                         *
*************************************************/
LibraryInitializer::LibraryInitializer(const std::string& arg_string)
   {
   Init::initialize(arg_string);
   }

/*************************************************
* Library Shutdown                               *
*************************************************/
LibraryInitializer::~LibraryInitializer()
   {
   Init::deinitialize();
   }

namespace Init {

namespace {

/*************************************************
* Register a mutex type, if possible             *
*************************************************/
void set_mutex()
   {
#if defined(BOTAN_EXT_MUTEX_PTHREAD)
   set_mutex_type(new Pthread_Mutex);
#elif defined(BOTAN_EXT_MUTEX_WIN32)
   set_mutex_type(new Win32_Mutex);
#elif defined(BOTAN_EXT_MUTEX_QT)
   set_mutex_type(new Qt_Mutex);
#else
   throw Exception("LibraryInitializer: thread safety impossible");
#endif
   }

/*************************************************
* Register a high resolution timer, if possible  *
*************************************************/
void set_timer()
   {
#if defined(BOTAN_EXT_TIMER_HARDWARE)
   set_timer_type(new Hardware_Timer);
#elif defined(BOTAN_EXT_TIMER_POSIX)
   set_timer_type(new POSIX_Timer);
#elif defined(BOTAN_EXT_TIMER_UNIX)
   set_timer_type(new Unix_Timer);
#elif defined(BOTAN_EXT_TIMER_WIN32)
   set_timer_type(new Win32_Timer);
#endif
   }

/*************************************************
* Register any usable entropy sources            *
*************************************************/
void add_entropy_sources()
   {
   Global_RNG::add_es(new File_EntropySource);

#if defined(BOTAN_EXT_ENTROPY_SRC_AEP)
   Global_RNG::add_es(new AEP_EntropySource);
#endif

#if defined(BOTAN_EXT_ENTROPY_SRC_EGD)
   Global_RNG::add_es(new EGD_EntropySource);
#endif

#if defined(BOTAN_EXT_ENTROPY_SRC_CAPI)
   Global_RNG::add_es(new Win32_CAPI_EntropySource);
#endif

#if defined(BOTAN_EXT_ENTROPY_SRC_WIN32)
   Global_RNG::add_es(new Win32_EntropySource);
#endif

#if defined(BOTAN_EXT_ENTROPY_SRC_UNIX)
   Global_RNG::add_es(new Unix_EntropySource);
#endif

#if defined(BOTAN_EXT_ENTROPY_SRC_BEOS)
   Global_RNG::add_es(new BeOS_EntropySource);
#endif

#if defined(BOTAN_EXT_ENTROPY_SRC_FTW)
   Global_RNG::add_es(new FTW_EntropySource);
#endif
   }

/*************************************************
* Register a more secure allocator, if possible  *
*************************************************/
void set_safe_allocator()
   {
#if defined(BOTAN_EXT_ALLOC_MMAP)
   add_allocator_type("mmap", new MemoryMapping_Allocator);
   set_default_allocator("mmap");
#endif
   }

/*************************************************
* Register any usable engines                    *
*************************************************/
void set_engines()
   {
#if defined(BOTAN_EXT_ENGINE_AEP)
   Botan::Engine_Core::add_engine(new Botan::AEP_Engine);
#endif

#if defined(BOTAN_EXT_ENGINE_GNU_MP)
   Botan::Engine_Core::add_engine(new Botan::GMP_Engine);
#endif

#if defined(BOTAN_EXT_ENGINE_OPENSSL)
   Botan::Engine_Core::add_engine(new Botan::OpenSSL_Engine);
#endif
   }

/*************************************************
* Parse the options string                       *
*************************************************/
std::map<std::string, std::string> parse_args(const std::string& arg_string)
   {
   std::map<std::string, std::string> arg_map;
   std::vector<std::string> args = split_on(arg_string, ' ');
   for(u32bit j = 0; j != args.size(); j++)
      {
      if(args[j].find('=') == std::string::npos)
         arg_map[args[j]] = "";
      else
         {
         std::vector<std::string> name_and_value = split_on(args[j], '=');
         arg_map[name_and_value[0]] = name_and_value[1];
         }
      }

   return arg_map;
   }

/*************************************************
* Check if an option is set in the argument      *
*************************************************/
bool arg_set(const std::map<std::string, std::string>& args,
             const std::string& option)
   {
   return (args.find(option) != args.end());
   }

}

/*************************************************
* Library Initialization                         *
*************************************************/
void initialize(const std::string& arg_string)
   {
   std::map<std::string, std::string> args = parse_args(arg_string);

   if(arg_set(args, "thread_safe"))
      set_mutex();

   startup_conf();
   startup_oids();
   set_default_options();
   startup_memory_subsystem();

   init_lookup_tables();

   if(arg_set(args, "secure_memory"))
      set_safe_allocator();
   set_timer();

   if(!arg_set(args, "no_aliases")) add_default_aliases();
   if(!arg_set(args, "no_oids"))    add_default_oids();
   if(arg_set(args, "config") && args["config"] != "")
      Config::load(args["config"]);

   startup_engines();
   if(arg_set(args, "use_engines"))
      set_engines();
   init_rng_subsystem();

   if(arg_set(args, "fips140"))
      set_global_rngs(new FIPS_186_RNG, new FIPS_186_RNG);
   else
      set_global_rngs(new Randpool, new ANSI_X917_RNG);

   add_entropy_sources();

   if(!FIPS140::passes_self_tests())
      {
      deinitialize();
      throw Self_Test_Failure("FIPS-140 startup tests");
      }

   const u32bit min_entropy = Config::get_u32bit("rng/min_entropy");

   if(min_entropy != 0 && !arg_set(args, "no_rng_seed"))
      {
      u32bit total_bits = 0;
      for(u32bit j = 0; j != 4; j++)
         {
         total_bits += Global_RNG::seed(true, min_entropy - total_bits);
         if(total_bits >= min_entropy)
            break;
         }

      if(total_bits < min_entropy)
         throw PRNG_Unseeded("Unable to collect sufficient entropy");
      }

   startup_dl_cache();
   }

/*************************************************
* Library Shutdown                               *
*************************************************/
void deinitialize()
   {
   shutdown_engines();
   shutdown_rng_subsystem();
   destroy_lookup_tables();
   shutdown_dl_cache();
   shutdown_conf();
   shutdown_oids();
   set_timer_type(0);
   set_mutex_type(0);
   shutdown_memory_subsystem();
   }

}

}
