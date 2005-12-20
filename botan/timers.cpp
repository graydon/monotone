/*************************************************
* Timestamp Functions Source File                *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#include <botan/timers.h>
#include <botan/util.h>
#include <botan/init.h>
#include <ctime>

namespace Botan {

namespace {

Timer* global_timer = 0;

}

/*************************************************
* Timer Access Functions                         *
*************************************************/
u64bit system_time()
   {
   return std::time(0);
   }

u64bit system_clock()
   {
   if(!global_timer)
      return combine_timers(std::time(0), std::clock(), CLOCKS_PER_SEC);
   return global_timer->clock();
   }

/*************************************************
* Combine a two time values into a single one    *
*************************************************/
u64bit combine_timers(u32bit seconds, u32bit parts, u32bit parts_hz)
   {
   const u64bit NANOSECONDS_UNITS = 1000000000;
   parts *= (NANOSECONDS_UNITS / parts_hz);
   return ((seconds * NANOSECONDS_UNITS) + parts);
   }

namespace Init {

/*************************************************
* Set the Timer type                             *
*************************************************/
void set_timer_type(Timer* timer)
   {
   delete global_timer;
   global_timer = timer;
   }

}

}
