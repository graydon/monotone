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

void
parse_uri(std::string const & in, uri & out);

std::string
urldecode(std::string const & in);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
