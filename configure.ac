# -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.58)
AC_INIT(monotone, 0.16, monotone-devel@nongnu.org)
AM_INIT_AUTOMAKE(1.7.1)
AC_CONFIG_SRCDIR([app_state.cc])
AC_CONFIG_TESTDIR([tests])
AM_CONFIG_HEADER([config.h])

# we have some sse2 code in cryptopp 
# currently gcc does some surprising things here, so we disable it
# completely.
# AC_CHECK_HEADERS(emmintrin.h, [CXXFLAGS_MSSE2="-msse2"])

# autoconf turns on -g by default unless a user overrides.  this is
# nice for smallish programs but it makes build times insane for
# spirit-based programs. we turn it off here unless the user turned it
# on themselves.

test "z$CXXFLAGS" = "z" && CXXFLAGS="-O2 -fno-strict-aliasing"
test "z$CFLAGS" = "z" && CFLAGS="-O2 -fno-strict-aliasing"

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_RANLIB
AC_LANG(C++)

# check to see how to make objects in subdirs
AM_PROG_CC_C_O

# intl / gettext
# the AM_GNU_GETTEXT macro test won't compile with g++
AC_LANG_PUSH(C)
AM_GNU_GETTEXT([external])
AM_GNU_GETTEXT_VERSION(0.12.1)
AM_ICONV
AC_LANG_POP()

# Checks for idna stuff
AC_HEADER_STDC
AC_CHECK_HEADERS(unistd.h errno.h)
AX_CREATE_STDINT_H(idna/idn-int.h)
AC_CACHE_CHECK([if setlocale() and nl_langinfo() works],
	ac_cv_locale_works, [
	AC_TRY_RUN([
#include <locale.h>
#include <langinfo.h>
int main ()
{
  char* cs;
  setlocale(LC_CTYPE, "");
  cs = nl_langinfo(CODESET);
  return !cs;
}
],
		ac_cv_locale_works=yes,
		ac_cv_locale_works=no,
		ac_cv_locale_works=no)
	])
if test "$ac_cv_locale_works" = "yes"; then
  AC_DEFINE(LOCALE_WORKS, 1, [Define if setlocale() and nl_langinfo() works.])
fi

# check various C++ libs which confuse autoconf

# We don't use threads, but some Boost libraries make locking calls
# anyway.  So we need to ensure that these symbols are available.  But
# It's okay if they're just stubs.
AC_CHECK_FUNC([pthread_mutex_lock], [],
# But if there isn't even a stub, find the real threading library...
              [ACX_PTHREAD(
# ...and if it exists, use it:
                           [LIBS="$PTHREAD_LIBS $LIBS"
                            CXXFLAGS="$PTHREAD_CFLAGS $CXXFLAGS"],
# ...but if it doesn't, oh well, maybe things will work out anyway:
                           [])])
# This is technically incorrect if Boost is for some reason compiled
# without threads on a system that does provide threads; we'll end up
# compiling monotone with thread support for no reason.

# Boost libraries have a string suffix that identifies the compiler
# they were built with, among other details.  For example, it can be
# '-gcc', '-gcc-mt', '-gcc-mt-1_31', and many other combinations
# depending on the build system.
# Some systems provide symlinks that hide these suffixes, avoiding
# this mess.  However, other systems don't; we have to provide a way
# to let the user manually specify a suffix.  Guessing can be very
# difficult, given the variety of possibilities.
AC_ARG_VAR(BOOST_SUFFIX,
           [String suffix that identifies Boost libraries (empty by default)])
AC_SUBST(BOOST_SUFFIX)

# Now check for Boost libraries proper.

LIBS="-lboost_unit_test_framework${BOOST_SUFFIX} $OLD_LIBS"
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

LIBS="-lboost_filesystem${BOOST_SUFFIX} $LIBS"
AC_MSG_CHECKING(libboost_filesystem library)
AC_LINK_IFELSE([AC_LANG_PROGRAM([[
  #include <boost/filesystem/path.hpp>
  #include <boost/filesystem/operations.hpp>
  using namespace boost::filesystem;
]],
[[exists(path("/boot"));]])],
 [AC_MSG_RESULT([yes])],
 [AC_MSG_FAILURE([libboost_filesystem failure])])

