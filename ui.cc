// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil -*-
// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// this file contains a couple utilities to deal with the user
// interface. the global user_interface object 'ui' owns clog, so no
// writing to it directly!

#include "config.h"
#include "platform.hh"
#include "sanity.hh"
#include "ui.hh"
#include "transforms.hh"

#include <iostream>
#include <iomanip>
#include <boost/lexical_cast.hpp>

using namespace std;
using boost::lexical_cast;
struct user_interface ui;

ticker::ticker(string const & tickname, std::string const & s, size_t mod,
    bool kilocount) :
  ticks(0),
  mod(mod),
  kilocount(kilocount),
  name(tickname),
  shortname(s)
{
  I(ui.tickers.find(tickname) == ui.tickers.end());
  ui.tickers.insert(make_pair(tickname,this));
}

ticker::~ticker()
{
  I(ui.tickers.find(name) != ui.tickers.end());
  if (ui.some_tick_is_dirty)
    {
      ui.write_ticks();
    }
  ui.tickers.erase(name);
  ui.finish_ticking();
}

void 
ticker::operator++()
{
  I(ui.tickers.find(name) != ui.tickers.end());
  ticks++;
  ui.some_tick_is_dirty = true;
  if (ticks % mod == 0)
    ui.write_ticks();
}

void 
ticker::operator+=(size_t t)
{
  I(ui.tickers.find(name) != ui.tickers.end());
  size_t old = ticks;

  ticks += t;
  if (t != 0)
    {
      ui.some_tick_is_dirty = true;
      if (ticks % mod == 0 || (ticks / mod) > (old / mod))
        ui.write_ticks();
    }
}


tick_write_count::tick_write_count() : last_tick_len(0)
{
}

tick_write_count::~tick_write_count()
{
}

void tick_write_count::write_ticks()
{
  string tickline1, tickline2;
  bool first_tick = true;

  tickline1 = "monotone: ";
  tickline2 = "\rmonotone:";
  
  unsigned int width;
  unsigned int minwidth = 7;
  for (map<string,ticker *>::const_iterator i = ui.tickers.begin();
       i != ui.tickers.end(); ++i)
    {
      width = 1 + i->second->name.size();
      if (!first_tick)
        {
          tickline1 += " | ";
          tickline2 += " |";
        }
      first_tick = false;
      if(i->second->name.size() < minwidth)
        {
          tickline1.append(minwidth - i->second->name.size(),' ');
          width += minwidth - i->second->name.size();
        }
      tickline1 += i->second->name;
      
      string count;
      if (i->second->kilocount && i->second->ticks >= 10000)
        { // automatic unit conversion is enabled
          float div;
          string suffix;
          if (i->second->ticks >= 1048576) {
          // ticks >=1MB, use Mb
            div = 1048576;
            suffix = "M";
          } else {
          // ticks <1MB, use kb
            div = 1024;
            suffix = "k";
          }
          count = (F("%.1f%s") % (i->second->ticks / div) % suffix).str();
        }
      else
        {
          count = (F("%d") % i->second->ticks).str();
        }
        
      if(count.size() < width)
        {
          tickline2.append(width-count.size(),' ');
        }
      else if(count.size() > width)
        {
          count = count.substr(count.size() - width);
        }
      tickline2 += count;
    }

  if (ui.tick_trailer.size() > 0)
    {
      tickline2 += " ";
      tickline2 += ui.tick_trailer;
    }
  
  size_t curr_sz = tickline2.size();
  if (curr_sz < last_tick_len)
    tickline2.append(last_tick_len - curr_sz, ' ');
  last_tick_len = curr_sz;

  unsigned int tw = terminal_width();
  if(!ui.last_write_was_a_tick)
    {
      if (tw && tickline1.size() > tw)
        {
          tickline1.resize(tw);
        }
      clog << tickline1 << "\n";
    }
  if (tw && tickline2.size() > tw)
    {
      // first character in tickline2 is "\r", which does not take up any
      // width, so we add 1 to compensate.
      tickline2.resize(tw + 1);
    }
  clog << tickline2;
  clog.flush();
}

void tick_write_count::clear_line()
{
  clog << endl;
}


tick_write_dot::tick_write_dot()
{
}

tick_write_dot::~tick_write_dot()
{
}

void tick_write_dot::write_ticks()
{
  string tickline1, tickline2;
  bool first_tick = true;

  if (ui.last_write_was_a_tick)
    {
      tickline1 = "";
      tickline2 = "";
    }
  else
    {
      tickline1 = "monotone: ticks: ";
      tickline2 = "\nmonotone: ";
    }

  for (map<string,ticker *>::const_iterator i = ui.tickers.begin();
       i != ui.tickers.end(); ++i)
    {
      map<string,size_t>::const_iterator old = last_ticks.find(i->first);

      if (!ui.last_write_was_a_tick)
        {
          if (!first_tick)
            tickline1 += ", ";

          tickline1 +=
            i->second->shortname + "=\"" + i->second->name + "\""
            + "/" + lexical_cast<string>(i->second->mod);
          first_tick = false;
        }

      if (old == last_ticks.end()
          || ((i->second->ticks / i->second->mod)
              > (old->second / i->second->mod)))
        {
          tickline2 += i->second->shortname;

          if (old == last_ticks.end())
            last_ticks.insert(make_pair(i->first, i->second->ticks));
          else
            last_ticks[i->first] = i->second->ticks;
        }
    }

  clog << tickline1 << tickline2;
  clog.flush();
}

void tick_write_dot::clear_line()
{
  clog << endl;
}


user_interface::user_interface() :
  last_write_was_a_tick(false),
  t_writer(0)
{
  clog.sync_with_stdio(false);
  clog.unsetf(ios_base::unitbuf);
  if (have_smart_terminal()) 
    set_tick_writer(new tick_write_count);
  else
    set_tick_writer(new tick_write_dot);
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
      t_writer->clear_line();
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
  some_tick_is_dirty = false;
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
  inform("this is almost certainly a bug in monotone.\n");
  inform("please send this error message, the output of 'monotone --full-version',\n");
  inform("and a description of what you were doing to " PACKAGE_BUGREPORT ".\n");
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
user_interface::ensure_clean_line()
{
  if (last_write_was_a_tick)
    {
      write_ticks();
      t_writer->clear_line();
    }
  last_write_was_a_tick = false;
}

void 
user_interface::inform(string const & line)
{
  string prefixedLine;
  prefix_lines_with("monotone: ", line, prefixedLine);
  ensure_clean_line();
  clog << sanitize(prefixedLine) << endl;
  clog.flush();
}
