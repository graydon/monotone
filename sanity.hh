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

#include <stdexcept>
#include <ostream>

#include "boost/current_function.hpp"

#include "i18n.h"
#include "numeric_vocab.hh"

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
  virtual ~sanity();
  virtual void initialize(int, char **, char const *);
  void dump_buffer();
  void set_debug();
  void set_quiet();
  void set_reallyquiet();
  // This takes a bare std::string because we don't want to expose vocab.hh
  // or paths.hh here.
  void set_dump_path(std::string const & path);

  // A couple of places need to look at the debug flag to avoid doing
  // expensive logging if it's off.
  bool debug_p();

  // ??? --quiet overrides any --ticker= setting if both are on the
  // command line (and needs to look at this to do so).
  bool quiet_p();

  void log(plain_format const & fmt,
           char const * file, int line);
  void progress(i18n_format const & fmt,
                char const * file, int line);
  void warning(i18n_format const & fmt,
               char const * file, int line);
  NORETURN(void naughty_failure(char const * expr, i18n_format const & explain,
                       char const * file, int line));
  NORETURN(void error_failure(char const * expr, i18n_format const & explain,
                     char const * file, int line));
  NORETURN(void invariant_failure(char const * expr,
                         char const * file, int line));
  NORETURN(void index_failure(char const * vec_expr,
                     char const * idx_expr,
                     unsigned long sz,
                     unsigned long idx,
                     char const * file, int line));
  void gasp();
  void push_musing(MusingI const *musing);
  void pop_musing(MusingI const *musing);

private:
  std::string do_format(format_base const & fmt,
                        char const * file, int line);
  virtual void inform_log(std::string const &msg) = 0;
  virtual void inform_message(std::string const &msg) = 0;
  virtual void inform_warning(std::string const &msg) = 0;
  virtual void inform_error(std::string const &msg) = 0;

  struct impl;
  impl * imp;
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

  explicit format_base(char const * pattern, bool use_locale);
  explicit format_base(std::string const & pattern, bool use_locale);
public:
  // It is a lie that these are const; but then, everything about this
  // class is a lie.
  std::ostream & get_stream() const;
  void flush_stream() const;
  void put_and_flush_signed(s64 const & s) const;
  void put_and_flush_signed(s32 const & s) const;
  void put_and_flush_signed(s16 const & s) const;
  void put_and_flush_signed(s8  const & s) const;
  void put_and_flush_unsigned(u64 const & u) const;
  void put_and_flush_unsigned(u32 const & u) const;
  void put_and_flush_unsigned(u16 const & u) const;
  void put_and_flush_unsigned(u8  const & u) const;
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
    : format_base(pattern, false)
  {}

  explicit plain_format(std::string const & pattern)
    : format_base(pattern, false)
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

ALL_CONST_VARIANTS(plain_format, s64, signed)
ALL_CONST_VARIANTS(plain_format, s32, signed)
ALL_CONST_VARIANTS(plain_format, s16, signed)
ALL_CONST_VARIANTS(plain_format, s8, signed)

ALL_CONST_VARIANTS(plain_format, u64, unsigned)
ALL_CONST_VARIANTS(plain_format, u32, unsigned)
ALL_CONST_VARIANTS(plain_format, u16, unsigned)
ALL_CONST_VARIANTS(plain_format, u8, unsigned)

ALL_CONST_VARIANTS(plain_format, float, float)
ALL_CONST_VARIANTS(plain_format, double, double)


struct
i18n_format
  : public format_base
{
  i18n_format()
  {}

  explicit i18n_format(const char * localized_pattern)
    : format_base(localized_pattern, true)
  {}

  explicit i18n_format(std::string const & localized_pattern)
    : format_base(localized_pattern, true)
  {}
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

ALL_CONST_VARIANTS(i18n_format, s64, signed)
ALL_CONST_VARIANTS(i18n_format, s32, signed)
ALL_CONST_VARIANTS(i18n_format, s16, signed)
ALL_CONST_VARIANTS(i18n_format, s8, signed)

ALL_CONST_VARIANTS(i18n_format, u64, unsigned)
ALL_CONST_VARIANTS(i18n_format, u32, unsigned)
ALL_CONST_VARIANTS(i18n_format, u16, unsigned)
ALL_CONST_VARIANTS(i18n_format, u8, unsigned)

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

// Last gasp dumps

class MusingI
{
public:
  MusingI() { global_sanity.push_musing(this); }
  virtual ~MusingI() { global_sanity.pop_musing(this); }
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

extern void print_var(std::string const & value,
                      char const * var,
                      char const * file,
                      int const line,
                      char const * func);

template <typename T> void
dump(T const & t, char const *var,
     char const * file, int const line, char const * func)
{
  std::string value;
  dump(t, value);
  print_var(value, var, file, line, func);
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
