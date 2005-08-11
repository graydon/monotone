dnl @synopsis AC_CXX_STLPORT_HASHMAP
dnl
dnl tests for the presence of the gcc hashmap stl extension
dnl
dnl @author Patrick Mauritz <oxygene@studentenbude.ath.cx>
dnl @version 2005-07-18
dnl @license AllPermissive

AC_DEFUN([AC_CXX_STLPORT_HASHMAP],[
AC_CACHE_CHECK(whether the compiler supports std::hash_map,
ac_cv_cxx_stlport_hashmap,
[AC_LANG_SAVE
 AC_LANG_CPLUSPLUS
 AC_TRY_COMPILE([#include <hash_map>
using std::hash_map;],
 [],
 ac_cv_cxx_stlport_hashmap=yes, ac_cv_cxx_stlport_hashmap=no)
 AC_LANG_RESTORE
])
if test "$ac_cv_cxx_stlport_hashmap" = yes; then
  AC_DEFINE(HAVE_STLPORT_HASHMAP,,[define if the compiler supports std::hash_map])
fi
])
