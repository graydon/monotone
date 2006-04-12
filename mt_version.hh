#ifndef __MT_VERSION_HH__
#define __MT_VERSION_HH__

// copyright (C) 2004 Nathaniel Smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>

void get_version(std::string & out);
void print_version();
void get_full_version(std::string & out);
void print_full_version();

#endif
