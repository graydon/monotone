// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <algorithm>
#include <iterator>
#include <iostream>
#include <fstream>
#include "vector.hh"
#include <sstream>

#include <boost/format.hpp>
#include <boost/circular_buffer.hpp>

#include "lexical_cast.hh"
#include "constants.hh"
#include "platform.hh"
#include "sanity.hh"
#include "simplestring_xform.hh"

using std::exception;
using std::locale;
using std::logic_error;
using std::ofstream;
using std::ostream;
using std::ostream_iterator;
using std::ostringstream;
using std::string;
using std::vector;

using boost::format;

struct sanity::impl
{
  bool debug;
  bool quiet;
  bool reallyquiet;
  boost::circular_buffer<char> logbuf;
  std::string filename;
  std::string gasp_dump;
  bool already_dumping;
  std::vector<MusingI const *> musings;

  impl() :
    debug(false), quiet(false), reallyquiet(false), logbuf(0xffff),
    already_dumping(false)
  {}
};

// debugging / logging system

sanity::~sanity()
{
  if (imp)
    delete imp;
}

void
sanity::initialize(int argc, char ** argv, char const * lc_all)
{
  imp = new impl;

  // set up some marked strings, so even if our logbuf overflows, we'll get
  // this data in a crash.  This (and subclass overrides) are probably the
  // only place PERM_MM should ever be used.

  string system_flavour;
  get_system_flavour(system_flavour);
  PERM_MM(system_flavour);
  L(FL("started up on %s") % system_flavour);

  string cmdline_string;
  {
    ostringstream cmdline_ss;
    for (int i = 0; i < argc; ++i)
      {
        if (i)
          cmdline_ss << ", ";
        cmdline_ss << '\'' << argv[i] << '\'';
      }
    cmdline_string = cmdline_ss.str();
  }
  PERM_MM(cmdline_string);
  L(FL("command line: %s") % cmdline_string);

  if (!lc_all)
    lc_all = "n/a";
  PERM_MM(string(lc_all));
  L(FL("set locale: LC_ALL=%s") % lc_all);
}

void
sanity::dump_buffer()
{
  I(imp);
  if (!imp->filename.empty())
    {
      ofstream out(imp->filename.c_str());
      if (out)
        {
          copy(imp->logbuf.begin(), imp->logbuf.end(),
               ostream_iterator<char>(out));
          copy(imp->gasp_dump.begin(), imp->gasp_dump.end(),
               ostream_iterator<char>(out));
          inform_message((FL("wrote debugging log to %s\n"
                        "if reporting a bug, please include this file")
                       % imp->filename).str());
        }
      else
        inform_message((FL("failed to write debugging log to %s")
                        % imp->filename).str());
    }
  else
    inform_message("discarding debug log, because I have nowhere to write it\n"
                   "(maybe you want --debug or --dump?)");
}

void
sanity::set_debug()
{
  I(imp);
  imp->quiet = false;
  imp->reallyquiet = false;
  imp->debug = true;

  // it is possible that some pre-setting-of-debug data
  // accumulated in the log buffer (during earlier option processing)
  // so we will dump it now
  ostringstream oss;
  vector<string> lines;
  copy(imp->logbuf.begin(), imp->logbuf.end(), ostream_iterator<char>(oss));
  split_into_lines(oss.str(), lines);
  for (vector<string>::const_iterator i = lines.begin(); i != lines.end(); ++i)
    inform_log((*i) + "\n");
}

void
sanity::set_quiet()
{
  I(imp);
  imp->debug = false;
  imp->quiet = true;
  imp->reallyquiet = false;
}

void
sanity::set_reallyquiet()
{
  I(imp);
  imp->debug = false;
  imp->quiet = true;
  imp->reallyquiet = true;
}

void
sanity::set_dump_path(std::string const & path)
{
  I(imp);
  if (imp->filename.empty())
    {
      L(FL("setting dump path to %s") % path);
      imp->filename = path;
    }
}

string
sanity::do_format(format_base const & fmt, char const * file, int line)
{
  try
    {
      return fmt.str();
    }
  catch (exception & e)
    {
      inform_error((F("fatal: formatter failed on %s:%d: %s")
                % file
                % line
                % e.what()).str());
      throw;
    }
}

bool
sanity::debug_p()
{
  if (!imp)
    throw std::logic_error("sanity::debug_p called "
                            "before sanity::initialize");
  return imp->debug;
}

bool
sanity::quiet_p()
{
  if (!imp)
    throw std::logic_error("sanity::quiet_p called "
                            "before sanity::initialize");
  return imp->quiet;
}

bool
sanity::reallyquiet_p()
{
  if (!imp)
    throw std::logic_error("sanity::reallyquiet_p called "
                            "before sanity::initialize");
  return imp->reallyquiet;
}

