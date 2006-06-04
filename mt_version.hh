#ifndef __MT_VERSION_HH__
#define __MT_VERSION_HH__

// Copyright (C) 2004 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <string>

void get_version(std::string & out);
void print_version();
void get_full_version(std::string & out);
void print_full_version();

#endif
