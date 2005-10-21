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
AC_CACHE_CHECK(whether the compiler knows -fexception,
ac_cv_cc_fexceptions,
[
 AC_LANG_SAVE
 AC_LANG_C
 
 OLDCFLAGS="$CFLAGS"
 CFLAGS="$OLDCFLAGS -fexceptions"
 AC_TRY_COMPILE(,(void)1,
                ac_cv_cc_fexceptions=yes,
                ac_cv_cc_fexceptions=no)
 CFLAGS="$OLDCFLAGS"
 AC_LANG_RESTORE
])
if test "$ac_cv_cc_fexceptions" = yes; then
  CFLAGS="$CFLAGS -fexceptions"
fi
])
