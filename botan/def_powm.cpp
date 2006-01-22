/*************************************************
* Modular Exponentiation Source File             *
* (C) 1999-2006 The Botan Project                *
*************************************************/

#include <botan/def_powm.h>
#include <botan/def_eng.h>

namespace Botan {

/*************************************************
* Choose a modular exponentation algorithm       *
*************************************************/
Modular_Exponentiator*
Default_Engine::mod_exp(const BigInt& n, Power_Mod::Usage_Hints hints) const
   {
   if(n.is_odd())
      return new Montgomery_Exponentiator(n, hints);
   return new Fixed_Window_Exponentiator(n, hints);
   }

}
