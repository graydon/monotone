/*************************************************
* Modular Exponentiation Source File             *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#include <botan/numthry.h>
#include <vector>

namespace Botan {

namespace {

/*************************************************
* Exponentiation Window Size                     *
*************************************************/
u32bit window_size(u32bit exp_bits)
   {
   struct mapping { u32bit bits; u32bit window_size; };

   static const mapping wsize[] = {
      { 2048, 7 },
      { 1024, 6 },
      {  256, 5 },
      {  128, 4 },
      {   64, 3 },
      {    0, 0 }
   };

   for(u32bit j = 0; wsize[j].bits; j++)
      {
      if(exp_bits >= wsize[j].bits)
         return wsize[j].window_size;
      }
   return 1;
   }

/*************************************************
* Left-to-Right Binary Modular Exponentiation    *
*************************************************/
BigInt power_mod_l2r(const BigInt& basex, const BigInt& exp,
                     ModularReducer* reducer)
   {
   const BigInt base = reducer->convert_in(basex);
   const u32bit exp_bits = exp.bits();

   BigInt x = reducer->convert_in(1);
   for(u32bit j = exp_bits; j > 0; j--)
      {
      x = reducer->square(x);
      if(exp.get_bit(j-1))
         x = reducer->multiply(x, base);
      }
   return reducer->convert_out(x);
   }

/*************************************************
* Modular Exponentiation with g = 2              *
*************************************************/
BigInt power_mod_g2(const BigInt& exp, ModularReducer* reducer)
   {
   if(reducer->must_convert())
      throw Internal_Error("power_mod_g2: Can't use this reducer");

   const u32bit exp_bits = exp.bits();
   BigInt x = 1;
   for(u32bit j = exp_bits; j > 0; j--)
      {
      x = reducer->square(x);
      if(exp.get_bit(j-1))
         {
         x <<= 1;
         x = reducer->reduce(x);
         }
      }
   return x;
   }

/*************************************************
* Window Modular Exponentiation                  *
*************************************************/
BigInt power_mod_window(const BigInt& base, const BigInt& exp,
                        ModularReducer* reducer, u32bit window_bits)
   {
   if(window_bits < 2)
      throw Internal_Error("power_mod_window: Window size too small");

   std::vector<BigInt> g((1 << window_bits) - 1);

   g[0] = reducer->convert_in(base);
   for(u32bit j = 1; j != g.size(); j++)
      g[j] = reducer->multiply(g[j-1], g[0]);

   const u32bit exp_nibbles = (exp.bits() + window_bits - 1) / window_bits;

   BigInt x = reducer->convert_in(1);
   for(u32bit j = exp_nibbles; j > 0; j--)
      {
      for(u32bit k = 0; k != window_bits; k++)
         x = reducer->square(x);
      u32bit nibble = exp.get_nibble(j-1, window_bits);
      if(nibble)
         x = reducer->multiply(x, g[nibble-1]);
      }
   return reducer->convert_out(x);
   }

}

/*************************************************
* Modular Exponentiation                         *
*************************************************/
BigInt power_mod(const BigInt& base, const BigInt& exp, const BigInt& mod)
   {
   ModularReducer* reducer = get_reducer(mod);
   BigInt x = power_mod(base, exp, reducer);
   delete reducer;
   return x;
   }

/*************************************************
* Modular Exponentiation Algorithm Dispatch      *
*************************************************/
BigInt power_mod(const BigInt& base, const BigInt& exp,
                 ModularReducer* reducer)
   {
   if(base.is_negative())
      throw Invalid_Argument("power_mod: base must be positive");
   if(exp.is_negative())
      throw Invalid_Argument("power_mod: exponent must be positive");
   if(exp.is_zero())
      return 1;

   const u32bit window_bits = window_size(exp.bits());

   if(base == 2 && !reducer->must_convert())
      return power_mod_g2(exp, reducer);
   if(window_bits > 1)
      return power_mod_window(base, exp, reducer, window_bits);
   return power_mod_l2r(base, exp, reducer);
   }

}
