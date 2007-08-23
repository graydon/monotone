dnl checks only done because the bundled sqlite needs them.
AC_DEFUN([MTN_SQLITE_DEPENDENCIES],
[SQLITE_CPPFLAGS=

# sqlite does not read our config.h so we have to shove all this on the
# command line.

AC_SEARCH_LIBS([fdatasync], [rt],
 [SQLITE_CPPFLAGS="$SQLITE_CPPFLAGS -DHAVE_FDATASYNC=1"])

AC_SEARCH_LIBS([usleep], [rt],
 [SQLITE_CPPFLAGS="$SQLITE_CPPFLAGS -DHAVE_USLEEP=1"])

AC_CHECK_FUNC([pread],
  [AC_CHECK_FUNC([pwrite],
    [SQLITE_CPPFLAGS="$SQLITE_CPPFLAGS -DUSE_PREAD=1"])])

# Let the user specify whether he wants large file support or not in sqlite.
AC_ARG_ENABLE([large-file],
   AS_HELP_STRING(
      [--disable-large-file],
      [Disable large file support in builtin sqlite]
   ), , enable_large_file=yes
)

if test "x$enable_large_file" = "xno"; then
   SQLITE_CPPFLAGS="$SQLITE_CPPFLAGS -DSQLITE_DISABLE_LFS"
fi

AC_SUBST(SQLITE_CPPFLAGS)
])
