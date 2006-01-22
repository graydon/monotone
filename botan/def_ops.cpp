/*************************************************
* Default Engine PK Operations Source File       *
* (C) 1999-2006 The Botan Project                *
*************************************************/

#include <botan/def_eng.h>
#include <botan/pow_mod.h>
#include <botan/numthry.h>
#include <botan/reducer.h>

namespace Botan {

namespace {

/*************************************************
* Default IF Operation                           *
*************************************************/
class Default_IF_Op : public IF_Operation
   {
   public:
      BigInt public_op(const BigInt& i) const
         { return powermod_e_n(i); }
      BigInt private_op(const BigInt&) const;

      IF_Operation* clone() const;

      Default_IF_Op(const BigInt&, const BigInt&, const BigInt&,
                    const BigInt&, const BigInt&, const BigInt&,
                    const BigInt&, const BigInt&);
      ~Default_IF_Op() { delete reduce_by_p; }
   private:
      const BigInt p, q, c;
      Fixed_Exponent_Power_Mod powermod_e_n, powermod_d1_p, powermod_d2_q;
      ModularReducer* reduce_by_p;
   };

/*************************************************
* Default_IF_Op Constructor                      *
*************************************************/
Default_IF_Op::Default_IF_Op(const BigInt& e, const BigInt& n, const BigInt&,
                             const BigInt& px, const BigInt& qx,
                             const BigInt& d1, const BigInt& d2,
                             const BigInt& cx) : p(px), q(qx), c(cx)
   {
   powermod_e_n = Fixed_Exponent_Power_Mod(e, n);
   reduce_by_p = 0;

   if(d1 != 0 && d2 != 0 && p != 0 && q != 0)
      {
      powermod_d1_p = Fixed_Exponent_Power_Mod(d1, p);
      powermod_d2_q = Fixed_Exponent_Power_Mod(d2, q);
      reduce_by_p = get_reducer(p);
      }
   }

/*************************************************
* Default IF Private Operation                   *
*************************************************/
BigInt Default_IF_Op::private_op(const BigInt& i) const
   {
   if(q == 0)
      throw Internal_Error("Default_IF_Op::private_op: No private key");

   BigInt j1 = powermod_d1_p(i);
   BigInt j2 = powermod_d2_q(i);
   j1 = reduce_by_p->reduce(sub_mul(j1, j2, c));
   return mul_add(j1, q, j2);
   }

/*************************************************
* Make a copy of this Default_IF_Op              *
*************************************************/
IF_Operation* Default_IF_Op::clone() const
   {
   Default_IF_Op* copy = new Default_IF_Op(*this);
   copy->reduce_by_p = 0;

   if(p != 0)
      copy->reduce_by_p = get_reducer(p);

   return copy;
   }


/*************************************************
* Default DSA Operation                          *
*************************************************/
class Default_DSA_Op : public DSA_Operation
   {
   public:
      bool verify(const byte[], u32bit, const byte[], u32bit) const;
      SecureVector<byte> sign(const byte[], u32bit, const BigInt&) const;

      DSA_Operation* clone() const { return new Default_DSA_Op(*this); }

