#ifndef __NETWORK_HH__
#define __NETWORK_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <boost/shared_ptr.hpp>
#include <boost/lexical_cast.hpp>
#include <string>
#include <vector>
#include <iostream>

#include "vocab.hh"

using namespace std;

bool parse_url(url const & u,
	       string & proto,
	       string & host,	       
	       string & path,
	       unsigned long & port);

void open_connection(std::string const & host,
		     unsigned long port,
		     boost::shared_ptr<std::iostream> & stream);

void queue_aggregated_merge(manifest_id const & ancestor, 
			    manifest_id const & merged, 
			    vector<manifest_id> const & heads, 
			    app_state & app);

void post_queued_blobs_to_network(vector< pair<url,group> > const & targets,
				  app_state & app);

void fetch_queued_blobs_from_network(vector< pair<url,group> > const & sources,
				     app_state & app);

void queue_blob_for_network(vector< pair<url,group> > const & targets,
			    string const & blob,
			    app_state & app);

#endif // __NETWORK_HH__
