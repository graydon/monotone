AC_DEFUN([AC_FUNC_ICONV_TRANSLIT],
[AC_CACHE_CHECK([if iconv supports //IGNORE//TRANSLIT],
	ac_cv_iconv_translit, [
	AC_TRY_RUN([
#include <iconv.h>

int main()
{
  return iconv_open("ASCII//IGNORE//TRANSLIT", "UTF8") == (iconv_t)(-1);
}
],
		ac_cv_iconv_translit=yes,
		ac_cv_iconv_translit=no,
		ac_cv_iconv_translit=no)
	])
if test "$ac_cv_iconv_translit" = "yes"; then
  AC_DEFINE(ICONV_TRANSLIT, 1, [Define if iconv supports //IGNORE//TRANSLIT.])
fi])
