AC_DEFUN([AC_HAVE_INADDR_NONE],
[AC_CACHE_CHECK([whether INADDR_NONE is defined], ac_cv_have_inaddr_none,
 [AC_COMPILE_IFELSE([AC_LANG_PROGRAM([
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
],[
unsigned long foo = INADDR_NONE;
])],
  [ac_cv_have_inaddr_none=yes],
  [ac_cv_have_inaddr_none=no])])
 if test x$ac_cv_have_inaddr_none != xyes; then
   AC_DEFINE(INADDR_NONE, 0xffffffff,
 [Define to value of INADDR_NONE if not provided by your system header files.])
 fi])
