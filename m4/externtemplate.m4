dnl @synopsis AC_CXX_EXTERN_TEMPLATE
dnl
dnl tests for the presence of the gcc c++ extension
dnl "extern template"
dnl
dnl @author Patrick Mauritz <oxygene@studentenbude.ath.cx>
dnl @version 2005-07-18
dnl @license AllPermissive

AC_DEFUN([AC_CXX_EXTERN_TEMPLATE],[
AC_CACHE_CHECK(whether the compiler supports extern template,
ac_cv_cxx_extern_template,
[AC_LANG_SAVE
 AC_LANG_CPLUSPLUS
 AC_TRY_COMPILE([template <typename T> void foo(T); extern template void foo<int>(int);],
 [],
 ac_cv_cxx_extern_template=yes, ac_cv_cxx_extern_template=no)
 AC_LANG_RESTORE
])
if test "$ac_cv_cxx_extern_template" = yes; then
  AC_DEFINE(HAVE_EXTERN_TEMPLATE,,[define if the compiler supports extern template])
fi
])
