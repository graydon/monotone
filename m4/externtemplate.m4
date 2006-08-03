dnl @synopsis AC_CXX_EXTERN_TEMPLATE
dnl
dnl tests for the presence of the gcc c++ extension
dnl "extern template"
dnl
dnl @author Patrick Mauritz <oxygene@studentenbude.ath.cx>
dnl @version 2005-07-18
dnl @license AllPermissive

AC_DEFUN([AC_CXX_EXTERN_TEMPLATE],[
AC_LANG_ASSERT([C++])
AC_CACHE_CHECK(whether the compiler supports extern template,
ac_cv_cxx_extern_template,
[AC_COMPILE_IFELSE([AC_LANG_SOURCE([
  template <typename T> void foo(T); 
  extern template void foo<int>(int);
])],
  [ac_cv_cxx_extern_template=yes],
  [ac_cv_cxx_extern_template=no])
])
if test x$ac_cv_cxx_extern_template = xyes; then
  AC_DEFINE(HAVE_EXTERN_TEMPLATE, 1,
    [Define if the compiler supports "extern template".])
fi
])