      Default_DSA_Op(const DL_Group&, const BigInt&, const BigInt&);
   private:
      const BigInt x, y;
      const DL_Group group;
      Fixed_Base_Power_Mod powermod_g_p, powermod_y_p;
   };

/*************************************************
* Default_DSA_Op Constructor                     *
*************************************************/
Default_DSA_Op::Default_DSA_Op(const DL_Group& grp, const BigInt& y1,
                               const BigInt& x1) : x(x1), y(y1), group(grp)
   {
   powermod_g_p = Fixed_Base_Power_Mod(group.get_g(), group.get_p());
   powermod_y_p = Fixed_Base_Power_Mod(y, group.get_p());
   }

/*************************************************
* Default DSA Verify Operation                   *
*************************************************/
bool Default_DSA_Op::verify(const byte msg[], u32bit msg_len,
                            const byte sig[], u32bit sig_len) const
   {
   const BigInt& q = group.get_q();
   const BigInt& p = group.get_p();

   if(sig_len != 2*q.bytes() || msg_len > q.bytes())
      return false;

   BigInt r(sig, q.bytes());
   BigInt s(sig + q.bytes(), q.bytes());
   BigInt i(msg, msg_len);

   if(r <= 0 || r >= q || s <= 0 || s >= q)
      return false;

   s = inverse_mod(s, q);
   s = mul_mod(powermod_g_p(mul_mod(s, i, q)),
               powermod_y_p(mul_mod(s, r, q)), p);

   return (s % q == r);
   }

/*************************************************
* Default DSA Sign Operation                     *
*************************************************/
SecureVector<byte> Default_DSA_Op::sign(const byte in[], u32bit length,
                                        const BigInt& k) const
   {
   if(x == 0)
      throw Internal_Error("Default_DSA_Op::sign: No private key");

   const BigInt& q = group.get_q();
   BigInt i(in, length);

   BigInt r = powermod_g_p(k) % q;
   BigInt s = mul_mod(inverse_mod(k, q), mul_add(x, r, i), q);
   if(r.is_zero() || s.is_zero())
      throw Internal_Error("Default_DSA_Op::sign: r or s was zero");

   SecureVector<byte> output(2*q.bytes());
   r.binary_encode(output + (output.size() / 2 - r.bytes()));
   s.binary_encode(output + (output.size() - s.bytes()));
   return output;
   }

/*************************************************
* Default NR Operation                           *
*************************************************/
class Default_NR_Op : public NR_Operation
   {
   public:
      SecureVector<byte> verify(const byte[], u32bit) const;
      SecureVector<byte> sign(const byte[], u32bit, const BigInt&) const;

      NR_Operation* clone() const { return new Default_NR_Op(*this); }

      Default_NR_Op(const DL_Group&, const BigInt&, const BigInt&);
   private:
      const BigInt x, y;
      const DL_Group group;
      Fixed_Base_Power_Mod powermod_g_p, powermod_y_p;
   };

/*************************************************
* Default_NR_Op Constructor                      *
*************************************************/
Default_NR_Op::Default_NR_Op(const DL_Group& grp, const BigInt& y1,
                             const BigInt& x1) : x(x1), y(y1), group(grp)
   {
   powermod_g_p = Fixed_Base_Power_Mod(group.get_g(), group.get_p());
   powermod_y_p = Fixed_Base_Power_Mod(y, group.get_p());
   }

/*************************************************
* Default NR Verify Operation                    *
*************************************************/
SecureVector<byte> Default_NR_Op::verify(const byte in[], u32bit length) const
   {
   const BigInt& p = group.get_p();
   const BigInt& q = group.get_q();

   if(length != 2*q.bytes())
      return false;

   BigInt c(in, q.bytes());
   BigInt d(in + q.bytes(), q.bytes());

   if(c.is_zero() || c >= q || d >= q)
      throw Invalid_Argument("Default_NR_Op::verify: Invalid signature");

   BigInt i = mul_mod(powermod_g_p(d), powermod_y_p(c), p);
   return BigInt::encode((c - i) % q);
   }

/*************************************************
* Default NR Sign Operation                      *
*************************************************/
SecureVector<byte> Default_NR_Op::sign(const byte in[], u32bit length,
                                       const BigInt& k) const
   {
   if(x == 0)
      throw Internal_Error("Default_NR_Op::sign: No private key");

   const BigInt& q = group.get_q();

   BigInt f(in, length);

   if(f >= q)
      throw Invalid_Argument("Default_NR_Op::sign: Input is out of range");

   BigInt c = (powermod_g_p(k) + f) % q;
   if(c.is_zero())
      throw Internal_Error("Default_NR_Op::sign: c was zero");
   BigInt d = (k - x * c) % q;

   SecureVector<byte> output(2*q.bytes());
   c.binary_encode(output + (output.size() / 2 - c.bytes()));
   d.binary_encode(output + (output.size() - d.bytes()));
   return output;
   }

/*************************************************
* Default ElGamal Operation                      *
*************************************************/
class Default_ELG_Op : public ELG_Operation
   {
   public:
      SecureVector<byte> encrypt(const byte[], u32bit, const BigInt&) const;
      BigInt decrypt(const BigInt&, const BigInt&) const;

