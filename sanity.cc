// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <stdio.h>
#include <stdarg.h>
#include <algorithm>
#include <iterator>
#include <iostream>
#include <string>

#include <boost/lexical_cast.hpp>

#include "constants.hh"
#include "sanity.hh"
#include "ui.hh"

using namespace std;
using boost::format;

// debugging / logging system

sanity global_sanity;

sanity::sanity() : 
  verbose(false), quiet(false), logbuf(0xffff)
{}

sanity::~sanity()
{}

void sanity::dump_buffer()
{
  copy(logbuf.begin(), logbuf.end(), ostream_iterator<char>(cout));
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

void sanity::log(format const & fmt)
{
  string str = fmt.str();
  if (str.size() > constants::log_line_sz)
    str.resize(constants::log_line_sz);
  copy(str.begin(), str.end(), back_inserter(logbuf));
  if (verbose)
    ui.inform(str);
}

void sanity::progress(format const & fmt)
{
  string str = fmt.str();
  if (str.size() > constants::log_line_sz)
    str.resize(constants::log_line_sz);
  copy(str.begin(), str.end(), back_inserter(logbuf));
  if (! quiet)
    ui.inform(str);
}

void sanity::warning(format const & fmt)
{
  string str = fmt.str();
  if (str.size() > constants::log_line_sz)
    str.resize(constants::log_line_sz);  
  string str2 = "warning: " + str;
  copy(str2.begin(), str2.end(), back_inserter(logbuf));
  if (! quiet)
    ui.warn(str);
}

void sanity::naughty_failure(string const & expr, format const & explain, 
			     string const & file, int line)
{
  log(format("%s:%d: usage constraint '%s' violated\n") % file % line % expr);
  throw informative_failure(string("misuse: ") + explain.str());  
}

void sanity::invariant_failure(string const & expr, 
			       string const & file, int line)
{
  format fmt = 
    format("%s:%d: invariant '%s' violated\n") 
    % file % line % expr;
  log(fmt);
  throw logic_error(fmt.str());
}

void sanity::index_failure(string const & vec_expr, 
			   string const & idx_expr, 
			   unsigned long sz,
			   unsigned long idx,
			   string const & file, int line)
{
  format fmt = 
    format("%s:%d: index '%s' = %d overflowed vector '%s' with size %d\n")
    % file % line % idx_expr % idx % vec_expr % sz;
  log(fmt);
  throw logic_error(fmt.str());
}

