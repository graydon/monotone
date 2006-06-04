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
#include <vector>
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

#endif // __LCS_HH__
