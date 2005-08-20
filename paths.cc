#include "paths.hh"




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

static void check_normalizes_to(char * before, char * after)
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
  
static void test_file_path_external()
{
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
  
  check_normalizes_to("", "", "");
  check_normalizes_to("foo", "foo");
  check_normalizes_to("foo/bar", "foo/bar");
  check_normalizes_to("foo/bar/baz", "foo/bar/baz");
  check_normalizes_to("foo/bar.baz", "foo/bar.baz");
  check_normalizes_to("foo/with-hyphen/bar", "foo/with-hyphen/bar");
  check_normalizes_to("foo/with_underscore/bar", "foo/with_underscore/bar");
  check_normalizes_to(".foo/bar", ".foo/bar");
  check_normalizes_to("..foo/bar", "..foo/bar");
  check_normalizes_to(".", "");

  check_normalizes_to("foo//bar", "foo/bar");
  check_normalizes_to("foo/../bar", "bar");
  check_normalizes_to("foo/bar/", "foo/bar");
  check_normalizes_to("foo/./bar/", "foo/bar");
  check_normalizes_to("./foo", "foo");
  check_normalizes_to("foo///.//", "foo");
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
  BOOST_CHECK(split3.size() == 0);
  BOOST_CHECK(fp3 == file_path(split3));
}

void add_paths_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&test_null_name));
  suite->add(BOOST_TEST_CASE(&test_file_path_internal));
  suite->add(BOOST_TEST_CASE(&test_file_path_external));
}

#endif // BUILD_UNIT_TESTS
