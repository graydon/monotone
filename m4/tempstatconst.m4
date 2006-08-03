# check for the obnoxious gcc 4.1.[01] template static data bug
# (see http://gcc.gnu.org/PR28016)
AC_DEFUN([AC_CXX_TEMPLATE_STATIC_CONST_BUG],
[AC_LANG_ASSERT([C++])
 code='
 template<class T> struct S { static const bool B; };
 template<class T> const bool S<T>::B = true;'

 codeA="
 $code
 int foo() { return 0; }"

 codeB="
 $code
 extern int foo();
 int main() { return foo(); }"

 AC_CACHE_CHECK(
  [whether $CXX handles templates with static const members correctly],
  ac_cv_prog_cxx_template_static_const_ok,
  [
   # Autoconf doesn't make it easy to do a test involving 
   # linking two files.  This is the best way I know of.
   AC_COMPILE_IFELSE([$codeA],
     [mv conftest.$ac_objext conftestA.$ac_objext
      save_LIBS="$LIBS"
      LIBS=conftestA.$ac_objext
      AC_LINK_IFELSE([$codeB],
        [ac_cv_prog_cxx_template_static_const_ok=yes],
        [ac_cv_prog_cxx_template_static_const_ok=no])
      LIBS="$save_LIBS"],
     [ac_cv_prog_cxx_template_static_const_ok=no])
  ])
if test $ac_cv_prog_cxx_template_static_const_ok = no; then
  # ??? This is the correct workaround for g++ and gnu ld, but maybe not
  # for others.  (But this is a GNU-specific bug, so...)
  LDFLAGS="$LDFLAGS -Wl,-z,muldefs"
fi
])
