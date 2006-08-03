dnl @synopsis AC_CXX_STLPORT_HASHMAP
dnl
dnl tests for the presence of the gcc hashmap stl extension
dnl
dnl @author Patrick Mauritz <oxygene@studentenbude.ath.cx>
dnl @version 2005-07-18
dnl @license AllPermissive

AC_DEFUN([AC_CXX_STLPORT_HASHMAP],[
AC_LANG_ASSERT([C++])
AC_CACHE_CHECK(whether the compiler supports std::hash_map,
ac_cv_cxx_stlport_hashmap,
[AC_COMPILE_IFELSE([AC_LANG_SOURCE([
#include <hash_map>
using std::hash_map;
])],
   [ac_cv_cxx_stlport_hashmap=yes],
   [ac_cv_cxx_stlport_hashmap=no])
])
if test "$ac_cv_cxx_stlport_hashmap" = yes; then
  AC_DEFINE(HAVE_STLPORT_HASHMAP, 1,
    [Define if the compiler supports std::hash_map.])
fi
])
