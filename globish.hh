#ifndef __GLOBISH_HH__
#define __GLOBISH_HH__

// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// a sort of glob-like pattern matcher, for use in specifying branch
// collections for netsync.  it is important that it not be too expensive to
// match (as opposed to common regex engines, which can be exponential on
// pathological patterns), because we must match branches against untrusted
// patterns when doing netsync.

// the syntax is:
//   most things - match themselves
//   * - match 0 or more characters
//   ? - match 0 or 1 characters
//   \<any character> - match <any character>
//   {<anything1>,<anything2>,...} - match any of the given items
// so like standard globs, except without [] character sets, and with {}
// alternation.
// the one strange thing is there is a special-case -- the empty pattern
// matches nothing, not even the empty string.  this hardly ever matters, but
// it's nice to have some way to say "don't exclude anything", for instance.

#include <set>
#include <string>
#include <boost/regex.hpp>

#include "vocab.hh"

void combine_and_check_globish(std::set<utf8> const &patterns, utf8 & pattern);

class globish_matcher
{
public:
  // this may throw an informative_failure if a pattern is invalid
  globish_matcher(utf8 const & include_pat, utf8 const & exclude_pat);
  // this method may throw a std::runtime_error if the pattern is really
  // pathological
  bool operator()(std::string const & s);
private:
  boost::regex r_inc, r_exc;
};

#endif