void
sanity::log(plain_format const & fmt,
            char const * file, int line)
{
  string str = do_format(fmt, file, line);

  if (str.size() > constants::log_line_sz)
    {
      str.resize(constants::log_line_sz);
      if (str.at(str.size() - 1) != '\n')
        str.at(str.size() - 1) = '\n';
    }
  copy(str.begin(), str.end(), back_inserter(imp->logbuf));
  if (str[str.size() - 1] != '\n')
    imp->logbuf.push_back('\n');

  inform_log(str);
}

void
sanity::progress(i18n_format const & i18nfmt,
                 char const * file, int line)
{
  string str = do_format(i18nfmt, file, line);

  if (str.size() > constants::log_line_sz)
    {
      str.resize(constants::log_line_sz);
      if (str.at(str.size() - 1) != '\n')
        str.at(str.size() - 1) = '\n';
    }
  copy(str.begin(), str.end(), back_inserter(imp->logbuf));
  if (str[str.size() - 1] != '\n')
    imp->logbuf.push_back('\n');

  inform_message(str);
}

void
sanity::warning(i18n_format const & i18nfmt,
                char const * file, int line)
{
  string str = do_format(i18nfmt, file, line);

  if (str.size() > constants::log_line_sz)
    {
      str.resize(constants::log_line_sz);
      if (str.at(str.size() - 1) != '\n')
        str.at(str.size() - 1) = '\n';
    }
  string str2 = "warning: " + str;
  copy(str2.begin(), str2.end(), back_inserter(imp->logbuf));
  if (str[str.size() - 1] != '\n')
    imp->logbuf.push_back('\n');

  inform_warning(str);
}

void
sanity::naughty_failure(char const * expr, i18n_format const & explain,
                        char const * file, int line)
{
  string message;
  if (!imp)
    throw std::logic_error("sanity::naughty_failure occured "
                            "before sanity::initialize");
  if (imp->debug)
    log(FL("%s:%d: usage constraint '%s' violated") % file % line % expr,
        file, line);
  prefix_lines_with(_("misuse: "), do_format(explain, file, line), message);
  gasp();
  throw informative_failure(message);
}

void
sanity::error_failure(char const * expr, i18n_format const & explain,
                      char const * file, int line)
{
  string message;
  if (!imp)
    throw std::logic_error("sanity::error_failure occured "
                            "before sanity::initialize");
  if (imp->debug)
    log(FL("%s:%d: detected error '%s' violated") % file % line % expr,
        file, line);
  gasp();
  prefix_lines_with(_("error: "), do_format(explain, file, line), message);
  throw informative_failure(message);
}

void
sanity::invariant_failure(char const * expr, char const * file, int line)
{
  char const * pattern = N_("%s:%d: invariant '%s' violated");
  if (!imp)
    throw std::logic_error("sanity::invariant_failure occured "
                            "before sanity::initialize");
  if (imp->debug)
    log(FL(pattern) % file % line % expr, file, line);
  gasp();
  throw logic_error((F(pattern) % file % line % expr).str());
}

void
sanity::index_failure(char const * vec_expr,
                      char const * idx_expr,
                      unsigned long sz,
                      unsigned long idx,
                      char const * file, int line)
{
  char const * pattern
    = N_("%s:%d: index '%s' = %d overflowed vector '%s' with size %d");
  if (!imp)
    throw std::logic_error("sanity::index_failure occured "
                            "before sanity::initialize");
  if (imp->debug)
    log(FL(pattern) % file % line % idx_expr % idx % vec_expr % sz,
        file, line);
  gasp();
  throw logic_error((F(pattern)
                     % file % line % idx_expr % idx % vec_expr % sz).str());
}

// Last gasp dumps

void
sanity::push_musing(MusingI const *musing)
{
  I(imp);
  if (!imp->already_dumping)
    imp->musings.push_back(musing);
}

void
sanity::pop_musing(MusingI const *musing)
{
  I(imp);
  if (!imp->already_dumping)
    {
      I(imp->musings.back() == musing);
      imp->musings.pop_back();
    }
}


void
sanity::gasp()
{
  if (!imp)
    return;
  if (imp->already_dumping)
    {
      L(FL("ignoring request to give last gasp; already in process of dumping"));
      return;
    }
  imp->already_dumping = true;
  L(FL("saving current work set: %i items") % imp->musings.size());
  ostringstream out;
  out << (F("Current work set: %i items") % imp->musings.size())
      << '\n'; // final newline is kept out of the translation
  for (vector<MusingI const *>::const_iterator
         i = imp->musings.begin(); i != imp->musings.end(); ++i)
    {
      string tmp;
      try
        {
          (*i)->gasp(tmp);
          out << tmp;
        }
      catch (logic_error)
        {
          out << tmp;
          out << "<caught logic_error>\n";
          L(FL("ignoring error trigged by saving work set to debug log"));
        }
      catch (informative_failure)
        {
          out << tmp;
          out << "<caught informative_failure>\n";
          L(FL("ignoring error trigged by saving work set to debug log"));
        }
    }
  imp->gasp_dump = out.str();
  L(FL("finished saving work set"));
  if (imp->debug)
    {
      inform_log("contents of work set:");
      inform_log(imp->gasp_dump);
    }
  imp->already_dumping = false;
}

