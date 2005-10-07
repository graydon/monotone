/*************************************************
* DES Source File                                *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#include <botan/des.h>

namespace Botan {

/*************************************************
* DES Encryption                                 *
*************************************************/
void DES::enc(const byte in[], byte out[]) const
   {
   u32bit left  = make_u32bit(in[0], in[1], in[2], in[3]),
          right = make_u32bit(in[4], in[5], in[6], in[7]);
   IP(left, right);       round(left, right, 0); round(right, left, 1);
   round(left, right, 2); round(right, left, 3); round(left, right, 4);
   round(right, left, 5); round(left, right, 6); round(right, left, 7);
   round(left, right, 8); round(right, left, 9); round(left, right,10);
   round(right, left,11); round(left, right,12); round(right, left,13);
   round(left, right,14); round(right, left,15); FP(left, right);
   out[0] = get_byte(0, right); out[1] = get_byte(1, right);
   out[2] = get_byte(2, right); out[3] = get_byte(3, right);
   out[4] = get_byte(0, left);  out[5] = get_byte(1, left);
   out[6] = get_byte(2, left);  out[7] = get_byte(3, left);
   }

/*************************************************
* DES Decryption                                 *
*************************************************/
void DES::dec(const byte in[], byte out[]) const
   {
   u32bit left  = make_u32bit(in[0], in[1], in[2], in[3]),
          right = make_u32bit(in[4], in[5], in[6], in[7]);
   IP(left, right);       round(left, right,15); round(right, left,14);
   round(left, right,13); round(right, left,12); round(left, right,11);
   round(right, left,10); round(left, right, 9); round(right, left, 8);
   round(left, right, 7); round(right, left, 6); round(left, right, 5);
   round(right, left, 4); round(left, right, 3); round(right, left, 2);
   round(left, right, 1); round(right, left, 0); FP(left, right);
   out[0] = get_byte(0, right); out[1] = get_byte(1, right);
   out[2] = get_byte(2, right); out[3] = get_byte(3, right);
   out[4] = get_byte(0, left);  out[5] = get_byte(1, left);
   out[6] = get_byte(2, left);  out[7] = get_byte(3, left);
   }

/*************************************************
* DES Round                                      *
*************************************************/
void DES::round(u32bit& left, u32bit right, u32bit n) const
   {
   u32bit T1 = rotate_right(right, 4) ^ round_key[2*n],
          T2 =              right     ^ round_key[2*n + 1];
   left ^= SPBOX1[get_byte(0, T1)] ^ SPBOX2[get_byte(0, T2)] ^
           SPBOX3[get_byte(1, T1)] ^ SPBOX4[get_byte(1, T2)] ^
           SPBOX5[get_byte(2, T1)] ^ SPBOX6[get_byte(2, T2)] ^
           SPBOX7[get_byte(3, T1)] ^ SPBOX8[get_byte(3, T2)];
   }

/*************************************************
* DES Initial Permutation                        *
*************************************************/
void DES::IP(u32bit& L, u32bit& R)
   {
   u64bit T = (IPTAB1[get_byte(0, L)]     ) | (IPTAB1[get_byte(1, L)] << 1) |
              (IPTAB1[get_byte(2, L)] << 2) | (IPTAB1[get_byte(3, L)] << 3) |
              (IPTAB1[get_byte(0, R)] << 4) | (IPTAB1[get_byte(1, R)] << 5) |
              (IPTAB1[get_byte(2, R)] << 6) | (IPTAB2[get_byte(3, R)]     );
   L = (u32bit)((T >> 32) & 0xFFFFFFFF);
   R = (u32bit)((T      ) & 0xFFFFFFFF);
   }

/*************************************************
* DES Final Permutation                          *
*************************************************/
void DES::FP(u32bit& L, u32bit& R)
   {
   u64bit T = (FPTAB1[get_byte(0, L)] << 5) | (FPTAB1[get_byte(1, L)] << 3) |
              (FPTAB1[get_byte(2, L)] << 1) | (FPTAB2[get_byte(3, L)] << 1) |
              (FPTAB1[get_byte(0, R)] << 4) | (FPTAB1[get_byte(1, R)] << 2) |
              (FPTAB1[get_byte(2, R)]     ) | (FPTAB2[get_byte(3, R)]     );
   L = (u32bit)((T >> 32) & 0xFFFFFFFF);
   R = (u32bit)((T      ) & 0xFFFFFFFF);
   }

