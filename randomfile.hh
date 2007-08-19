#ifndef __RANDOMFILE_HH__
#define __RANDOMFILE_HH__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "vector.hh"
#include "lexical_cast.hh"

#include "randomizer.hh"

struct file_randomizer
{
  randomizer & rng;
  std::vector<std::string> lines;
  std::string prefix;

  file_randomizer(randomizer & rng)
    : rng(rng)
  {}

  size_t random_index(bool last_line_ok = true)
  {
    if (last_line_ok)
      return static_cast<size_t>(rng.uniform(lines.size()));
    else
      {
        if (lines.size() == 0)
          return 0;
        else
          return static_cast<size_t>(rng.uniform(lines.size() - 1));
      }
  }

  void set_prefix(std::string const & p)
  {
    prefix = p;
  }

  void append_to(std::vector<std::string> & other)
  {
    for (std::vector<std::string>::const_iterator i = lines.begin();
         i != lines.end(); ++i)
      other.push_back(prefix + *i);
  }

  void initial_sequential_lines(int num_lines = 100) {
    lines.clear();
    for (int i = 0; i < num_lines; ++i)
      {
        lines.push_back(std::string("initial ") + boost::lexical_cast<std::string>(i));
      }
  }

  void append_sequential_lines(int num_lines = 10) {
    lines.clear();
    for (int i = 0; i < num_lines; ++i)
      {
        lines.push_back(std::string("append ") + boost::lexical_cast<std::string>(i));
      }
  }

  void prepend_sequential_lines(int num_lines = 10) {
    lines.clear();
    for (int i = 0; i < num_lines; ++i)
      {
        lines.push_back(std::string("prepend ") + boost::lexical_cast<std::string>(i));
      }
  }

  void delete_percent_of_lines_randomly(int percent = 50) {
    double scale = static_cast<double>(percent) / 100.0;
    double nlines_d = static_cast<double>(lines.size()) * scale;
    int nlines = static_cast<int>(nlines_d);
    for (int i = 0; i < nlines; ++i)
      lines.erase(lines.begin() + random_index(false));
  }

  void insert_sequential_percent_of_lines_randomly(int percent = 50) {
    double scale = static_cast<double>(percent) / 100.0;
    double nlines_d = static_cast<double>(lines.size()) * scale;
    int nlines = static_cast<int>(nlines_d);
    for (int i = 0; i < nlines; ++i)
      {
        lines.insert(lines.begin() + random_index(),
                     std::string("insert ") + boost::lexical_cast<std::string>(i));
      }
  }

  static void build_random_fork(std::vector<std::string> & ancestor,
                                std::vector<std::string> & left,
                                std::vector<std::string> & right,
                                std::vector<std::string> & merged,
                                int n_hunks,
                                randomizer & rng)
  {
    bool last_was_insert = false;
    bool last_insert_was_left = false;

    file_randomizer fr(rng);
    // maybe prepend something to one side or the other
    if (rng.flip())
      {
        last_was_insert = true;
        fr.prepend_sequential_lines();
        last_insert_was_left = rng.flip();
        if (last_insert_was_left)
          fr.append_to(left);
        else
          fr.append_to(right);
        fr.append_to(merged);
      }
    fr.lines.clear();

    for (int h = 0; h < n_hunks; ++h)
      {
        file_randomizer hr(rng);
        hr.set_prefix(std::string("hunk ") + boost::lexical_cast<std::string>(h) + " -- ");
        hr.initial_sequential_lines(10);
        if (rng.flip())
          {
            bool this_insert_is_left = rng.flip();
            if (last_was_insert && (this_insert_is_left != last_insert_was_left))
              {
                fr.set_prefix("spacer ");
                fr.initial_sequential_lines(3);
                fr.append_to(left);
                fr.append_to(right);
                fr.append_to(ancestor);
                fr.append_to(merged);
                fr.set_prefix("");
              }
            last_insert_was_left = this_insert_is_left;
            // doing an insert
            hr.append_to(ancestor);
            if (this_insert_is_left)
              {
                // inserting in left
                hr.append_to(right);
                hr.insert_sequential_percent_of_lines_randomly();
                hr.append_to(left);
              }
            else
              {
                // inserting in right
                hr.append_to(left);
                hr.insert_sequential_percent_of_lines_randomly();
                hr.append_to(right);
              }
            hr.append_to(merged);
            last_was_insert = true;
          }
        else
          {
            // doing a delete
            hr.append_to(ancestor);
            if (rng.flip())
              {
                // deleting in left
                hr.append_to(right);
                hr.delete_percent_of_lines_randomly();
                hr.append_to(left);
              }
            else
              {
                // deleting in right
                hr.append_to(left);
                hr.delete_percent_of_lines_randomly();
                hr.append_to(right);
              }
            hr.append_to(merged);
            last_was_insert = false;
          }
      }

    // maybe append something to one side or the other
    if (rng.flip())
      {
        bool this_insert_is_left = rng.flip();
        if (last_was_insert && (this_insert_is_left != last_insert_was_left))
          {
            fr.set_prefix("spacer ");
            fr.initial_sequential_lines(3);
            fr.append_to(left);
            fr.append_to(right);
            fr.append_to(ancestor);
            fr.append_to(merged);
            fr.set_prefix("");
          }
        fr.append_sequential_lines();
        if (this_insert_is_left)
          fr.append_to(left);
        else
          fr.append_to(right);
        fr.append_to(merged);
      }
  }
};

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __RANDOMFILE_HH__
