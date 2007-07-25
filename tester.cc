#include "base.hh"
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include "lua.hh"
#include "platform.hh"
#include "sanity.hh"
#include "option.hh"

#include <cstdlib>
#include <ctime>
#include <cerrno>
#include <map>
#include <utility>
#include <vector>

/* for mkdir() */
#include <sys/stat.h>
#include <sys/types.h>

#ifdef WIN32
/* For _mktemp() */
#include <io.h>
#define mktemp(t) _mktemp(t)
/* For _mkdir() */
#include <direct.h>
#define mkdir(d,m) _mkdir(d)
#endif

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN // we don't need the GUI interfaces
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#endif

#ifdef WIN32
#define NULL_DEVICE "NUL:"
#else
#define NULL_DEVICE "/dev/null"
#endif

// defined in testlib.c, generated from testlib.lua
extern char const testlib_constant[];

using std::string;
using std::map;
using std::memcpy;
using std::getenv;
using std::exit;
using std::make_pair;
using std::vector;
using std::time_t;

// Lua uses the c i/o functions, so we need to too.
struct tester_sanity : public sanity
{
  void inform_log(std::string const &msg)
  {fprintf(stdout, "%s", msg.c_str());}
  void inform_message(std::string const &msg)
  {fprintf(stdout, "%s", msg.c_str());};
  void inform_warning(std::string const &msg)
  {fprintf(stderr, "warning: %s", msg.c_str());};
  void inform_error(std::string const &msg)
  {fprintf(stderr, "error: %s", msg.c_str());};
};
tester_sanity real_sanity;
sanity & global_sanity = real_sanity;


void make_accessible(string const &name)
{
#ifdef WIN32

  DWORD attrs = GetFileAttributes(name.c_str());
  E(attrs != INVALID_FILE_ATTRIBUTES,
    F("GetFileAttributes(%s) failed: %s") % name % os_strerror(GetLastError()));

  E(SetFileAttributes(name.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY),
    F("SetFileAttributes(%s) failed: %s") % name % os_strerror(GetLastError()));

#else

  struct stat st;
  E(stat(name.c_str(), &st) == 0,
    F("stat(%s) failed: %s") % name % os_strerror(errno));

  mode_t new_mode = st.st_mode;
  if (S_ISDIR(st.st_mode))
    new_mode |= S_IEXEC;
  new_mode |= S_IREAD | S_IWRITE;

  E(chmod(name.c_str(), new_mode) == 0,
    F("chmod(%s) failed: %s") % name % os_strerror(errno));

#endif
}

time_t get_last_write_time(string const & name)
{
#ifdef WIN32

  HANDLE h = CreateFile(name.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, 0, NULL);
  E(h != INVALID_HANDLE_VALUE,
    F("CreateFile(%s) failed: %s") % name % os_strerror(GetLastError()));

  FILETIME ft;
  E(GetFileTime(h, NULL, NULL, &ft),
    F("GetFileTime(%s) failed: %s") % name % os_strerror(GetLastError()));

  CloseHandle(h);

  // A FILETIME is a 64-bit quantity (represented as a pair of DWORDs)
  // representing the number of 100-nanosecond intervals elapsed since
  // 12:00 AM, January 1, 1601 UTC.  A time_t is the same as it is for
  // Unix: seconds since 12:00 AM, January 1, 1970 UTC.  The offset is
  // taken verbatim from MSDN.
  LONGLONG ft64 = ((LONGLONG)ft.dwHighDateTime) << 32 + ft.dwLowDateTime;
  return (time_t)((ft64/10000000) - 11644473600LL);

#else

  struct stat st;
  E(stat(name.c_str(), &st) == 0,
    F("stat(%s) failed: %s") % name % os_strerror(errno));

  return st.st_mtime;

#endif
}

void do_copy_file(string const & from, string const & to)
{
#ifdef WIN32
  // For once something is easier with Windows.
  E(CopyFile(from.c_str(), to.c_str(), true),
    F("copy %s to %s: %s") % from % to % os_strerror(GetLastError()));

#else
  char buf[32768];
  int ifd, ofd;
  ifd = open(from.c_str(), O_RDONLY);
  E(ifd >= 0, F("open %s: %s") % from % os_strerror(errno));
  struct stat st;
  st.st_mode = 0666;  // sane default if fstat fails
  fstat(ifd, &st);
  ofd = open(to.c_str(), O_WRONLY|O_CREAT|O_EXCL, st.st_mode);
  if (ofd < 0)
    {
      close(ifd);
      E(false, F("open %s: %s") % to % os_strerror(errno));
    }

  ssize_t nread, nwrite;
  int ndead;
  for (;;)
    {
      nread = read(ifd, buf, 32768);
      if (nread < 0)
        goto read_error;
      if (nread == 0)
        break;

      nwrite = 0;
      ndead = 0;
      do
        {
          ssize_t nw = write(ofd, buf + nwrite, nread - nwrite);
          if (nw < 0)
            goto write_error;
          if (nw == 0)
            ndead++;
          if (ndead == 4)
            goto spinning;
          nwrite += nw;
        }
      while (nwrite < nread);
    }
  close(ifd);
  close(ofd);
  return;

 read_error:
  {
    int err = errno;
    close(ifd);
    close(ofd);
    E(false, F("read error copying %s to %s: %s")
      % from % to % os_strerror(err));
  }
 write_error:
  {
    int err = errno;
    close(ifd);
    close(ofd);
    E(false, F("write error copying %s to %s: %s")
      % from % to % os_strerror(err));
  }
 spinning:
  {
    close(ifd);
    close(ofd);
    E(false, F("abandoning copy of %s to %s after four zero-length writes")
      % from % to);
  }

#endif
}


void set_env(char const * var, char const * val)
{
#if defined(WIN32)
  SetEnvironmentVariable(var, val);
#elif defined(HAVE_SETENV)
  setenv(var, val, 1);
#elif defined(HAVE_PUTENV)
  // note: this leaks memory, but the tester is short lived so it probably
  // doesn't matter much.
  string * tempstr = new string(var);
  tempstr->append("=");
  tempstr->append(val);
  putenv(const_cast<char *>(tempstr->c_str()));
#else
#error set_env needs to be ported to this platform
#endif
}

void unset_env(char const * var)
{
#if defined(WIN32)
  SetEnvironmentVariable(var, 0);
#elif defined(HAVE_UNSETENV)
  unsetenv(var);
#else
#error unset_env needs to be ported to this platform
#endif
}

string basename(string const & s)
{
  string::size_type sep = s.rfind('/');
  if (sep == string::npos)
    return s;  // force use of short circuit
  if (sep == s.size())
    return "";
  return s.substr(sep + 1);
}

string dirname(string const & s)
{
  string::size_type sep = s.rfind('/');
  if (sep == string::npos)
    return ".";
  if (sep == s.size() - 1) // dirname() of the root directory is itself
    return s;

  return s.substr(0, sep);
}

#if !defined(HAVE_MKDTEMP)
static char * _impl_mkdtemp(char * templ)
{
  char * tmpdir = new char[strlen(templ) + 1];
  char * result = 0;

  /* There's a possibility that the name returned by mktemp() will already
     be created by someone else, a typical race condition.  However, since
     mkdir() will not clobber an already existing file or directory, we
     can simply loop until we find a suitable name.  There IS a very small
     risk that we loop endlessly, but that's under extreme conditions, and
     the problem is likely to really be elsewhere... */
  do
    {
      strcpy(tmpdir, templ);
      result = mktemp(tmpdir);
      if (result && mkdir(tmpdir, 0700) != 0)
        {
          result = 0;
        }
    }
  while(!result && errno == EEXIST);

  if (result)
    {
      strcpy(templ, result);
      result = templ;
    }

  delete [] tmpdir;
  return result;
}

#define mkdtemp _impl_mkdtemp
#endif

char * do_mkdtemp(char const * parent)
{
  char * tmpdir = new char[strlen(parent) + sizeof "/mtXXXXXX"];

  strcpy(tmpdir, parent);
  strcat(tmpdir, "/mtXXXXXX");

  char * result = mkdtemp(tmpdir);

  E(result != 0,
    F("mkdtemp(%s) failed: %s") % tmpdir % os_strerror(errno));
  I(result == tmpdir);
  return tmpdir;
}

#if !defined(HAVE_MKDTEMP)
#undef mkdtemp
#endif

map<string, string> orig_env_vars;

static string argv0;
static string firstdir;
static string source_dir;
static string run_dir;
static string testfile;

static int panic_thrower(lua_State * st)
{
  throw oops("lua error");
}

// N.B. some of this code is copied from file_io.cc

namespace
{
  struct fill_vec : public dirent_consumer
  {
    fill_vec(vector<string> & v) : v(v) { v.clear(); }
    virtual void consume(char const * s)
    { v.push_back(s); }

  private:
    vector<string> & v;
  };

  struct file_deleter : public dirent_consumer
  {
    file_deleter(string const & p) : parent(p) {}
    virtual void consume(char const * f)
    {
      string e(parent + "/" + f);
      make_accessible(e);
      do_remove(e);
    }

  private:
    string const & parent;
  };

  struct file_accessible_maker : public dirent_consumer
  {
    file_accessible_maker(string const & p) : parent(p) {}
    virtual void consume(char const * f)
    { make_accessible(parent + "/" + f); }

  private:
    string const & parent;
  };

  struct file_copier : public dirent_consumer
  {
    file_copier(string const & f, string const & t) : from(f), to(t) {}
    virtual void consume(char const * f)
    {
      do_copy_file(from + "/" + f, to + "/" + f);
    }

  private:
    string const & from;
    string const & to;
  };
}

void do_remove_recursive(string const & p)
{
  switch (get_path_status(p))
    {
    case path::directory:
      {
        make_accessible(p);
        vector<string> subdirs;
        struct fill_vec get_subdirs(subdirs);
        struct file_deleter del_files(p);

        do_read_directory(p, del_files, get_subdirs, del_files);
        for(vector<string>::const_iterator i = subdirs.begin();
            i != subdirs.end(); i++)
          do_remove_recursive(p + "/" + *i);
        do_remove(p);
      }
      return;

    case path::file:
      make_accessible(p);
      do_remove(p);
      return;

    case path::nonexistent:
      return;
    }
}

void do_make_tree_accessible(string const & p)
{
  switch (get_path_status(p))
    {
    case path::directory:
      {
        make_accessible(p);
        vector<string> subdirs;
        struct fill_vec get_subdirs(subdirs);
        struct file_accessible_maker access_files(p);

        do_read_directory(p, access_files, get_subdirs, access_files);
        for(vector<string>::const_iterator i = subdirs.begin();
            i != subdirs.end(); i++)
          do_make_tree_accessible(p + "/" + *i);
      }
      return;

    case path::file:
      make_accessible(p);
      return;

    case path::nonexistent:
      return;
    }
}

void do_copy_recursive(string const & from, string to)
{
  path::status fromstat = get_path_status(from);
  
  E(fromstat != path::nonexistent,
    F("Source '%s' for copy does not exist") % from);

  switch (get_path_status(to))
    {
    case path::nonexistent:
      if (fromstat == path::directory)
        do_mkdir(to);
      break;

    case path::file:
      do_remove(to);
      if (fromstat == path::directory)
        do_mkdir(to);
      break;

    case path::directory:
      to = to + "/" + basename(from);
      break;
    }

  if (fromstat == path::directory)
    {
      vector<string> subdirs, specials;
      struct fill_vec get_subdirs(subdirs), get_specials(specials);
      struct file_copier copy_files(from, to);

      do_read_directory(from, copy_files, get_subdirs, get_specials);
      E(specials.empty(), F("cannot copy special files in '%s'") % from);
      for (vector<string>::const_iterator i = subdirs.begin();
           i != subdirs.end(); i++)
        do_copy_recursive(from + "/" + *i, to + "/" + *i);
    }
  else
    do_copy_file(from, to);
}

LUAEXT(posix_umask, )
{
#ifdef WIN32
  lua_pushnil(L);
  return 1;
#else
  unsigned int from = (unsigned int)luaL_checknumber(L, -1);
  mode_t mask = 64*((from / 100) % 10) + 8*((from / 10) % 10) + (from % 10);
  mode_t oldmask = umask(mask);
  int res = 100*(oldmask/64) + 10*((oldmask/8) % 8) + (oldmask % 8);
  lua_pushnumber(L, res);
  return 1;
#endif
}

LUAEXT(chdir, )
{
  try
    {
      string from = get_current_working_dir();
      change_current_working_dir(luaL_checkstring(L, -1));
      lua_pushstring(L, from.c_str());
      return 1;
    }
  catch(informative_failure & e)
    {
      lua_pushnil(L);
      return 1;
    }
}

LUAEXT(remove_recursive, )
{
  try
    {
      do_remove_recursive(luaL_checkstring(L, -1));
      lua_pushboolean(L, true);
      return 1;
    }
  catch(informative_failure & e)
    {
      lua_pushboolean(L, false);
      lua_pushstring(L, e.what());
      return 2;
    }
}

LUAEXT(make_tree_accessible, )
{
  try
    {
      do_make_tree_accessible(luaL_checkstring(L, -1));
      lua_pushboolean(L, true);
      return 1;
    }
  catch(informative_failure & e)
    {
      lua_pushboolean(L, false);
      lua_pushstring(L, e.what());
      return 2;
    }
}

LUAEXT(copy_recursive, )
{
  try
    {
      string from(luaL_checkstring(L, -2));
      string to(luaL_checkstring(L, -1));
      do_copy_recursive(from, to);
      lua_pushboolean(L, true);
      return 1;
    }
  catch(informative_failure & e)
    {
      lua_pushboolean(L, false);
      lua_pushstring(L, e.what());
      return 2;
    }
}

LUAEXT(mkdir, )
{
  try
    {
      char const * dirname = luaL_checkstring(L, -1);
      do_mkdir(dirname);
      lua_pushboolean(L, true);
      return 1;
    }
  catch(informative_failure & e)
    {
      lua_pushnil(L);
      return 1;
    }
}

LUAEXT(make_temp_dir, )
{
  try
    {
      char const * parent;
      parent = getenv("TMPDIR");
      if (parent == 0)
        parent = getenv("TEMP");
      if (parent == 0)
        parent = getenv("TMP");
      if (parent == 0)
        parent = "/tmp";

      char * tmpdir = do_mkdtemp(parent);
      lua_pushstring(L, tmpdir);
      delete [] tmpdir;
      return 1;
    }
  catch(informative_failure & e)
    {
      lua_pushnil(L);
      return 1;
    }
}


LUAEXT(mtime, )
{
  try
    {
      char const * file = luaL_checkstring(L, -1);

      time_t t = get_last_write_time(file);
      if (t == time_t(-1))
        lua_pushnil(L);
      else
        lua_pushnumber(L, t);
      return 1;
    }
  catch(informative_failure & e)
    {
      lua_pushnil(L);
      return 1;
    }
}

LUAEXT(exists, )
{
  try
    {
      char const * name = luaL_checkstring(L, -1);
      switch (get_path_status(name))
        {
        case path::nonexistent:  lua_pushboolean(L, false); break;
        case path::file:
        case path::directory:    lua_pushboolean(L, true); break;
        }
    }
  catch(informative_failure & e)
    {
      lua_pushnil(L);
    }
  return 1;
}

LUAEXT(isdir, )
{
  try
    {
      char const * name = luaL_checkstring(L, -1);
      switch (get_path_status(name))
        {
        case path::nonexistent:
        case path::file:         lua_pushboolean(L, false); break;
        case path::directory:    lua_pushboolean(L, true); break;
        }
    }
  catch(informative_failure & e)
    {
      lua_pushnil(L);
    }
  return 1;
}

namespace
{
  struct build_table : public dirent_consumer
  {
    build_table(lua_State * st) : st(st), n(1)
    {
      lua_newtable(st);
    }
    virtual void consume(const char *s)
    {
      lua_pushstring(st, s);
      lua_rawseti(st, -2, n);
      n++;
    }
  private:
    lua_State * st;
    unsigned int n;
  };
}

LUAEXT(read_directory, )
{
  int top = lua_gettop(L);
  try
    {
      string path(luaL_checkstring(L, -1));
      build_table tbl(L);

      do_read_directory(path, tbl, tbl, tbl);
    }
  catch(informative_failure &)
    {
      // discard the table and any pending path element
      lua_settop(L, top);
      lua_pushnil(L);
    }
  catch (...)
    {
      lua_settop(L, top);
      throw;
    }
  return 1;
}

LUAEXT(get_source_dir, )
{
  lua_pushstring(L, source_dir.c_str());
  return 1;
}

LUAEXT(save_env, )
{
  orig_env_vars.clear();
  return 0;
}

LUAEXT(restore_env, )
{
  for (map<string,string>::const_iterator i = orig_env_vars.begin();
       i != orig_env_vars.end(); ++i)
    set_env(i->first.c_str(), i->second.c_str());
  orig_env_vars.clear();
  return 0;
}

LUAEXT(set_env, )
{
  char const * var = luaL_checkstring(L, -2);
  char const * val = luaL_checkstring(L, -1);
  if (orig_env_vars.find(string(var)) == orig_env_vars.end()) {
    char const * old = getenv(var);
    if (old)
      orig_env_vars.insert(make_pair(string(var), string(old)));
    else
      orig_env_vars.insert(make_pair(string(var), ""));
  }
  set_env(var, val);
  return 0;
}

LUAEXT(unset_env, )
{
  char const * var = luaL_checkstring(L, -1);
  if (orig_env_vars.find(string(var)) == orig_env_vars.end()) {
    char const * old = getenv(var);
    if (old)
      orig_env_vars.insert(make_pair(string(var), string(old)));
    else
      orig_env_vars.insert(make_pair(string(var), ""));
  }
  unset_env(var);
  return 0;
}

LUAEXT(timed_wait, )
{
  pid_t pid = static_cast<pid_t>(luaL_checknumber(L, -2));
  int time = static_cast<int>(luaL_checknumber(L, -1));
  int res;
  int ret;
  ret = process_wait(pid, &res, time);
  lua_pushnumber(L, res);
  lua_pushnumber(L, ret);
  return 2;
}

LUAEXT(require_not_root, )
{
#ifdef WIN32
  bool running_as_root = false;
#else
  bool running_as_root = !geteuid();
#endif
  // E() doesn't work here, I just get "warning: " in the
  // output.  Why?
  if (running_as_root)
    {
      P(F("This test suite cannot be run as the root user.\n"
          "Please try again with a normal user account.\n"));
      exit(1);
    }
  return 0;  
}

// run_tests_in_children (to_run, reporter)

// Run all of the tests in TO_RUN, each in its own isolated directory and
// child process.  As each exits, call REPORTER with the test number and
// name, and the exit status.  If REPORTER returns true, delete the test
// directory, otherwise leave it alone. The point of this exercise is to
// isolate in one C function all the black art involved with safe creation
// and destruction of child processes and their context.
LUAEXT(run_tests_in_children, )
{
  // lua arguments
  const int to_run = 1;
  const int reporter = 2;

  if (lua_gettop(L) != 2)
    return luaL_error(L, "wrong number of arguments");

  luaL_argcheck(L, lua_istable(L, 1), 1, "expected a table");
  luaL_argcheck(L, lua_isfunction(L, 2), 2, "expected a function");

  // iterate over to_run...
  char const * argv[6];
  lua_pushnil(L);
  while (lua_next(L, to_run) != 0)
    {
      luaL_checkinteger(L, -2);
      string testname = luaL_checkstring(L, -1);
      string testdir = run_dir + "/" + testname;
      int status;

      argv[0] = argv0.c_str();
      argv[1] = "-r";
      argv[2] = testfile.c_str();
      argv[3] = firstdir.c_str();
      argv[4] = testname.c_str();
      argv[5] = 0;

      do_remove_recursive(testdir);
      do_mkdir(testdir);

      {
        change_current_working_dir(testdir);
        pid_t child = process_spawn_redirected(NULL_DEVICE,
                                               "tester.log",
                                               "tester.log",
                                               argv);
        change_current_working_dir(run_dir);
        process_wait(child, &status);
      }

      // Set up a call to REPORTER with the appropriate arguments ...
      lua_pushvalue(L, reporter); // t_r r tno tna r
      lua_insert(L, -2);          // t_r r tno r tna
      lua_pushvalue(L, -3);       // t_r r tno r tna tno
      lua_insert(L, -2);          // t_r r tno r tno tna
      lua_pushinteger(L, status); // t_r r tno r tno tna st

      // ... call it ...
      lua_call(L, 3, 1);

      // ... and if it returns true, delete testdir.
      I(lua_isboolean(L, -1));
      if (lua_toboolean(L, -1))
        do_remove_recursive(testdir);

      // pop return value, leaving to_run reporter testno, as expected by
      // lua_next.
      lua_remove(L, -1);
    }
  return 0;
}

int main(int argc, char **argv)
{
  int retcode = 2;
  lua_State *st = 0;

  try
    {
      vector<string> tests_to_run;
      bool want_help = false;
      bool need_help = false;
      bool debugging = false;
      bool list_only = false;
      bool run_one   = false;

      option::concrete_option_set os;
      os("help,h", "display help message", option::setter(want_help))
        ("debug,d", "don't erase per-test directories for successful tests",
         option::setter(debugging))
        ("list,l", "list tests that would be run, but don't run them",
         option::setter(list_only))
        ("run-one,r", "", // internal use only!
         option::setter(run_one))
        ("--", "", option::setter(tests_to_run));

      try
        {
          os.from_command_line(argc, argv);
        }
      catch (option::option_error & e)
        {
          P(F("%s: %s\n") % argv[0] % e.what());
          need_help = true;
        }

      if (tests_to_run.size() == 0)
        {
          P(F("%s: no test suite specified"));
          need_help = true;
        }

      if (run_one && (want_help || debugging || list_only
                      || tests_to_run.size() != 3))
        {
          P(F("%s: incorrect self-invocation") % argv[0]);
          need_help = true;
        }

      if (want_help || need_help)
        {
          P(F("Usage: %s test-file testsuite [options] [tests]\n") % argv[0]);
          P(F("Testsuite: a Lua script defining the test suite to run.\n"));
          P(F("Options:\n%s\n") % os.get_usage_str());
          P(F("Tests may be specified as:\n"
              "  nothing - run all tests.\n"
              "  numbers - run the tests with those numbers\n"
              "            negative numbers count back from the end\n"
              "            ranges may be specified as A..B (inclusive)\n"
              "  regexes - run the tests whose names match (unanchored)\n"));

          return want_help ? 0 : 2;
        }

      st = luaL_newstate();
      lua_atpanic (st, &panic_thrower);
      luaL_openlibs(st);
      add_functions(st);

      if (run_one)
        {
          // This is a self-invocation, requesting that we actually run a
          // single named test.  Contra the help above, the command line
          // arguments are the absolute pathname of the testsuite definition,
          // the original working directory, and the name of the test, in
          // that order.  No other options are valid in combination with -r.
          // We have been invoked inside the directory where we should run
          // the test.  Stdout and stderr have been redirected to a per-test
          // logfile.

          lua_pushstring(st, tests_to_run[1].c_str());
          lua_setglobal(st, "initial_dir");

          source_dir = dirname(tests_to_run[0]);

          run_string(st, testlib_constant, "testlib.lua");
          run_file(st, tests_to_run[0].c_str());

          Lua ll(st);
          ll.func("run_one_test");
          ll.push_str(tests_to_run[2]);
          ll.call(1,1)
            .extract_int(retcode);
        }
      else
        {
          firstdir = get_current_working_dir();
          run_dir = firstdir + "/tester_dir";
          testfile = tests_to_run.front();

          if (argv[0][0] == '/'
#ifdef WIN32
              || argv[0][0] != '\0' && argv[0][1] == ':'
#endif
              )
            argv0 = argv[0];
          else
            argv0 = firstdir + "/" + argv[0];

          change_current_working_dir(dirname(testfile));
          source_dir = get_current_working_dir();
          testfile = source_dir + "/" + basename(testfile);

          switch (get_path_status(run_dir))
            {
            case path::directory: break;
            case path::file:
              P(F("cannot create directory '%s': it is a file") % run_dir);
              return 1;
            case path::nonexistent:
              do_mkdir(run_dir);
            }

          change_current_working_dir(run_dir);

          run_string(st, testlib_constant, "testlib.lua");
          lua_pushstring(st, firstdir.c_str());
          lua_setglobal(st, "initial_dir");
          run_file(st, testfile.c_str());

          // arrange for isolation between different test suites running in
          // the same build directory.
          lua_getglobal(st, "testdir");
          const char *testdir = lua_tostring(st, 1);
          I(testdir);
          string testdir_base = basename(testdir);
          run_dir = run_dir + "/" + testdir_base;
          string logfile = run_dir + ".log";
          switch (get_path_status(run_dir))
            {
            case path::directory: break;
            case path::file:
              P(F("cannot create directory '%s': it is a file") % run_dir);
              return 1;
            case path::nonexistent:
              do_mkdir(run_dir);
            }

          Lua ll(st);
          ll.func("run_tests");
          ll.push_bool(debugging);
          ll.push_bool(list_only);
          ll.push_str(run_dir);
          ll.push_str(logfile);
          ll.push_table();
          for (int i = 2; i < argc; ++i)
            {
              ll.push_int(i-1);
              ll.push_str(argv[i]);
              ll.set_table();
            }
          ll.call(5,1)
            .extract_int(retcode);
        }
    }
  catch (informative_failure & e)
    {
      P(F("%s\n") % e.what());
      retcode = 1;
    }
  catch (std::logic_error & e)
    {
      P(F("Invariant failure: %s\n") % e.what());
      retcode = 3;
    }
  catch (std::exception & e)
    {
      P(F("Uncaught exception: %s") % e.what());
      retcode = 3;
    }
  catch (...)
    {
      P(F("Uncaught exception of unknown type"));
      retcode = 3;
    }

  if (st)
    lua_close(st);
  return retcode;
}

// The functions below are used by option.cc, and cloned from ui.cc and
// simplestring_xform.cc, which we cannot use here.  They do not cover
// several possibilities handled by the real versions.

unsigned int
guess_terminal_width()
{
  unsigned int w = terminal_width();
  if (!w)
    w = 80; // can't use constants:: here
  return w;
}

static void
split_into_lines(string const & in, vector<string> & out)
{
  out.clear();
  string::size_type begin = 0;
  string::size_type end = in.find_first_of("\r\n", begin);
  while (end != string::npos && end >= begin)
    {
      out.push_back(in.substr(begin, end-begin));
      if (in.at(end) == '\r'
          && in.size() > end+1
          && in.at(end+1) == '\n')
        begin = end + 2;
      else
        begin = end + 1;
      if (begin >= in.size())
        break;
      end = in.find_first_of("\r\n", begin);
    }
  if (begin < in.size())
    out.push_back(in.substr(begin, in.size() - begin));
}

static vector<string> split_into_words(string const & in)
{
  vector<string> out;

  string::size_type begin = 0;
  string::size_type end = in.find_first_of(" ", begin);

  while (end != string::npos && end >= begin)
    {
      out.push_back(in.substr(begin, end-begin));
      begin = end + 1;
      if (begin >= in.size())
        break;
      end = in.find_first_of(" ", begin);
    }
  if (begin < in.size())
    out.push_back(in.substr(begin, in.size() - begin));

  return out;
}

// See description for format_text below for more details.
static string
format_paragraph(string const & text, size_t const col, size_t curcol)
{
  I(text.find('\n') == string::npos);

  string formatted;
  if (curcol < col)
    {
      formatted = string(col - curcol, ' ');
      curcol = col;
    }

  const size_t maxcol = guess_terminal_width();

  vector< string > words = split_into_words(text);
  for (vector< string >::const_iterator iter = words.begin();
       iter != words.end(); iter++)
    {
      string const & word = *iter;

      if (iter != words.begin() &&
          curcol + word.size() + 1 > maxcol)
        {
          formatted += '\n' + string(col, ' ');
          curcol = col;
        }
      else if (iter != words.begin())
        {
          formatted += ' ';
          curcol++;
        }

      formatted += word;
      curcol += word.size();
    }

  return formatted;
}

// Reformats the given text so that it fits in the current screen with no
// wrapping.
//
// The input text is a series of words and sentences.  Paragraphs may be
// separated with a '\n' character, which is taken into account to do the
// proper formatting.  The text should not finish in '\n'.
//
// 'col' specifies the column where the text will start and 'curcol'
// specifies the current position of the cursor.
string
format_text(string const & text, size_t const col, size_t curcol)
{
  I(curcol <= col);

  string formatted;

  vector< string > lines;
  split_into_lines(text, lines);
  for (vector< string >::const_iterator iter = lines.begin();
       iter != lines.end(); iter++)
    {
      string const & line = *iter;

      formatted += format_paragraph(line, col, curcol);
      if (iter + 1 != lines.end())
        formatted += "\n\n";
      curcol = 0;
    }

  return formatted;
}



// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
