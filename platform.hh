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
void get_system_flavour(std::string & ident);

// For LUA
int existsonpath(const char *exe);
int make_executable(const char *path);
int process_spawn(const char * const argv[]);
int process_wait(int pid, int *res);
int process_kill(int pid, int signal);
int process_sleep(unsigned int seconds);

// for term selection
bool have_smart_terminal();

// for netsync
void start_platform_netsync();
void end_platform_netsync();

#endif // __PLATFORM_HH__
