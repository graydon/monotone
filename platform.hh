#ifndef __PLATFORM_HH__
#define __PLATFORM_HH__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// this describes functions to be found, alternatively, in win32/* or unix/*
// directories.

#include "config.h"

#include <string>

#include "vocab.hh"
#include "paths.hh"

void read_password(std::string const & prompt, char * buf, size_t bufsz);
void get_system_flavour(std::string & ident);
bool is_executable(const char *path);

// For LUA
int existsonpath(const char *exe);
int make_executable(const char *path);
pid_t process_spawn(const char * const argv[]);
int process_wait(pid_t pid, int *res, int timeout = -1);// default infinite
int process_kill(pid_t pid, int signal);
int process_sleep(unsigned int seconds);

// stop "\n"->"\r\n" from breaking automate on Windows
void make_io_binary();

#ifdef WIN32
std::string munge_argv_into_cmdline(const char* const argv[]);
#endif
// for term selection
bool have_smart_terminal();
// this function cannot call W/P/L, because it is called by the tick printing
// code.
// return value of 0 means "unlimited"
unsigned int terminal_width();

// for "reckless mode" workspace change detection.
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
system_path get_default_confdir();
utf8 get_homedir();
namespace path
{
  typedef enum { nonexistent, directory, file } status;
};
path::status get_path_status(any_path const & path);

void rename_clobberingly(any_path const & from, any_path const & to);

// strerror wrapper for OS-specific errors (e.g. use FormatMessage on Win32)
std::string os_strerror(os_err_t errnum);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __PLATFORM_HH__
