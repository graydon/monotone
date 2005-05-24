#ifndef __RANDOMFILE_HH__
#define __RANDOMFILE_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>
#include <vector>
#include <stdlib.h>
#include <boost/lexical_cast.hpp>

struct file_randomizer
{

  std::vector<std::string> lines;
  std::string prefix;

  void seed(int seed = 0xf00feed) 
  { 
    srand (seed); 
  }

  size_t random_index(bool last_line_ok = true)
  {
    if (last_line_ok)
      return static_cast<size_t>(rand() % lines.size());
    else
      {
        if (lines.size() == 0)
          return 0;
        else
          return static_cast<size_t>(rand() % (lines.size() - 1));
      }
  }

  bool random_bool()
  {
    return (rand() % 2 == 0);
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
                                int seed,
                                int n_hunks = 10)
  {    

    file_randomizer fr;
    fr.seed(seed);
    // maybe prepend something to one side or the other
    if (fr.random_bool())
      {
        fr.prepend_sequential_lines();
        if (fr.random_bool())
          fr.append_to(left);
        else
          fr.append_to(right);
        fr.append_to(merged);
      }
    fr.lines.clear();

    for (int h = 0; h < n_hunks; ++h)
      {
        file_randomizer hr;
        hr.seed(seed + h);
        hr.set_prefix(std::string("hunk ") + boost::lexical_cast<std::string>(h) + " -- ");
        hr.initial_sequential_lines(10);
        hr.append_to(ancestor);
        if (hr.random_bool())
          {
            // doing an insert
            if (hr.random_bool())
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
          }
        else
          {
            // doing a delete
            if (hr.random_bool())
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
          }
      }

    // maybe append something to one side or the other
    if (fr.random_bool())
      {
        fr.append_sequential_lines();
        if (fr.random_bool())
          fr.append_to(left);
        else
          fr.append_to(right);
        fr.append_to(merged);
      }
  }
};

#endif // __RANDOMFILE_HH__
