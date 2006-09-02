# checks only done because the bundled sqlite needs them.
AC_DEFUN([MTN_SQLITE_DEPENDENCIES],
[AC_SEARCH_LIBS([fdatasync], [rt], AC_DEFINE(HAVE_FDATASYNC))
AH_TEMPLATE([HAVE_FDATASYNC], [For SQLite; use fdatasync if available])
AC_SEARCH_LIBS([usleep], [rt], AC_DEFINE(HAVE_USLEEP))
AH_TEMPLATE([HAVE_USLEEP], [For SQLite; use usleep if available])

# Now let the user specify whether he wants large file support or not in
# sqlite.
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
