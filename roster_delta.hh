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
                   

#endif // __ROSTER_DELTA_HH__
