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

struct i18n_format;
class system_path;

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

struct tick_writer;
struct tick_write_count;
struct tick_write_dot;

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
  void set_tick_write_dot();
  void set_tick_write_count();
  void set_tick_write_nothing();
  void ensure_clean_line();
  void redirect_log_to(system_path const & filename);

  std::string output_prefix();
  std::string prog_name;

private:
  void finish_ticking();
  void write_ticks();

  struct impl;
  impl * imp;

  friend struct ticker;
  friend struct tick_write_count;
  friend struct tick_write_dot;
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
