#ifndef __ROSTER_DELTA_HH__

// Copyright (C) 2006 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// This file contains "diff"/"patch" code that operates directly on rosters
// (with their associated markings).

#include "vocab.hh"
#include "roster.hh"

void
delta_rosters(roster_t const & from, marking_map const & from_markings,
              roster_t const & to, marking_map const & to_markings,
              roster_delta & del);

// mutates its arguments
void
apply_roster_delta(roster_delta const & del,
                   roster_t & roster, marking_map & markings);
                   

#ifdef BUILD_UNIT_TESTS

// instead of having elaborate tests here, we just export a function, and then
// let all the other code that already generates every strange and weird test
// case there is feed us our tests
void test_roster_delta_on(roster_t const & a, marking_map const & a_marking,
                          roster_t const & b, marking_map const & b_marking);

#endif // BUILD_UNIT_TESTS

#endif // __ROSTER_DELTA_HH__
