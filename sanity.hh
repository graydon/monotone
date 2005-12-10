// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
#ifndef __SANITY_HH__
#define __SANITY_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <stdexcept>
#include <string>
#include <vector>

#include "boost/format.hpp"
#include "boost/circular_buffer.hpp"
#include "boost/current_function.hpp"

#include <config.h> // Required for ENABLE_NLS
#include "i18n.h"
#include "ui.hh"

#include "quick_alloc.hh" // to get the QA() macro

#include "paths.hh"

#ifdef __GNUC__
#define NORETURN __attribute__((noreturn))
#else
#define NORETURN
#endif

// our assertion / sanity / error logging system *was* based on GNU Nana,
// but we're only using a small section of it, and have anyways rewritten
// that to use typesafe boost-formatter stuff.

// this is for error messages where we want a clean and inoffensive error
// message to make it to the user, not a diagnostic error indicating
// internal failure but a suggestion that they do something differently.

struct informative_failure {
  informative_failure(std::string const & s) : what(s) {}
  std::string what;
};

class MusingI;

struct sanity {
  sanity();
  ~sanity();
  void dump_buffer();
  void set_debug();
  void set_brief();
  void set_quiet();
  void set_relaxed(bool rel);

  bool debug;
  bool brief;
  bool quiet;
  bool relaxed;
  boost::circular_buffer<char> logbuf;
  system_path filename;
  std::string gasp_dump;
  bool already_dumping;
  bool clean_shutdown;
  std::vector<MusingI const *> musings;

  void log(boost::format const & fmt, 
           char const * file, int line);
  void progress(boost::format const & fmt,
                char const * file, int line);
  void warning(boost::format const & fmt, 
               char const * file, int line);
  void naughty_failure(std::string const & expr, boost::format const & explain, 
                       std::string const & file, int line) NORETURN;
  void error_failure(std::string const & expr, boost::format const & explain, 
                     std::string const & file, int line) NORETURN;
  void invariant_failure(std::string const & expr, 
                         std::string const & file, int line) NORETURN;
  void index_failure(std::string const & vec_expr, 
                     std::string const & idx_expr, 
                     unsigned long sz, 
                     unsigned long idx,
                     std::string const & file, int line) NORETURN;
  void gasp();

private:
  std::string do_format(boost::format const & fmt,
                        char const * file, int line);
};

typedef std::runtime_error oops;

extern sanity global_sanity;

// F is for when you want to build a boost formatter for display
boost::format F(const char * str);

// FP is for when you want to build a boost formatter for displaying a plural
boost::format FP(const char * str1, const char * strn, unsigned long count);

// L is for logging, you can log all you want
#define L(fmt) global_sanity.log(fmt, __FILE__, __LINE__)

// P is for progress, log only stuff which the user might
// normally like to see some indication of progress of
#define P(fmt) global_sanity.progress(fmt, __FILE__, __LINE__)

// W is for warnings, which are handled like progress only
// they are only issued once and are prefixed with "warning: "
#define W(fmt) global_sanity.warning(fmt, __FILE__, __LINE__)


// invariants and assertions

#ifdef __GNUC__
#define LIKELY(zz) (__builtin_expect((zz), 1))
#define UNLIKELY(zz) (__builtin_expect((zz), 0))
#else
#define LIKELY(zz) (zz)
#define UNLIKELY(zz) (zz)
#endif

// I is for invariants that "should" always be true
// (if they are wrong, there is a *bug*)
#define I(e) \
do { \
  if(UNLIKELY(!(e))) { \
    global_sanity.invariant_failure("I("#e")", __FILE__, __LINE__); \
  } \
} while(0)

// N is for naughtyness on behalf of the user
// (if they are wrong, the user just did something wrong)
#define N(e, explain)\
do { \
  if(UNLIKELY(!(e))) { \
    global_sanity.naughty_failure("N("#e")", (explain), __FILE__, __LINE__); \
  } \
} while(0)

// E is for errors; they are normal (i.e., not a bug), but not necessarily
// attributable to user naughtiness
#define E(e, explain)\
do { \
  if(UNLIKELY(!(e))) { \
    global_sanity.error_failure("E("#e")", (explain), __FILE__, __LINE__); \
  } \
} while(0)


// we're interested in trapping index overflows early and precisely,
// because they usually represent *very significant* logic errors.  we use
// an inline template function because the idx(...) needs to be used as an
// expression, not as a statement.

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



// Last gasp dumps

class MusingI
{
public:
  MusingI();
  virtual ~MusingI();
  virtual void gasp(std::string & out) const = 0;
};


class MusingBase
{
  char const * name;
  char const * file;
  char const * func;
  int line;

protected:
  MusingBase(char const * name, char const * file, int line, char const * func)
    : name(name), file(file), func(func), line(line)  {}

  void gasp_head(std::string & out) const;
  void gasp_body(const std::string & objstr, std::string & out) const;
};


template <typename T>
class Musing : public MusingI, private MusingBase
{
public:
  Musing(T const & obj, char const * name, char const * file, int line, char const * func)
    : MusingBase(name, file, line, func), obj(obj) {}
  virtual void gasp(std::string & out) const;
private:
  T const & obj;
};

// The header line must be printed into the "out" string before
// dump() is called.
// This is so that even if the call to dump() throws an error,
// the header line ("----- begin ...") will be printed.
// If these calls are collapsed into one, then *no* identifying
// information will be printed in the case of dump() throwing.
// Having the header line without the body is still useful, as it
// provides some semblance of a backtrace.
template <typename T> void
Musing<T>::gasp(std::string & out) const
{
  std::string tmp;
  MusingBase::gasp_head(out);
  dump(obj, tmp);

  MusingBase::gasp_body(tmp, out);
}

// Yes, this is insane.  No, it doesn't work if you do something more sane.
// ## explicitly skips macro argument expansion on the things passed to it.
// Therefore, if we simply did foo ## __LINE__, we would get foo__LINE__ in
// the output.  In fact, even if we did real_M(obj, __LINE__), we would get
// foo__LINE__ in the output.  (## substitutes arguments, but does not expand
// them.)  However, while fake_M does nothing directly, it doesn't pass its
// line argument to ##; therefore, its line argument is fully expanded before
// being passed to real_M.
#ifdef HAVE_TYPEOF
// even worse, this is g++ only!
#define real_M(obj, line) Musing<typeof(obj)> this_is_a_musing_fnord_object_ ## line (obj, #obj, __FILE__, __LINE__, BOOST_CURRENT_FUNCTION)
#define fake_M(obj, line) real_M(obj, line)
#define MM(obj) fake_M(obj, __LINE__)
#else
#define MM(obj) /* */ 
#endif

void dump(std::string const & obj, std::string & out);

#endif // __SANITY_HH__
