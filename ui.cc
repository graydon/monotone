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

ticker::ticker(string const & tickname, std::string const & s, size_t mod) :
  ticks(0),
  mod(mod),
  name(tickname),
  shortname(s)
{
  I(ui.tickers.find(tickname) == ui.tickers.end());
  ui.tickers.insert(make_pair(tickname,this));
}

ticker::~ticker()
{
  I(ui.tickers.find(name) != ui.tickers.end());
  if (!ui.dont_write_ticks)
    {
      ui.write_ticks();
      ui.dont_write_ticks = true;
    }
  ui.tickers.erase(name);
  ui.finish_ticking();
}

void 
ticker::operator++()
{
  I(ui.tickers.find(name) != ui.tickers.end());
  ticks++;
  if (ticks % mod == 0)
    ui.write_ticks();
  ui.dont_write_ticks = false;
}

void 
ticker::operator+=(size_t t)
{
  I(ui.tickers.find(name) != ui.tickers.end());
  size_t old = ticks;

  ticks += t;
  if (ticks % mod == 0
      || (ticks / mod) > (old / mod))
    ui.write_ticks();
  ui.dont_write_ticks = false;
}


tick_write_count::tick_write_count() : last_tick_len(0)
{
}

tick_write_count::~tick_write_count()
{
}

void tick_write_count::write_ticks()
{
  string tickline = "\rmonotone: ";
  for (map<string,ticker *>::const_iterator i = ui.tickers.begin();
       i != ui.tickers.end(); ++i)
    {
      tickline +=
	string("[")
	+ i->first + ": " + lexical_cast<string>(i->second->ticks)
	+ "] ";
    }
  tickline += ui.tick_trailer;

  size_t curr_sz = tickline.size();
  if (curr_sz < last_tick_len)
    tickline += string(last_tick_len - curr_sz, ' ');
  last_tick_len = curr_sz;

  clog << tickline;
  clog.flush();
}


tick_write_dot::tick_write_dot()
{
}

tick_write_dot::~tick_write_dot()
{
}

void tick_write_dot::write_ticks()
{
  string tickline;

  if (ui.last_write_was_a_tick)
    {
      tickline = "";
    }
  else
    {
      tickline = "monotone: ";
    }

  for (map<string,ticker *>::const_iterator i = ui.tickers.begin();
       i != ui.tickers.end(); ++i)
    {
      map<string,size_t>::const_iterator old = last_ticks.find(i->first);
      if (old == last_ticks.end()
	  || ((i->second->ticks / i->second->mod)
	      > (old->second / i->second->mod)))
	{
	  tickline += i->second->shortname;

	  if (old == last_ticks.end())
	    last_ticks.insert(make_pair(i->first, i->second->ticks));
	  else
	    last_ticks[i->first] = i->second->ticks;
	}
    }

  clog << tickline;
  clog.flush();
}


user_interface::user_interface() :
  last_write_was_a_tick(false),
  t_writer(0)
{
  clog.sync_with_stdio(false);
  clog.unsetf(ios_base::unitbuf);
  set_tick_writer(new tick_write_count);
}

user_interface::~user_interface()
{
  delete t_writer;
}

void 
user_interface::finish_ticking()
{
  if (tickers.size() == 0 && 
      last_write_was_a_tick)
    {
      tick_trailer = "";
      clog << endl;
      last_write_was_a_tick = false;
    }
}

void 
user_interface::set_tick_trailer(string const & t)
{
  tick_trailer = t;
}

void 
user_interface::set_tick_writer(tick_writer * t)
{
  if (t_writer != 0)
    delete t_writer;
  t_writer = t;
}

void 
user_interface::write_ticks()
{
  t_writer->write_ticks();
  last_write_was_a_tick = true;
}

void 
user_interface::warn(string const & warning)
{
  if (issued_warnings.find(warning) == issued_warnings.end())
    inform("warning: " + warning);
  issued_warnings.insert(warning);
}

void 
user_interface::fatal(string const & fatal)
{
  inform("fatal: " + fatal);
}


static inline string 
sanitize(string const & line)
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

void 
user_interface::inform(string const & line)
{
  if (last_write_was_a_tick)
    {
      write_ticks();
      clog << endl;
    }
  clog << "monotone: " << sanitize(line);
  clog.flush();
  last_write_was_a_tick = false;
}
