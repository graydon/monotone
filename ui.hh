#ifndef __UI_HH__
#define __UI_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// this file contains a couple utilities to deal with the user
// interface. the global user_interface object 'ui' owns cerr, so
// no writing to it directly!

#include <map>
#include <set>
#include <string>
#include <boost/format.hpp>

#include "sanity.hh"

struct user_interface;

struct ticker
{
  size_t ticks;
  size_t mod;
  size_t total;
  bool kilocount;
  bool use_total;
  std::string name;
  std::string shortname;
  ticker(std::string const & n, std::string const & s, size_t mod = 64, 
      bool kilocount=false);
  void set_total(size_t tot) { use_total = true; total = tot; }
  void operator++();
  void operator+=(size_t t);
  ~ticker();
};

struct tick_writer
{
public:
  tick_writer() {}
  virtual ~tick_writer() {}
  virtual void write_ticks() = 0;
  virtual void clear_line() = 0;
};

struct tick_write_count : virtual public tick_writer
{
public:
  tick_write_count();
  ~tick_write_count();
  void write_ticks();
  void clear_line();
private:
  std::vector<size_t> last_tick_widths;
  size_t last_tick_len;
};

struct tick_write_dot : virtual public tick_writer
{
public:
  tick_write_dot();
  ~tick_write_dot();
  void write_ticks();
  void clear_line();
private:
  std::map<std::string,size_t> last_ticks;
  unsigned int chars_on_line;
};

struct tick_write_nothing : virtual public tick_writer
{
public:
  void write_ticks() {}
  void clear_line() {}
};

struct user_interface
{
public:
  user_interface();
  ~user_interface();
  void warn(std::string const & warning);
  void warn(boost::format const & fmt) { warn(fmt.str()); }
  void fatal(std::string const & warning);
  void fatal(boost::format const & fmt) { warn(fmt.str()); }
  void inform(std::string const & line);
  void inform(boost::format const & fmt) { inform(fmt.str()); }
  void inform(i18n_format const & fmt) { inform(fmt.str()); }
  void set_tick_trailer(std::string const & trailer);
  void set_tick_writer(tick_writer * t_writer);
  void ensure_clean_line();
  void redirect_log_to(system_path const & filename);

private:  
  std::set<std::string> issued_warnings;  

  bool some_tick_is_dirty;    // At least one tick needs being printed
  bool last_write_was_a_tick;
  std::map<std::string,ticker *> tickers;
  tick_writer * t_writer;
  void finish_ticking();
  void write_ticks();
  std::string tick_trailer;

  friend struct tick_write_dot;
  friend struct tick_write_count;
  friend struct ticker;
};

extern struct user_interface ui;

// like platform.hh's "terminal_width", but always returns a sensible value
// (even if there is no terminal)
unsigned int guess_terminal_width();

// returns the a default user locale value
const std::locale & get_user_locale();

#endif // __UI_HH__
