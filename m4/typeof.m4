dnl @synopsis AC_CXX_GNUCXX_HASHMAP
dnl
dnl tests for the presence of the gcc hashmap stl extension
dnl
dnl @author Patrick Mauritz <oxygene@studentenbude.ath.cx>
dnl @version 2005-08-15
dnl @license AllPermissive

AC_DEFUN([AC_CXX_TYPEOF],[
AC_CACHE_CHECK(whether the compiler supports typeof,
ac_cv_cxx_typeof,
[AC_LANG_SAVE
 AC_LANG_CPLUSPLUS
 AC_TRY_COMPILE([int x; typeof (x) y[6];],
  [],
  ac_cv_cxx_typeof=yes, ac_cv_cxx_typeof=no)
 AC_LANG_RESTORE
])
if test "$ac_cv_cxx_typeof" = yes; then
  AC_DEFINE(HAVE_TYPEOF,,[define if the compiler supports typeof])
fi 
])

