// copyright (C) 2004 Nathaniel Smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// This is split off into its own file to minimize recompilation time; it is
// the only .cc file that depends on the revision/full_revision header files,
// which change constantly. 

#include "config.h"

#include <iostream>

#include "platform.hh"
#include "mt_version.hh"
#include "package_revision.h"
#include "package_full_revision.h"

void
print_version()
{
  std::cout << PACKAGE_STRING
            << " (base revision: " << package_revision_constant << ")" << std::endl;
}

void
print_full_version()
{
  print_version();
  std::string s;
  get_system_flavour(s);
  std::cout << "Running on: " << s << std::endl;
  std::cout << "Changes since base revision:" << std::endl
            << package_full_revision_constant;
}

