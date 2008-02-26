// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.


#include "base.hh"
#include "constants.hh"
#include "hash_map.hh"
#include "sanity.hh"
#include "vocab.hh"
#include "transforms.hh"

using std::string;

// verifiers for various types of data

// the verify() stuff gets a little complicated; there doesn't seem to be a
// really nice way to achieve what we want with c++'s type system.  the
// problem is this: we want to give verify(file_path) and verify(local_path)
// access to the internals of file_path and local_path, i.e. make them
// friends, so they can normalize the file paths they're given.  this means
// that verify() needs to be declared publically, so that the definition of
// these classes can refer to them.  it also means that they -- and all other
// ATOMIC types -- cannot fall back on a templated version of verify if no
// other version is defined, because, well, the friend thing and the template
// thing just don't work out, as far as I can tell.  So, every ATOMIC type
// needs an explicitly defined verify() function, so we have both ATOMIC() and
// ATOMIC_NOVERIFY() macros, the latter of which defines a type-specific noop
// verify function.  DECORATE and ENCODING, on the other hand, cannot make use
// of a trick like these, because they are template types themselves, and we
// want to be able to define verify(hexenc<id>) without defining
// verify(hexenc<data>) at the same time, for instance.  Fortunately, these
// types never need to be friends with their verify functions (yet...), so we
// _can_ use a templated fallback function.  This templated function is used
// _only_ by DECORATE and ENCODING; it would be nice to make it take an
// argument of type T1<T2> to document that, but for some reason that doesn't
// work either.
template <typename T>
static inline void
verify(T & val)
{}

template <typename T>
static inline void
verify_full(T & val)
{ val.ok = true; }

// NOTE: _not_ verify_full; you use verify_full for ATOMICs, verify() for
// everything else.
inline void
verify(hexenc<id> & val)
{
  if (val.ok)
    return;

  if (val().empty())
    return;

  N(val().size() == constants::idlen,
    F("hex encoded ID '%s' size != %d") % val % constants::idlen);
  for (string::const_iterator i = val().begin(); i != val().end(); ++i)
    {
      N(is_xdigit(*i),
        F("bad character '%c' in id name '%s'") % *i % val);
    }
  val.ok = true;
}

inline void
verify(id & val)
{
  if (val.ok)
    return;

  if (val().empty())
    return;

  N(val().size() == constants::sha1_digest_length,
    F("binary ID '%s' size != %d")
      % encode_hexenc(val())
      % constants::sha1_digest_length);
  val.ok = true;
}

inline void
verify_full(symbol & val)
{
  for (string::const_iterator i = val().begin(); i != val().end(); ++i)
    {
      N(is_alnum(*i) || *i == '_',
        F("bad character '%c' in symbol '%s'") % *i % val);
    }

  val.ok = true;
}

inline void
verify_full(cert_name & val)
{
  string::size_type pos = val().find_first_not_of(constants::legal_cert_name_bytes);
  N(pos == string::npos,
    F("bad character '%c' in cert name '%s'") % val().at(pos) % val);

  val.ok = true;
}

inline void
verify_full(rsa_keypair_id & val)
{
  string::size_type pos = val().find_first_not_of(constants::legal_key_name_bytes);
  N(pos == string::npos,
    F("bad character '%c' in key name '%s'") % val().at(pos) % val);

  val.ok = true;
}

inline void
verify_full(netsync_session_key & val)
{
  if (val().size() == 0)
    {
      val.s = std::string(constants::netsync_session_key_length_in_bytes, 0);
      return;
    }

  N(val().size() == constants::netsync_session_key_length_in_bytes,
    F("Invalid key length of %d bytes") % val().length());

  val.ok = true;
}

inline void
verify_full(netsync_hmac_value & val)
{
  if (val().size() == 0)
    {
      val.s = std::string(constants::netsync_hmac_value_length_in_bytes, 0);
      return;
    }

  N(val().size() == constants::netsync_hmac_value_length_in_bytes,
    F("Invalid hmac length of %d bytes") % val().length());

  val.ok = true;
}


// Note that ATOMIC types each keep a static symbol-table object and a
// counter of activations, and when there is an activation, the
// members of the ATOMIC type initialize their internal string using a
// copy of the string found in the symtab. Since some (all?) C++
// string implementations are copy-on-write, this has the affect
// of making the ATOMIC(foo) values constructed within a symbol table
// scope share string storage.
struct
symtab_impl
{
  typedef hashmap::hash_set<string> hset;
  hset vals;
  symtab_impl() : vals() {}
  void clear() { vals.clear(); }
  string const & unique(string const & in)
  {
    // This produces a pair <iter,bool> where iter points to an
    // element of the table; the bool indicates whether the element is
    // new, but we don't actually care. We just want the iter.
    return *(vals.insert(in).first);
  }
};

// Sometimes it's handy to have a non-colliding, meaningless id.

id
fake_id()
{
  static u32 counter = 0;
  ++counter;
  I(counter >= 1); // detect overflow
  string s((FL("00000000000000000000000000000000%08x") % counter).str());
  return id(decode_hexenc(s));
}

// instantiation of various vocab functions



#include "vocab_macros.hh"
#define ENCODING(enc) cc_ENCODING(enc)
#define DECORATE(dec) cc_DECORATE(dec)
#define ATOMIC(ty) cc_ATOMIC(ty)
#define ATOMIC_NOVERIFY(ty) cc_ATOMIC_NOVERIFY(ty)
#define ATOMIC_BINARY(ty) cc_ATOMIC_BINARY(ty)

