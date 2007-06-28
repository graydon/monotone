#ifndef __STRING_QUEUE_HH__
#define __STRING_QUEUE_HH__

// Copyright (C) 2005 Eric Anderson <anderse@hpl.hp.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <cstring>
#include <cstdlib>

#include "sanity.hh"

// a class for handling inserting at the back of a string and removing
// from the front efficiently while still preserving the data as
// contiguous; could also do the reverse, but that wasn't needed, and
// so hasn't been implemented.

class string_queue
{
public:
  string_queue (size_t default_size = 8192)
    {
      buf = new char[default_size];
      front = back = buf;
      end = buf + default_size;
    }

  ~string_queue ()
    {
      delete[]buf;
    }

  void append (const std::string & v)
    {
      selfcheck ();
      reserve_additional (v.size ());
      simple_append (v);
      if (do_selfcheck)
        {
          selfcheck_str.append (v);
          selfcheck ();
        }
    };

  void append (const char *str, size_t bytes)
    {
      selfcheck ();
      reserve_additional (bytes);
      simple_append (str, bytes);
      if (do_selfcheck)
        {
          selfcheck_str.append (std::string (str, bytes));
          selfcheck ();
        }
    };

  void append (const char v)
    {
      selfcheck ();
      if (available_size () >= 1)
        {
          *back = v;
          ++back;
        }
      else
        {
          std::string tmp;
          tmp += v;
          I (tmp.size () == 1 && tmp[0] == v);
          append (tmp);
        }
      if (do_selfcheck)
        {
          selfcheck_str += v;
          selfcheck ();
        }
    }

  string_queue & operator+= (const char v)
    {
      append (v);
      return *this;
    }

  string_queue & operator+= (const std::string & v)
    {
      append (v);
      return *this;
    }

  char &operator[] (size_t pos)
    {
      I (pos < used_size ());
      return front[pos];
    }

  const char &operator[] (size_t pos) const
    {
      I (pos < used_size ());
      return front[pos];
    }

  void pop_front (size_t amount)
    {
      selfcheck ();
      I (used_size () >= amount);
      front += amount;
      if (front == back)
        {
          front = back = buf;
        }
      if (used_size () * 3 < buffer_size () && buffer_size () > (1024 * 1024))
        {
          // don't bother shrinking unless it will help a lot, and
          // we're using enough memory to care.
          size_t a_new_size = (size_t) (used_size () * 1.1);	// leave some headroom
          resize_buffer (std::max ((size_t) 8192, a_new_size));
        }
      if (do_selfcheck)
        {
          selfcheck_str.erase (0, amount);
          selfcheck ();
        }
    }

  std::string substr (size_t pos, size_t size) const
    {
      I (size <= max_string_queue_incr);
      I (pos <= max_string_queue_size);
      I (used_size () >= (pos + size));
      return std::string (front + pos, size);
    }

  const char *front_pointer (size_t strsize) const
    {
      I (strsize <= max_string_queue_size);
      I (used_size () >= strsize);
      return front;
    }

  size_t size () const
    {
      return used_size ();
    }
  size_t used_size () const
    {
      return (size_t) (back - front);
    }
  size_t buffer_size () const
    {
      return (size_t) (end - buf);
    }
  size_t available_size () const
    {
      return (size_t) (end - back);
    }
  bool empty () const
    {
      return front == back;
    }

  void selfcheck ()
    {
      if (do_selfcheck)
        {
          I (buf <= front && front <= back && back <= end);
          I (selfcheck_str.size () == used_size ()
             && std::memcmp (selfcheck_str.data (), front, used_size ()) == 0);
        }
    }

  void reserve_total (size_t amount)
    {
      if ((size_t) (end - front) >= amount)
        {
          return;
        }
      reserve_additional (amount - available_size ());
    }

  void reserve_additional (size_t amount)
    {
      I(amount <= max_string_queue_incr);
      if (available_size () >= amount)
        return;
      if (1.25 * (used_size () + amount) < buffer_size ())
        {
          // 1.25* to make sure that we don't do a lot of remove 1 byte from
          // beginning, move entire array, append a byte, etc.
          size_t save_used_size = used_size ();
          std::memmove (buf, front, save_used_size);
          front = buf;
          back = front + save_used_size;
          selfcheck ();
          return;
        }
      // going to expand the buffer, increase by at least 1.25x so that
      // we don't have a pathological case of reserving a little extra
      // a whole bunch of times
      size_t new_buffer_size =
        std::max ((size_t) (1.25 * buffer_size ()),
                  used_size () + amount);
      resize_buffer (new_buffer_size);
      selfcheck ();
    }

protected:
  void simple_append (const std::string & v)
    {
      I ((size_t) (end - back) >= v.size ());
      I (v.size() <= max_string_queue_incr);
      std::memcpy (back, v.data (), v.size ());
      back += v.size ();
    }

  void simple_append (const char *str, size_t bytes)
    {
      I ((size_t) (end - back) >= bytes);
      I (bytes <= max_string_queue_incr);
      std::memcpy (back, str, bytes);
      back += bytes;
    }

  void resize_buffer (size_t new_buffer_size)
    {
      I (new_buffer_size <= max_string_queue_size);
      size_t save_used_size = used_size ();
      char *newbuf = new char[new_buffer_size];
      std::memcpy (newbuf, front, save_used_size);
      delete[]buf;
      buf = front = newbuf;
      back = front + save_used_size;
      end = buf + new_buffer_size;
    }

private:
  static const bool do_selfcheck = false;
  // used to avoid integer wraparound, 500 megs should be enough:
  static const size_t max_string_queue_size = 500 * 1024 * 1024;
  static const size_t max_string_queue_incr = 500 * 1024 * 1024;
  string_queue (string_queue & from)
    {
      std::abort ();
    }
  char *buf, *front, *back, *end;
  std::string selfcheck_str;
};

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
