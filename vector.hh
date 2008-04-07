#ifndef __VECTOR_HH__
#define __VECTOR_HH__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// we're interested in trapping index overflows early and precisely,
// because they usually represent *very significant* logic errors.  we use
// an inline template function because the idx(...) needs to be used as an
// expression, not as a statement.

// to ensure that our index checkers are always visible when std::vector
// is in use, this header is the only file that should include <vector>;
// all others should include this file instead.

#include <vector>
#include "sanity.hh"
#include "quick_alloc.hh" // to get the QA() macro

template <typename T>
inline T & checked_index(std::vector<T> & v,
                         typename std::vector<T>::size_type i,
                         char const * vec,
                         char const * index,
                         char const * file,
                         int line)
{
  if (UNLIKELY(i >= v.size()))
    global_sanity.index_failure(vec, index, v.size(), i, file, line);
  return v[i];
}

template <typename T>
inline T const & checked_index(std::vector<T> const & v,
                               typename std::vector<T>::size_type i,
                               char const * vec,
                               char const * index,
                               char const * file,
                               int line)
{
  if (UNLIKELY(i >= v.size()))
    global_sanity.index_failure(vec, index, v.size(), i, file, line);
  return v[i];
}

#ifdef QA_SUPPORTED
template <typename T>
inline T & checked_index(std::vector<T, QA(T)> & v,
                         typename std::vector<T>::size_type i,
                         char const * vec,
                         char const * index,
                         char const * file,
                         int line)
{
  if (UNLIKELY(i >= v.size()))
    global_sanity.index_failure(vec, index, v.size(), i, file, line);
  return v[i];
}

template <typename T>
inline T const & checked_index(std::vector<T, QA(T)> const & v,
                               typename std::vector<T>::size_type i,
                               char const * vec,
                               char const * index,
                               char const * file,
                               int line)
{
  if (UNLIKELY(i >= v.size()))
    global_sanity.index_failure(vec, index, v.size(), i, file, line);
  return v[i];
}
#endif // QA_SUPPORTED

#define idx(v, i) checked_index((v), (i), #v, #i, __FILE__, __LINE__)

template <typename T> void
dump(std::vector<T> const & vec, std::string & out)
{
  for (size_t i = 0; i < vec.size(); ++i)
    {
      T const & val = vec[i];
      std::string msg;
      dump(val, msg);
      out += msg;
    }
};

// Local Variables:
// mode: C++
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __VECTOR_HH__