#ifdef EXTERN
#undef EXTERN
#endif
#define EXTERN

#include "vocab_terms.hh"

#undef EXTERN
#undef ATOMIC
#undef DECORATE


template
void dump<rsa_pub_key>(base64<rsa_pub_key> const&, string &);

template
void dump(roster_data const & d, string &);

template
void dump(roster_delta const & d, string &);

template
void dump(manifest_data const & d, string &);

/*
 * specializations for hexenc<id>, which allows the encoded id
 * to be dumped out, even if id itself must never be dumped
 * because it's binary.
 */
template <>
void dump <class id>(hexenc<id> const & obj, std::string & out)
{
  out = encode_hexenc(obj());
}

template <>
std::ostream & 
operator<< <class id>(std::ostream & out, hexenc<id> const & hex_id)
{
  return (out << encode_hexenc(hex_id()));
}


/*
 * specializations for decorated binary values, which shouldn't
 * be dumped out as binaries.
 *
 * FIXME: these shouldn't be necessary. Heck, you shouldn't even
 *        use them. They are here to tell you *where* you are
 *        calling them. Otherwise, you'd just get a linker error
 *        for undefined references.
 */

template <>
void dump <class id>(revision<id> const & obj, std::string & out)
{
  W(F("shouldn't dump out binary revision_id contents!"));
  *((char*) NULL) = 0;
  out = encode_hexenc(obj.inner()());
}

template <> std::ostream &
operator<< <class id>(std::ostream & out, revision<id> const & id)
{
  W(F("shouldn't dump out binary revision_id contents!"));
  *((char*) NULL) = 0;
  return (out << encode_hexenc(id.inner()()));
}

template <>
void dump <class id>(manifest<id> const & obj, std::string & out)
{
  W(F("shouldn't dump out binary manifest_id contents!"));
  *((char*) NULL) = 0;
  out = encode_hexenc(obj.inner()());
}

template <> std::ostream &
operator<< <class id>(std::ostream & out, manifest<id> const & id)
{
  W(F("shouldn't dump out binary manifest_id contents!"));
  *((char*) NULL) = 0;
  return (out << encode_hexenc(id.inner()()));
}

template <>
void dump <class id>(file<id> const & obj, std::string & out)
{
  W(F("shouldn't dump out binary file_id contents!"));
  *((char*) NULL) = 0;
  out = encode_hexenc(obj.inner()());
}

template <> std::ostream &
operator<< <class id>(std::ostream & out, file<id> const & id)
{
  W(F("shouldn't dump out binary file_id contets!"));
  *((char*) NULL) = 0;
  return (out << encode_hexenc(id.inner()()));
}

#ifdef BUILD_UNIT_TESTS

#include "unit_tests.hh"

UNIT_TEST(vocab, verify_hexenc_id)
{
  // -------- magic empty string and default constructor are okay:
  UNIT_TEST_CHECK(hexenc<id>("")() == "");
  hexenc<id> my_default_id;
  UNIT_TEST_CHECK(my_default_id() == "");

  // -------- wrong length:
  UNIT_TEST_CHECK_THROW(hexenc<id>("a"), informative_failure);
  // 39 letters
  UNIT_TEST_CHECK_THROW(hexenc<id>("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
                    informative_failure);
  // 41 letters
  UNIT_TEST_CHECK_THROW(hexenc<id>("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
                    informative_failure);
  // but 40 is okay
  UNIT_TEST_CHECK(hexenc<id>("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")()
              == "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");

  // -------- bad characters:
  UNIT_TEST_CHECK_THROW(hexenc<id>("g000000000000000000000000000000000000000"), informative_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("h000000000000000000000000000000000000000"), informative_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("G000000000000000000000000000000000000000"), informative_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("H000000000000000000000000000000000000000"), informative_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("*000000000000000000000000000000000000000"), informative_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("`000000000000000000000000000000000000000"), informative_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("z000000000000000000000000000000000000000"), informative_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("Z000000000000000000000000000000000000000"), informative_failure);
  // different positions:
  UNIT_TEST_CHECK_THROW(hexenc<id>("g000000000000000000000000000000000000000"), informative_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("0g00000000000000000000000000000000000000"), informative_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("00g0000000000000000000000000000000000000"), informative_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("000g000000000000000000000000000000000000"), informative_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("0000g00000000000000000000000000000000000"), informative_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("000000000000000000000g000000000000000000"), informative_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("0000000000000000000000g00000000000000000"), informative_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("000000000000000000000000000000g000000000"), informative_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("000000000000000000000000000000000000g000"), informative_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("0000000000000000000000000000000000000g00"), informative_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("00000000000000000000000000000000000000g0"), informative_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("000000000000000000000000000000000000000g"), informative_failure);
  // uppercase hex is bad too!
  UNIT_TEST_CHECK_THROW(hexenc<id>("A000000000000000000000000000000000000000"), informative_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("B000000000000000000000000000000000000000"), informative_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("C000000000000000000000000000000000000000"), informative_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("D000000000000000000000000000000000000000"), informative_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("E000000000000000000000000000000000000000"), informative_failure);
  UNIT_TEST_CHECK_THROW(hexenc<id>("F000000000000000000000000000000000000000"), informative_failure);
  // but lowercase and digits are all fine
  UNIT_TEST_CHECK(hexenc<id>("0123456789abcdef0123456789abcdef01234567")()
              == "0123456789abcdef0123456789abcdef01234567");
}

#endif // BUILD_UNIT_TESTS

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
