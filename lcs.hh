#ifndef __LCS_HH__
#define __LCS_HH__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <iterator>
#include "vector.hh"
#include "quick_alloc.hh"

void
longest_common_subsequence(std::vector<long, QA(long)>::const_iterator begin_a,
			   std::vector<long, QA(long)>::const_iterator end_a,
			   std::vector<long, QA(long)>::const_iterator begin_b,
			   std::vector<long, QA(long)>::const_iterator end_b,
			   long p_lim,
			   std::back_insert_iterator< std::vector<long, QA(long)> > lcs);

void
edit_script(std::vector<long, QA(long)>::const_iterator begin_a,
	    std::vector<long, QA(long)>::const_iterator end_a,
	    std::vector<long, QA(long)>::const_iterator begin_b,
	    std::vector<long, QA(long)>::const_iterator end_b,
	    long p_lim,
	    std::vector<long, QA(long)> & edits_out);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __LCS_HH__
