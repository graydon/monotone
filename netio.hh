#ifndef __NETIO_HH__
#define __NETIO_HH__

// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// all network i/o decoding and encoding in netcmd and merkle is done using
// the primitives in this header. it has to be very correct.

#include <boost/format.hpp>
#include <boost/static_assert.hpp>

#include "numeric_vocab.hh"
#include "sanity.hh"

struct bad_decode {
  bad_decode(boost::format const & fmt) : what(fmt.str()) {}
  std::string what;
};

BOOST_STATIC_ASSERT(sizeof(char) == 1);
BOOST_STATIC_ASSERT(CHAR_BIT == 8);

template <typename T>
static inline T read_datum_msb(char const * in)
{
  size_t const nbytes = sizeof(T);
  T out = 0;
  for (size_t i = 0; i < nbytes; ++i)
    {
      out <<= 8;
      out |= (0xff & static_cast<T>(in[i]));
    }
  return out;
}

template <typename T>
static inline void write_datum_msb(T in, std::string & out)
{
  size_t const nbytes = sizeof(T);
  char tmp[nbytes];
  for (size_t i = nbytes; i > 0; --i)
    {
      tmp[i-1] = static_cast<char>(in & 0xff);
      in >>= 8;
    }
  out.append(std::string(tmp, tmp+nbytes));
}

static inline void 
require_bytes(std::string const & str, 
	      size_t pos, 
	      size_t len, 
	      std::string const & name)
{
  // if you've gone past the end of the buffer, there's a logic error,
  // and this program is not safe to keep running. shut down.
  I(pos < str.size());
  // otherwise make sure there's room for this decode operation, but
  // use a recoverable exception type.
  if (str.size() < pos + len)
    throw bad_decode(F("need %d bytes to decode %s at %d, only have %d") 
		     % len % name % pos % (str.size() - pos));
}

static inline std::string extract_substring(std::string const & str, 
					    size_t & pos,
					    size_t len, 
					    std::string const & name)
{
  require_bytes(str, pos, len, name);
  std::string tmp = str.substr(pos, len);
  pos += len;
  return tmp;
}

template <typename T>
static inline T extract_datum_msb(std::string const & str, 
				  size_t & pos, 
				  std::string const & name)
{
  require_bytes(str, pos, sizeof(T), name);
  T tmp = read_datum_msb<T>(str.data() + pos);
  pos += sizeof(T);
  return tmp;  
}

static inline void assert_end_of_buffer(std::string const & str, 
					size_t pos, 
					std::string const & name)
{
  if (str.size() != pos)
    throw bad_decode(F("expected %s to end at %d, have %d bytes") 
		     % name % pos % str.size());
}

#endif // __NETIO_HH__
