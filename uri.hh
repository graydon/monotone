#ifndef __URI_HH__
#define __URI_HH__

// copyright (C) 2006 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

struct uri
{
  std::string scheme;
  std::string user;
  std::string host;
  std::string port;
  std::string path;
  std::string query;
  std::string fragment;
};

bool
parse_uri(std::string const & in, uri & out);

#endif
