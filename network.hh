#ifndef __NETWORK_HH__
#define __NETWORK_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <boost/shared_ptr.hpp>
#include <boost/lexical_cast.hpp>
#include <string>
#include <set>
#include <iostream>

#include "vocab.hh"

void post_queued_blobs_to_network(std::set<url> const & targets,
				  app_state & app);

void fetch_queued_blobs_from_network(std::set<url> const & sources,
				     app_state & app);


#endif // __NETWORK_HH__
