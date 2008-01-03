# Detect libpcre or fall back to our bundled version.
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
 fi
 if test $with_system_pcre = no; then
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
     PKG_CHECK_MODULES([PCRE], [libpcre >= 6.7],
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
      with_system_pcre=no
   fi])
