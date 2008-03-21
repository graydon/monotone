#ifndef __CMD_MERGING_HH__
#define __CMD_MERGING__HH__

// Copyright (C) 2008 Stephen Leake <stephen_leake@stephe-leake.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// Functions in cmd_merging.cc that are shared between command-line
// and automate interfaces

// If basic_io, display results in basic_io stanzas. Otherwise use
// command-line format.
void
show_conflicts_core (database & db, revision_id const & l_id, revision_id const & r_id, bool const basic_io, std::ostream & output);

#endif
