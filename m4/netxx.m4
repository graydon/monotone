# checks done primarily for the benefit of netxx

# Check for IPv6.  Let the user enable or disable it manually using a
# three-state (yes|no|auto) --enable argument.
AC_DEFUN([AC_NET_IPV6],
[AC_ARG_ENABLE(ipv6,
   AS_HELP_STRING([--enable-ipv6],[enable IPv6 support (default=auto)]), ,
   enable_ipv6=auto)
 if test x"${enable_ipv6}" = xauto || test x"${enable_ipv6}" = xyes; then
   AC_CHECK_TYPE([sockaddr_in6],
      [enable_ipv6=yes],
      [if test x"${enable_ipv6}" = xyes; then
         AC_MSG_FAILURE([IPv6 explicitly requested but it could not be found])
       fi
       enable_ipv6=no],
		    [#ifdef WIN32
                     #include <winsock2.h>
                     #else
                     #include <sys/types.h>
                     #include <sys/socket.h>
                     #include <netinet/in.h>
                     #include <arpa/inet.h>
                     #endif])
 fi
 # Control cannot reach this point without $enable_ipv6 being either
 # "yes" or "no".
 if test $enable_ipv6 = yes; then
   AC_DEFINE(USE_IPV6, 1, [Define if IPv6 support should be included.])
 fi
])

AC_DEFUN([MTN_NETXX_DEPENDENCIES],
[AC_NET_IPV6
 AM_CONDITIONAL(MISSING_INET6, [test $enable_ipv6 = no])
 AC_SEARCH_LIBS([gethostbyname], [nsl])
 AC_SEARCH_LIBS([accept], [socket])
 AC_SEARCH_LIBS([inet_aton], [resolv])
 AC_CHECK_FUNCS([gethostbyaddr inet_ntoa socket])
 AC_CHECK_FUNC(inet_pton, [AM_CONDITIONAL(MISSING_INET_PTON, false)], 
			  [AM_CONDITIONAL(MISSING_INET_PTON, true)])

 AC_CHECK_FUNC(inet_ntop, [AM_CONDITIONAL(MISSING_INET_NTOP, false)], 
			  [AM_CONDITIONAL(MISSING_INET_NTOP, true)])

 AC_CHECK_FUNC(getaddrinfo, [AM_CONDITIONAL(MISSING_GETADDRINFO, false)], 
			    [AM_CONDITIONAL(MISSING_GETADDRINFO, true)])
 AC_HAVE_INADDR_NONE
 AC_CHECK_TYPES([socklen_t],,,[
  #include <sys/types.h>
  #include <sys/socket.h>
 ])
])
