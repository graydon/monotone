dnl Grab-bag of checks related to boost.

# We don't use threads, but some Boost libraries make locking calls
# anyway.  So we need to ensure that these symbols are available.
# But it's okay if they're just stubs.  This is technically incorrect
# if Boost is for some reason compiled without threads on a system
# that does provide threads; we'll end up compiling monotone with
# thread support for no reason.
AC_DEFUN([BOOST_THREAD_STUBS],
[AC_CHECK_FUNC([pthread_mutex_lock], [],
# But if there isn't even a stub, find the real threading library...
              [ACX_PTHREAD(
# ...and if it exists, use it:
                           [LIBS="$PTHREAD_LIBS $LIBS"
                            CXXFLAGS="$PTHREAD_CFLAGS $CXXFLAGS"],
# ...but if it doesn't, oh well, maybe things will work out anyway:
                           [])])
])

# Check for suitably new version of boost.
AC_DEFUN([BOOST_VERSION_CHECK],
[AC_LANG_ASSERT([C++])
 AC_CACHE_CHECK([boost version 1.33.0 or newer], ac_cv_boost_version_least_1_33_0,
 [
  AC_COMPILE_IFELSE(
  [#include <boost/version.hpp>
  #if BOOST_VERSION >= 103300
  int main() { return 0; }
  #else
  #error boost version is too old
  #endif
  ],
  ac_cv_boost_version_least_1_33_0=yes,
  ac_cv_boost_version_least_1_33_0=no)
 ])
  if test x$ac_cv_boost_version_least_1_33_0 = xno; then
	AC_MSG_ERROR([boost 1.33.0 or newer required])
  fi
])

# We currently don't need any checks for boost version-specific bugs,
# but we have in the past and may again.  They go in this macro.
AC_DEFUN([BOOST_VERSION_SPECIFIC_BUGS],
[AC_LANG_ASSERT([C++])
])

# Boost libraries have a string suffix that identifies the compiler
# they were built with, among other details.  For example, it can be
# '-gcc', '-gcc-mt', '-gcc-mt-1_31', and many other combinations
# depending on the build system.  Some systems provide symlinks that
# hide these suffixes, avoiding this mess.  However, other systems
# don't; we have to provide a way to let the user manually specify a
# suffix.  Guessing can be very difficult, given the variety of
# possibilities.  Note that you cannot expand a variable inside the
# second argument to AC_ARG_VAR, so we're stuck listing it twice.
AC_DEFUN([BOOST_SUFFIX_ARG],
[AC_ARG_VAR([BOOST_SUFFIX],
            [Space-separated list of suffixes to try appending to the names
	     of Boost libraries.  "none" means no suffix. The default is:
	     "none -gcc -mipspro -mt -sunpro -sw -mgw -gcc-mt-s"])
if test x"$BOOST_SUFFIX" = x; then
  BOOST_SUFFIX="none -gcc -mipspro -mt -sunpro -sw -mgw -gcc-mt-s"
fi
])

