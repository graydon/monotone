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

struct user_interface;

struct ticker
{
  std::string name;
  ticker(std::string const & n);
  void operator++();
  void operator+=(size_t t);
  ~ticker();
};

struct user_interface
{
public:
  user_interface();
  ~user_interface();
  void warn(std::string const & warning);
  void inform(std::string const & line);
  void set_tick_trailer(std::string const & trailer);

private:  
  bool last_write_was_a_tick;
  size_t max_tick_len;
  std::set<std::string> issued_warnings;  
  std::map<std::string,size_t> ticks;

  void finish_ticking();
  void write_ticks();
  std::string tick_trailer;
  friend struct ticker;
};

extern struct user_interface ui;


#endif // __UI_HH__
