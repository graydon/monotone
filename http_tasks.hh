#ifndef __HTTP_TASKS_HH__
#define __HTTP_TASKS_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>

#include "packet.hh"
#include "vocab.hh"

using namespace std;

// this file contains simple functions which talk through HTTP to
// a depot.

bool post_http_packets(string const & group_name,
		       string const & user,
		       string const & signature,
		       string const & packets,
		       string const & http_host,
		       string const & http_path,
		       unsigned long port,
		       std::iostream & stream);
  
void fetch_http_packets(string const & group_name,
			unsigned long & maj_number,
			unsigned long & min_number,
			packet_consumer & consumer,
			string const & http_host,
			string const & http_path,
			unsigned long port,
			std::iostream & stream);

#endif // __HTTP_TASKS_HH__
