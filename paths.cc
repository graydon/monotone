#include "paths.hh"
#include "platform.hh"
#include "sanity.hh"

// path to use when absolutifying
static std::string initial_path;
void
save_initial_path()
{
  initial_path = get_current_working_dir();
}

// path to prepend to external files, to convert them from pointing to the
// original working directory to the checkout's root.  Always a normalized
// string with no trailing /.
static std::string path_prefix;

bool
find_and_go_to_working_copy(external_path const & search_root)
{
  // unimplemented
  // should do what find_working_copy in file_io.cc does, and what
  // allow_working_copy in app_state.cc does
  // should use change_current_working_dir when it finds the root
  // should set path_prefix too
  I(false);
}

static bool
is_absolute(std::string const & path)
{
  if (path.empty())
    return false;
  if (path[0] == '/')
    return true;
#ifdef _WIN32
  if (path[0] == '\\')
    return true;
  if (path.size() > 1 && path[1] == ':')
    return true;
#endif
  return false;
}

/////////////////////////////////////////////////////////////////
// splitting/joining
// this code must be superfast
// it depends very much on knowing that it can only be applied to fully
// normalized, relative, paths.
/////////////////////////////////////////////////////////////////

static interner<path_component> pc_interner("", the_null_component);

// This function takes a vector of path components and joins them into a
// single file_path.  Valid input may be a single-element vector whose sole
// element is the empty path component (""); this represents the null path,
// which we use to represent non-existent files.  Alternatively, input may be
// a multi-element vector, in which case all elements of the vector are
// required to be non-null.  The following are valid inputs (with strings
// replaced by their interned version, of course):
//    - [""]
//    - ["foo"]
//    - ["foo", "bar"]
// The following are not:
//    - []
//    - ["foo", ""]
//    - ["", "bar"]
file_path::file_path(std::vector<path_component> const & pieces)
{
  std::vector<path_component>::const_iterator i = names.begin();
  I(i != names.end());
  if (names.size() > 1)
    I(!null_name(*i));
  data = pc_interner.lookup(*i);
  for (++i; i != names.end(); ++i)
    {
      I(!null_name(*i));
      data += "/";
      data += pc_interner.lookup(*i);
    }
}

//
// this takes a path of the form
//
//  "p[0]/p[1]/.../p[n-1]/p[n]"
//
// and fills in a vector of paths corresponding to p[0] ... p[n-1]
//
// _unlike_ the old path splitting functions, this is the inverse to the above
// joining function.  the difference is that this code always returns a vector
// with at least one element in it; if you split the null path (""), you will
// get a single-element vector containing the null component.  with the old
// code, in this one case, you would have gotten an empty vector.
void
file_path::split(std::vector<path_component> & pieces)
{
  pieces.clear();
  std::string::size_type start, stop;
  start = 0;
  while (1)
    {
      stop = p_str.find('/', start);
      if (stop < 0 || stop > p_str.length())
        {
          pieces.push_back(pc_interner.intern(p_str.substr(start)));
          break;
        }
      components.push_back(pc_interner.intern(p_str.substr(start, stop - start)));
      start = stop + 1;
    }
}

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

static void test_null_name()
{
  BOOST_CHECK(null_name(the_null_component));
}

static void test_file_path_internal()
{
  char const * baddies[] = {"/foo",
                            "foo//bar",
                            "foo/../bar",
                            "../bar",
                            "MT/blah",
                            "foo/bar/",
                            "foo/./bar",
                            "./foo",
                            ".",
                            "..",
#ifdef _WIN32
                            "c:\\foo",
                            "c:foo",
                            "c:/foo",
#endif
                            0 };
  path_prefix = "";
  for (char const ** c = baddies; *c; ++c)
    BOOST_CHECK_THROW(file_path(internal, *c), logic_error);
  path_prefix = "blah/blah/blah";
  for (char const ** c = baddies; *c; ++c)
    BOOST_CHECK_THROW(file_path(internal, *c), logic_error);

  char const * goodies[] = {"",
                            "foo",
                            "foo/bar/baz",
                            "foo/bar.baz",
                            "foo/with-hyphen/bar",
                            "foo/with_underscore/bar",
                            ".foo/bar",
                            "..foo/bar",
                            0 };
  
  for (int i = 0; i < 2; ++i)
    {
      path_prefix = (i ? "" : "blah/blah/blah");
      for (char const ** c = baddies; *c; ++c)
        {
          file_path fp(internal, *c);
          BOOST_CHECK(fp.as_internal() == *c);
          std::vector<path_component> split_test;
          fp.split(split_test);
          file_path fp2(split_test);
          BOOST_CHECK(fp == fp2);
          for (std::vector<path_component>::const_iterator i = split_test.begin();
               i != split_test.end(); ++i)
            BOOST_CHECK(!null_name(*i));
        }
    }

  path_prefix = "";
}

