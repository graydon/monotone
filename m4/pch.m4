# check for precompiled headers option
AC_DEFUN([AC_PROG_CXX_PCH],
[AC_ARG_ENABLE(pch,
 AS_HELP_STRING([--enable-pch],
                [use precompiled boost headers]))
 AM_CONDITIONAL(BUILD_PCH, false)
 if test x"${enable_pch}" = x"yes"
 then
	AC_MSG_CHECKING(for gcc with precompiled header support)
	gxx_version=`${CXX} -dumpversion`
	case $gxx_version in
		4.*)
			AC_MSG_RESULT(found)
			AM_CONDITIONAL(BUILD_PCH, true)
			;;
		*)
			AC_MSG_RESULT(not found)
			;;
 	esac
 fi])
