/*************************************************
* Modular Exponentiation Header File             *
* (C) 1999-2006 The Botan Project                *
*************************************************/

#include <botan/pow_mod.h>
#include <vector>

namespace Botan {

/*************************************************
* Fixed Window Exponentiator                     *
*************************************************/
class Fixed_Window_Exponentiator : public Modular_Exponentiator
   {
   public:
      void set_exponent(const BigInt&);
      void set_base(const BigInt&);
      BigInt execute() const;

      Modular_Exponentiator* copy() const;

      Fixed_Window_Exponentiator(const BigInt&, Power_Mod::Usage_Hints);
      ~Fixed_Window_Exponentiator();
   private:
      class ModularReducer* reducer;
      BigInt exp;
      u32bit window_bits;
      std::vector<BigInt> g;
      Power_Mod::Usage_Hints hints;
   };

/*************************************************
* Montgomery Exponentiator                       *
*************************************************/
class Montgomery_Exponentiator : public Modular_Exponentiator
   {
   public:
      void set_exponent(const BigInt&);
      void set_base(const BigInt&);
      BigInt execute() const;

      Modular_Exponentiator* copy() const;

      Montgomery_Exponentiator(const BigInt&, Power_Mod::Usage_Hints);
   private:
      BigInt reduce(const BigInt&) const;

      BigInt exp, modulus;
      BigInt R2, R_mod;
      std::vector<BigInt> g;
      word mod_prime;
      u32bit exp_bits, window_bits;
      Power_Mod::Usage_Hints hints;
   };
}
