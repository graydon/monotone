#ifndef __GLOBISH_HH__
#define __GLOBISH_HH__

// Copyright (C) 2005 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

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

void combine_and_check_globish(std::vector<globish> const &patterns,
                               globish & pattern);

class globish_matcher
{
public:
  // this may throw an informative_failure if a pattern is invalid
  globish_matcher(globish const & include_pat, globish const & exclude_pat);
  // this method may throw a std::runtime_error if the pattern is really
  // pathological
  bool operator()(std::string const & s);
private:
  boost::regex r_inc, r_exc;
};

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
