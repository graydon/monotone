dnl @synopsis AC_PROG_CXX_WARNINGS([ANSI])
dnl
dnl Enables a reasonable set of warnings for the C compiler.
dnl Optionally, if the first argument is nonempty, turns on flags which
dnl enforce and/or enable proper ANSI C if such are known with the
dnl compiler used.
dnl
dnl Currently this macro knows about GCC, Solaris C compiler, Digital
dnl Unix C compiler, C for AIX Compiler, HP-UX C compiler, IRIX C
dnl compiler, NEC SX-5 (Super-UX 10) C compiler, and Cray J90 (Unicos
dnl 10.0.0.8) C compiler.
dnl
dnl for C++, some of them might not work, esp. because I didn't adapt
dnl the strings to compare compiler type. known to work are:
dnl GCC, Sun Studio
dnl
dnl based on VL_PROG_CC_WARNINGS by Ville Laurikari <vl@iki.fi>
dnl
dnl WORK IN PROGRESS: please don't release separately yet.
dnl                   patches welcome!
dnl
dnl @category Cxx
dnl @author Patrick Mauritz <oxygene@studentenbude.ath.cx>
dnl @version 2005-08-15
dnl @license AllPermissive

AC_DEFUN([AC_PROG_CXX_WARNINGS], [
  ansi=$1
  if test -z "$ansi"; then
    msg="for C++ compiler warning flags"
  else
    msg="for C++ compiler warning and ANSI conformance flags"
  fi
  AC_CACHE_CHECK($msg, ac_cv_prog_cxx_warnings, [
    if test -n "$CXX"; then
      cat > conftest.c <<EOF
int main(int argc, char **argv) { return 0; }
EOF

      dnl GCC
      if test "$GXX" = "yes"; then
        if test -z "$ansi"; then
          ac_cv_prog_cxx_warnings="-Wall -W -Wno-unused"
        else
          ac_cv_prog_cxx_warnings="-Wall -W -Wno-unused -ansi -pedantic"
        fi

      dnl Most compilers print some kind of a version string with some command
      dnl line options (often "-V").  The version string should be checked
      dnl before doing a test compilation run with compiler-specific flags.
      dnl This is because some compilers (like the Cray compiler) only
      dnl produce a warning message for unknown flags instead of returning
      dnl an error, resulting in a false positive.  Also, compilers may do
      dnl erratic things when invoked with flags meant for a different
      dnl compiler.

      dnl Solaris C compiler (WorkShop)
      elif $CXX -V 2>&1 | grep -i "WorkShop" > /dev/null 2>&1 &&
           $CXX -c -v -Xc conftest.c > /dev/null 2>&1 &&
           test -f conftest.o; then
        if test -z "$ansi"; then
          ac_cv_prog_cxx_warnings="-v"
        else
          ac_cv_prog_cxx_warnings="-v -Xc"
        fi

      dnl Solaris C compiler (Studio)
      dnl has warnings by default. not sure if additional warnings (+w)
      dnl are really useful
      elif $CXX -V 2>&1 | grep -i "SUN C++ 5" > /dev/null 2>&1 &&
           $CXX -c -v -Xc conftest.c > /dev/null 2>&1 &&
           test -f conftest.o; then
        if test -z "$ansi"; then
          ac_cv_prog_cxx_warnings=""
        else
          ac_cv_prog_cxx_warnings=""
        fi

      dnl Digital Unix C compiler
      elif $CXX -V 2>&1 | grep -i "Digital UNIX Compiler" > /dev/null 2>&1 &&
           $CXX -c -verbose -w0 -warnprotos -std1 conftest.c > /dev/null 2>&1 &&
           test -f conftest.o; then
        if test -z "$ansi"; then
          ac_cv_prog_cxx_warnings="-verbose -w0 -warnprotos"
        else
          ac_cv_prog_cxx_warnings="-verbose -w0 -warnprotos -std1"
        fi

      dnl C for AIX Compiler
      elif $CXX 2>&1 | grep -i "C for AIX Compiler" > /dev/null 2>&1 &&
           $CXX -c -qlanglvl=ansi -qinfo=all conftest.c > /dev/null 2>&1 &&
           test -f conftest.o; then
        if test -z "$ansi"; then
          ac_cv_prog_cxx_warnings="-qsrcmsg -qinfo=all:noppt:noppc:noobs:nocnd"
        else
          ac_cv_prog_cxx_warnings="-qsrcmsg -qinfo=all:noppt:noppc:noobs:nocnd -qlanglvl=ansi"
        fi

      dnl IRIX C compiler
      elif $CXX -version 2>&1 | grep -i "MIPSpro Compilers" > /dev/null 2>&1 &&
           $CXX -c -fullwarn -ansi -ansiE conftest.c > /dev/null 2>&1 &&
           test -f conftest.o; then
        if test -z "$ansi"; then
          ac_cv_prog_cxx_warnings="-fullwarn"
        else
          ac_cv_prog_cxx_warnings="-fullwarn -ansi -ansiE"
        fi

      dnl HP-UX C compiler
      elif what $CXX 2>&1 | grep -i "HP C Compiler" > /dev/null 2>&1 &&
           $CXX -c -Aa +w1 conftest.c > /dev/null 2>&1 &&
           test -f conftest.o; then
        if test -z "$ansi"; then
          ac_cv_prog_cxx_warnings="+w1"
        else
          ac_cv_prog_cxx_warnings="+w1 -Aa"
        fi

      dnl The NEC SX-5 (Super-UX 10) C compiler
      elif $CXX -V 2>&1 | grep "/SX" > /dev/null 2>&1 &&
           $CXX -c -pvctl[,]fullmsg -Xc conftest.c > /dev/null 2>&1 &&
           test -f conftest.o; then
        if test -z "$ansi"; then
          ac_cv_prog_cxx_warnings="-pvctl[,]fullmsg"
        else
          ac_cv_prog_cxx_warnings="-pvctl[,]fullmsg -Xc"
        fi

      dnl The Cray C compiler (Unicos)
      elif $CXX -V 2>&1 | grep -i "Cray" > /dev/null 2>&1 &&
           $CXX -c -h msglevel 2 conftest.c > /dev/null 2>&1 &&
           test -f conftest.o; then
        if test -z "$ansi"; then
          ac_cv_prog_cxx_warnings="-h msglevel 2"
        else
          ac_cv_prog_cxx_warnings="-h msglevel 2 -h conform"
        fi

      fi
      rm -f conftest.*
    fi
    if test -z "$ac_cv_prog_cxx_warnings"; then
      ac_cv_prog_cxx_warnings="unknown"
    fi
  ])
  if test x"$ac_cv_prog_cxx_warnings" != xunknown; then
    CXXFLAGS="$CXXFLAGS $ac_cv_prog_cxx_warnings"
  fi
])
