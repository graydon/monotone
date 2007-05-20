#ifndef __UI_HH__
#define __UI_HH__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// this file contains a couple utilities to deal with the user
// interface. the global user_interface object 'ui' owns cerr, so
// no writing to it directly!

#include <map>
#include <set>
#include <string>

#include "paths.hh"
#include "sanity.hh"

struct user_interface;

struct ticker
{
  size_t ticks;
  size_t mod;
  size_t total;
  size_t previous_total;
  bool kilocount;
  bool use_total;
  std::string keyname;
  std::string name; // translated name
  std::string shortname;
  size_t count_size;
  ticker(std::string const & n, std::string const & s, size_t mod = 64,
      bool kilocount=false);
  void set_total(size_t tot) { use_total = true; total = tot; }
  void set_count_size(size_t csiz) { count_size = csiz; }
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
  void initialize();
  void deinitialize();
  void warn(std::string const & warning);
  void warn(format_base const & fmt) { warn(fmt.str()); }
  void fatal(std::string const & fatal);
  void fatal(format_base const & fmt) { fatal(fmt.str()); }
  void inform(std::string const & line);
  void inform(format_base const & fmt) { inform(fmt.str()); }
  void fatal_exception(std::exception const & ex);
  void fatal_exception();
  void set_tick_trailer(std::string const & trailer);
  void set_tick_writer(tick_writer * t_writer);
  void ensure_clean_line();
  void redirect_log_to(system_path const & filename);

  std::string output_prefix();
  std::string prog_name;

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

std::string format_text(std::string const & text,
                        size_t const col = 0, size_t curcol = 0);
std::string format_text(i18n_format const & text,
                        size_t const col = 0, size_t curcol = 0);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __UI_HH__
