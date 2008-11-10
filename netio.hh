#ifndef __NETIO_HH__
#define __NETIO_HH__

// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// all network i/o decoding and encoding in netcmd and merkle is done using
// the primitives in this header. it has to be very correct.

#include <boost/static_assert.hpp>

#include "numeric_vocab.hh"
#include "sanity.hh"
#include "string_queue.hh"

inline void
require_bytes(std::string const & str,
              size_t pos,
              size_t len,
              std::string const & name)
{
  // if you've gone past the end of the buffer, there's a logic error,
  // and this program is not safe to keep running. shut down.
  I(pos < str.size() || (pos == str.size() && len == 0));
  // otherwise make sure there's room for this decode operation, but
  // use a recoverable exception type.
  if (len == 0)
    return;
  if (str.size() < pos + len)
    throw bad_decode(F("need %d bytes to decode %s at %d, only have %d")
                     % len % name % pos % (str.size() - pos));
}

inline void
require_bytes(string_queue const & str,
              size_t pos,
              size_t len,
              std::string const & name)
{
  // if you've gone past the end of the buffer, there's a logic error,
  // and this program is not safe to keep running. shut down.
  I(pos < str.size() || (pos == str.size() && len == 0));
  // otherwise make sure there's room for this decode operation, but
  // use a recoverable exception type.
  if (len == 0)
    return;
  if (str.size() < pos + len)
    throw bad_decode(F("need %d bytes to decode %s at %d, only have %d")
                     % len % name % pos % (str.size() - pos));
}

template <typename T>
inline bool
try_extract_datum_uleb128(std::string const & in,
                          size_t & pos,
                          std::string const & name,
                          T & out)
{
  BOOST_STATIC_ASSERT(std::numeric_limits<T>::is_signed == false);
  size_t shift = 0;
  size_t maxbytes = sizeof(T) + 1 + (sizeof(T) / 8);
  out = 0;
  while (maxbytes > 0)
    {
      if (pos >= in.size())
        return false;
      T curr = widen<T,u8>(in[pos]);
      ++pos;
      out |= ((static_cast<u8>(curr)
               & static_cast<u8>(0x7f)) << shift);
      bool finished = ! static_cast<bool>(static_cast<u8>(curr)
                                          & static_cast<u8>(0x80));
      if (finished)
        break;
      else if (maxbytes == 1)
        throw bad_decode(F("overflow while decoding variable length integer '%s' into a %d-byte field")
                         % name % maxbytes);
      else
        {
          --maxbytes;
          shift += 7;
        }
    }
  return true;
}

template <typename T>
inline bool
try_extract_datum_uleb128(string_queue const & in,
                          size_t & pos,
                          std::string const & name,
                          T & out)
{
  BOOST_STATIC_ASSERT(std::numeric_limits<T>::is_signed == false);
  size_t shift = 0;
  size_t maxbytes = sizeof(T) + 1 + (sizeof(T) / 8);
  out = 0;
  while (maxbytes > 0)
    {
      if (pos >= in.size())
        return false;
      T curr = widen<T,u8>(in[pos]);
      ++pos;
      out |= ((static_cast<u8>(curr)
               & static_cast<u8>(0x7f)) << shift);
      bool finished = ! static_cast<bool>(static_cast<u8>(curr)
                                          & static_cast<u8>(0x80));
      if (finished)
        break;
      else if (maxbytes == 1)
        throw bad_decode(F("overflow while decoding variable length integer '%s' into a %d-byte field")
                         % name % maxbytes);
      else
        {
          --maxbytes;
          shift += 7;
        }
    }
  return true;
}

template <typename T>
inline T
extract_datum_uleb128(std::string const & in,
                      size_t & pos,
                      std::string const & name)
{
  T out;
  size_t tpos = pos;
  if (! try_extract_datum_uleb128(in, tpos, name, out))
    throw bad_decode(F("ran out of bytes reading variable length integer '%s' at pos %d")
                     % name % pos);
  pos = tpos;
  return out;
}

template <typename T>
inline void
insert_datum_uleb128(T in, std::string & out)
{
  BOOST_STATIC_ASSERT(std::numeric_limits<T>::is_signed == false);
  size_t maxbytes = sizeof(T) + 1 + (sizeof(T) / 8);
  while (maxbytes > 0)
    {
      u8 item = (static_cast<u8>(in) & static_cast<u8>(0x7f));
      T remainder = in >> 7;
      bool finished = ! static_cast<bool>(remainder);
      if (finished)
        {
          out += item;
          break;
        }
      else
        {
          out += (item | static_cast<u8>(0x80));
          --maxbytes;
          in = remainder;
        }
    }
}