      ELG_Operation* clone() const { return new Default_ELG_Op(*this); }

      Default_ELG_Op(const DL_Group&, const BigInt&, const BigInt&);
   private:
      const BigInt p;
      Fixed_Base_Power_Mod powermod_g_p, powermod_y_p;
      Fixed_Exponent_Power_Mod powermod_x_p;
   };

/*************************************************
* Default_ELG_Op Constructor                     *
*************************************************/
Default_ELG_Op::Default_ELG_Op(const DL_Group& group, const BigInt& y,
                               const BigInt& x) : p(group.get_p())
   {
   powermod_g_p = Fixed_Base_Power_Mod(group.get_g(), p);
   powermod_y_p = Fixed_Base_Power_Mod(y, p);

   if(x != 0)
      powermod_x_p = Fixed_Exponent_Power_Mod(x, p);
   }

/*************************************************
* Default ElGamal Encrypt Operation              *
*************************************************/
SecureVector<byte> Default_ELG_Op::encrypt(const byte in[], u32bit length,
                                           const BigInt& k) const
   {
   BigInt m(in, length);
   if(m >= p)
      throw Invalid_Argument("Default_ELG_Op::encrypt: Input is too large");

   BigInt a = powermod_g_p(k);
   BigInt b = mul_mod(m, powermod_y_p(k), p);

   SecureVector<byte> output(2*p.bytes());
   a.binary_encode(output + (p.bytes() - a.bytes()));
   b.binary_encode(output + output.size() / 2 + (p.bytes() - b.bytes()));
   return output;
   }

/*************************************************
* Default ElGamal Decrypt Operation              *
*************************************************/
BigInt Default_ELG_Op::decrypt(const BigInt& a, const BigInt& b) const
   {
   if(a >= p || b >= p)
      throw Invalid_Argument("Default_ELG_Op: Invalid message");

   return mul_mod(b, inverse_mod(powermod_x_p(a), p), p);
   }

/*************************************************
* Default DH Operation                           *
*************************************************/
class Default_DH_Op : public DH_Operation
   {
   public:
      BigInt agree(const BigInt& i) const { return powermod_x_p(i); }
      DH_Operation* clone() const { return new Default_DH_Op(*this); }

      Default_DH_Op(const DL_Group& group, const BigInt& x) :
         powermod_x_p(x, group.get_p()) {}
   private:
      const Fixed_Exponent_Power_Mod powermod_x_p;
   };

}

/*************************************************
* Acquire an IF op                               *
*************************************************/
IF_Operation* Default_Engine::if_op(const BigInt& e, const BigInt& n,
                                    const BigInt& d, const BigInt& p,
                                    const BigInt& q, const BigInt& d1,
                                    const BigInt& d2, const BigInt& c) const
   {
   return new Default_IF_Op(e, n, d, p, q, d1, d2, c);
   }

/*************************************************
* Acquire a DSA op                               *
*************************************************/
DSA_Operation* Default_Engine::dsa_op(const DL_Group& group, const BigInt& y,
                                      const BigInt& x) const
   {
   return new Default_DSA_Op(group, y, x);
   }

/*************************************************
* Acquire a NR op                                *
*************************************************/
NR_Operation* Default_Engine::nr_op(const DL_Group& group, const BigInt& y,
                                    const BigInt& x) const
   {
   return new Default_NR_Op(group, y, x);
   }

/*************************************************
* Acquire an ElGamal op                          *
*************************************************/
ELG_Operation* Default_Engine::elg_op(const DL_Group& group, const BigInt& y,
                                      const BigInt& x) const
   {
   return new Default_ELG_Op(group, y, x);
   }

/*************************************************
* Acquire a DH op                                *
*************************************************/
DH_Operation* Default_Engine::dh_op(const DL_Group& group,
                                    const BigInt& x) const
   {
   return new Default_DH_Op(group, x);
   }

}
