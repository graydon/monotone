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
[AC_LANG_PUSH([C++])
 AC_COMPILE_IFELSE([AC_LANG_SOURCE([[ int x; typeof (x) y[6]; ]])],
  [ac_cv_cxx_typeof=yes],
  [ac_cv_cxx_typeof=no])
 AC_LANG_POP([C++])
])
if test "$ac_cv_cxx_typeof" = yes; then
  AC_DEFINE(HAVE_TYPEOF, 1, [Define if the compiler supports typeof.])
fi 
])
