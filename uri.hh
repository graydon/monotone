#ifndef __URI_HH__
#define __URI_HH__

// Copyright (C) 2006 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <string>

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
