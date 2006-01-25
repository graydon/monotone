/*************************************************
* Fused and Important MP Algorithms Source File  *
* (C) 1999-2006 The Botan Project                *
*************************************************/

#include <botan/numthry.h>
#include <botan/mp_core.h>
#include <algorithm>

namespace Botan {

/*************************************************
* Square a BigInt                                *
*************************************************/
BigInt square(const BigInt& x)
   {
   const u32bit x_sw = x.sig_words();

   BigInt z(BigInt::Positive, 2*x_sw);
   bigint_sqr(z.get_reg(), z.size(), x.data(), x.size(), x_sw);
   return z;
   }

/*************************************************
* Multiply-Add Operation                         *
*************************************************/
BigInt mul_add(const BigInt& a, const BigInt& b, const BigInt& c)
   {
   if(c.is_negative() || c.is_zero())
      throw Invalid_Argument("mul_add: Third argument must be > 0");

   BigInt::Sign sign = BigInt::Positive;
   if(a.sign() != b.sign())
      sign = BigInt::Negative;

   const u32bit a_sw = a.sig_words();
   const u32bit b_sw = b.sig_words();
   const u32bit c_sw = c.sig_words();

   BigInt r(sign, std::max(a.size() + b.size(), c_sw) + 1);
   bigint_mul(r.get_reg(), r.size(),
              a.data(), a.size(), a_sw,
              b.data(), b.size(), b_sw);
   const u32bit r_size = std::max(r.sig_words(), c_sw);
   bigint_add2(r.get_reg(), r_size, c.data(), c_sw);
   return r;
   }

/*************************************************
* Subtract-Multiply Operation                    *
*************************************************/
BigInt sub_mul(const BigInt& a, const BigInt& b, const BigInt& c)
   {
   if(a.is_negative() || b.is_negative())
      throw Invalid_Argument("sub_mul: First two arguments must be >= 0");

   BigInt r = a;
   r -= b;
   r *= c;
   return r;
   }

/*************************************************
* Multiply-Modulo Operation                      *
*************************************************/
BigInt mul_mod(const BigInt& a, const BigInt& b, const BigInt& m)
   {
   if(a.is_negative() || b.is_negative())
      throw Invalid_Argument("mul_mod: First two arguments must be >= 0");
   if(m <= 0)
      throw Invalid_Argument("mul_mod: Modulo must be positive");

   BigInt r = a;
   r *= b;
   r %= m;
   return r;
   }

}
