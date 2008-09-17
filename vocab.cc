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
#include "char_classifiers.hh"
#include "transforms.hh"

using std::string;

// verifiers for various types of data

// Every ENCODING and ATOMIC type not defined with the _NOVERIFY variant in
// vocab_terms.hh must have a verify function defined here.  DECORATE types
// use the verify function of their inner type.

// ENCODING types ... hexenc<id> has a fixed size, hexenc<other> doesn't.
template <typename INNER>
inline void
verify(hexenc<INNER> const & val)
{
  for (string::const_iterator i = val().begin(); i != val().end(); ++i)
    {
      N(is_xdigit(*i),
        F("bad character '%c' in '%s'") % *i % val);
    }
}

template <>
inline void
verify(hexenc<id> const & val)
{
  if (val().empty())
    return;

  N(val().size() == constants::idlen,
    F("hex encoded ID '%s' size != %d") % val % constants::idlen);
  for (string::const_iterator i = val().begin(); i != val().end(); ++i)
    {
      N(is_xdigit(*i),
        F("bad character '%c' in id name '%s'") % *i % val);
    }
}

// ATOMIC types ...
inline void
verify(id & val)
{
  if (val().empty())
    return;

  N(val().size() == constants::idlen_bytes,
    F("invalid ID '%s'") % val);
}

inline void
verify(symbol const & val)
{
  for (string::const_iterator i = val().begin(); i != val().end(); ++i)
    {
      N(is_alnum(*i) || *i == '_',
        F("bad character '%c' in symbol '%s'") % *i % val);
    }
}

inline void
verify(cert_name const & val)
{
  string::size_type pos = val().find_first_not_of(constants::legal_cert_name_bytes);
  N(pos == string::npos,
    F("bad character '%c' in cert name '%s'") % val().at(pos) % val);
}

inline void
verify(rsa_keypair_id const & val)
{
  string::size_type pos = val().find_first_not_of(constants::legal_key_name_bytes);
  N(pos == string::npos,
    F("bad character '%c' in key name '%s'") % val().at(pos) % val);
}

// These two may modify their argument, to set a more sensible value when
// initializing from the empty string or the default constructor; therefore
// they cannot take a const argument and must be friends with their class.

inline void
verify(netsync_session_key & val)
{
  if (val().empty())
    {
      val.s = std::string(constants::netsync_session_key_length_in_bytes, 0);
      return;
    }

  N(val().size() == constants::netsync_session_key_length_in_bytes,
    F("Invalid key length of %d bytes") % val().length());
}

inline void
verify(netsync_hmac_value & val)
{
  if (val().empty())
    {
      val.s = std::string(constants::netsync_hmac_value_length_in_bytes, 0);
      return;
    }

  N(val().size() == constants::netsync_hmac_value_length_in_bytes,
    F("Invalid hmac length of %d bytes") % val().length());
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
#define ENCODING_NOVERIFY(enc) cc_ENCODING_NOVERIFY(enc)
#define DECORATE(dec) cc_DECORATE(dec)
#define ATOMIC(ty) cc_ATOMIC(ty)
#define ATOMIC_HOOKED(ty,hook) cc_ATOMIC(ty)
#define ATOMIC_NOVERIFY(ty) cc_ATOMIC_NOVERIFY(ty)
#define ATOMIC_BINARY(ty) cc_ATOMIC_BINARY(ty)

#undef EXTERN
#define EXTERN

#include "vocab_terms.hh"

#undef EXTERN
#undef ATOMIC
#undef ATOMIC_HOOKED
#undef ATOMIC_NOVERIFY
#undef DECORATE
#undef ENCODING
#undef ENCODING_NOVERIFY

template void dump(revision_id const & r, string &);
template void dump(manifest_id const & r, string &);
template void dump(file_id const & r, string &);
template void dump(hexenc<id> const & r, string &);
template void dump(rsa_pub_key const&, string &);
template void dump(roster_data const & d, string &);
template void dump(roster_delta const & d, string &);
template void dump(manifest_data const & d, string &);
template void dump(revision_data const & d, string &);

template std::ostream & operator<< <>(std::ostream &,    epoch<id> const &);
template std::ostream & operator<< <>(std::ostream &,     file<id> const &);
template std::ostream & operator<< <>(std::ostream &,   hexenc<id> const &);
template std::ostream & operator<< <>(std::ostream &,      key<id> const &);
template std::ostream & operator<< <>(std::ostream &, manifest<id> const &);
template std::ostream & operator<< <>(std::ostream &, revision<id> const &);
template std::ostream & operator<< <>(std::ostream &,   roster<id> const &);

template std::ostream & operator<< <>(std::ostream &,    epoch<data> const &);
template std::ostream & operator<< <>(std::ostream &,     file<data> const &);
template std::ostream & operator<< <>(std::ostream &, manifest<data> const &);
template std::ostream & operator<< <>(std::ostream &, revision<data> const &);
template std::ostream & operator<< <>(std::ostream &,   roster<data> const &);

/*
 * specializations for id, which allows the encoded id
 * to be dumped out as a human readable, hex encoded
 * string.
 */
template <>
void dump (id const & obj, std::string & out)
{
  out = encode_hexenc(obj());
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
