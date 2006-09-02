dnl @synopsis AC_CXX_TR1_UNORDERED_MAP
dnl
dnl tests for the presence of C++ TR1 unordered_map and relatives.
dnl
dnl @author Zack Weinberg <zackw@panix.com
dnl @version 2006-07-23
dnl @license AllPermissive

AC_DEFUN([AC_CXX_TR1_UNORDERED_MAP],
[AC_LANG_ASSERT([C++])
 AC_CACHE_CHECK([whether the compiler supports tr1::unordered_map],
                       ac_cv_cxx_tr1_unordered_map_and_set,
          [AC_COMPILE_IFELSE(AC_LANG_SOURCE([
        #include <tr1/functional>
        #include <tr1/unordered_map>
        #include <tr1/unordered_set>
        using std::tr1::hash;
        using std::tr1::unordered_map;
        using std::tr1::unordered_set;
        using std::tr1::unordered_multimap;
        ]),
              ac_cv_cxx_tr1_unordered_map_and_set=yes,
              ac_cv_cxx_tr1_unordered_map_and_set=no)])
        if test x$ac_cv_cxx_tr1_unordered_map_and_set = xyes; then
          AC_DEFINE(HAVE_TR1_UNORDERED_MAP_AND_SET, 1,
                    [Define to 1 if your C++ compiler provides
        tr1::unordered_map and tr1::unordered_set.])
        fi
])

dnl @synopsis AC_CXX_TR1_UNORDERED_MAP_CONST_CORRECT
dnl
dnl checks for http://gcc.gnu.org/bugzilla/show_bug.cgi?id=23053
dnl
dnl @author Nathaniel Smith <njs@pobox.com>
dnl @version 2006-07-27

AC_DEFUN([AC_CXX_TR1_UNORDERED_MAP_CONST_CORRECT],
[AC_LANG_ASSERT([C++])
 AC_CACHE_CHECK([for early 4.0-series g++ bug in tr1 hashtables],
    ac_cv_cxx_tr1_hashtable_bug,
    [
     AC_COMPILE_IFELSE(AC_LANG_PROGRAM(
		    [#include <tr1/unordered_set>],
                    [std::tr1::unordered_set<int> s;
                     const std::tr1::unordered_set<int> &s_ref = s;
                     s_ref.find(27);]),
                     [ac_cv_cxx_tr1_hashtable_bug=no],
                     [ac_cv_cxx_tr1_hashtable_bug=yes])])
 if test x$ac_cv_cxx_tr1_hashtable_bug = xno; then
   AC_DEFINE(HAVE_WORKING_TR1_UNORDERED_MAP_AND_SET, 1,
             [Define if tr1::unordered_{map,set} actually work])
 fi
])
