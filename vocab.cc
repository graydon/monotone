// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>
#include <boost/filesystem/path.hpp>

#include "vocab.hh"
#include "sanity.hh"

// verifiers for various types of data

using namespace std;

template <typename T>
static inline void verify(T const & val)
{}

template<>
static inline void verify(local_path const & val)
{
  using boost::filesystem::path;
  boost::filesystem::path p(val());

  if (p.has_root_path() || p.has_root_name() || p.has_root_directory())
    throw oops("prohibited absolute path '" + val() + "'");

  for(path::iterator i = p.begin(); i != p.end(); ++i)
    {
      if ( *i == "" && (! p.empty()))
	throw oops("empty path component in '" + val() + "'");
      if (*i == "." || *i == "..")
	throw oops("prohibited path component '" + *i + "' in '" + val() + "'");
      if ( ! boost::filesystem::generic_name(*i))
	throw oops("prohibited path component '" + *i + "' in '" + val() + "'");

      // we're going to make a small exception here for files ending in ",v", since
      // those are RCS files and we want to walk CVS trees to import them. FIXME this
      // is a kludge I am not happy about.
      {
	string s = *i;
	if ( s.size() > 2 &&
	     s.substr(s.size() - 2) == string(",v") &&	     
	     boost::filesystem::posix_name(s.substr(0, s.size() - 2)))
	  continue;
      }

      if ( ! boost::filesystem::posix_name(*i))
	throw oops("non-posix file path component '" + *i + "' in '" + val() + "'");
    }
}

// fwd declare..
bool book_keeping_file(local_path const & path);

template<>
static inline void verify(file_path const & val)
{
  local_path loc(val());
  verify(loc);
  if (book_keeping_file(loc))
    throw oops("prohibited book-keeping path in '" + val() + "'");
}

template<>
static inline void verify(hexenc<id> const & val)
{
  if (val() == "")
    return;
  if (val().size() != 40)
    throw oops("hex encoded ID '" + val() + "' size != 40");
  if (val().find_first_not_of("0123456789abcdef") != string::npos)
    throw oops("non-hex (or non-lowercase) character in ID '" + val() + "'");
}


// instantiation of various vocab functions

#define ATOMIC(ty)                           \
                                             \
ty::ty(string const & str) : s(str)          \
{ verify(*this); }                           \
                                             \
ty::ty(ty const & other) : s(other.s)        \
{ verify(*this); }                           \
                                             \
ty const & ty::operator=(ty const & other)   \
{ s = other.s; verify(*this); return *this; }



#define ENCODING(enc)                                    \
                                                         \
template<typename INNER>                                 \
enc<INNER>::enc(string const & s) : i(s)                 \
  { verify(*this); }                                     \
                                                         \
template<typename INNER>                                 \
enc<INNER>::enc(enc<INNER> const & other)                \
  : i(other.i()) { verify(*this); }                      \
                                                         \
template<typename INNER>                                 \
enc<INNER>::enc(INNER const & inner) : i(inner)          \
  { verify(*this); }                                     \
                                                         \
template<typename INNER>                                 \
enc<INNER> const &                                       \
enc<INNER>::operator=(enc<INNER> const & other)          \
  { i = other.i; verify(*this); return *this;}           


#define DECORATE(dec)                                    \
                                                         \
template<typename INNER>                                 \
dec<INNER>::dec(dec<INNER> const & other)                \
  : i(other.i) { verify(*this); }                        \
                                                         \
template<typename INNER>                                 \
dec<INNER>::dec(INNER const & inner) : i(inner)          \
  { verify(*this); }                                     \
                                                         \
template<typename INNER>                                 \
dec<INNER> const &                                       \
dec<INNER>::operator=(dec<INNER> const & other)          \
  { i = other.i; verify(*this); return *this;}           

#define EXTERN 

#include "vocab_terms.hh"

#undef EXTERN
#undef ATOMIC
#undef DECORATE

template class manifest<cert>;
template class file<cert>;

// the rest is unit tests

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

static void test_file_path_verification()
{
  char const * baddies [] = {"./redundant",
			     "../escape",
			     "foo/../../escape",
			     "foo//nonsense",
			     "/rooted",
			     "c:\\windows\\rooted",
			     "c:/windows/rooted",
			     "c:thing",
			     0 };
  
  for (char const ** c = baddies; *c; ++c)
    BOOST_CHECK_THROW(file_path p(*c), std::runtime_error);      
  
  char const * bad = "!@#$%^&*()+=[]{};:?,<>~`'| \\";
  char badboy[] = "bad";
  for (char const * c = bad; *c; ++c)
    {
      badboy[1] = *c;
      BOOST_CHECK_THROW(file_path p(badboy), std::runtime_error);
    }
  
  for (char c = 1; c < ' '; ++c)
    {
      badboy[1] = c;
      BOOST_CHECK_THROW(file_path p(badboy), std::runtime_error);
    }

  char const * goodies [] = {"unrooted", 
			     "unrooted.txt",
			     "fun_with_underscore.png",
			     "fun-with-hyphen.tiff", 
			     "unrooted/../unescaping",
			     "unrooted/general/path",
			     0 };

  for (char const ** c = goodies; *c; ++c)
    BOOST_CHECK_NOT_THROW(file_path p(*c), std::runtime_error);

}

void add_vocab_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&test_file_path_verification));
}

#endif // BUILD_UNIT_TESTS
