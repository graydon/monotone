# Check whether sync_with_stdio(false) run on C++ streams works correctly.
# It causes strange problems (EOF returned too early) on some versions of
# MinGW and OS X.  The first test is for the MinGW issue, the second for
# the OS X issue.
#
# In order to check for this issue, we first generate a large input file
# (over 128KB) and then try to read it (using a buffer size over 512
# bytes).  If the amount of bytes read does not match the generated input,
# the implementation is broken.
AC_DEFUN([AC_CXX_SYNC_WITH_STDIO_WORKS],
[AC_CACHE_CHECK([whether C++ streams support sync_with_stdio],
		ac_cv_cxx_sync_with_stdio_works, [
  if test x$cross_compiling = xyes; then
    AC_MSG_FAILURE([cannot run test program while cross compiling])
  fi
  # Note that we can't use AC_RUN_IFELSE for these because we have to
  # redirect standard input.  Instead, we use AC_LINK_IFELSE to
  # generate the binaries and then run them by hand.  Hence the manual
  # failure-if-cross-compiling above.
  AC_RUN_IFELSE([AC_LANG_SOURCE([
    /*
     * Generates the input file.
     */
    #include <cstdlib>
    #include <fstream>

    int
    main(void)
    {
      std::ofstream ofs("_conftest.dat");
      for (int i = 0; i < 50000; i++)
        ofs << "0123456789\r\n";
      ofs.close();

      return EXIT_SUCCESS;
    }
  ])], [], [AC_MSG_FAILURE([could not generate input for test 1])])

  AC_LINK_IFELSE([AC_LANG_SOURCE([[
    /*
     * Reads the input file.
     */
    #include <cstdlib>
    #include <fstream>
    #include <iostream>

    int
    main(void)
    {
      std::cin.sync_with_stdio(false);
      int length = 0;
      while (std::cin.good())
        {
          char buffer[1024];
          (void)std::cin.read(buffer, sizeof(buffer));
          length += std::cin.gcount();
        }

      return length == 600000 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    ]])],
  [if ./conftest$ac_exeext <_conftest.dat; then
    :
  else
    ac_cv_sync_with_stdio_works=no
  fi],
  [AC_MSG_FAILURE([could not link test program for test 1])])

  rm -f _conftest.dat
  if test "x$ac_cv_sync_with_stdio_works" != xno; then
  # To check for issue 2, we have a writer with a short delay between
  # writes, and a reader who has set sync_with_stdio(false).
  AC_LINK_IFELSE([AC_LANG_SOURCE([[
    #include <cstdlib>
    #include <iostream>
    #ifdef WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #define sleep(x) Sleep((x) * 1000)
    #else
    #include <unistd.h>
    #endif

    int
    main(int argc, char * argv[])
    {
      if (argc != 2)
        return EXIT_FAILURE;

      if (argv[1][0] == 'R')
        {
          std::cin.sync_with_stdio(false);
          int length = 0;
          while (std::cin.good())
            {
              char buffer@<:@1024@:>@;
              (void)std::cin.read(buffer, sizeof(buffer));
              length += std::cin.gcount();
            }

          return length == 82 ? EXIT_SUCCESS : EXIT_FAILURE;
        }
      else
        {
          std::cout << "1234567890123456789012345678901234567890" << std::endl;
          sleep(1);
          std::cout << "1234567890123456789012345678901234567890" << std::endl;
        }
      return EXIT_SUCCESS;
    }
    ]])],
  [if ./conftest$ac_exeext W | ./conftest$ac_exeext R; then
    ac_cv_cxx_sync_with_stdio_works=yes
  else
    ac_cv_cxx_sync_with_stdio_works=no
  fi],
  [AC_MSG_FAILURE([could not link test program for test 2])])
  fi # skip test 2
 ])
 if test $ac_cv_cxx_sync_with_stdio_works = yes; then
  AC_DEFINE(SYNC_WITH_STDIO_WORKS, 1,
              [Define to 1 if ios::sync_with_stdio(false) works])
 fi
])
