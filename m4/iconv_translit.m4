dnl AC_LIB_APPENDTOVAR(VAR, CONTENTS) appends the elements of CONTENTS to VAR,
dnl unless already present in VAR.
dnl Works only for CPPFLAGS, not for LIB* variables because that sometimes
dnl contains two or three consecutive elements that belong together.
AC_DEFUN([AC_LIB_APPENDTOVAR],
[
 for element in [$2]; do
 haveit=
 for x in $[$1]; do
 AC_LIB_WITH_FINAL_PREFIX([eval x=\"$x\"])
 if test "X$x" = "X$element"; then
 haveit=yes
 break
 fi
 done
 if test -z "$haveit"; then
 [$1]="${[$1]}${[$1]:+ }$element"
 fi
 done
]) 

dnl variables prefix and exec_prefix bound to the values they will have
dnl at the end of the configure script.
AC_DEFUN([AC_LIB_WITH_FINAL_PREFIX],
[
  acl_save_prefix="$prefix"
  prefix="$acl_final_prefix"
  acl_save_exec_prefix="$exec_prefix"
  exec_prefix="$acl_final_exec_prefix"
  $1
  exec_prefix="$acl_save_exec_prefix"
  prefix="$acl_save_prefix"
])

AC_DEFUN([AC_FUNC_ICONV_TRANSLIT],
[AC_CACHE_CHECK([if iconv supports //TRANSLIT],
    ac_cv_iconv_translit, [
    am_save_CPPFLAGS="$CPPFLAGS"
    AC_LIB_APPENDTOVAR([CPPFLAGS], [$INCICONV])
    am_save_LIBS="$LIBS"
    LIBS="$LIBS $LIBICONV"
    AC_TRY_RUN([
#include <iconv.h>

int main()
{
  iconv_t i = iconv_open("ASCII//TRANSLIT", "UTF-8");
  return i == (iconv_t)(-1);
}
],
        ac_cv_iconv_translit=yes,
        ac_cv_iconv_translit=no,
        ac_cv_iconv_translit=no)
    LIBS="$am_save_LIBS"
    CPPFLAGS="$am_save_CPPFLAGS"
    ])
if test "$ac_cv_iconv_translit" = "yes"; then
  AC_DEFINE(ICONV_TRANSLIT, 1, [Define if iconv supports //TRANSLIT.])
fi])
