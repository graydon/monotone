/*************************************************
* Modular Exponentiation Source File             *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#include <botan/mod_exp.h>
#include <botan/numthry.h>

namespace Botan {

/*************************************************
* FixedExponent_Exp Constructor                  *
*************************************************/
FixedExponent_Exp::FixedExponent_Exp(const BigInt& exp, const BigInt& mod) :
   reducer(get_reducer(mod)), exponent(exp)
   {
   if(mod <= 0)
      throw Invalid_Argument("FixedExponent_Exp: Invalid modulus");
   if(exp < 0)
      throw Invalid_Argument("FixedExponent_Exp: Invalid exponent");
   }

/*************************************************
* FixedExponent_Exp Copy Constructor             *
*************************************************/
FixedExponent_Exp::FixedExponent_Exp(const FixedExponent_Exp& exp)
   {
   exponent = 0;
   reducer = 0;

   if(exp.initialized())
      {
      exponent = exp.get_exponent();
      reducer = get_reducer(exp.get_modulus());
      }
   }

/*************************************************
* FixedExponent_Exp Assignment Operator          *
*************************************************/
FixedExponent_Exp& FixedExponent_Exp::operator=(const FixedExponent_Exp& exp)
   {
   delete reducer;
   exponent = 0;
   reducer = 0;

   if(exp.initialized())
      {
      reducer = get_reducer(exp.get_modulus());
      exponent = exp.get_exponent();
      }
   return (*this);
   }

/*************************************************
* Fixed Exponent Exponentiation                  *
*************************************************/
BigInt FixedExponent_Exp::power_mod(const BigInt& base) const
   {
   init_check();
   return Botan::power_mod(reducer->reduce(base), exponent, reducer);
   }

/*************************************************
* Calculate n modulo the fixed modulus           *
*************************************************/
BigInt FixedExponent_Exp::reduce(const BigInt& n) const
   {
   init_check();
   return reducer->reduce(n);
   }

/*************************************************
* Return the exponent being used                 *
*************************************************/
const BigInt& FixedExponent_Exp::get_exponent() const
   {
   init_check();
   return exponent;
   }

/*************************************************
* Return the modulus being used                  *
*************************************************/
const BigInt& FixedExponent_Exp::get_modulus() const
   {
   init_check();
   return reducer->get_modulus();
   }

/*************************************************
* Ensure the object has been initialized         *
*************************************************/
void FixedExponent_Exp::init_check() const
   {
   if(!initialized())
      throw Invalid_State("FixedExponent_Exp: Uninitialized access");
   }

/*************************************************
* FixedBase_Exp Constructor                      *
*************************************************/
FixedBase_Exp::FixedBase_Exp(const BigInt& base, const BigInt& mod) :
   reducer(get_reducer(mod)), g(255)
   {
   if(mod <= 0)
      throw Invalid_Argument("FixedBase_Exp: Invalid modulus");
   if(base < 0)
      throw Invalid_Argument("FixedBase_Exp: Invalid base");

   g[0] = base;
   for(u32bit j = 1; j != g.size(); j++)
      g[j] = reducer->multiply(g[j-1], g[0]);
   }

/*************************************************
* FixedBase_Exp Copy Constructor                 *
*************************************************/
FixedBase_Exp::FixedBase_Exp(const FixedBase_Exp& exp)
   {
   reducer = 0;

   if(exp.initialized())
      {
      reducer = get_reducer(exp.get_modulus());
      g = exp.g;
      }
   }

/*************************************************
* FixedBase_Exp Assignment Operator              *
*************************************************/
FixedBase_Exp& FixedBase_Exp::operator=(const FixedBase_Exp& exp)
   {
   delete reducer;
   reducer = 0;

   if(exp.initialized())
      {
      reducer = get_reducer(exp.get_modulus());
      g = exp.g;
      }
   return (*this);
   }

/*************************************************
* Fixed Base Exponentiation                      *
*************************************************/
BigInt FixedBase_Exp::power_mod(const BigInt& exp) const
   {
   init_check();
   if(exp.is_negative())
      throw Invalid_Argument("power_mod: exponent must be positive");
   if(exp.is_zero())
      return 1;

   const u32bit exp_bytes = (exp.bits() + 7) / 8;

   BigInt x = 1;
   for(u32bit j = exp_bytes; j > 0; j--)
      {
      for(u32bit k = 0; k != 8; k++)
         x = reducer->square(x);
      u32bit nibble = exp.byte_at(j-1);
      if(nibble)
         x = reducer->multiply(x, g[nibble-1]);
      }
   return x;
   }

/*************************************************
* Calculate n modulo the fixed modulus           *
*************************************************/
BigInt FixedBase_Exp::reduce(const BigInt& n) const
   {
   init_check();
   return reducer->reduce(n);
   }

/*************************************************
* Return the generator being used                *
*************************************************/
const BigInt& FixedBase_Exp::get_base() const
   {
   init_check();
   return g[0];
   }

/*************************************************
* Return the modulus being used                  *
*************************************************/
const BigInt& FixedBase_Exp::get_modulus() const
   {
   init_check();
   return reducer->get_modulus();
   }

/*************************************************
* Ensure the object has been initialized         *
*************************************************/
void FixedBase_Exp::init_check() const
   {
   if(!initialized())
      throw Invalid_State("FixedBase_Exp: Uninitialized access");
   }


}