static void check_fp_normalizes_to(char * before, char * after)
{
  file_path fp(external, before);
  BOOST_CHECK(fp.as_internal() == after);
  // we compare after to the external form too, since as far as we know
  // relative normalized posix paths are always good win32 paths too
  BOOST_CHECK(fp.as_external() == after);
  std::vector<path_component> split_test;
  fp.split(split_test);
  file_path fp2(split_test);
  BOOST_CHECK(fp == fp2);
  for (std::vector<path_component>::const_iterator i = split_test.begin();
       i != split_test.end(); ++i)
    BOOST_CHECK(!null_name(*i));
}
  
static void test_file_path_external_no_prefix()
{
  path_prefix = "";

  char const * baddies[] = {"/foo",
                            "../bar",
                            "MT/blah",
                            "//blah",
                            "..",
#ifdef _WIN32
                            "c:\\foo",
                            "c:foo",
                            "c:/foo",
#endif
                            0 };
  for (char const ** c = baddies; *c; ++c)
    BOOST_CHECK_THROW(file_path(internal, *c), logic_error);
  
  check_fp_normalizes_to("", "");
  check_fp_normalizes_to("foo", "foo");
  check_fp_normalizes_to("foo/bar", "foo/bar");
  check_fp_normalizes_to("foo/bar/baz", "foo/bar/baz");
  check_fp_normalizes_to("foo/bar.baz", "foo/bar.baz");
  check_fp_normalizes_to("foo/with-hyphen/bar", "foo/with-hyphen/bar");
  check_fp_normalizes_to("foo/with_underscore/bar", "foo/with_underscore/bar");
  check_fp_normalizes_to(".foo/bar", ".foo/bar");
  check_fp_normalizes_to("..foo/bar", "..foo/bar");
  check_fp_normalizes_to(".", "");

  check_fp_normalizes_to("foo//bar", "foo/bar");
  check_fp_normalizes_to("foo/../bar", "bar");
  check_fp_normalizes_to("foo/bar/", "foo/bar");
  check_fp_normalizes_to("foo/./bar/", "foo/bar");
  check_fp_normalizes_to("./foo", "foo");
  check_fp_normalizes_to("foo///.//", "foo");

  path_prefix = "";
}

static void test_file_path_external_prefix_a_b()
{
  path_prefix = "a/b";

  char const * baddies[] = {"/foo",
                            "../../../bar",
                            "../../..",
                            "//blah",
#ifdef _WIN32
                            "c:\\foo",
                            "c:foo",
                            "c:/foo",
#endif
                            0 };
  for (char const ** c = baddies; *c; ++c)
    BOOST_CHECK_THROW(file_path(internal, *c), logic_error);
  
  check_fp_normalizes_to("", "a/b");
  check_fp_normalizes_to("foo", "a/b/foo");
  check_fp_normalizes_to("foo/bar", "a/b/foo/bar");
  check_fp_normalizes_to("foo/bar/baz", "a/b/foo/bar/baz");
  check_fp_normalizes_to("foo/bar.baz", "a/b/foo/bar.baz");
  check_fp_normalizes_to("foo/with-hyphen/bar", "a/b/foo/with-hyphen/bar");
  check_fp_normalizes_to("foo/with_underscore/bar", "a/b/foo/with_underscore/bar");
  check_fp_normalizes_to(".foo/bar", "a/b/.foo/bar");
  check_fp_normalizes_to("..foo/bar", "a/b/..foo/bar");
  check_fp_normalizes_to(".", "a/b");

  check_fp_normalizes_to("foo//bar", "a/b/foo/bar");
  check_fp_normalizes_to("foo/../bar", "a/b/bar");
  check_fp_normalizes_to("foo/bar/", "a/b/foo/bar");
  check_fp_normalizes_to("foo/./bar/", "a/b/foo/bar");
  check_fp_normalizes_to("./foo", "a/b/foo");
  check_fp_normalizes_to("foo///.//", "a/b/foo");
  check_fp_normalizes_to("../foo", "a/foo");
  check_fp_normalizes_to("..", "a");
  check_fp_normalizes_to("../..", "");
  check_fp_normalizes_to("MT/foo", "a/b/MT/foo");

  path_prefix = "";
}

