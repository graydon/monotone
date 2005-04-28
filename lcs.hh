#ifndef __LCS_HH__
#define __LCS_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

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
