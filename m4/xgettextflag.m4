# Check whether xgettext supports the --flag option.  If it does not,
# we forcibly override XGETTEXT_OPTIONS in po/Makefile.in to prevent
# its being used.  (See po/Makevars for further explanation.)
#
# Yes, this is a big hack, but due to the way po/Makefile is
# generated, there is no other way to do this in a dynamic fashion.

AC_DEFUN([AC_PROG_XGETTEXT_FLAG_OPTION],
[AC_CACHE_CHECK([whether xgettext supports --flag],
		ac_cv_prog_xgettext_flag_option,
  [echo 'int main(void) { return 0; }' >> conftest.c
   if ${XGETTEXT} --flag printf:1:c-format -o conftest.po conftest.c \
      >/dev/null 2>&1
   then ac_cv_prog_xgettext_flag_option=yes
   else ac_cv_prog_xgettext_flag_option=no
   fi])
 AC_CONFIG_FILES([po/Makefile.in],
  [if test $xgettext_flag_option = no; then
     echo 'XGETTEXT_OPTIONS = $(XGETTEXT_OPTIONS_NO_FLAG)' >>po/Makefile.in
   fi],
  [xgettext_flag_option=$ac_cv_prog_xgettext_flag_option])
])
