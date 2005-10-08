#ifndef __PLATFORM_HH__
#define __PLATFORM_HH__

// copyright (C) 2002, 2003, 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// this describes functions to be found, alternatively, in win32/* or unix/*
// directories.

#include <string>

#include "vocab.hh"
#include "config.h"
#include "paths.hh"

void read_password(std::string const & prompt, char * buf, size_t bufsz);
void get_system_flavour(std::string & ident);
bool is_executable(const char *path);

// For LUA
int existsonpath(const char *exe);
int make_executable(const char *path);
pid_t process_spawn(const char * const argv[]);
int process_wait(pid_t pid, int *res);
int process_kill(pid_t pid, int signal);
int process_sleep(unsigned int seconds);

// for term selection
bool have_smart_terminal();
// this function cannot call W/P/L, because it is called by the tick printing
// code.
// return value of 0 means "unlimited"
unsigned int terminal_width();

// for "reckless mode" working copy change detection.
// returns 'true' if it has generated a valid inodeprint; returns 'false' if
// there was a problem, in which case we should act as if the inodeprint has
// changed.
bool inodeprint_file(file_path const & file, hexenc<inodeprint> & ip);

// for netsync 'serve' pidfile support
pid_t get_process_id();

// filesystem stuff
// FIXME: BUG: this returns a string in the filesystem charset/encoding
std::string get_current_working_dir();
// calls N() if fails
void change_current_working_dir(any_path const & to);
utf8 tilde_expand(utf8 const & path);
utf8 get_homedir();
namespace path
{
  typedef enum { nonexistent, directory, file } status;
};
path::status get_path_status(any_path const & path);

void rename_clobberingly(any_path const & from, any_path const & to);

#endif // __PLATFORM_HH__
