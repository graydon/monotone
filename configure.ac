# -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.57)
AC_INIT(monotone, 0.6, graydon@pobox.com)
AM_INIT_AUTOMAKE(1.7.1)
AC_CONFIG_SRCDIR([app_state.cc])
AC_CONFIG_TESTDIR([tests])
AM_CONFIG_HEADER([config.h])

# autoconf turns on -g by default unless a user overrides.  this is
# nice for smallish programs but it makes build times insane for
# spirit-based programs. we turn it off here unless the user turned it
# on themselves.

test "z$CXXFLAGS" = "z" && CXXFLAGS="-O0"
test "z$CFLAGS" = "z" && CFLAGS="-O0"

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_RANLIB
AC_LANG(C++)

# check to see how to make objects in subdirs
AM_PROG_CC_C_O

# check various C++ libs which confuse autoconf

OLD_LIBS=$LIBS
LIBS="-lboost_prg_exec_monitor $OLD_LIBS"
AC_MSG_CHECKING(libboost_prg_exec_monitor library)
AC_LINK_IFELSE([AC_LANG_SOURCE([[
int cpp_main(int argc, char ** argv)
{}
]])],
 [AC_MSG_RESULT([yes])],
 [AC_MSG_FAILURE([libboost_prg_exec_monitor failure])])

LIBS="-lboost_unit_test_framework $OLD_LIBS"
AC_MSG_CHECKING(libboost_unit_test_framework library)
AC_LINK_IFELSE([AC_LANG_SOURCE([[
#include <boost/test/unit_test_suite.hpp>
#include <boost/test/test_tools.hpp>
using boost::unit_test_framework::test_suite;
test_suite * init_unit_test_suite(int argc, char * argv[]) 
{ return NULL; }
]])],
 [AC_MSG_RESULT([yes])],
 [AC_MSG_FAILURE([libboost_unit_test_framework failure])])
LIBS=$OLD_LIBS

LIBS="-lboost_filesystem $LIBS"
AC_MSG_CHECKING(libboost_filesystem library)
AC_LINK_IFELSE([AC_LANG_PROGRAM([[
  #include <boost/filesystem/path.hpp>
  #include <boost/filesystem/operations.hpp>
  using namespace boost::filesystem;
]],
[[exists(path("/boot"));]])],
 [AC_MSG_RESULT([yes])],
 [AC_MSG_FAILURE([libboost_filesystem failure])])

LIBS="-lboost_date_time $LIBS"
AC_MSG_CHECKING(libboost_date_time library)
AC_LINK_IFELSE([AC_LANG_PROGRAM([[
  #include <boost/date_time/posix_time/posix_time.hpp>
  #include <iostream>
  using namespace boost::posix_time; 
]],
[[std::cout << to_iso_extended_string(second_clock::universal_time());]])],
 [AC_MSG_RESULT([yes])],
 [AC_MSG_FAILURE([libboost_date_time failure])])


LIBS="-lboost_regex $LIBS"
AC_MSG_CHECKING(libboost_regex library)
AC_LINK_IFELSE([AC_LANG_PROGRAM([[
  #include <boost/regex.hpp>
  using namespace boost; 
]],
[[regex expr("foo");]])],
 [AC_MSG_RESULT([yes])],
 [AC_MSG_FAILURE([libboost_regex failure])])


# simple library checks 
AC_CHECK_LIB([dl], [dlsym])
AC_CHECK_LIB([popt], [poptGetArg], , AC_MSG_FAILURE([popt is required]))
AC_CHECK_LIB([pthread], [pthread_attr_init])

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([arpa/inet.h fcntl.h netdb.h netinet/in.h stdlib.h string.h sys/file.h sys/socket.h sys/time.h termios.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_C_INLINE
AC_TYPE_OFF_T
AC_TYPE_SIZE_T

# Checks for library functions.
AC_PROG_GCC_TRADITIONAL
AC_FUNC_STAT
AC_CHECK_FUNCS([atexit gethostbyaddr gethostbyname inet_ntoa memset setenv socket])

AC_CONFIG_FILES([Makefile])
AC_CONFIG_FILES([tests/atlocal])
AC_OUTPUT
