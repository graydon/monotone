# -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.58)
AC_INIT(monotone, 0.28, monotone-devel@nongnu.org)
AM_INIT_AUTOMAKE([1.9 tar-ustar check-news std-options])
AC_CONFIG_SRCDIR([app_state.cc])
AM_CONFIG_HEADER([config.h])
AC_CONFIG_FILES([Makefile])

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC

test "z$GXX" = "zyes" && CXXFLAGS="$CXXFLAGS -fno-strict-aliasing"
test "z$GCC" = "zyes" && CFLAGS="$CFLAGS -fno-strict-aliasing"

AC_PROG_RANLIB
AC_CC_FEXCEPTIONS
AC_LANG(C++)

AC_PROG_CXX_WARNINGS
AC_CXX_TYPEOF

AC_CXX_GNUCXX_HASHMAP
AC_CXX_STLPORT_HASHMAP
AC_CXX_TR1_UNORDERED_MAP
AC_CXX_TR1_UNORDERED_MAP_CONST_CORRECT

# check to see how to make objects in subdirs
AM_PROG_CC_C_O

# check for language features
AC_CXX_EXTERN_TEMPLATE

# intl / gettext
# the AM_GNU_GETTEXT macro test won't compile with g++
AC_LANG_PUSH(C)
AM_GNU_GETTEXT([external])
AM_GNU_GETTEXT_VERSION(0.11.5)
AM_ICONV
AC_LANG_POP()
AC_PROG_XGETTEXT_FLAG_OPTION

# Checks for idna stuff
AC_HEADER_STDC
AC_CHECK_HEADERS(unistd.h errno.h)
AC_FUNC_LOCALE_WORKS

# check for all things boost-related
BOOST_THREAD_STUBS
BOOST_VERSION_CHECK
BOOST_VERSION_SPECIFIC_BUGS

MTN_BOOST_LIB_FILESYSTEM
MTN_BOOST_LIB_DATE_TIME
MTN_BOOST_LIB_REGEX
MTN_BOOST_LIB_UNIT_TEST_FRAMEWORK

AC_PROG_CXX_TEMPLATE_STATIC_CONST_BUG
AC_PROG_CXX_PCH

AC_CACHE_CHECK([if this is Windows],
       ac_win32, [
       AC_TRY_COMPILE([#ifdef WIN32
                       #include <windows.h>
                       #endif],
                      [HANDLE h; DWORD d;],
                      ac_win32=yes,
                      ac_win32=no)
])
if test "$ac_win32" = "yes"; then
   AM_CONDITIONAL(WIN32_PLATFORM, true)
   AC_DEFINE(os_err_t, unsigned int, [OS-specific error type])
else
   AM_CONDITIONAL(WIN32_PLATFORM, false)
   AC_DEFINE(os_err_t, int, [OS-specific error type])
fi

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([arpa/inet.h fcntl.h netdb.h netinet/in.h stdlib.h string.h \
		  sys/file.h sys/socket.h sys/time.h termios.h unistd.h])
AX_CREATE_STDINT_H(mt-stdint.h)

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_C_INLINE
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_PID_T

AC_CHECK_MEMBERS([struct stat.st_ctim.tv_nsec,
	          struct stat.st_mtim.tv_nsec,
		  struct stat.st_ctimespec.tv_nsec, 
	          struct stat.st_mtimespec.tv_nsec,
		  struct stat.st_ctimensec,
		  struct stat.st_mtimensec])

# Checks for library functions.
AC_PROG_GCC_TRADITIONAL
AC_FUNC_STAT
AC_CHECK_FUNCS([atexit memset mkstemp setenv])

AC_CXX_SYNC_WITH_STDIO_WORKS

# simple library checks 
AC_CHECK_LIB([z], [deflate], , AC_MSG_FAILURE([zlib is required]))
AC_SEARCH_LIBS([dlsym], [dl], AC_DEFINE(USE_DLOPEN))
AH_TEMPLATE([USE_DLOPEN], [For LUA: use dlopen for loadlib])

MTN_NETXX_DEPENDENCIES
MTN_SQLITE_DEPENDENCIES
MTN_POPT_DEPENDENCIES

# allow compiling with different flags/optimisation
# for monotone versus libs, useful for testing.
# only makes sense if CFLAGS="" or CXXFLAGS=""
AC_SUBST(MONOTONE_CFLAGS)
AC_SUBST(MONOTONE_CXXFLAGS)
AC_SUBST(LIB3RDPARTY_CFLAGS)
AC_SUBST(LIB3RDPARTY_CXXFLAGS)

AC_OUTPUT