/*************************************************
* DES Raw Encryption                             *
*************************************************/
void DES::raw_encrypt(u32bit& left, u32bit& right) const
   {
   round(left, right, 0); round(right, left, 1); round(left, right, 2);
   round(right, left, 3); round(left, right, 4); round(right, left, 5);
   round(left, right, 6); round(right, left, 7); round(left, right, 8);
   round(right, left, 9); round(left, right,10); round(right, left,11);
   round(left, right,12); round(right, left,13); round(left, right,14);
   round(right, left,15);
   }

/*************************************************
* DES Raw Decryption                             *
*************************************************/
void DES::raw_decrypt(u32bit& left, u32bit& right) const
   {
   round(left, right,15); round(right, left,14); round(left, right,13);
   round(right, left,12); round(left, right,11); round(right, left,10);
   round(left, right, 9); round(right, left, 8); round(left, right, 7);
   round(right, left, 6); round(left, right, 5); round(right, left, 4);
   round(left, right, 3); round(right, left, 2); round(left, right, 1);
   round(right, left, 0);
   }

/*************************************************
* DES Key Schedule                               *
*************************************************/
void DES::key(const byte key[], u32bit)
   {
   static const byte ROT[16] = { 1, 1, 2, 2, 2, 2, 2, 2,
                                 1, 2, 2, 2, 2, 2, 2, 1 };
   u32bit C = ((key[7] & 0x80) << 20) | ((key[6] & 0x80) << 19) |
              ((key[5] & 0x80) << 18) | ((key[4] & 0x80) << 17) |
              ((key[3] & 0x80) << 16) | ((key[2] & 0x80) << 15) |
              ((key[1] & 0x80) << 14) | ((key[0] & 0x80) << 13) |
              ((key[7] & 0x40) << 13) | ((key[6] & 0x40) << 12) |
              ((key[5] & 0x40) << 11) | ((key[4] & 0x40) << 10) |
              ((key[3] & 0x40) <<  9) | ((key[2] & 0x40) <<  8) |
              ((key[1] & 0x40) <<  7) | ((key[0] & 0x40) <<  6) |
              ((key[7] & 0x20) <<  6) | ((key[6] & 0x20) <<  5) |
              ((key[5] & 0x20) <<  4) | ((key[4] & 0x20) <<  3) |
              ((key[3] & 0x20) <<  2) | ((key[2] & 0x20) <<  1) |
              ((key[1] & 0x20)      ) | ((key[0] & 0x20) >>  1) |
              ((key[7] & 0x10) >>  1) | ((key[6] & 0x10) >>  2) |
              ((key[5] & 0x10) >>  3) | ((key[4] & 0x10) >>  4);
   u32bit D = ((key[7] & 0x02) << 26) | ((key[6] & 0x02) << 25) |
              ((key[5] & 0x02) << 24) | ((key[4] & 0x02) << 23) |
              ((key[3] & 0x02) << 22) | ((key[2] & 0x02) << 21) |
              ((key[1] & 0x02) << 20) | ((key[0] & 0x02) << 19) |
              ((key[7] & 0x04) << 17) | ((key[6] & 0x04) << 16) |
              ((key[5] & 0x04) << 15) | ((key[4] & 0x04) << 14) |
              ((key[3] & 0x04) << 13) | ((key[2] & 0x04) << 12) |
              ((key[1] & 0x04) << 11) | ((key[0] & 0x04) << 10) |
              ((key[7] & 0x08) <<  8) | ((key[6] & 0x08) <<  7) |
              ((key[5] & 0x08) <<  6) | ((key[4] & 0x08) <<  5) |
              ((key[3] & 0x08) <<  4) | ((key[2] & 0x08) <<  3) |
              ((key[1] & 0x08) <<  2) | ((key[0] & 0x08) <<  1) |
              ((key[3] & 0x10) >>  1) | ((key[2] & 0x10) >>  2) |
              ((key[1] & 0x10) >>  3) | ((key[0] & 0x10) >>  4);
   for(u32bit j = 0; j != 16; j++)
      {
      C = ((C << ROT[j]) | (C >> (28-ROT[j]))) & 0x0FFFFFFF;
      D = ((D << ROT[j]) | (D >> (28-ROT[j]))) & 0x0FFFFFFF;
      round_key[2*j  ] = ((C & 0x00000010) << 22) | ((C & 0x00000800) << 17) |
                         ((C & 0x00000020) << 16) | ((C & 0x00004004) << 15) |
                         ((C & 0x00000200) << 11) | ((C & 0x00020000) << 10) |
                         ((C & 0x01000000) >>  6) | ((C & 0x00100000) >>  4) |
                         ((C & 0x00010000) <<  3) | ((C & 0x08000000) >>  2) |
                         ((C & 0x00800000) <<  1) | ((D & 0x00000010) <<  8) |
                         ((D & 0x00000002) <<  7) | ((D & 0x00000001) <<  2) |
                         ((D & 0x00000200)      ) | ((D & 0x00008000) >>  2) |
                         ((D & 0x00000088) >>  3) | ((D & 0x00001000) >>  7) |
                         ((D & 0x00080000) >>  9) | ((D & 0x02020000) >> 14) |
                         ((D & 0x00400000) >> 21);
      round_key[2*j+1] = ((C & 0x00000001) << 28) | ((C & 0x00000082) << 18) |
                         ((C & 0x00002000) << 14) | ((C & 0x00000100) << 10) |
                         ((C & 0x00001000) <<  9) | ((C & 0x00040000) <<  6) |
                         ((C & 0x02400000) <<  4) | ((C & 0x00008000) <<  2) |
                         ((C & 0x00200000) >>  1) | ((C & 0x04000000) >> 10) |
                         ((D & 0x00000020) <<  6) | ((D & 0x00000100)      ) |
                         ((D & 0x00000800) >>  1) | ((D & 0x00000040) >>  3) |
                         ((D & 0x00010000) >>  4) | ((D & 0x00000400) >>  5) |
                         ((D & 0x00004000) >> 10) | ((D & 0x04000000) >> 13) |
                         ((D & 0x00800000) >> 14) | ((D & 0x00100000) >> 18) |
                         ((D & 0x01000000) >> 24) | ((D & 0x08000000) >> 26);
      }
   }

