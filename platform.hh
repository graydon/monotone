#ifndef __PLATFORM_HH__
#define __PLATFORM_HH__

// copyright (C) 2002, 2003, 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// this describes functions to be found, alternatively, in win32/* or unix/*
// directories.

#include <string>

void read_password(std::string const & prompt, char * buf, size_t bufsz);

#endif // __PLATFORM_HH__