# Option to link statically against Boost.
# FIXME: Decouple prefix for boost libraries from static/shared linkage?
AC_DEFUN([BOOST_STATIC_LINK_OPTION],
[AC_ARG_ENABLE(static-boost,
 AS_HELP_STRING([--enable-static-boost@<:@=prefix@:>@],
               [use static libs from boost]),
	       [], [enable_static_boost=no])
 case "$enable_static_boost" in
   # unconfuse emacs: (((( 
   ""|no) static_boost_prefixes="" ;;
   yes)   static_boost_prefixes="/ /usr /opt /usr/local" ;;
   /*)    static_boost_prefixes="$enable_static_boost" ;;
   *)     AC_MSG_ERROR([prefix argument to --enable-static-boost must be an absolute path]) ;;
 esac
 if test x"$static_boost_prefixes" != x; then
   AC_MSG_CHECKING([location of static boost libraries])
   for i in ${static_boost_prefixes}
   do
     for s in $BOOST_SUFFIX
     do
        if test "x$s" = xnone; then
          s=''
	fi
	# FIXME: We should not be hardwiring /lib64/ nor should we be
	# unconditionally overriding it with /lib/ (if there are both
	# 32- and 64-bit static Boost libraries available, we must
	# pick the one that matches the code the compiler generates).
	# Fortunately, this is C++, so we'll get link errors if we get
	# it wrong.
	if test -f $i/lib64/libboost_filesystem${s}.a
	then
		BOOST_LIBDIR=$i/lib64
	fi
	if test -f $i/lib/libboost_filesystem${s}.a 
	then
		BOOST_LIBDIR=$i/lib
	fi
      done
   done
   if test "x${BOOST_LIBDIR}" = x
   then
        AC_MSG_RESULT([not found])
	AC_MSG_ERROR([cannot find boost libraries for static link])
   else
        AC_MSG_RESULT([${BOOST_LIBDIR}])
   fi
 fi
])

# BOOST_LIB_IFELSE(library, testprog, if_found, if_not_found)
# This is tricksome, as we only want to process a list of suffixes
# until we've selected one; once we've done that, it must be used for
# all libraries.  (But we need the shell loop in all uses, as previous
# scans might be unsuccessful.)

AC_DEFUN([BOOST_LIB_IFELSE],
[AC_LANG_ASSERT(C++)
 AC_REQUIRE([BOOST_SUFFIX_ARG])
 AC_REQUIRE([BOOST_STATIC_LINK_OPTION])
 found=no
 OLD_LIBS="$LIBS"
 for s in $BOOST_SUFFIX
 do
    if test "x$s" = xnone; then
      s=''
    fi
    if test "x${BOOST_LIBDIR}" != x; then
      lib="${BOOST_LIBDIR}/libboost_$1${s}.a"
    else
      lib="-lboost_$1$s"
    fi

    LIBS="$lib $OLD_LIBS"
    cv=AS_TR_SH([ac_cv_boost_lib_$1${s}_${BOOST_LIBDIR}])
    AC_CACHE_CHECK([for the boost_$1$s library],
    		   $cv,
      [AC_LINK_IFELSE([$2],
		      [eval $cv=yes],
		      [eval $cv=no])])
    if eval "test \"\${$cv}\" = yes"; then
       found=yes
       break
    fi
 done
 LIBS="$OLD_LIBS"
 AS_IF([test $found = yes], 
        [BOOST_SUFFIX=${s:-none}
	 $3],
        [$4])])

# Checks for specific boost libraries.
# These are named MTN_BOOST_* because their actions are monotone-specific.

AC_DEFUN([MTN_NEED_BOOST_LIB],
[BOOST_LIB_IFELSE([$1], [$2],
    [BOOSTLIBS="$lib $BOOSTLIBS"],
    [AC_MSG_FAILURE([the boost_$1 library is required])])
 AC_SUBST(BOOSTLIBS)
])

AC_DEFUN([MTN_BOOST_LIB_FILESYSTEM],
[MTN_NEED_BOOST_LIB([filesystem],
  [AC_LANG_PROGRAM([[
      #include <boost/filesystem/path.hpp>
      #include <boost/filesystem/operations.hpp>
      using namespace boost::filesystem;
    ]],[[
      exists(path("/boot"));
    ]])])])

AC_DEFUN([MTN_BOOST_LIB_DATE_TIME],
[MTN_NEED_BOOST_LIB([date_time],
  [AC_LANG_PROGRAM([[
      #include <boost/date_time/posix_time/posix_time.hpp>
      #include <iostream>
      using namespace boost::posix_time; 
    ]],[[
      std::cout << to_iso_extended_string(second_clock::universal_time());
    ]])])])

dnl Unlike all the others, if we don't have this library we can still
dnl build monotone; we just can't do all the tests.
AC_DEFUN([MTN_BOOST_LIB_UNIT_TEST_FRAMEWORK],
[BOOST_LIB_IFELSE([unit_test_framework],
  [AC_LANG_SOURCE([[
      #include <boost/test/unit_test_suite.hpp>
      #include <boost/test/test_tools.hpp>
      using boost::unit_test_framework::test_suite;
      test_suite * init_unit_test_suite(int argc, char * argv[]) 
      { return NULL; }
    ]])],
 [AM_CONDITIONAL(BUILD_UNIT_TESTS, true)
  BOOST_UNITTEST="$lib"],
 [AM_CONDITIONAL(BUILD_UNIT_TESTS, false)])
 AC_SUBST(BOOST_UNITTEST)
])
