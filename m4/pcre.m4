# Set up to use either a bundled or a system-provided version of libpcre.
#
# If --with-system-pcre is specified and the library cannot be found or is
# unsuitable, the configure script will error out rather than falling back
# to the bundled version.  This is to avoid surprising a user who expected
# their system libpcre to be used.
#
# "Unsuitable" is defined as "any version other than the bundled one".  In
# other words, if you want to use a system PCRE you must provide the exact
# same version that was shipped with this release of Monotone.  We do this
# because regexes are part of the visible user interface, but their syntax
# may change from release to release of PCRE.  We want each version of our
# program to exhibit the regex syntax that was documented in its manual.
#
# If you want to rebuild monotone with a newer system PCRE to get some bug
# fixes, and you've verified that the newer version of PCRE doesn't change
# the regex syntax, you can override the check by changing the #defines in
# pcre/pcre.h that state the bundled library's version number.  You do not
# have to regenerate 'configure'.
#
# We are discussing this with the upstream maintainers of PCRE and hope to
# find a better solution soon.

AC_DEFUN([MTN_LIB_PCRE],
[AC_ARG_WITH([system-pcre],
    AC_HELP_STRING([--with-system-pcre],
     [use a system-provided copy of libpcre instead of the default bundled
      copy. (To use a specific installed version, use the environment
      variables PCRE_CFLAGS and/or PCRE_LIBS.)]),
   [case "$withval" in
      ""|yes) with_system_pcre=yes ;;
      no)     with_system_pcre=no  ;;
      *)      AC_MSG_ERROR([--with(out)-system-pcre takes no argument]) ;;
    esac],
   [with_system_pcre=no])
 if test "$with_system_pcre" = yes; then
   MTN_FIND_PCRE
 else
   AC_DEFINE([PCRE_STATIC],[1],[Define if using bundled pcre])
   AC_MSG_NOTICE([using the bundled copy of PCRE])
 fi
 AM_CONDITIONAL([INCLUDED_PCRE], [test $with_system_pcre = no])
 AC_SUBST([PCRE_CFLAGS])
 AC_SUBST([PCRE_LIBS])
])

# This is a separate macro primarily to trick autoconf into not looking
# for pkg-config if we are using the bundled pcre.
AC_DEFUN([MTN_FIND_PCRE],
[  PKG_PROG_PKG_CONFIG
   # We manually test the variables here because we want them to work
   # even if pkg-config isn't installed.  The use of + instead of :+ is
   # deliberate; the user should be able to tell us that the empty string
   # is the correct set of flags.  (PKG_CHECK_MODULES gets this wrong!)
   if test -n "${PCRE_CFLAGS+set}" || test -n "${PCRE_LIBS+set}"; then
     found_libpcre=yes
   else
     PKG_CHECK_MODULES([PCRE], [libpcre],
                       [found_libpcre=yes], [found_libpcre=no])
   fi
   if test $found_libpcre = no; then
     # try pcre-config, in case we're on a system with no pkg-config
     AC_MSG_CHECKING([for PCRE using pcre-config])
     if test -n "`type -p pcre-config`"; then
       PCRE_CFLAGS="`pcre-config --cflags`"
       # pcre-config has the annoying habit of telling us to use
       # -L switches for directories that the compiler will search
       # automatically.
       PCRE_LIBS="`pcre-config --libs | \
                   sed -e 's:-L */usr/lib/*::' -e 's:-R */usr/lib/*::' \
                       -e 's:-L */lib/*::' -e 's:-R */lib/*::'`"
       found_libpcre=yes
       AC_MSG_RESULT([yes])
     fi
   fi
   if test $found_libpcre = no; then
     AC_MSG_RESULT([no; guessing])
     PCRE_CFLAGS=
     PCRE_LIBS=-lpcre
   fi

   # Wherever we got the settings from, make sure they work.
   PCRE_CFLAGS="`echo :$PCRE_CFLAGS | sed -e 's/^:@<:@	 @:>@*//; s/@<:@	 @:>@*$//'`"
   PCRE_LIBS="`echo :$PCRE_LIBS | sed -e 's/^:@<:@	 @:>@*//; s/@<:@	 @:>@*$//'`"
   if test x"$PCRE_CFLAGS" != x""; then
     AC_MSG_NOTICE([using PCRE compile flags: "$PCRE_CFLAGS"])
   fi
   if test x"$PCRE_LIBS" != x"-lpcre"; then
     AC_MSG_NOTICE([using PCRE link flags: "$PCRE_LIBS"])
   fi
   AC_CACHE_CHECK([whether the system libpcre is usable], ac_cv_lib_pcre_works,
    [save_LIBS="$LIBS"
     save_CFLAGS="$CFLAGS"
     LIBS="$LIBS $PCRE_LIBS"
     CFLAGS="$CFLAGS $PCRE_CFLAGS"
     AC_LINK_IFELSE([AC_LANG_PROGRAM(
      [#include <pcre.h>],
      [const char *e;
       int dummy;
       int o;
       /* Make sure some definitions are present. */
       dummy = PCRE_NEWLINE_CR;
       dummy = PCRE_DUPNAMES;
       pcre *re = pcre_compile("foo", 0, &e, &o, 0);])],
      [ac_cv_lib_pcre_works=yes], [ac_cv_lib_pcre_works=no])
     LIBS="$save_LIBS"
     CFLAGS="$save_CFLAGS"])
   if test $ac_cv_lib_pcre_works = no; then
      AC_MSG_ERROR([system-provided libpcre is not usable.  Correct your settings or use --with-system-pcre=no.])
   fi

   # This is deliberately not cached.
   AC_MSG_CHECKING([whether the system libpcre matches the bundled version])
   sed -n -e 's/#define PCRE_MAJOR[ 	]*/#define BUNDLED_PCRE_MAJOR /p' \
          -e 's/#define PCRE_MINOR[ 	]*/#define BUNDLED_PCRE_MINOR /p' \
          $srcdir/pcre/pcre.h > conftest.h
   save_CFLAGS="$CFLAGS"
   CFLAGS="$CFLAGS $PCRE_CFLAGS"
   AC_PREPROC_IFELSE([
#include "conftest.h"
#include "pcre.h"
#if PCRE_MAJOR != BUNDLED_PCRE_MAJOR || PCRE_MINOR != BUNDLED_PCRE_MINOR
#error PCRE version mismatch
#endif],
   [pcre_version_match=yes],
   [pcre_version_match=no])
   AC_MSG_RESULT($pcre_version_match)
   if test $pcre_version_match = no; then
     AC_MSG_ERROR([system-provided libpcre does not match bundled pcre.  Correct your settings, use --with-system-pcre=no, or read m4/pcre.m4 for advice.])
   fi
])
