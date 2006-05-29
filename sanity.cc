// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <stdio.h>
#include <stdarg.h>

#include <algorithm>
#include <iterator>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>

#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>

#include "constants.hh"
#include "platform.hh"
#include "sanity.hh"
#include "simplestring_xform.hh"
#include "ui.hh"

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

// debugging / logging system

sanity global_sanity;

sanity::sanity() : 
  debug(false), quiet(false), reallyquiet(false), relaxed(false), logbuf(0xffff), 
  already_dumping(false), clean_shutdown(false)
{
  string flavour;
  get_system_flavour(flavour);
  L(FL("started up on %s\n") % flavour);
}

sanity::~sanity()
{}

void 
sanity::dump_buffer()
{
  if (!filename.empty())
    {
      ofstream out(filename.as_external().c_str());
      if (out)
        {
          copy(logbuf.begin(), logbuf.end(), ostream_iterator<char>(out));
          copy(gasp_dump.begin(), gasp_dump.end(), ostream_iterator<char>(out));
          ui.inform((FL("wrote debugging log to %s\n"
                        "if reporting a bug, please include this file")
                       % filename).str());
        }
      else
        ui.inform((FL("failed to write debugging log to %s\n") % filename).str());
    }
  else
    ui.inform("discarding debug log, because I have nowhere to write it\n"
              "(maybe you want --debug or --dump?)");
}

void 
sanity::set_debug()
{
  quiet = false;
  reallyquiet = false;
  debug = true;

  // it is possible that some pre-setting-of-debug data
  // accumulated in the log buffer (during earlier option processing)
  // so we will dump it now  
  ostringstream oss;
  vector<string> lines;
  copy(logbuf.begin(), logbuf.end(), ostream_iterator<char>(oss));
  split_into_lines(oss.str(), lines);
  for (vector<string>::const_iterator i = lines.begin(); i != lines.end(); ++i)
    ui.inform((*i) + "\n");
}

void
sanity::set_brief()
{
  brief = true;
}

void 
sanity::set_quiet()
{
  debug = false;
  quiet = true;
  reallyquiet = false;
}

void 
sanity::set_reallyquiet()
{
  debug = false;
  quiet = true;
  reallyquiet = true;
}

void 
sanity::set_relaxed(bool rel)
{
  relaxed = rel;
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
      ui.inform(F("fatal: formatter failed on %s:%d: %s")
                % file
                % line
                % e.what());
      throw;
    }
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
  copy(str.begin(), str.end(), back_inserter(logbuf));
  if (str[str.size() - 1] != '\n')
    logbuf.push_back('\n');
  if (debug)
    ui.inform(str);
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
  copy(str.begin(), str.end(), back_inserter(logbuf));
  if (str[str.size() - 1] != '\n')
    logbuf.push_back('\n');
  if (! quiet)
    ui.inform(str);
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
  copy(str2.begin(), str2.end(), back_inserter(logbuf));
  if (str[str.size() - 1] != '\n')
    logbuf.push_back('\n');
  if (! reallyquiet)
    ui.warn(str);
}

void 
sanity::naughty_failure(string const & expr, i18n_format const & explain, 
                        string const & file, int line)
{
  string message;
  log(FL("%s:%d: usage constraint '%s' violated\n") % file % line % expr,
      file.c_str(), line);
  prefix_lines_with(_("misuse: "), do_format(explain, file.c_str(), line), message);
  gasp();
  throw informative_failure(message);
}

void 
sanity::error_failure(string const & expr, i18n_format const & explain, 
                      string const & file, int line)
{
  string message;
  log(FL("%s:%d: detected error '%s' violated\n") % file % line % expr,
      file.c_str(), line);
  prefix_lines_with(_("error: "), do_format(explain, file.c_str(), line), message);
  throw informative_failure(message);
}

void 
sanity::invariant_failure(string const & expr, 
                          string const & file, int line)
{
  char const * pattern = N_("%s:%d: invariant '%s' violated");
  log(FL(pattern) % file % line % expr, file.c_str(), line);
  gasp();
  throw logic_error((F(pattern) % file % line % expr).str());
}

void 
sanity::index_failure(string const & vec_expr, 
                      string const & idx_expr, 
                      unsigned long sz,
                      unsigned long idx,
                      string const & file, int line)
{
  char const * pattern = N_("%s:%d: index '%s' = %d overflowed vector '%s' with size %d\n");
  log(FL(pattern) % file % line % idx_expr % idx % vec_expr % sz,
      file.c_str(), line);
  gasp();
  throw logic_error((F(pattern)
                     % file % line % idx_expr % idx % vec_expr % sz).str());
}

// Last gasp dumps

void
sanity::gasp()
{
  if (already_dumping)
    {
      L(FL("ignoring request to give last gasp; already in process of dumping\n"));
      return;
    }
  already_dumping = true;
  L(FL("saving current work set: %i items") % musings.size());
  ostringstream out;
  out << F("Current work set: %i items\n") % musings.size();
  for (vector<MusingI const *>::const_iterator
         i = musings.begin(); i != musings.end(); ++i)
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
  gasp_dump = out.str();
  L(FL("finished saving work set"));
  if (debug)
    {
      ui.inform("contents of work set:");
      ui.inform(gasp_dump);
    }
  already_dumping = false;
}

MusingI::MusingI()
{
  if (!global_sanity.already_dumping)
    global_sanity.musings.push_back(this);
}

MusingI::~MusingI()
{
  if (!global_sanity.already_dumping)
    {
      I(global_sanity.musings.back() == this);
      global_sanity.musings.pop_back();
    }
}

template <> void
dump(string const & obj, string & out)
{
  out = obj;
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
  
format_base::format_base(char const * pattern)
  : pimpl(new impl(pattern))
{}
 
format_base::format_base(std::string const & pattern)
  : pimpl(new impl(pattern))
{}
 
format_base::format_base(char const * pattern, locale const & loc)
  : pimpl(new impl(pattern, loc))
{}
 
format_base::format_base(string const & pattern, locale const & loc)
  : pimpl(new impl(pattern, loc))
{}
 
ostream &
format_base::get_stream()
{
  return pimpl->oss;
}
  
void 
format_base::flush()
{
  pimpl->fmt % pimpl->oss.str();
  pimpl->oss.str(string());
}
  
std::string 
format_base::str() const
{
  return pimpl->fmt.str();
}
 
i18n_format::i18n_format(const char * localized_pattern)
  : format_base(localized_pattern, get_user_locale())
{
}
 
i18n_format::i18n_format(std::string const & localized_pattern)
  : format_base(localized_pattern, get_user_locale())
{
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
