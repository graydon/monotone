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
#include "constants.hh"

#include <iostream>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <boost/lexical_cast.hpp>

using namespace std;
using boost::lexical_cast;
struct user_interface ui;

ticker::ticker(string const & tickname, std::string const & s, size_t mod,
    bool kilocount) :
  ticks(0),
  mod(mod),
  total(0),
  kilocount(kilocount),
  use_total(false),
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
  vector<size_t> tick_widths;
  vector<string> tick_title_strings;
  vector<string> tick_count_strings;

  for (map<string,ticker *>::const_iterator i = ui.tickers.begin();
       i != ui.tickers.end(); ++i)
    {
      string count;
      ticker * tick = i->second;
      if (tick->kilocount && tick->ticks)
        { 
          // automatic unit conversion is enabled
          float div = 1.0;
          const char *message;

          if (tick->ticks >= 1073741824) 
            {
              div = 1073741824;
              // xgettext: gibibytes (2^30 bytes)
              message = N_("%.1f G");
            } 
          else if (tick->ticks >= 1048576) 
            {
              div = 1048576;
              // xgettext: mebibytes (2^20 bytes)
              message = N_("%.1f M");
            } 
          else if (tick->ticks >= 1024)
            {
              div = 1024;
              // xgettext: kibibytes (2^10 bytes)
              message = N_("%.1f k");
            }
          else
            {
              div = 1;
              message = "%.0f";
            }
          // We reset the mod to the divider, to avoid spurious screen updates.
          tick->mod = max(static_cast<int>(div / 10.0), 1);
          count = (F(message) % (tick->ticks / div)).str();
        }
      else if (tick->use_total)
        {
          // We know that we're going to eventually have 'total' displayed
          // twice on screen, plus a slash. So we should pad out this field
          // to that eventual size to avoid spurious re-issuing of the 
          // tick titles as we expand to the goal.
          string complete = (F("%d/%d") % tick->total % tick->total).str();          
          // xgettext: bytes
          string current = (F("%d/%d") % tick->ticks % tick->total).str();
          count.append(complete.size() - current.size(),' ');
          count.append(current);
        }
      else
        {
          // xgettext: bytes
          count = (F("%d") % tick->ticks).str();
        }
      
      size_t title_width = display_width(utf8(tick->name));
      size_t count_width = display_width(utf8(count));
      size_t max_width = std::max(title_width, count_width);

      string name;
      name.append(max_width - title_width, ' ');
      name.append(tick->name);

      string count2;
      count2.append(max_width - count_width, ' ');
      count2.append(count);

      tick_title_strings.push_back(name);
      tick_count_strings.push_back(count2);
      tick_widths.push_back(max_width);
    }

  string tickline1;
  bool write_tickline1 = !(ui.last_write_was_a_tick 
                           && (tick_widths == last_tick_widths));
  if (write_tickline1)
    {
      // Reissue the titles if the widths have changed.
      tickline1 = "monotone: ";
      for (size_t i = 0; i < tick_widths.size(); ++i)
        {
          if (i != 0)
            tickline1.append(" | ");
          tickline1.append(idx(tick_title_strings, i));
        }
      last_tick_widths = tick_widths;
      write_tickline1 = true;
    }

  // Always reissue the counts.
  string tickline2 = "monotone: ";
  for (size_t i = 0; i < tick_widths.size(); ++i)
    {
      if (i != 0)
        tickline2.append(" | ");
      tickline2.append(idx(tick_count_strings, i));
    }

  if (!ui.tick_trailer.empty())
    {
      tickline2 += " ";
      tickline2 += ui.tick_trailer;
    }
  
  size_t curr_sz = display_width(utf8(tickline2));
  if (curr_sz < last_tick_len)
    tickline2.append(last_tick_len - curr_sz, ' ');
  last_tick_len = curr_sz;

  unsigned int tw = terminal_width();
  if(write_tickline1)
    {
      if (ui.last_write_was_a_tick)
        clog << "\n";

      if (tw && display_width(utf8(tickline1)) > tw)
        {
          // FIXME: may chop off more than necessary (because we chop by
          // bytes, not by characters)
          tickline1.resize(tw);
        }
      clog << tickline1 << "\n";
    }
  if (tw && display_width(utf8(tickline2)) > tw)
    {
      // FIXME: may chop off more than necessary (because we chop by
      // bytes, not by characters)
      tickline2.resize(tw);
    }
  clog << "\r" << tickline2;
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
  static const string tickline_prefix = "monotone: ";
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
      tickline2 = "\n" + tickline_prefix;
      chars_on_line = tickline_prefix.size();
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
          chars_on_line += i->second->shortname.size();
          if (chars_on_line > guess_terminal_width())
            {
              chars_on_line = tickline_prefix.size() + i->second->shortname.size();
              tickline2 += "\n" + tickline_prefix;
            }
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
  cout.exceptions(ios_base::badbit);
#ifdef SYNC_WITH_STDIO_WORKS
  clog.sync_with_stdio(false);
#endif
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
  inform(F("fatal: %s\n"
           "this is almost certainly a bug in monotone.\n"
           "please send this error message, the output of 'monotone --full-version',\n"
           "and a description of what you were doing to %s.\n")
         % fatal % PACKAGE_BUGREPORT);
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
          || (static_cast<unsigned char>(line[i]) >= static_cast<unsigned char>(0x20) 
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
user_interface::redirect_log_to(system_path const & filename)
{
  static ofstream filestr;
  if (filestr.is_open())
    filestr.close();
  filestr.open(filename.as_external().c_str());
  clog.rdbuf(filestr.rdbuf());
}

void 
user_interface::inform(string const & line)
{
  string prefixedLine;
  prefix_lines_with(_("monotone: "), line, prefixedLine);
  ensure_clean_line();
  clog << sanitize(prefixedLine) << endl;
  clog.flush();
}

unsigned int
guess_terminal_width()
{
  unsigned int w = terminal_width();
  if (!w)
    w = constants::default_terminal_width;
  return w;
}

const locale &
get_user_locale()
{
  // this is awkward because if LC_CTYPE is set to something the
  // runtime doesn't know about, it will fail. in that case,
  // the default will have to do.
  static bool init = false;
  static locale user_locale;
  if (!init)
    {
      init = true;
      try
        {
          user_locale = locale("");
        }
      catch( ... )
        {}
    }
  return user_locale;
}
