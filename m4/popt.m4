##########
# We use our bundled popt unconditionally, because the external one has
# serious bugs that we can't live with.  Maybe some time in the future...
# The following was simply taken from the configure.in that comes with popt.
AC_DEFUN([MTN_POPT_DEPENDENCIES],
[AC_CHECK_HEADERS([alloca.h float.h libintl.h mcheck.h],,,
		  [AC_INCLUDES_DEFAULT()])
AC_MSG_CHECKING(for /usr/ucblib in LIBS)
if test -d /usr/ucblib ; then
	if test "$build" = "mips-sni-sysv4" ; then
		addlib /usr/ccs/lib -lc
	fi

	addlib /usr/ucblib
	
	AC_MSG_RESULT(yes)
else
	AC_MSG_RESULT(no)
fi
AC_CHECK_FUNCS(strerror strptime mtrace getuid geteuid)
AC_CHECK_FUNC(setreuid, [], [
    AC_CHECK_LIB(ucb, setreuid, [if echo $LIBS | grep -- -lucb >/dev/null ;then :; else LIBS="$LIBS -lc -lucb" USEUCB=y;fi])
])

POPT_SOURCE_PATH="`pwd`"
AC_DEFINE_UNQUOTED(POPT_SOURCE_PATH, "$POPT_SOURCE_PATH",
	[Full path to popt top_sourcedir.])
AC_SUBST(POPT_SOURCE_PATH)
])
