#ifndef __NNTP_TASKS_HH__
#define __NNTP_TASKS_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>

#include "packet.hh"
#include "vocab.hh"

using namespace std;

// this file contains simple functions which build up NNTP state
// machines and run them using the infrastructure in nntp_machine.{cc,hh}

bool post_nntp_article(string const & group_name,
		       string const & from,
		       string const & subject,
		       string const & article,
		       std::iostream & stream);

void fetch_nntp_articles(string const & group_name,
			 unsigned long & seq_number,
			 packet_consumer & consumer,
			 std::iostream & stream);

#endif // __NNTP_TASKS_HH__