template <typename T>
inline void
insert_datum_uleb128(T in, string_queue & out)
{
  BOOST_STATIC_ASSERT(std::numeric_limits<T>::is_signed == false);
  size_t maxbytes = sizeof(T) + 1 + (sizeof(T) / 8);
  while (maxbytes > 0)
    {
      u8 item = (static_cast<u8>(in) & static_cast<u8>(0x7f));
      T remainder = in >> 7;
      bool finished = ! static_cast<bool>(remainder);
      if (finished)
        {
          out += item;
          break;
        }
      else
        {
          out += (item | static_cast<u8>(0x80));
          --maxbytes;
          in = remainder;
        }
    }
}

template <typename T>
inline T
extract_datum_lsb(std::string const & in,
                  size_t & pos,
                  std::string const & name)
{
  size_t nbytes = sizeof(T);
  T out = 0;
  size_t shift = 0;

  require_bytes(in, pos, nbytes, name);

  while (nbytes > 0)
    {
      out |= widen<T,u8>(in[pos++]) << shift;
      shift += 8;
      --nbytes;
    }
  return out;
}

template <typename T>
inline T
extract_datum_lsb(string_queue const & in,
                  size_t & pos,
                  std::string const & name)
{
  size_t nbytes = sizeof(T);
  T out = 0;
  size_t shift = 0;

  require_bytes(in, pos, nbytes, name);

  while (nbytes > 0)
    {
      out |= widen<T,u8>(in[pos++]) << shift;
      shift += 8;
      --nbytes;
    }
  return out;
}

template <typename T>
inline void
insert_datum_lsb(T in, std::string & out)
{
  size_t const nbytes = sizeof(T);
  char tmp[nbytes];
  for (size_t i = 0; i < nbytes; ++i)
    {
      tmp[i] = static_cast<u8>(in) & static_cast<u8>(0xff);
      in >>= 8;
    }
  out.append(std::string(tmp, tmp+nbytes));
}

template <typename T>
inline void
insert_datum_lsb(T in, string_queue & out)
{
  size_t const nbytes = sizeof(T);
  char tmp[nbytes];
  for (size_t i = 0; i < nbytes; ++i)
    {
      tmp[i] = static_cast<u8>(in) & static_cast<u8>(0xff);
      in >>= 8;
    }
  out.append(std::string(tmp, tmp+nbytes));
}

inline void
extract_variable_length_string(std::string const & buf,
                               std::string & out,
                               size_t & pos,
                               std::string const & name,
                               size_t maxlen = std::numeric_limits<size_t>::max())
{
  BOOST_STATIC_ASSERT(sizeof(std::string::size_type) == sizeof(size_t));
  size_t len = extract_datum_uleb128<size_t>(buf, pos, name);
  if (len > maxlen)
    throw bad_decode(F("decoding variable length string of %d bytes for '%s', maximum is %d")
                     % len % name % maxlen);
  require_bytes(buf, pos, len, name);
  out.assign(buf, pos, len);
  pos += len;
}

inline void
insert_variable_length_string(std::string const & in,
                              std::string & buf)
{
  size_t len = in.size();
  insert_datum_uleb128<size_t>(len, buf);
  buf.append(in);
}

inline void
insert_variable_length_string(std::string const & in,
                              string_queue & buf)
{
  size_t len = in.size();
  insert_datum_uleb128<size_t>(len, buf);
  buf.append(in);
}

inline std::string
extract_substring(std::string const & buf,
                  size_t & pos,
                  size_t len,
                  std::string const & name)
{
  require_bytes(buf, pos, len, name);
  std::string tmp = buf.substr(pos, len);
  pos += len;
  return tmp;
}

inline std::string
extract_substring(string_queue const & buf,
                  size_t & pos,
                  size_t len,
                  std::string const & name)
{
  require_bytes(buf, pos, len, name);
  std::string tmp = buf.substr(pos, len);
  pos += len;
  return tmp;
}

inline void
assert_end_of_buffer(std::string const & str,
                     size_t pos,
                     std::string const & name)
{
  if (str.size() != pos)
    throw bad_decode(F("expected %s to end at %d, have %d bytes")
                     % name % pos % str.size());
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __NETIO_HH__
