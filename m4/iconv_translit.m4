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
