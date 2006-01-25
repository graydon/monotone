/*************************************************
* Montgomery Exponentiation Source File          *
* (C) 1999-2006 The Botan Project                *
*************************************************/

#include <botan/def_powm.h>
#include <botan/numthry.h>
#include <botan/mp_core.h>

namespace Botan {

namespace {

/*************************************************
* Perform the actual Montgomery reduction        *
*************************************************/
void montgomery_reduce(word* z, u32bit z_size,
                       const word* mod_bits, u32bit mod_size,
                       word mod_prime)
   {
   for(u32bit j = 0; j != mod_size; ++j)
      bigint_linmul_add(z + j, z_size - j,
                        mod_bits, mod_size, z[j] * mod_prime);

   for(u32bit j = 0; j != mod_size + 1; ++j)
      z[j] = z[j + mod_size];

   if(bigint_cmp(z, mod_size + 1, mod_bits, mod_size) >= 0)
      bigint_sub2(z, mod_size + 1, mod_bits, mod_size);
   }

/*************************************************
* Try to choose a good window size               *
*************************************************/
u32bit choose_window_bits(u32bit exp_bits, u32bit base_bits,
                          Power_Mod::Usage_Hints hints)
   {
   static const u32bit wsize[][2] = {
      { 2048, 4 }, { 1024, 3 }, { 256, 2 }, { 128, 1 }, { 0, 0 }
   };

   u32bit window_bits = 1;

   if(exp_bits)
      {
      for(u32bit j = 0; wsize[j][0]; ++j)
         {
         if(exp_bits >= wsize[j][0] || base_bits >= wsize[j][0])
            {
            window_bits += wsize[j][1];
            break;
            }
         }
      }

   if(hints & Power_Mod::BASE_IS_FIXED)
      window_bits += 2;
   if(hints & Power_Mod::EXP_IS_LARGE)
      window_bits++;

   return window_bits;
   }

}

/*************************************************
* Set the exponent                               *
*************************************************/
void Montgomery_Exponentiator::set_exponent(const BigInt& exp)
   {
   this->exp = exp;
   exp_bits = exp.bits();
   }

/*************************************************
* Set the base                                   *
*************************************************/
void Montgomery_Exponentiator::set_base(const BigInt& base)
   {
   window_bits = choose_window_bits(exp.bits(), base.bits(), hints);

   g.resize((1 << window_bits) - 1);

   g[0] = reduce(((base >= modulus) ? (base % modulus) : base) * R2);
   for(u32bit j = 1; j != g.size(); ++j)
      g[j] = reduce(g[j-1] * g[0]);
   }

/*************************************************
* Compute the result                             *
*************************************************/
BigInt Montgomery_Exponentiator::execute() const
   {
   const u32bit exp_nibbles = (exp.bits() + window_bits - 1) / window_bits;
   const u32bit mod_size = modulus.sig_words();

   BigInt z = R_mod;
   SecureVector<word> workspace(2 * (mod_size + 1));

   for(u32bit j = exp_nibbles; j > 0; --j)
      {
      for(u32bit k = 0; k != window_bits; ++k)
         {
         workspace.clear();
         bigint_sqr(workspace.begin(), workspace.size(),
                    z.data(), z.size(), z.sig_words());

         const u32bit mod_size = modulus.sig_words();
         montgomery_reduce(workspace.begin(), workspace.size(),
                           modulus.data(), mod_size, mod_prime);
         z.get_reg().set(workspace.begin(), mod_size + 1);
         }

      u32bit nibble = exp.get_substring(window_bits*(j-1), window_bits);
      if(nibble)
         {
         const BigInt& y = g.at(nibble-1);

         workspace.clear();
         bigint_mul(workspace.begin(), workspace.size(),
                    z.data(), z.size(), z.sig_words(),
                    y.data(), y.size(), y.sig_words());

         montgomery_reduce(workspace.begin(), workspace.size(),
                           modulus.data(), mod_size, mod_prime);
         z.get_reg().set(workspace.begin(), mod_size + 1);
         }
      }

   workspace.clear();
   workspace.copy(z.data(), z.size());

   montgomery_reduce(workspace.begin(), workspace.size(),
                     modulus.data(), mod_size, mod_prime);

   BigInt x;
   x.get_reg().set(workspace.begin(), mod_size + 1);
   return x;
   }

/*************************************************
* Montgomery Reduction                           *
*************************************************/
BigInt Montgomery_Exponentiator::reduce(const BigInt& n) const
   {
   const u32bit mod_size = modulus.sig_words();

   SecureVector<word> z(2 * (mod_size + 1));
   z.copy(n.data(), n.size());

   montgomery_reduce(z.begin(), z.size(), modulus.data(), mod_size, mod_prime);

   BigInt x;
   x.get_reg().set(z.begin(), mod_size + 1);
   return x;
   }

/*************************************************
* Make a copy of this exponentiator              *
*************************************************/
Modular_Exponentiator* Montgomery_Exponentiator::copy() const
   {
   return new Montgomery_Exponentiator(*this);
   }

/*************************************************
* Montgomery_Exponentiator Constructor           *
*************************************************/
Montgomery_Exponentiator::Montgomery_Exponentiator(const BigInt& mod,
   Power_Mod::Usage_Hints hints)
   {
   if(!mod.is_positive())
      throw Exception("Montgomery_Exponentiator: modulus must be positive");
   if(mod.is_even())
      throw Exception("Montgomery_Exponentiator: modulus must be odd");

   window_bits = 0;
   this->hints = hints;
   modulus = mod;

   BigInt mod_prime_bn(BigInt::Power2, MP_WORD_BITS);
   mod_prime = (mod_prime_bn - inverse_mod(modulus, mod_prime_bn)).word_at(0);

   R_mod = BigInt(BigInt::Power2, MP_WORD_BITS * modulus.sig_words());
   R_mod %= modulus;

   R2 = BigInt(BigInt::Power2, 2 * MP_WORD_BITS * modulus.sig_words());
   R2 %= modulus;
   }

}
