#ifndef __HTTP_TASKS_HH__
#define __HTTP_TASKS_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>

#include "packet.hh"
#include "vocab.hh"

// this file contains simple functions which talk through HTTP to
// a depot.

bool post_http_packets(std::string const & group_name,
		       std::string const & user,
		       std::string const & signature,
		       std::string const & packets,
		       std::string const & http_host,
		       std::string const & http_path,
		       unsigned long port,
		       std::iostream & stream);
  
void fetch_http_packets(std::string const & group_name,
			unsigned long & maj_number,
			unsigned long & min_number,
			packet_consumer & consumer,
			std::string const & http_host,
			std::string const & http_path,
			unsigned long port,
			std::iostream & stream);

#endif // __HTTP_TASKS_HH__
