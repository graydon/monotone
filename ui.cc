#include "ui.hh"
#include "sanity.hh"

#include <iostream>
#include <boost/lexical_cast.hpp>

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// this file contains a couple utilities to deal with the user
// interface. the global user_interface object 'ui' owns clog, so no
// writing to it directly!

using namespace std;
using boost::lexical_cast;
struct user_interface ui;

ticker::ticker(string const & tickname, size_t mod) : mod(mod), name (tickname)
{
  I(ui.ticks.find(tickname) == ui.ticks.end());
  ui.ticks.insert(make_pair(tickname,0));
}

ticker::~ticker()
{
  I(ui.ticks.find(name) != ui.ticks.end());
  ui.ticks.erase(name);
  ui.finish_ticking();
}

void ticker::operator++()
{
  I(ui.ticks.find(name) != ui.ticks.end());
  ui.ticks[name]++;
  if (ui.ticks[name] % mod == 0)
    ui.write_ticks();
}

void ticker::operator+=(size_t t)
{
  I(ui.ticks.find(name) != ui.ticks.end());
  size_t old = ui.ticks[name];

  ui.ticks[name] += t;
  if ((old+t) % mod == 0
      || (old % mod) > ((old+t) % mod)
      || t > mod)
    ui.write_ticks();
}


user_interface::user_interface() :
  last_write_was_a_tick(false),
  last_tick_len(0)
{
  clog.sync_with_stdio(false);
  clog.unsetf(ios_base::unitbuf);
}

user_interface::~user_interface()
{
}

void user_interface::finish_ticking()
{
  if (ticks.size() == 0 && 
      last_write_was_a_tick)
    {
      tick_trailer = "";
      clog << endl;
      last_write_was_a_tick = false;
    }
}

void user_interface::set_tick_trailer(string const & t)
{
  tick_trailer = t;
}

void user_interface::write_ticks()
{

  string tickline = "\rmonotone: ";
  for (map<string,size_t>::const_iterator i = ticks.begin();
       i != ticks.end(); ++i)
    tickline += string("[") + i->first + ": " + lexical_cast<string>(i->second) + "] ";
  tickline += tick_trailer;

  size_t curr_sz = tickline.size();
  if (curr_sz < last_tick_len)
    tickline += string(last_tick_len - curr_sz, ' ');
  last_tick_len = curr_sz;

  clog << tickline;
  clog.flush();
  last_write_was_a_tick = true;
}

void user_interface::warn(string const & warning)
{
  if (issued_warnings.find(warning) == issued_warnings.end())
    inform("warning: " + warning);
  issued_warnings.insert(warning);
}


static inline string sanitize(string const & line)
{
  // FIXME: you might want to adjust this if you're using a charset
  // which has safe values in the sub-0x20 range. ASCII, UTF-8, 
  // and most ISO8859-x sets do not.
  string tmp;
  tmp.reserve(line.size());
  for (size_t i = 0; i < line.size(); ++i)
    {
      if ((line[i] == '\n')
	  || (line[i] >= static_cast<char>(0x20) 
	      && line[i] != static_cast<char>(0x7F)))
	tmp += line[i];
      else
	tmp += ' ';
    }
  return tmp;
}

void user_interface::inform(string const & line)
{
  if (last_write_was_a_tick)
    clog << endl;
  clog << "monotone: " << sanitize(line);
  clog.flush();
  last_write_was_a_tick = false;
}