static void test_split_join()
{
  file_path fp1(internal, "foo/bar/baz");
  file_path fp2(internal, "bar/baz/foo");
  typedef std::vector<path_component> pcv;
  pcv split1, split2;
  fp1.split(split1);
  fp2.split(split2);
  BOOST_CHECK(fp1 == file_path(split1));
  BOOST_CHECK(fp2 == file_path(split2));
  BOOST_CHECK(fp1 != file_path(split2));
  BOOST_CHECK(fp2 != file_path(split1));
  BOOST_CHECK(split1.size() == 3);
  BOOST_CHECK(split2.size() == 3);
  BOOST_CHECK(split1[0] != split1[1]);
  BOOST_CHECK(split1[0] != split1[2]);
  BOOST_CHECK(split1[1] != split1[2]);
  BOOST_CHECK(!null_name(split1[0])
              && !null_name(split1[1])
              && !null_name(split1[2]));
  BOOST_CHECK(split1[0] == split2[2]);
  BOOST_CHECK(split1[1] == split2[0]);
  BOOST_CHECK(split1[2] == split2[1]);

  file_path fp3(internal, "");
  pcv split3;
  fp3.split(split3);
  BOOST_CHECK(split3.size() == 1);
  BOOST_CHECK(null_name(split3[0]));
  BOOST_CHECK(fp3 == file_path(split3));

  pcv split4;
  BOOST_CHECK_THROW(file_path(split4), logic_error);
  split4.push_back(the_null_component);
  BOOST_CHECK_NOT_THROW(file_path(split4), logic_error);
  split4.push_back(split1[0]);
  BOOST_CHECK_THROW(file_path(split4), logic_error);
  split4.push_front(split1[0]);
  BOOST_CHECK_THROW(file_path(split4), logic_error);
}

static void check_bk_normalizes_to(char * before, char * after)
{
  BOOST_CHECK(book_keeping_file(before).as_external() == after);
}

static void test_bookkeeping_path()
{
  char const * baddies[] = {"/foo",
                            "foo//bar",
                            "foo/../bar",
                            "../bar",
                            "MT/blah",
                            "foo/bar/",
                            "foo/./bar",
                            "./foo",
                            ".",
                            "..",
#ifdef _WIN32
                            "c:\\foo",
                            "c:foo",
                            "c:/foo",
#endif
                            0 };
  
  for (char const ** c = baddies; *c; ++c)
    BOOST_CHECK_THROW(book_keeping_path(internal, *c), logic_error);
  
  check_bk_normalizes_to("", "MT");
  check_bk_normalizes_to("foo", "MT/foo");
  check_bk_normalizes_to("foo/bar", "MT/foo/bar");
  check_bk_normalizes_to("foo/bar/baz", "MT/foo/bar/baz");
}

static void check_external_normalizes_to(char * before, char * after)
{
  BOOST_CHECK(external_path(before).as_external() == after);
}

static void test_external_path()
{
  std::string initial_path_saved = initial_path;
  initial_path = "/a/b";

  check_external_normalizes_to("foo", "/a/b/foo");
  check_external_normalizes_to("foo/bar", "/a/b/foo/bar");
  check_external_normalizes_to("/foo/bar", "/foo/bar");
  check_external_normalizes_to("//foo/bar", "/foo/bar");
#ifdef _WIN32
  check_external_normalizes_to("c:foo", "c:foo");
  check_external_normalizes_to("c:/foo", "c:/foo");
  check_external_normalizes_to("c:\\foo", "c:\\foo");
#else
  check_external_normalizes_to("c:foo", "a/b/c:foo");
  check_external_normalizes_to("c:/foo", "a/b/c:/foo");
  check_external_normalizes_to("c:\\foo", "a/b/c:\\foo");
#endif
  // can't do particularly interesting checking of tilde expansion, but at
  // least we can check that it's doing _something_...
  std::string tilde_expanded = external_path("~/foo").as_external();
  BOOST_CHECK(tilde_expanded[0] == '/');
  BOOST_CHECK(tilde_expanded.find('~') == std::string::npos);

  initial_path = initial_path_saved;
}

void add_paths_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&test_null_name));
  suite->add(BOOST_TEST_CASE(&test_file_path_internal));
  suite->add(BOOST_TEST_CASE(&test_file_path_external_no_prefix));
  suite->add(BOOST_TEST_CASE(&test_file_path_external_prefix_a_b));
  suite->add(BOOST_TEST_CASE(&test_split_join));
  suite->add(BOOST_TEST_CASE(&test_bookkeeping_path));
  suite->add(BOOST_TEST_CASE(&test_external_path));
}

#endif // BUILD_UNIT_TESTS
