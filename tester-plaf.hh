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

#include <ctime>

void make_accessible(std::string const & name);
std::time_t get_last_write_time(char const * name);
void do_copy_file(std::string const & from, std::string const & to);
void set_env(char const * var, char const * val);
void unset_env(char const * var);
int do_umask(int mask);
char * make_temp_dir();
bool running_as_root();

// This function has a decidedly awkward interface because (a) Windows
// doesn't have fork(), (b) platform files can't talk directly to the Lua
// interpreter.  [Also it would be nice not to have to declare the full
// content of the callback functors, but that is not so bad.]
//
// next_test() is called repeatedly until it returns false.  Each time it
// returns true, it shall fill in its test_to_run argument with the number
// and name of the next test to run.
//
// For each such test, *either* invoke() is called in a forked child
// process, *or* the program named in 'runner' is spawned with argument
// vector [runner, '-r', testfile, firstdir, test-name].  Either way, the
// child process is running in a just-created, empty directory which is
// exclusive to that test.  Standard I/O is not touched; the child is
// expected not to use stdin, stdout, or stderr.  If invoke() is called,
// its return value is the process exit code.
//
// After each per-test child process completes, cleanup() is called for that
// test.  If cleanup() returns true, the per-test directory is deleted.

struct lua_State;

struct test_to_run
{
  int number;
  std::string name;
};

struct test_enumerator
{
  lua_State * st;
  int table_ref;
  mutable int last_index;
  mutable bool iteration_begun;
  test_enumerator(lua_State *st, int t)
    : st(st), table_ref(t), last_index(0), iteration_begun(false) {}
  bool operator()(test_to_run & next_test) const;
};

struct test_invoker
{
  lua_State * st;
  test_invoker(lua_State *st) : st(st) {}
  int operator()(std::string const & testname) const;
};

struct test_cleaner
{
  lua_State * st;
  int reporter_ref;
  test_cleaner(lua_State *st, int r) : st(st), reporter_ref(r) {}
  bool operator()(test_to_run const & test, int status) const;
};

void run_tests_in_children(test_enumerator const & next_test,
                           test_invoker const & invoke,
                           test_cleaner const & cleanup,
                           std::string const & run_dir,
                           std::string const & runner,
                           std::string const & testfile,
                           std::string const & firstdir);
void prepare_for_parallel_testcases(int, int, int);

// These functions are actually in tester.cc but are used by tester-plaf.cc.
void do_remove_recursive(std::string const & dir);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif

