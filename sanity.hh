#ifndef __SANITY_HH__
#define __SANITY_HH__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "config.h" // Required for ENABLE_NLS

#include <iosfwd>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "boost/circular_buffer.hpp"
#include "boost/current_function.hpp"

#include "i18n.h"
#include "mt-stdint.h"
#include "quick_alloc.hh" // to get the QA() macro

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

class informative_failure : public std::exception {
  std::string const whatmsg;
public:
  explicit informative_failure(std::string const & s) : whatmsg(s) {};
  virtual ~informative_failure() throw() {};
  virtual char const * what() const throw() { return whatmsg.c_str(); }
};

class MusingI;

class format_base;
struct plain_format;
struct i18n_format;

struct sanity {
  sanity();
  virtual ~sanity();
  virtual void initialize(int, char **, char const *);
  void dump_buffer();
  void set_debug();
  void set_brief();
  void set_quiet();
  void set_reallyquiet();

  bool debug;
  bool brief;
  bool quiet;
  bool reallyquiet;
  boost::circular_buffer<char> logbuf;
  std::string filename;
  std::string gasp_dump;
  bool already_dumping;
  std::vector<MusingI const *> musings;

  void log(plain_format const & fmt,
           char const * file, int line);
  void progress(i18n_format const & fmt,
                char const * file, int line);
  void warning(i18n_format const & fmt,
               char const * file, int line);
  void naughty_failure(std::string const & expr, i18n_format const & explain,
                       std::string const & file, int line) NORETURN;
  void error_failure(std::string const & expr, i18n_format const & explain,
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
  std::string do_format(format_base const & fmt,
                        char const * file, int line);
  virtual void inform_log(std::string const &msg) = 0;
  virtual void inform_message(std::string const &msg) = 0;
  virtual void inform_warning(std::string const &msg) = 0;
  virtual void inform_error(std::string const &msg) = 0;
};

extern sanity & global_sanity;

typedef std::runtime_error oops;

// This hides boost::format from infecting every source file. Instead, we
// implement a single very small formatter.

class
format_base
{
protected:
  struct impl;
  impl *pimpl;

  format_base() : pimpl(NULL) {}
  ~format_base();
  format_base(format_base const & other);
  format_base & operator=(format_base const & other);
  explicit format_base(char const * pattern);
  explicit format_base(std::string const & pattern);
  explicit format_base(char const * pattern, std::locale const & loc);
  explicit format_base(std::string const & pattern, std::locale const & loc);
public:
  // It is a lie that these are const; but then, everything about this
  // class is a lie.
  std::ostream & get_stream() const;
  void flush_stream() const;
  void put_and_flush_signed(int64_t const & s) const;
  void put_and_flush_signed(int32_t const & s) const;
  void put_and_flush_signed(int16_t const & s) const;
  void put_and_flush_signed(int8_t const & s) const;
  void put_and_flush_unsigned(uint64_t const & u) const;
  void put_and_flush_unsigned(uint32_t const & u) const;
  void put_and_flush_unsigned(uint16_t const & u) const;
  void put_and_flush_unsigned(uint8_t const & u) const;
  void put_and_flush_float(float const & f) const;
  void put_and_flush_double(double const & d) const;

  std::string str() const;
};


struct
plain_format
  : public format_base
{
  plain_format()
  {}

  explicit plain_format(char const * pattern)
    : format_base(pattern)
  {}

  explicit plain_format(std::string const & pattern)
    : format_base(pattern)
  {}
};

template<typename T> inline plain_format const & 
operator %(plain_format const & f, T const & t)
{
  f.get_stream() << t;
  f.flush_stream();
  return f;
}

template<typename T> inline plain_format const & 
operator %(const plain_format & f, T & t)
{
  f.get_stream() << t;
  f.flush_stream();
  return f;
}

template<typename T> inline plain_format & 
operator %(plain_format & f, T const & t)
{
  f.get_stream() << t;
  f.flush_stream();
  return f;
}

template<typename T> inline plain_format & 
operator %(plain_format & f, T & t)
{
  f.get_stream() << t;
  f.flush_stream();
  return f;
}

#define SPECIALIZED_OP(format_ty, specialize_arg_ty, overload_arg_ty, s)  \
template <> inline format_ty &                                            \
operator %<specialize_arg_ty>(format_ty & f, overload_arg_ty & a)         \
{                                                                         \
  f.put_and_flush_ ## s (a);                                              \
  return f;                                                               \
}

#define ALL_CONST_VARIANTS(fmt_ty, arg_ty, stem) \
SPECIALIZED_OP(      fmt_ty, arg_ty,       arg_ty, stem) \
SPECIALIZED_OP(      fmt_ty, arg_ty, const arg_ty, stem) \
SPECIALIZED_OP(const fmt_ty, arg_ty,       arg_ty, stem) \
SPECIALIZED_OP(const fmt_ty, arg_ty, const arg_ty, stem)

ALL_CONST_VARIANTS(plain_format, int64_t, signed)
ALL_CONST_VARIANTS(plain_format, int32_t, signed)
ALL_CONST_VARIANTS(plain_format, int16_t, signed)
ALL_CONST_VARIANTS(plain_format, int8_t, signed)

ALL_CONST_VARIANTS(plain_format, uint64_t, unsigned)
ALL_CONST_VARIANTS(plain_format, uint32_t, unsigned)
ALL_CONST_VARIANTS(plain_format, uint16_t, unsigned)
ALL_CONST_VARIANTS(plain_format, uint8_t, unsigned)

ALL_CONST_VARIANTS(plain_format, float, float)
ALL_CONST_VARIANTS(plain_format, double, double)


struct
i18n_format
  : public format_base
{
  i18n_format() {}
  explicit i18n_format(const char * localized_pattern);
  explicit i18n_format(std::string const & localized_pattern);
};

template<typename T> inline i18n_format const & 
operator %(i18n_format const & f, T const & t)
{
  f.get_stream() << t;
  f.flush_stream();
  return f;
}

template<typename T> inline i18n_format const & 
operator %(i18n_format const & f, T & t)
{
  f.get_stream() << t;
  f.flush_stream();
  return f;
}

template<typename T> inline i18n_format & 
operator %(i18n_format & f, T const & t)
{
  f.get_stream() << t;
  f.flush_stream();
  return f;
}

template<typename T> inline i18n_format & 
operator %(i18n_format & f, T & t)
{
  f.get_stream() << t;
  f.flush_stream();
  return f;
}

ALL_CONST_VARIANTS(i18n_format, int64_t, signed)
ALL_CONST_VARIANTS(i18n_format, int32_t, signed)
ALL_CONST_VARIANTS(i18n_format, int16_t, signed)
ALL_CONST_VARIANTS(i18n_format, int8_t, signed)

ALL_CONST_VARIANTS(i18n_format, uint64_t, unsigned)
ALL_CONST_VARIANTS(i18n_format, uint32_t, unsigned)
ALL_CONST_VARIANTS(i18n_format, uint16_t, unsigned)
ALL_CONST_VARIANTS(i18n_format, uint8_t, unsigned)

ALL_CONST_VARIANTS(i18n_format, float, float)
ALL_CONST_VARIANTS(i18n_format, double, double)

#undef ALL_CONST_VARIANTS
#undef SPECIALIZED_OP

std::ostream & operator<<(std::ostream & os, format_base const & fmt);

// F is for when you want to build a boost formatter for display
i18n_format F(const char * str);

// FP is for when you want to build a boost formatter for displaying a plural
i18n_format FP(const char * str1, const char * strn, unsigned long count);

// FL is for when you want to build a boost formatter for the developers -- it
// is not gettextified.  Think of the L as "literal" or "log".
plain_format FL(const char * str);

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


// remove_reference is a workaround for C++ defect #106.
template <typename T>
struct remove_reference {
  typedef T type;
};

template <typename T>
struct remove_reference <T &> {
  typedef typename remove_reference<T>::type type;
};


template <typename T>
class Musing : public MusingI, private MusingBase
{
public:
  Musing(typename remove_reference<T>::type const & obj, char const * name, char const * file, int line, char const * func)
    : MusingBase(name, file, line, func), obj(obj) {}
  virtual void gasp(std::string & out) const;
private:
  typename remove_reference<T>::type const & obj;
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

// This is to be used for objects that should stay on the musings list
// even after the caller returns.  Note that all PERM_MM objects must
// be before all MM objects on the musings list, or you will get an
// invariant failure.  (In other words, don't use PERM_MM unless you
// are sanity::initialize.)
#define PERM_MM(obj) \
  new Musing<typeof(obj)>(*(new remove_reference<typeof(obj)>::type(obj)), \
                          #obj, __FILE__, __LINE__, BOOST_CURRENT_FUNCTION)

#else
#define MM(obj) /* */
#define PERM_MM(obj) /* */
#endif

template <typename T>
void dump(T const &, std::string &);

template <> void dump(std::string const & obj, std::string & out);

// debugging utility to dump out vars like MM but without requiring a crash

template <typename T> void
dump(T const & t, std::string var, 
     std::string const & file, int const line, std::string const & func)
{
  std::string out;
  dump(t, out);
  std::cout << (FL("----- begin '%s' (in %s, at %s:%d)") 
                % var % func % file % line) << std::endl
            << out << std::endl
            << (FL("-----   end '%s' (in %s, at %s:%d)") 
                % var % func % file % line) << std::endl << std::endl;
};

#define DUMP(foo) dump(foo, #foo, __FILE__, __LINE__, BOOST_CURRENT_FUNCTION)

//////////////////////////////////////////////////////////////////////////
// Local Variables:
// mode: C++
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
//////////////////////////////////////////////////////////////////////////

#endif // __SANITY_HH__
