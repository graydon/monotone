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


#include <stdio.h>

void read_password(std::string const & prompt, char * buf, size_t bufsz);
void get_system_flavour(std::string & ident);
bool is_executable(const char *path);

// For LUA
int existsonpath(const char *exe);
int make_executable(const char *path);
pid_t process_spawn(const char * const argv[]);
pid_t process_spawn_redirected(char const * in,
                               char const * out,
                               char const * err,
                               char const * const argv[]);
pid_t process_spawn_pipe(char const * const argv[], FILE** in, FILE** out);
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
class inodeprint_calculator
{
public:
  template<typename T> void add_item(T obj)
  {
    size_t size(sizeof(obj));
    add_item(&size, sizeof(size));
    add_item(&obj, sizeof(obj));
  }
  // When adding a time to the print use these to note if it's
  // close to the current time (within about 3 seconds) or
  // in the future.
  // To make this more robust, there are some tricks:
  //   -- we refuse to inodeprint files whose times are within a few seconds of
  //      'now'.  This is because, we might memorize the inodeprint, then
  //      someone writes to the file, and this write does not update the
  //      timestamp -- or rather, it does update the timestamp, but nothing
  //      happens, because the new value is the same as the old value.  We use
  //      "a few seconds" to make sure that it is larger than whatever the
  //      filesystem's timekeeping granularity is (rounding to 2 seconds is
  //      known to exist in the wild).
  //   -- by the same reasoning, we should also refuse to inodeprint files whose
  //      time is in the future, because it is possible that someone will write
  //      to that file exactly when that future second arrives, and we will
  //      never notice.  However, this would create persistent and hard to
  //      diagnosis slowdowns, whenever a tree accidentally had its times set
  //      into the future.  Therefore, to handle this case, we include a "is
  //      this time in the future?" bit in the hashed information.  This bit
  //      will change when we pass the future point, and trigger a re-check of
  //      the file's contents.
  //
  // This is, of course, still not perfect.  There is no way to make our stat
  // atomic with the actual read of the file, so there's always a race condition
  // there.  Additionally, this handling means that checkout will never actually
  // inodeprint anything, but rather the first command after checkout will be
  // slow.  There doesn't seem to be anything that could be done about this.
  virtual void note_future(bool f = true) = 0;
  virtual void note_nowish(bool f = true) = 0;
  virtual ~inodeprint_calculator() {};
protected:
  virtual void add_item(void *dat, size_t size) = 0;
};
bool inodeprint_file(std::string const & file, inodeprint_calculator & calc);

// for netsync 'serve' pidfile support
pid_t get_process_id();

// netsync wants to ignore sigpipe; this is meaningless on Win32
#ifdef WIN32
inline void ignore_sigpipe() {}
#else
void ignore_sigpipe(); // in unix/process.cc
#endif

// filesystem stuff
// FIXME: BUG: this returns a string in the filesystem charset/encoding
std::string get_current_working_dir();
// calls N() if fails
void change_current_working_dir(std::string const & to);
std::string tilde_expand(std::string const & path);
std::string get_default_confdir();

inline std::string get_default_keydir()
{ return get_default_confdir() + "/keys"; }

std::string get_homedir();
namespace path
{
  typedef enum { nonexistent, directory, file } status;
};
path::status get_path_status(std::string const & path);

struct dirent_consumer
{
  virtual ~dirent_consumer() {}
  virtual void consume(const char *) = 0;
};
void do_read_directory(std::string const & path,
                       dirent_consumer & files,
                       dirent_consumer & dirs,
                       dirent_consumer & other_files);

void rename_clobberingly(std::string const & from, std::string const & to);
void do_remove(std::string const & path);

void do_mkdir(std::string const & path);
void write_data_worker(std::string const & p,
                       std::string const & dat,
                       std::string const & tmpdir,
                       bool user_private);

// strerror wrapper for OS-specific errors (e.g. use FormatMessage on Win32)
std::string os_strerror(os_err_t errnum);

// for running cpu benchmarks
// Returns the processor time used by the current process, plus some
// arbitrary constant, measured in seconds.
double cpu_now();

// determine directory to load locale data from
std::string get_locale_dir();

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __PLATFORM_HH__