template <> void
dump(string const & obj, string & out)
{
  out = obj;
}

void
print_var(std::string const & value, char const * var,
          char const * file, int const line, char const * func)
{
  std::cout << (FL("----- begin '%s' (in %s, at %s:%d)\n") 
                % var % func % file % line)
            << value
            << (FL("\n-----   end '%s' (in %s, at %s:%d)\n\n") 
                % var % func % file % line);
}

void MusingBase::gasp_head(string & out) const
{
  out = (boost::format("----- begin '%s' (in %s, at %s:%d)\n")
         % name % func % file % line
         ).str();
}

void MusingBase::gasp_body(const string & objstr, string & out) const
{
  out += (boost::format("%s%s"
                        "-----   end '%s' (in %s, at %s:%d)\n")
          % objstr
          % (*(objstr.end() - 1) == '\n' ? "" : "\n")
          % name % func % file % line
          ).str();
}

const locale &
get_user_locale()
{
  // this is awkward because if LC_CTYPE is set to something the
  // runtime doesn't know about, it will fail. in that case,
  // the default will have to do.
  static bool init = false;
  static locale user_locale;
  if (!init)
    {
      init = true;
      try
        {
          user_locale = locale("");
        }
      catch( ... )
        {}
    }
  return user_locale;
}

struct
format_base::impl
{
  format fmt;
  ostringstream oss;

  impl(impl const & other) : fmt(other.fmt)
  {}

  impl & operator=(impl const & other)
  {
    if (&other != this)
      {
        fmt = other.fmt;
        oss.str(string());
      }
    return *this;
  }

  impl(char const * pattern)
    : fmt(pattern)
  {}
  impl(string const & pattern)
    : fmt(pattern)
  {}
  impl(char const * pattern, locale const & loc)
    : fmt(pattern, loc)
  {}
  impl(string const & pattern, locale const & loc)
    : fmt(pattern, loc)
  {}
};

format_base::format_base(format_base const & other)
  : pimpl(other.pimpl ? new impl(*(other.pimpl)) : NULL)
{

}

format_base::~format_base()
{
        delete pimpl;
}

format_base &
format_base::operator=(format_base const & other)
{
  if (&other != this)
    {
      impl * tmp = NULL;

      try
        {
          if (other.pimpl)
            tmp = new impl(*(other.pimpl));
        }
      catch (...)
        {
          if (tmp)
            delete tmp;
        }

      if (pimpl)
        delete pimpl;

      pimpl = tmp;
    }
  return *this;
}

format_base::format_base(char const * pattern, bool use_locale)
  : pimpl(use_locale ? new impl(pattern, get_user_locale())
                     : new impl(pattern))
{}

format_base::format_base(std::string const & pattern, bool use_locale)
  : pimpl(use_locale ? new impl(pattern, get_user_locale())
                     : new impl(pattern))
{}

ostream &
format_base::get_stream() const
{
  return pimpl->oss;
}

void
format_base::flush_stream() const
{
  pimpl->fmt % pimpl->oss.str();
  pimpl->oss.str(string());
}

void format_base::put_and_flush_signed(s64 const & s) const { pimpl->fmt % s; }
void format_base::put_and_flush_signed(s32 const & s) const { pimpl->fmt % s; }
void format_base::put_and_flush_signed(s16 const & s) const { pimpl->fmt % s; }
void format_base::put_and_flush_signed(s8  const & s) const { pimpl->fmt % s; }

void format_base::put_and_flush_unsigned(u64 const & u) const { pimpl->fmt % u; }
void format_base::put_and_flush_unsigned(u32 const & u) const { pimpl->fmt % u; }
void format_base::put_and_flush_unsigned(u16 const & u) const { pimpl->fmt % u; }
void format_base::put_and_flush_unsigned(u8  const & u) const { pimpl->fmt % u; }

void format_base::put_and_flush_float(float const & f) const { pimpl->fmt % f; }
void format_base::put_and_flush_double(double const & d) const { pimpl->fmt % d; }

std::string
format_base::str() const
{
  return pimpl->fmt.str();
}

ostream &
operator<<(ostream & os, format_base const & fmt)
{
  return os << fmt.str();
}

i18n_format F(const char * str)
{
  return i18n_format(gettext(str));
}


i18n_format FP(const char * str1, const char * strn, unsigned long count)
{
  return i18n_format(ngettext(str1, strn, count));
}

plain_format FL(const char * str)
{
  return plain_format(str);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
