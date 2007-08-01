#ifndef __TESTER_PLAF_HH__
#define __TESTER_PLAF_HH__

// Copyright (C) 2007 Zack Weinberg <zackw@panix.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// this describes functions to be found, alternatively, in win32/* or unix/*
// directories and used only by the tester.

#include <sys/types.h> // time_t and pid_t

void make_accessible(std::string const & name);
time_t get_last_write_time(char const * name);
void do_copy_file(std::string const & from, std::string const & to);
void set_env(char const * var, char const * val);
void unset_env(char const * var);
int do_umask(int mask);
char * make_temp_dir();
bool running_as_root();

struct lua_State;
pid_t run_one_test_in_child(std::string const & testname,
			    std::string const & testdir,
			    lua_State * st,
			    std::string const & argv0,
			    std::string const & testfile,
			    std::string const & firstdir);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif

