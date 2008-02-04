#include "base.hh"
#include "lua.hh"
#include "paths.hh"
#include "platform.hh"
#include "platform-wrapped.hh"
#include "tester-plaf.hh"
#include "vector.hh"
#include "sanity.hh"
#include <boost/lexical_cast.hpp>
#include <botan/hex.h>
#include <botan/pipe.h>
#include <cstring>

using std::string;
using std::map;
using std::vector;
using boost::lexical_cast;
using boost::bad_lexical_cast;

// defined in testlib.c, generated from testlib.lua
extern char const testlib_constant[];

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

// Ensure the existence of DIR before proceeding.
static void ensure_dir(string const & dir)
{
  try
    {
      do_mkdir(dir);
    }
  catch (informative_failure &)
    {
      if (get_path_status(dir) != path::directory)
        throw;
    }
}

map<string, string> orig_env_vars;

static string argv0;
static string firstdir;
static string source_dir;
static string run_dir;
static string testfile;

static int panic_thrower(lua_State * st)
{
  throw oops((FL("lua error: %s\n") % luaL_checkstring(st, -1)).str().c_str());
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

// For convenience in calling from Lua (which has no syntax for writing
// octal numbers) this function takes a three-digit *decimal* number and
// treats each digit as octal.  For example, 777 (decimal) is converted to
// 0777 (octal) for the system call.  Note that the system always forces the
// high three bits of the supplied mode to zero; i.e. it is impossible to
// have the setuid, setgid, or sticky bits on in the process umask.
// Therefore, there is no point accepting arguments higher than 777.
LUAEXT(posix_umask, )
{
  unsigned int decmask = (unsigned int)luaL_checknumber(L, -1);
  E(decmask <= 777,
    F("invalid argument %d to umask") % decmask);

  unsigned int a = decmask / 100  % 10;
  unsigned int b = decmask / 10   % 10;
  unsigned int c = decmask / 1    % 10;

  E(a <= 7 && b <= 7 && c <= 7,
    F("invalid octal number %d in umask") % decmask);

  int oldmask = do_umask((a*8 + b)*8 + c);
  if (oldmask == -1)
    {
      lua_pushinteger(L, 0);
      return 1;
    }
  else
    {
      a = ((unsigned int)oldmask) / 64 % 8;
      b = ((unsigned int)oldmask) / 8  % 8;
      c = ((unsigned int)oldmask) / 1  % 8;

      lua_pushinteger(L, (a*10 + b)*10 + c);
      return 1;
    }
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
      char * tmpdir = make_temp_dir();

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
  // E() doesn't work here, I just get "warning: " in the
  // output.  Why?
  if (running_as_root())
    {
      P(F("This test suite cannot be run as the root user.\n"
          "Please try again with a normal user account.\n"));
      exit(1);
    }
  return 0;
}

// run_tests_in_children (to_run, reporter)
//
// Run all of the tests in TO_RUN, each in its own isolated directory and
// child process.  As each exits, call REPORTER with the test number and
// name, and the exit status.  If REPORTER returns true, delete the test
// directory, otherwise leave it alone.
//
// The meat of the work done by this function is so system-specific that it
// gets shoved off into tester-plaf.cc.  However, all interaction with the
// Lua layer needs to remain in this file, so we have a mess of callback
// "closures" (or as close as C++ lets you get, anyway).

// Iterate over the Lua table containing all the tests to run.
bool test_enumerator::operator()(test_to_run & next_test) const
{
  int top = lua_gettop(st);
  luaL_checkstack(st, 2, "preparing to retrieve next test");

  lua_rawgeti(st, LUA_REGISTRYINDEX, table_ref);
  if (iteration_begun)
    lua_pushinteger(st, last_index);
  else
    lua_pushnil(st);

  if (lua_next(st, -2) == 0)
    {
      lua_settop(st, top);
      return false;
    }
  else
    {
      iteration_begun = true;
      next_test.number = last_index = luaL_checkinteger(st, -2);
      next_test.name = luaL_checkstring(st, -1);
      lua_settop(st, top);
      return true;
    }
}

// Invoke one test case in the child.  This may be called by
// run_tests_in_children, or by main, because Windows doesn't have fork.
// It is not allowed to write to standard output or standard error under
// any circumstances whatsoever.  Not calling lua_close is deliberate.

int test_invoker::operator()(std::string const & testname) const
{
  int retcode;
  try
    {
      luaL_checkstack(st, 2, "preparing call to run_one_test");
      lua_getglobal(st, "run_one_test");
      I(lua_isfunction(st, -1));

      lua_pushstring(st, testname.c_str());
      lua_call(st, 1, 1);

      retcode = luaL_checkinteger(st, -1);
      lua_remove(st, -1);
    }
  catch (std::exception & e)
    {
      E(false, F("test %s: %s") % testname % e.what());
      retcode = 124;
    }
  return retcode;
}


// Clean up after one child process.

bool test_cleaner::operator()(test_to_run const & test,
                              int status) const
{
  // call reporter(testno, testname, status)
  luaL_checkstack(st, 4, "preparing call to reporter");

  lua_rawgeti(st, LUA_REGISTRYINDEX, reporter_ref);
  lua_pushinteger(st, test.number);
  lua_pushstring(st, test.name.c_str());
  lua_pushinteger(st, status);
  lua_call(st, 3, 1);

  // return is a boolean.  There is, for no apparent reason, no
  // luaL_checkboolean().
  I(lua_isboolean(st, -1));
  bool ret = lua_toboolean(st, -1);
  lua_remove(st, -1);
  return ret;
}

LUAEXT(run_tests_in_children, )
{
  if (lua_gettop(L) != 2)
    return luaL_error(L, "wrong number of arguments");

  luaL_argcheck(L, lua_istable(L, 1), 1, "expected a table");
  luaL_argcheck(L, lua_isfunction(L, 2), 2, "expected a function");

  int reporter_ref = luaL_ref(L, LUA_REGISTRYINDEX);
  int table_ref = luaL_ref(L, LUA_REGISTRYINDEX);

  run_tests_in_children(test_enumerator(L, table_ref),
                        test_invoker(L),
                        test_cleaner(L, reporter_ref),
                        run_dir, argv0, testfile, firstdir);

  luaL_unref(L, LUA_REGISTRYINDEX, table_ref);
  luaL_unref(L, LUA_REGISTRYINDEX, reporter_ref);
  return 0;
}

// Write all arguments to standard output.  This is not a normal LUAEXT
// because it is only made available to run_tests as an argument, not
// established as globally visible.  (Only a very limited number of places
// at the Lua level are allowed to talk to standard output.)
int run_tests_progress(lua_State *st)
{
  int n = lua_gettop(st);
  for (int i = 1; i <= n; i++)
    fputs(luaL_checkstring(st, i), stdout);
  return 0;
}

// RAII wrapper around a Lua state structure; also takes care of doing the
// initialization as we want it.  Of note is that we do not want any
// Lua-level code getting its grubby fingers on stdin/out/err, so we have to
// take just about everything out of the io table, and we do not trust
// testlib.lua to do this for us.

namespace {
  struct lua_lib
  {
    lua_lib(string const & initial_dir, string const & suite);
    ~lua_lib() { lua_close(st); }
    lua_State * operator()() { return st; }
  private:
    lua_State * st;
  };
  lua_lib::lua_lib(string const & initial_dir, string const & suite)
      : st(luaL_newstate())
    {
      static char const * const allowed_io_funcs[] = {
        "open", "lines", "type", "tmpfile"
      };

      lua_atpanic (st, &panic_thrower);
      luaL_openlibs(st);
      add_functions(st);

      lua_getglobal(st, "io");
      lua_newtable(st);

      for (unsigned int i = 0;
           i < sizeof allowed_io_funcs / sizeof allowed_io_funcs[0]; i++)
        {
          // this looks like it's a no-op, but the trick is that stack element
          // -2 is the original "io" table in the getfield operation, but the
          // new table we are constructing in the setfield operation (because
          // getfield leaves its value at top of stack, and setfield pops it).
          lua_getfield(st, -2, allowed_io_funcs[i]);
          lua_setfield(st, -2, allowed_io_funcs[i]);
        }

      lua_remove(st, -2); // oldtable newtable -- newtable

      // establish our new table as the value of
      // package.loaded["io"].
      lua_getglobal(st, "package");         // -- newtable package
      lua_getfield(st, -1, "loaded");       // -- newtable package loaded
      lua_remove(st, -2);                   // -- newtable loaded
      lua_pushvalue(st, -2);                // -- newtable loaded newtable
      lua_setfield(st, -2, "io");           // -- newtable loaded
      lua_remove(st, -1);                   // -- newtable

      // also establish it as the value of the global "io" variable.
      lua_setglobal(st, "io");              // --

      // we can now load testlib.lua.
      run_string(st, testlib_constant, "testlib.lua");

      // the suite definition may know the initial working directory.
      lua_pushstring(st, initial_dir.c_str());
      lua_setglobal(st, "initial_dir");

      run_file(st, suite.c_str());
    }
}

// This function is cloned from simplestring_xform.cc, which we cannot use
// here.  It does not cover several possibilities handled by the real
// version but of no interest here.

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


// Parse a boolean command line option: if ARG is either SHORTOPT or
// LONGOPT, return true, else false.
static bool
bool_option(char const * arg, char const * shortopt, char const * longopt)
{
  return ((shortopt && !strcmp(arg, shortopt))
          || (longopt && !strcmp(arg, longopt)));
}

// Parse an integer-valued command line option: if ARG is either SHORTOPT
// or LONGOPT and a decimal integer follows, write that integer to VAL and
// return true, else leave VAL untouched and return false.
static bool
int_option(char const * arg, char const * shortopt, char const * longopt,
           int & val)
{
  if (shortopt && !strncmp(arg, shortopt, strlen(shortopt)))
    {
      char *end;
      int v = strtoul(arg + strlen(shortopt), &end, 10);
      if (end != arg + strlen(shortopt) && *end == '\0')
        {
          val = v;
          return true;
        }
    }

  if (longopt && !strncmp(arg, longopt, strlen(longopt)))
    {
      char *end;
      int v = strtoul(arg + strlen(longopt), &end, 10);
      if (end != arg + strlen(longopt) && *end == '\0')
        {
          val = v;
          return true;
        }
    }

  return false;
}

// Parse a two-integer-valued command line option: if ARG begins with OPT
// and continues with a pair of decimal integers separated by a comma, write
// the integers to VAL1 and VAL2 and return true; else leave VAL1 and VAL2
// untouched and return false.
static bool
int_int_option(char const * arg, char const * opt, int & val1, int & val2)
{
  if (strncmp(arg, opt, strlen(opt)))
    return false;

  char *end1, *end2, *p;
  int v1, v2;

  p = const_cast<char *>(arg + strlen(opt));

  v1 = strtoul(p, &end1, 10);

  if (end1 == p || *end1 != ',')
    return false;

  v2 = strtoul(end1 + 1, &end2, 10);

  if (end1 == end2 || *end2 != '\0')
    return false;

  val1 = v1;
  val2 = v2;
  return true;
}

// Extract parallelization-related options from MFLAGS.  We can rely on
// Make to pass these arguments in a particular form:
// -j [N]   no more than N parallel jobs (absent = no limit)
// -l [N]   no more jobs if the system load average rises above N
//          (absent = no limit) (not supported except with no N)
// --jobserver-fds=M,N  talk to a job server on fds M and N to limit
//                      concurrency
// Anything else in MFLAGS is ignored.
// The first word in MFLAGS should have a dash prepended to it unless it
// already has one.

static void
parse_makeflags(char const * mflags,
                int & jobs,
                int & jread,
                int & jwrite)
{
  if (mflags == 0)
    return;

  while (*mflags == ' ') mflags++;

  vector<string> mf(split_into_words(mflags));

  if (mf.size() == 0 || (mf.size() == 1 && mf[0] == ""))
    return;

  if (mf[0][0] != '-')
    mf[0] = string("-") + mf[0];

  int jxx = 0;
  for (vector<string>::const_iterator i = mf.begin(); i != mf.end(); i++)
    {
      if (*i == "-j")
        {
          jxx = -1;
          i++;
          if (i == mf.end())
            break;
          try
            {
              jxx = lexical_cast<int>(*i);
              if (jxx <= 0)
                {
                  W(F("-j %d makes no sense, option ignored") % jxx);
                  jxx = 0;
                }
            }
          catch (bad_lexical_cast &)
            {
              i--;
            }
        }
      else if (*i == "-l")
        {
          i++;
          if (i == mf.end())
            break;
          try
            {
              double dummy = lexical_cast<double>(*i);
              W(F("no support for -l %f: forcing -j1") % dummy);
              jxx = 1;
            }
          catch (bad_lexical_cast &)
            {
              i--;
            }
        }
      else if (int_int_option(i->c_str(), "--jobserver-fds=", jread, jwrite))
        ;
    }

  // do not permit -j in MAKEFLAGS to override -j on the command line.
  if (jxx != 0 && jobs == 0)
    jobs = jxx;
}

static void
parse_command_line(int argc, char const * const * argv,
                   bool & want_help, bool & need_help,
                   bool & debugging, bool & list_only,
                   bool & run_one, int & jobs,
                   vector<string> & tests_to_run)
{
  int i;
  int jxx = 0;

  for (i = 1; i < argc; i++)
    {
      if (string(argv[i]) == "--")
        break;

      if (bool_option(argv[i], "-h", "--help"))
        want_help = true;
      else if (bool_option(argv[i], "-d", "--debug"))
        debugging = true;
      else if (bool_option(argv[i], "-l", "--list-only"))
        list_only = true;
      else if (bool_option(argv[i], "-r", 0))
        run_one = true;
      else if (bool_option(argv[i], "-j", "--jobs"))
        {
          // if there turns out not to be a number, this is -j infinity.
          jxx = -1;

          if (i+1 < argc)
            try
              {
                jxx = lexical_cast<int>(argv[i]);
                if (jxx <= 0)
                  {
                    W(F("-j %d makes no sense, option ignored") % jxx);
                    jxx = 0;
                  }
                i++;
              }
            catch (bad_lexical_cast &)
              {
                // it wasn't a number.
              }
        }
      else if (int_option(argv[i], "-j", "--jobs=", jobs))
        /* no action required */;
      else if (argv[i][0] == '-')
        {
          P(F("unrecognized option '%s'") % argv[i]);
          need_help = true;
        }
      else
        tests_to_run.push_back(argv[i]);
    }

  // all argv elements from i+1 to argc go into tests_to_run without further
  // interpretation.
  if (i < argc)
    for (i++; i < argc; i++)
      tests_to_run.push_back(argv[i]);

  if (jxx != 0)
    jobs = jxx;

  E(!run_one || (!want_help && !debugging && !list_only
                 && tests_to_run.size() == 3 && jobs == 0),
    F("incorrect self-invocation; -r <abs path to lua-testsuite.lua> <abs path to tester_dir> <test>"));

  if (tests_to_run.size() == 0)
    {
      P(F("%s: no test suite specified\n") % argv[0]);
      need_help = true;
    }
}

int main(int argc, char **argv)
{
  int retcode = 2;

  vector<string> tests_to_run;
  bool want_help = false;
  bool need_help = false;
  bool debugging = false;
  bool list_only = false;
  bool run_one   = false;
  int  jobs      = 0;
  int  jread     = -1;
  int  jwrite    = -1;

  try
    {
      global_sanity.initialize(argc, argv, "C");
      parse_command_line(argc, argv,
                         want_help, need_help, debugging, list_only,
                         run_one, jobs, tests_to_run);

      parse_makeflags(getenv("MAKEFLAGS"), jobs, jread, jwrite);

      if (want_help || need_help)
        {
          P(F("Usage: %s test-file testsuite [options] [tests]\n") % argv[0]);
          P(F("Testsuite: a Lua script defining the test suite to run.\n"
              "Options:\n"
              "  -l, --list     just list tests that would be run\n"
              "  -d, --debug    don't erase working dirs of successful tests\n"
              "  -j N, --jobs=N run N test cases in parallel\n"
              "                 (note: unlike make, the N is not optional)\n"
              "  -h, --help     display this help message\n"
              // -r is deliberately not mentioned.
              "Tests may be specified as:\n"
              "  nothing - run all tests.\n"
              "  numbers - run the tests with those numbers\n"
              "            negative numbers count back from the end\n"
              "            ranges may be specified as A..B (inclusive)\n"
              "  regexes - run the tests whose names match (unanchored)\n"));

          return want_help ? 0 : 2;
        }

      if (jobs == 0) // no setting from command line or MAKEFLAGS
        jobs = 1;

      if (run_one)
        {
#ifdef WIN32
          // This is a self-invocation, requesting that we actually run a
          // single named test.  Contra the help above, the command line
          // arguments are the absolute pathname of the testsuite definition,
          // the original working directory, and the name of the test, in
          // that order.  No other options are valid in combination with -r.
          // We have been invoked inside the directory where we should run
          // the test.  Stdout and stderr have been redirected to a per-test
          // logfile.
          source_dir = dirname(tests_to_run[0]);
          lua_lib st(tests_to_run[1], tests_to_run[0]);
          return test_invoker(st())(tests_to_run[2]);
#else
          E(false, F("self-invocation should not be used on Unix\n"));
#endif
        }
      else
        {
          firstdir = get_current_working_dir();
          run_dir = firstdir + "/tester_dir";
          testfile = tests_to_run.front();

#if defined(WIN32)
          char name[MAX_PATH];
          int len = 0;
          len = (int)GetModuleFileName(0, name, MAX_PATH);
          if(len != 0) {
            argv0 = system_path(name).as_external();
          }
#else
          if (argv[0][0] == '/')
            argv0 = argv[0];
          else
            argv0 = firstdir + "/" + argv[0];
#endif

          change_current_working_dir(dirname(testfile));
          source_dir = get_current_working_dir();
          testfile = source_dir + "/" + basename(testfile);

          ensure_dir(run_dir);
          change_current_working_dir(run_dir);

          lua_lib st(firstdir, testfile);

          // arrange for isolation between different test suites running in
          // the same build directory.
          lua_getglobal(st(), "testdir");
          const char *testdir = lua_tostring(st(), 1);
          I(testdir);
          string testdir_base = basename(testdir);
          run_dir = run_dir + "/" + testdir_base;
          string logfile = run_dir + ".log";

          ensure_dir(run_dir);

          prepare_for_parallel_testcases(jobs, jread, jwrite);

          Lua ll(st());
          ll.func("run_tests");
          ll.push_bool(debugging);
          ll.push_bool(list_only);
          ll.push_str(run_dir);
          ll.push_str(logfile);
          ll.push_table();
          // i = 1 skips the first element of tests_to_run, which is the
          // testsuite definition.
          for (unsigned int i = 1; i < tests_to_run.size(); i++)
            {
              ll.push_int(i);
              ll.push_str(tests_to_run.at(i).c_str());
              ll.set_table();
            }

          // the Lua object doesn't wrap this
          if (ll.ok())
            lua_pushcfunction(st(), run_tests_progress);

          ll.call(6,1)
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

  return retcode;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
