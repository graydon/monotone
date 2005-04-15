/*************************************************
* Blinder Source File                            *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#include <botan/blinding.h>
#include <botan/numthry.h>

namespace Botan {

/*************************************************
* Blinder Constructor                            *
*************************************************/
Blinder::Blinder()
   {
   reducer = 0;
   }

/*************************************************
* Blinder Copy Constructor                       *
*************************************************/
Blinder::Blinder(const Blinder& blinder)
   {
   reducer = 0;
   initialize(blinder.e, blinder.d, blinder.n);
   }

/*************************************************
* Blinder Assignment Operator                    *
*************************************************/
Blinder& Blinder::operator=(const Blinder& blinder)
   {
   delete reducer;
   reducer = 0;

   if(blinder.reducer)
      initialize(blinder.e, blinder.d, blinder.n);
   return (*this);
   }

/*************************************************
* Initialize a Blinder object                    *
*************************************************/
void Blinder::initialize(const BigInt& e1, const BigInt& d1, const BigInt& n1)
   {
   if(e1 < 1 || d1 < 1 || n1 < 1)
      throw Invalid_Argument("Blinder::initialize: Arguments too small");

   e = e1;
   d = d1;
   n = n1;
   delete reducer;
   reducer = get_reducer(n);
   }

/*************************************************
* Blinder Destructor                             *
*************************************************/
Blinder::~Blinder()
   {
   delete reducer;
   }

/*************************************************
* Blind a number                                 *
*************************************************/
BigInt Blinder::blind(const BigInt& i) const
   {
   if(!reducer) return i;
   e = reducer->square(e);
   d = reducer->square(d);
   return reducer->multiply(i, e);
   }

/*************************************************
* Unblind a number                               *
*************************************************/
BigInt Blinder::unblind(const BigInt& i) const
   {
   if(!reducer) return i;
   return reducer->multiply(i, d);
   }

}
