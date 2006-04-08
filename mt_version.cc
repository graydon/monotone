// copyright (C) 2004 Nathaniel Smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// This is split off into its own file to minimize recompilation time; it is
// the only .cc file that depends on the revision/full_revision header files,
// which change constantly. 

#include "config.h"

#include <iostream>
#include <sstream>

#include <boost/version.hpp>
#include <boost/config.hpp>

#include "platform.hh"
#include "mt_version.hh"
#include "package_revision.h"
#include "package_full_revision.h"
#include "sanity.hh"

void
get_version(std::string & out)
{
  out = (F("%s (base revision: %s)")
         % PACKAGE_STRING % package_revision_constant).str();
}

void
print_version()
{
  std::string s;
  get_version(s);
  std::cout << s << std::endl;
}

void
get_full_version(std::string & out)
{
  std::ostringstream oss;
  std::string s;
  get_version(s);
  oss << s << "\n";
  get_system_flavour(s);
  oss << F("Running on          : %s\n"
           "C++ compiler        : %s\n"
           "C++ standard library: %s\n"
           "Boost version       : %s\n"
           "Changes since base revision:\n"
           "%s")
    % s
    % BOOST_COMPILER
    % BOOST_STDLIB
    % BOOST_LIB_VERSION
    % package_full_revision_constant;
  out = oss.str();
}

void
print_full_version()
{
  std::string s;
  get_full_version(s);
  std::cout << s << std::endl;
}
