// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <stdio.h>
#include <stdarg.h>
#include <iostream>
#include <string>

#include <boost/lexical_cast.hpp>

#include "constants.hh"
#include "sanity.hh"
#include "ui.hh"

using namespace std;

// debugging / logging system

sanity global_sanity;

sanity::sanity() : verbose(false), quiet(false), logbuf(NULL), pos(0), size(0xffff)
{
  logbuf = reinterpret_cast<char *>(malloc(size));
  memset (logbuf, 0, size);
  I(logbuf);
}

sanity::~sanity()
{
  if (logbuf)
    free (logbuf);
}

void sanity::set_verbose()
{
  quiet = false;
  verbose = true;
}

void sanity::set_quiet()
{
  verbose = false;
  quiet = true;
}

void sanity_log(sanity & s, const char *format, ...)
{
  size_t sz = log_line_sz * 2;
  char buf[sz];
  memset(buf, 0, sz);

  va_list ap;
  va_start(ap, format);
  int wrote = vsnprintf(buf, log_line_sz, format, ap);
  va_end(ap);

  if (wrote < 0)
    throw std::logic_error ("vsnprintf failed in sanity_printf");

  // we're not really interested in anything past the first null,
  // wherever that may be.

  buf[sz - 1] = '\0';
  wrote = strlen(buf);

  if (wrote == 0)
    return;

  int segment = s.size - s.pos;
  if (wrote < segment)
    segment = wrote;
  memcpy (s.logbuf + s.pos, buf, segment);
  s.pos += segment;
  if (segment < wrote)
    {
      memcpy (s.logbuf, buf + segment, wrote - segment);
      s.pos = wrote - segment;
    }
    
  if (s.verbose)
    ui.inform(string(buf));
}

void sanity_progress(sanity & s, const char *format, ...)
{
  size_t sz = log_line_sz * 2;
  char buf[sz];
  memset(buf, 0, sz);

  va_list ap;
  va_start(ap, format);
  int wrote = vsnprintf(buf, log_line_sz, format, ap);
  va_end(ap);

  if (wrote < 0)
    throw std::logic_error ("vsnprintf failed in sanity_printf");

  // we're not really interested in anything past the first null,
  // wherever that may be.

  buf[sz - 1] = '\0';
  wrote = strlen(buf);

  if (wrote == 0)
    return;
  
  sanity_log(s, "progress: %s", buf);

  if (! s.quiet)
    ui.inform(string(buf));

}

void sanity::dump_buffer()
{
  if (logbuf[size - 1] != '\0')
    {
      // we've wrapped
      for (size_t i = 0; i < size && logbuf[(pos + i) % size] != '\0'; ++i)
	cout << logbuf[(pos + i) % size];
    }
  else
    {
      for (size_t i = 0; i < size && logbuf[i] != '\0'; ++i)
	cout << logbuf[i];
    }
}

void sanity_naughtyness_handler(char *expr, string const & explanation, char *file, int line)
{
  if (global_sanity.logbuf)
    L("usage constraint '%s' violated at %s:%d\n", expr, file, line);
  else
    ui.inform("usage constraint '" 
	      + string(expr) 
	      + "' violated at " 
	      + string(file) 
	      + ":" 
	      + boost::lexical_cast<string>(line)
	      + "\n");
  // FIXME: is "misuse" too rude? you don't want to scare the user if all they
  // did was type something a little wrong.. revisit perhaps with someone very
  // sensitive and kind, and maybe a thesaurus.
  throw informative_failure(string("misuse: ") + explanation);
}

void sanity_invariant_handler(char *expr, char *file, int line)
{
  if (global_sanity.logbuf)
    L("invariant '%s' violated at %s:%d\n", expr, file, line);
  else
    ui.inform("invariant '" 
	      + string(expr) 
	      + "' violated at " 
	      + string(file) 
	      + ":" 
	      + boost::lexical_cast<string>(line)
	      + "\n");

  throw std::logic_error(string(file) + string(":")
			 + boost::lexical_cast<string>(line) + string(": ")
			 + string(expr));
}