LIBS="-lboost_date_time${BOOST_SUFFIX} $LIBS"
AC_MSG_CHECKING(libboost_date_time library)
AC_LINK_IFELSE([AC_LANG_PROGRAM([[
  #include <boost/date_time/posix_time/posix_time.hpp>
  #include <iostream>
  using namespace boost::posix_time; 
]],
[[std::cout << to_iso_extended_string(second_clock::universal_time());]])],
 [AC_MSG_RESULT([yes])],
 [AC_MSG_FAILURE([libboost_date_time failure])])


LIBS="-lboost_regex${BOOST_SUFFIX} $LIBS"
AC_MSG_CHECKING(libboost_regex library)
AC_LINK_IFELSE([AC_LANG_PROGRAM([[
  #include <boost/regex.hpp>
  using namespace boost; 
]],
[[regex expr("foo");]])],
 [AC_MSG_RESULT([yes])],
 [AC_MSG_FAILURE([libboost_regex failure])])


# check for static boost option
AC_ARG_ENABLE(static-boost,
AS_HELP_STRING([--enable-static-boost@<:@=prefix@:>@],
               [use static libs from boost]))
AM_CONDITIONAL(STATIC_BOOST, test "x${enable_static_boost}" != x)
if test "x${enable_static_boost}" != x
then
   if echo "${enable_static_boost}" | grep '^/' >/dev/null
   then
      static_boost_prefixes="${enable_static_boost}"
   else
      static_boost_prefixes="/ /usr /opt /usr/local"
   fi
   AC_MSG_CHECKING([location of static boost libraries])
   for i in ${static_boost_prefixes}
   do
	if test -e $i/lib/libboost_regex${BOOST_SUFFIX}.a 
	then
		BOOST_LIBDIR=$i/lib
	fi
   done
   if test "x${BOOST_LIBDIR}" = x
   then
        AC_MSG_RESULT([not found])
	AC_MSG_ERROR([cannot find boost libraries for static link])
   else
        AC_MSG_RESULT([${BOOST_LIBDIR}])
	LIBS=`echo ${LIBS} | tr ' ' '\n' | grep -v boost | tr '\n' ' '`
	AC_SUBST(BOOST_LIBDIR)
   fi
fi

AC_CACHE_CHECK([if this is Windows],
       ac_win32, [
       AC_TRY_RUN([
#ifdef WIN32
#include <windows.h>
int main(void)
{
  return 0;
}
#else
__wont_compile_
#endif
],
   AM_CONDITIONAL(WIN32_PLATFORM, true),
   AM_CONDITIONAL(WIN32_PLATFORM, false),
   AM_CONDITIONAL(WIN32_PLATFORM, false)
)])


# Check for IPv6.  Let the user enable or disable it manually using a
# three-state (yes|no|auto) --enable argument.
AC_ARG_ENABLE(ipv6,
   AS_HELP_STRING([--enable-ipv6],[enable IPv6 support (default=auto)]), ,
   enable_ipv6=auto
)
AC_CACHE_CHECK([presence of IPv6],
   ac_inet6,
   [
      ac_inet6=no
      if test x"${enable_ipv6}" = x"auto" || test x"${enable_ipv6}" = x"yes"
      then
         AC_TRY_RUN([
#if defined(WIN32)
# include <winsock2.h>
#else
# include <sys/types.h>
# include <sys/socket.h>
# include <netinet/in.h>
# include <arpa/inet.h>
#endif
int main(void)
{
  sockaddr_in6 sa_in6;
}
],
            ac_inet6=yes
         )

         if test x"${enable_ipv6}" = x"yes" && test x"${ac_inet6}" = x"no"
         then
            AC_MSG_ERROR([IPv6 explicitly requested but it could not be found])
         fi
      fi
   ]
)
AM_CONDITIONAL(MISSING_INET6, test x"${ac_inet6}" = x"no")

