#ifndef __ROSTER_DELTA_HH__

//////////
// Experimental roster delta stuff

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

void test_roster_delta_on(roster_t const & a, marking_map const & a_marking,
                          roster_t const & b, marking_map const & b_marking);

#endif // BUILD_UNIT_TESTS

#endif // __ROSTER_DELTA_HH__
