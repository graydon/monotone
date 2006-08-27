dnl @synopsis AC_CC_FEXCEPTIONS
dnl
dnl tests if compiler bails out on -fexception. doesn't catch situation
dnl where it just emits a warning.
dnl
dnl taken from expat's configure.in
dnl
dnl @author Patrick Mauritz <oxygene@studentenbude.ath.cx>
dnl @version 2005-08-17
dnl @license MIT/X

AC_DEFUN([AC_CC_FEXCEPTIONS],[
AC_LANG_ASSERT([C])
AC_CACHE_CHECK(whether the compiler knows -fexception,
ac_cv_cc_fexceptions,
[OLDCFLAGS="$CFLAGS"
 CFLAGS="$OLDCFLAGS -fexceptions"
 AC_COMPILE_IFELSE([AC_LANG_PROGRAM([], [(void)1])],
   [ac_cv_cc_fexceptions=yes],
   [ac_cv_cc_fexceptions=no])
 CFLAGS="$OLDCFLAGS"
])
if test "$ac_cv_cc_fexceptions" = yes; then
  CFLAGS="$CFLAGS -fexceptions"
fi
])
