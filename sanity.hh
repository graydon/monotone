#ifndef __SANITY_HH__
#define __SANITY_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <stdexcept>
#include <string>

// our assertion / sanity / error logging system was based on GNU Nana,
// but we're only using a small section of it, so we just replicate it
// in place rather than add Yet Another Library Dependency

// this is for error messages where we want a clean and inoffensive error
// message to make it to the user, not a diagnostic error indicating
// internal failure but a suggestion that they do something differently.
struct informative_failure {
  informative_failure(std::string const & s) : what(s) {}
  std::string what;
};

struct sanity {
  sanity();
  ~sanity();
  void dump_buffer();
  void set_verbose();
  void set_quiet();
  bool verbose;
  bool quiet;

  char * logbuf;
  size_t pos;
  size_t size;
};

typedef std::runtime_error oops;

extern sanity global_sanity;
void sanity_invariant_handler(char *expr, char *file, int line);
void sanity_naughtyness_handler(char *expr, std::string const & explain, char *file, int line);

#ifdef __GNUC__
void sanity_log(sanity & s, const char *format, ...)
  __attribute__((format (printf, 2, 3)));
void sanity_progress(sanity & s, const char *format, ...)
  __attribute__((format (printf, 2, 3)));
#else
void sanity_log(sanity & s, const char *format, ...);
void sanity_progress(sanity & s, const char *format, ...);
#endif

// L is for logging, you can log all you want
#define L(f...)                       \
do {                                  \
  sanity_log (global_sanity, ##f); \
} while(0)

// P is for progress, log only stuff which the user might
// normally like to see some indication of progress of
#define P(f...)                       \
do {                                  \
  sanity_progress (global_sanity, ##f); \
} while(0)

// I is for invariants that "should" always be true
// (if they are wrong, there is a *bug*)
#define I(e)                                                   \
do {                                                           \
  if(!(e)) {                                                   \
    sanity_invariant_handler ("I("#e")", __FILE__, __LINE__);  \
  }                                                            \
} while(0)

// N is for naughtyness on behalf of the user
// (if they are wrong, the user just did something wrong)
#define N(e, explain)                                          \
do {                                                           \
  if(!(e)) {                                                   \
    sanity_naughtyness_handler ("N("#e")", explain,            \
                                __FILE__, __LINE__);           \
  }                                                            \
} while(0)

#endif // __SANITY_HH__
