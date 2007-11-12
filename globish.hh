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

// all characters stand for themselves except
//
// \x      matches x, even if x is one of the metacharacters
// *       matches zero or more characters of any kind (greedily)
// ?       matches any single character
// [...]   matches any single character that appears within the brackets
// [^..]   matches any single character that does _not_ appear
// [!..]   same as [^..]
// {a,b,c} matches a or b or c (may be of arbitrary length, have arbitrary
//         number of alternations; nesting is allowed but only five deep)
//
// [\]]    is how you put a ] in a character class
// [\[]    similarly (otherwise a syntax error)
// [\\]    similarly
// [{}?*]  within [] these stand for themselves
//
// \n      matches n, not newline
// \007    same as '007'
//
// to match, the _entire_ target must match the pattern; there is no scan
// for a substring match, nor is a prefix match a match.  the pattern is
// expected to be utf8, and characters in the 0x00 - 0x1f range are not
// permitted.
//
// as an extra special case, the empty string matches nothing, not even an
// empty string.  this hardly ever matters, but it's nice to have some way
// to say "don't exclude anything", for instance.

#include "vector.hh"

class arg_type;

struct globish
{
  globish() : compiled_pattern() {}
  globish(char const * pat);
  globish(std::string const & pat);
  globish(std::vector<arg_type> const & pat);
  globish(std::vector<arg_type>::const_iterator const & beg,
          std::vector<arg_type>::const_iterator const & end);

  std::string operator()(void) const;
  bool matches(std::string const & target) const;

private:
  std::string compiled_pattern;
};

std::ostream & operator<<(std::ostream &, globish const &);
template <> void dump(globish const &, std::string &);

// convenience functor for when you want to match all things
// that _do_ match one glob but do _not_ match another
struct globish_matcher
{
  globish_matcher(globish const & incl, globish const & excl)
    : included(incl), excluded(excl) {}

  bool operator()(std::string const & s)
  { return included.matches(s) && !excluded.matches(s); }

private:
  globish included;
  globish excluded;
};


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
