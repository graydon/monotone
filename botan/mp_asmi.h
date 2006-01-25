/*************************************************
* Lowest Level MPI Algorithms Header File        *
* (C) 1999-2006 The Botan Project                *
*************************************************/

#ifndef BOTAN_MP_ASM_INTERNAL_H__
#define BOTAN_MP_ASM_INTERNAL_H__

#include <botan/mp_asm.h>

namespace Botan {

extern "C" {

/*************************************************
* Word Addition                                  *
*************************************************/
inline word word_add(word x, word y, word* carry)
   {
   word z = x + y;
   word c1 = (z < x);
   z += *carry;
   *carry = c1 | (z < *carry);
   return z;
   }

/*************************************************
* Four Word Block Addition, Two Argument         *
*************************************************/
inline void word4_add2(word x[4], const word y[4], word* carry)
   {
   x[0] = word_add(x[0], y[0], carry);
   x[1] = word_add(x[1], y[1], carry);
   x[2] = word_add(x[2], y[2], carry);
   x[3] = word_add(x[3], y[3], carry);
   }

/*************************************************
* Four Word Block Addition, Three Argument       *
*************************************************/
inline void word4_add3(word z[4], const word x[4],
                       const word y[4], word* carry)
   {
   z[0] = word_add(x[0], y[0], carry);
   z[1] = word_add(x[1], y[1], carry);
   z[2] = word_add(x[2], y[2], carry);
   z[3] = word_add(x[3], y[3], carry);
   }

/*************************************************
* Word Subtraction                               *
*************************************************/
inline word word_sub(word x, word y, word* carry)
   {
   word t0 = x - y;
   word c1 = (t0 > x);
   word z = t0 - *carry;
   *carry = c1 | (z > t0);
   return z;
   }

/*************************************************
* Four Word Block Subtraction, Two Argument      *
*************************************************/
inline void word4_sub2(word x[4], const word y[4], word* carry)
   {
   x[0] = word_sub(x[0], y[0], carry);
   x[1] = word_sub(x[1], y[1], carry);
   x[2] = word_sub(x[2], y[2], carry);
   x[3] = word_sub(x[3], y[3], carry);
   }

/*************************************************
* Four Word Block Subtraction, Three Argument    *
*************************************************/
inline void word4_sub3(word z[4], const word x[4],
                       const word y[4], word* carry)
   {
   z[0] = word_sub(x[0], y[0], carry);
   z[1] = word_sub(x[1], y[1], carry);
   z[2] = word_sub(x[2], y[2], carry);
   z[3] = word_sub(x[3], y[3], carry);
   }

/*************************************************
* Four Word Block Linear Multiplication          *
*************************************************/
inline void word4_linmul2(word x[4], word y, word* carry)
   {
   x[0] = word_mul(x[0], y, carry);
   x[1] = word_mul(x[1], y, carry);
   x[2] = word_mul(x[2], y, carry);
   x[3] = word_mul(x[3], y, carry);
   }

/*************************************************
* Four Word Block Linear Multiplication          *
*************************************************/
inline void word4_linmul3(word z[4], const word x[4], word y, word* carry)
   {
   z[0] = word_mul(x[0], y, carry);
   z[1] = word_mul(x[1], y, carry);
   z[2] = word_mul(x[2], y, carry);
   z[3] = word_mul(x[3], y, carry);
   }

/*************************************************
* Eight Word Block Multiply-Add                  *
*************************************************/
inline void word8_madd3(word z[], word x, const word y[], word* carry)
   {
   word_madd(x, y[0], z[0], *carry, z + 0, carry);
   word_madd(x, y[1], z[1], *carry, z + 1, carry);
   word_madd(x, y[2], z[2], *carry, z + 2, carry);
   word_madd(x, y[3], z[3], *carry, z + 3, carry);
   word_madd(x, y[4], z[4], *carry, z + 4, carry);
   word_madd(x, y[5], z[5], *carry, z + 5, carry);
   word_madd(x, y[6], z[6], *carry, z + 6, carry);
   word_madd(x, y[7], z[7], *carry, z + 7, carry);
   }

}

}

#endif