# simple library checks 
AC_SEARCH_LIBS([dlsym], [dl], AC_DEFINE(USE_DLOPEN))
AH_TEMPLATE([USE_DLOPEN], [For LUA: use dlopen for loadlib])
AC_CHECK_LIB([popt], [poptGetArg], , AC_MSG_FAILURE([popt is required]))
AC_SEARCH_LIBS([inet_aton], [resolv])
AC_SEARCH_LIBS([accept], [socket])
AC_SEARCH_LIBS([inet_ntoa], [nsl])


##########
# Checks for external lua library, revert to bundled version if not found or
# user gives --with-bundled-lua.
AC_ARG_WITH([bundled-lua],
   AS_HELP_STRING(
      [--with-bundled-lua],
      [Force use of bundled version of lua (default=yes)]
   ), [bundled_lua="$withval"], [bundled_lua=yes]
)

if test "x$bundled_lua" = "xno" ; then
   bundled_lua=yes
   AC_CHECK_HEADERS([lua.h lualib.h],
      AC_CHECK_LIB([lua], [lua_open],
         AC_CHECK_LIB([lualib], [luaopen_base],
            [LIBS="$LIBS -llua -llualib" ; bundled_lua=no], , [-llua]
         )
      )
   )
fi

AM_CONDITIONAL(BUNDLED_LUA, test "x$bundled_lua" != "xno")

##########
# Checks for external sqlite library, revert to bundled version if not found or
# user gives --with-bundled-sqlite.
AC_ARG_WITH([bundled-sqlite],
   AS_HELP_STRING(
      [--with-bundled-sqlite],
      [Force use of bundled version of sqlite (default=yes)]
   ), [bundled_sqlite="$withval"], [bundled_sqlite=yes]
)

if test "x$bundled_sqlite" = "xno" ; then
   bundled_sqlite=yes
   AC_CHECK_HEADER([sqlite3.h],
      AC_CHECK_LIB([sqlite3], [sqlite3_open],
         [LIBS="$LIBS -lsqlite3" ; bundled_sqlite=no]
      )
   )
fi

AM_CONDITIONAL(BUNDLED_SQLITE, test "x$bundled_sqlite" != "xno")

# Now let the user specify whether he wants large file support or not in
# sqlite.
AC_ARG_ENABLE([large-file],
   AS_HELP_STRING(
      [--disable-large-file],
      [Disable large file support in builtin sqlite]
   ), , enable_large_file=yes
)

if test "x$enable_large_file" = "xno"; then
   if test "x$bundled_sqlite" = "xno"; then
      AC_MSG_WARN(--disable-large-file has no effect when using the bundled sqlite)
   else
      SQLITE_CPPFLAGS="$SQLITE_CPPFLAGS -DSQLITE_DISABLE_LFS"
   fi
fi
AC_SUBST(SQLITE_CPPFLAGS)

##########


# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([arpa/inet.h fcntl.h netdb.h netinet/in.h stdlib.h string.h sys/file.h sys/socket.h sys/time.h termios.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_C_INLINE
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_HAVE_INADDR_NONE

# Checks for library functions.
AC_PROG_GCC_TRADITIONAL
AC_FUNC_STAT
AC_CHECK_FUNCS([atexit gethostbyaddr gethostbyname inet_ntoa memset mkstemp setenv socket])

AC_CHECK_FUNC(inet_pton, [AM_CONDITIONAL(MISSING_INET_PTON, false)], 
			 [AM_CONDITIONAL(MISSING_INET_PTON, true)])

AC_CHECK_FUNC(inet_ntop, [AM_CONDITIONAL(MISSING_INET_NTOP, false)], 
			 [AM_CONDITIONAL(MISSING_INET_NTOP, true)])

AC_CHECK_FUNC(getaddrinfo, [AM_CONDITIONAL(MISSING_GETADDRINFO, false)], 
			   [AM_CONDITIONAL(MISSING_GETADDRINFO, true)])


AC_CONFIG_FILES([Makefile tests/atlocal po/Makefile.in])
AC_OUTPUT