/*************************************************
* TripleDES Encryption                           *
*************************************************/
void TripleDES::enc(const byte in[], byte out[]) const
   {
   u32bit left  = make_u32bit(in[0], in[1], in[2], in[3]),
          right = make_u32bit(in[4], in[5], in[6], in[7]);
   DES::IP(left, right);
   des1.raw_encrypt(left, right);
   des2.raw_decrypt(right, left);
   des3.raw_encrypt(left, right);
   DES::FP(left, right);
   out[0] = get_byte(0, right); out[1] = get_byte(1, right);
   out[2] = get_byte(2, right); out[3] = get_byte(3, right);
   out[4] = get_byte(0, left);  out[5] = get_byte(1, left);
   out[6] = get_byte(2, left);  out[7] = get_byte(3, left);
   }

/*************************************************
* TripleDES Decryption                           *
*************************************************/
void TripleDES::dec(const byte in[], byte out[]) const
   {
   u32bit left  = make_u32bit(in[0], in[1], in[2], in[3]),
          right = make_u32bit(in[4], in[5], in[6], in[7]);
   DES::IP(left, right);
   des3.raw_decrypt(left, right);
   des2.raw_encrypt(right, left);
   des1.raw_decrypt(left, right);
   DES::FP(left, right);
   out[0] = get_byte(0, right); out[1] = get_byte(1, right);
   out[2] = get_byte(2, right); out[3] = get_byte(3, right);
   out[4] = get_byte(0, left);  out[5] = get_byte(1, left);
   out[6] = get_byte(2, left);  out[7] = get_byte(3, left);
   }

/*************************************************
* TripleDES Key Schedule                         *
*************************************************/
void TripleDES::key(const byte key[], u32bit length)
   {
   des1.set_key(key, 8);
   des2.set_key(key + 8, 8);
   if(length == 24)
      des3.set_key(key + 16, 8);
   else
      des3.set_key(key, 8);
   }

/*************************************************
* DESX Encryption                                *
*************************************************/
void DESX::enc(const byte in[], byte out[]) const
   {
   xor_buf(out, in, K1.begin(), BLOCK_SIZE);
   des.encrypt(out);
   xor_buf(out, K2.begin(), BLOCK_SIZE);
   }

/*************************************************
* DESX Decryption                                *
*************************************************/
void DESX::dec(const byte in[], byte out[]) const
   {
   xor_buf(out, in, K2.begin(), BLOCK_SIZE);
   des.decrypt(out);
   xor_buf(out, K1.begin(), BLOCK_SIZE);
   }

/*************************************************
* DESX Key Schedule                              *
*************************************************/
void DESX::key(const byte key[], u32bit)
   {
   K1.copy(key, 8);
   des.set_key(key + 8, 8);
   K2.copy(key + 16, 8);
   }

}
