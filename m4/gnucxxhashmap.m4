dnl @synopsis AC_CXX_GNUCXX_HASHMAP
dnl
dnl tests for the presence of the gcc hashmap stl extension
dnl
dnl @author Patrick Mauritz <oxygene@studentenbude.ath.cx>
dnl @version 2005-07-18
dnl @license AllPermissive

AC_DEFUN([AC_CXX_GNUCXX_HASHMAP],[
AC_LANG_ASSERT([C++])
 AC_CACHE_CHECK(whether the compiler supports __gnu_cxx::hash_map,
ac_cv_cxx_gnucxx_hashmap,
[AC_COMPILE_IFELSE([AC_LANG_SOURCE([
#include <ext/hash_map>
using __gnu_cxx::hash_map;
])],
   [ac_cv_cxx_gnucxx_hashmap=yes],
   [ac_cv_cxx_gnucxx_hashmap=no])
])
if test "$ac_cv_cxx_gnucxx_hashmap" = yes; then
  AC_DEFINE(HAVE_GNUCXX_HASHMAP, 1,
   [Define if the compiler supports __gnu_cxx::hash_map.])
fi
])
