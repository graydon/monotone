# -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.58)
AC_INIT(monotone, 0.30, monotone-devel@nongnu.org)
AM_INIT_AUTOMAKE([1.9 tar-ustar std-options])
AC_CONFIG_SRCDIR([app_state.cc])
AM_CONFIG_HEADER([config.h])
AC_CONFIG_FILES([Makefile])

# Checks for programs.
dnl Note: AC_PROG_CC is implied by AM_PROG_CC_C_O and in any case has
dnl already been done by AM_INIT_AUTOMAKE.
AM_PROG_CC_C_O
AC_PROG_GCC_TRADITIONAL

AC_PROG_CXX
AC_PROG_CXX_WARNINGS
AC_PROG_CXX_PCH

AC_PROG_RANLIB

# ??? Shouldn't be necessary (anymore).
test "z$GCC" = "zyes" && CFLAGS="$CFLAGS -fno-strict-aliasing"
test "z$GXX" = "zyes" && CXXFLAGS="$CXXFLAGS -fno-strict-aliasing"

AC_C_CONST
AC_C_INLINE
AC_CC_FEXCEPTIONS

AC_CACHE_CHECK([whether this is Windows],
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
# Explicitly specifying AC_INCLUDES_DEFAULT gets autoconf not to do all
# the tests twice.
AC_CHECK_HEADERS([arpa/inet.h fcntl.h netdb.h netinet/in.h sys/file.h \
		  sys/socket.h sys/time.h termios.h errno.h],,,
		 [AC_INCLUDES_DEFAULT()])
AC_HEADER_STDBOOL
AX_CREATE_STDINT_H(mt-stdint.h)
# gettext is nominally a library, but its tests don't work in C++ mode.
AM_GNU_GETTEXT([external])
AM_GNU_GETTEXT_VERSION(0.11.5)
AM_ICONV
AC_PROG_XGETTEXT_FLAG_OPTION
# similarly, this calls ACX_PTHREAD, which expects C.
BOOST_THREAD_STUBS

AC_LANG([C++])
# This header may not be visible to the C compiler.
AC_CHECK_HEADERS([cxxabi.h],,, [AC_INCLUDES_DEFAULT()])

# check for language features and compiler bugs
AC_CXX_TYPEOF
AC_CXX_EXTERN_TEMPLATE
AC_CXX_GNUCXX_HASHMAP
AC_CXX_STLPORT_HASHMAP
AC_CXX_TR1_UNORDERED_MAP
AC_CXX_TR1_UNORDERED_MAP_CONST_CORRECT
AC_CXX_SYNC_WITH_STDIO_WORKS
AC_CXX_TEMPLATE_STATIC_CONST_BUG

# Checks for typedefs and structures.
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
AC_FUNC_STAT
AC_FUNC_LOCALE_WORKS
AC_CHECK_FUNCS([atexit memset mkstemp setenv strptime lrint \
		__cxa_current_exception_type __cxa_demangle])

# simple library checks
AC_SEARCH_LIBS([deflate], [z], , AC_MSG_FAILURE([zlib is required]))

# check for all things boost-related
BOOST_VERSION_CHECK
BOOST_VERSION_SPECIFIC_BUGS

MTN_BOOST_LIB_FILESYSTEM
MTN_BOOST_LIB_DATE_TIME
MTN_BOOST_LIB_REGEX
MTN_BOOST_LIB_UNIT_TEST_FRAMEWORK

# more complex library checks
MTN_NETXX_DEPENDENCIES
MTN_SQLITE_DEPENDENCIES

# allow compiling with different flags/optimisation
# for monotone versus libs, useful for testing.
# only makes sense if CFLAGS="" or CXXFLAGS=""
AC_SUBST(MONOTONE_CFLAGS)
AC_SUBST(MONOTONE_CXXFLAGS)
AC_SUBST(LIB3RDPARTY_CFLAGS)
AC_SUBST(LIB3RDPARTY_CXXFLAGS)

AC_OUTPUT
