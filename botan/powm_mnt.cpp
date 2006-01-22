/*************************************************
* Montgomery Exponentiation Source File          *
* (C) 1999-2006 The Botan Project                *
*************************************************/

#include <botan/def_powm.h>
#include <botan/numthry.h>
#include <botan/mp_core.h>

#include <assert.h> // remove

namespace Botan {

namespace {

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
   BigInt z = R_mod;

   const u32bit exp_nibbles = (exp.bits() + window_bits - 1) / window_bits;

   SecureVector<word> workspace(3 * (modulus.sig_words() + 1));
   for(u32bit j = exp_nibbles; j > 0; j--)
      {
      for(u32bit k = 0; k != window_bits; ++k)
         square_and_reduce(z, workspace);

      u32bit nibble = exp.get_substring(window_bits*(j-1), window_bits);
      if(nibble)
         mul_and_reduce(z, g.at(nibble-1), workspace);
      }

   return reduce(z);
   }

/*************************************************
* Square and Montgomery Reduction                *
*************************************************/
void Montgomery_Exponentiator::square_and_reduce(BigInt& x,
                                                 MemoryRegion<word>& z) const
   {
   const u32bit mod_size = modulus.sig_words();

   assert(z.size() == 3*mod_size + 3);
   z.clear();

   bigint_sqr(z.begin(), z.size(), x.data(), x.size(), x.sig_words());

   const word* mod_bits = modulus.data();
   for(u32bit j = 0; j != mod_size; ++j)
      {
      word* workspace = z.begin() + 2 * (mod_size + 1);

      word u = z[j] * mod_prime;

      bigint_linmul3(workspace, mod_bits, mod_size, u);
      bigint_add2(z + j, z.size() - j, workspace, mod_size+1);
      }

   for(u32bit j = 0; j != mod_size + 1; ++j)
      z[j] = z[j + mod_size];

   if(bigint_cmp(z, mod_size + 1, mod_bits, mod_size) >= 0)
      bigint_sub2(z, mod_size + 1, mod_bits, mod_size);

   x.get_reg().set(z, mod_size + 1);
   }

/*************************************************
* Multiply and Montgomery Reduction              *
*************************************************/
void Montgomery_Exponentiator::mul_and_reduce(BigInt& x, const BigInt& y,
                                              MemoryRegion<word>& z) const
   {
   const u32bit mod_size = modulus.sig_words();

   assert(z.size() == 3*mod_size + 3);
   z.clear();

   bigint_mul3(z.begin(), z.size(), x.data(), x.size(), x.sig_words(),
                                    y.data(), y.size(), y.sig_words());

   const word* mod_bits = modulus.data();
   for(u32bit j = 0; j != mod_size; ++j)
      {
      word* workspace = z.begin() + 2 * (mod_size + 1);

      word u = z[j] * mod_prime;

      bigint_linmul3(workspace, mod_bits, mod_size, u);
      bigint_add2(z + j, z.size() - j, workspace, mod_size+1);
      }

   for(u32bit j = 0; j != mod_size + 1; ++j)
      z[j] = z[j + mod_size];

   if(bigint_cmp(z, mod_size + 1, mod_bits, mod_size) >= 0)
      bigint_sub2(z, mod_size + 1, mod_bits, mod_size);

   x.get_reg().set(z, mod_size + 1);
   }

/*************************************************
* Montgomery Reduction                           *
*************************************************/
BigInt Montgomery_Exponentiator::reduce(const BigInt& n) const
   {
   const word* mod_bits = modulus.data();
   const u32bit mod_size = modulus.sig_words();

   SecureVector<word> z(n.data(), n.size()), workspace(mod_size+1);
   z.grow_to(2*mod_size + 2);

   for(u32bit j = 0; j != mod_size; ++j)
      {
      word u = z[j] * mod_prime;

      bigint_linmul3(workspace, mod_bits, mod_size, u);
      bigint_add2(z + j, z.size() - j, workspace, mod_size+1);
      }

   for(u32bit j = 0; j != mod_size + 1; ++j)
      z[j] = z[j + mod_size];

   if(bigint_cmp(z, mod_size + 1, mod_bits, mod_size) >= 0)
      bigint_sub2(z, mod_size + 1, mod_bits, mod_size);

   BigInt r;
   r.get_reg().set(z, mod_size + 1);
   return r;
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
