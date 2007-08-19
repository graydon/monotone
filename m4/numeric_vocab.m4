dnl Copyright (C) 2007 Zack Weinberg <zackw@panix.com>
dnl This program is made available under the GNU GPL version 2.0 or
dnl greater. See the accompanying file COPYING for details.
dnl
dnl This program is distributed WITHOUT ANY WARRANTY; without even the
dnl implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
dnl PURPOSE.

dnl @synopsis MTN_NUMERIC_VOCAB
dnl
dnl This macro determines appropriate types for Monotone's set of
dnl exact-width numeric types, u8/u16/u32/u64 (unsigned) and
dnl s8/s16/s32/s64 (signed).  If it cannot find appropriate types, or
dnl if 'char' is not exactly 8 bits, it aborts the configure
dnl operation.
dnl
dnl It should be mentioned that sizeof([[un]signed] char) is 1 *by
dnl definition*.  It is written into both the C and C++ standards.
dnl It is never necessary to check this.

dnl We don't use AC_CHECK_SIZEOF because we don't want the macro it
dnl defines.  However, we do use the same test construct and the same
dnl cache variable; except that, as this is used only with fundamental
dnl types, we don't bother with the typedef checks.
AC_DEFUN([MTN_COMPUTE_SIZEOF],
[AS_LITERAL_IF([$1], [],
               [AC_FATAL([$0: requires literal arguments])])dnl
AC_CACHE_CHECK([size of $1], [AS_TR_SH([ac_cv_sizeof_$1])],
  [AC_COMPUTE_INT([AS_TR_SH([ac_cv_sizeof_$1])], 
    [(long int)(sizeof (ac__type_sizeof_))],
    [AC_INCLUDES_DEFAULT()
     typedef $1 ac__type_sizeof_;],
    [AC_MSG_FAILURE([cannot compute sizeof ($1)], 77)])])])

AC_DEFUN([MTN_CHOOSE_USE_OF_TYPE], [
MTN_COMPUTE_SIZEOF([$1])
case $AS_TR_SH([ac_cv_sizeof_$1]) in
  2) mtn_s16_type='$1'
     mtn_u16_type='$2' ;;
  4) mtn_s32_type='$1'
     mtn_u32_type='$2' ;;
  8) mtn_s64_type='$1'
     mtn_u64_type='$2' ;;
esac])

dnl AC_LANG_BOOL_COMPILE_TRY is not documented, but is very helpful.
dnl The content of these conditional definitions was lifted from
dnl autoconf 2.61.
m4_ifdef([AC_LANG_BOOL_COMPILE_TRY(C)], [],
[m4_define([AC_LANG_BOOL_COMPILE_TRY(C)],
[AC_LANG_PROGRAM([$1], [static int test_array @<:@1 - 2 * !($2)@:>@;
test_array @<:@0@:>@ = 0
])])])
m4_ifdef([AC_LANG_BOOL_COMPILE_TRY(C++)], [],
[m4_copy([AC_LANG_BOOL_COMPILE_TRY(C)], [AC_LANG_BOOL_COMPILE_TRY(C++)])])
m4_ifdef([AC_LANG_BOOL_COMPILE_TRY], [],
[AC_DEFUN([AC_LANG_BOOL_COMPILE_TRY],
[_AC_LANG_DISPATCH([$0], _AC_LANG, $@)])])

dnl AC_COMPUTE_INT is new in autoconf 2.61.
dnl Let's see if we can get away with just forwarding to the older
dnl _AC_COMPUTE_INT.
m4_ifdef([AC_COMPUTE_INT], [],
[m4_ifdef([_AC_COMPUTE_INT],
 [AC_DEFUN([AC_COMPUTE_INT], [_AC_COMPUTE_INT([$2],[$1],[$3],[$4])])],
 [m4_fatal([A version of Autoconf that provides AC_COMPUTE_INT or _AC_COMPUTE_INT is required], 63)])])

AC_DEFUN([MTN_ASSERT_EIGHT_BIT_CHARS],
[AC_LANG_ASSERT([C++])
 AC_MSG_CHECKING([whether char has 8 bits])
 AC_COMPILE_IFELSE([AC_LANG_BOOL_COMPILE_TRY([@%:@include <limits>],
    [std::numeric_limits<unsigned char>::digits == 8])],
    [AC_MSG_RESULT([yes])],
    [AC_MSG_RESULT([no])
     AC_MSG_ERROR([*** Monotone requires a platform with 8-bit bytes.])])])

AC_DEFUN([MTN_CHECK_PLAIN_CHAR_SIGNED],
[AC_CACHE_CHECK([whether plain char is signed], mtn_cv_plain_char_signed,
  [AC_COMPILE_IFELSE([AC_LANG_BOOL_COMPILE_TRY([], [((int)(char)-1) < 0])],
                     [mtn_cv_plain_char_signed=yes],
                     [mtn_cv_plain_char_signed=no])])])


AC_DEFUN([MTN_NUMERIC_VOCAB], [dnl
MTN_ASSERT_EIGHT_BIT_CHARS

# s8 and u8 will be some sort of 'char', but we want to use the plain
# variety for whichever one it is.
MTN_CHECK_PLAIN_CHAR_SIGNED
if test $mtn_cv_plain_char_signed = yes ; then
  mtn_s8_type='char'
  mtn_u8_type='unsigned char'
else
  mtn_s8_type='signed char'
  mtn_u8_type='char'
fi

mtn_s16_type=unknown
mtn_u16_type=unknown
mtn_s32_type=unknown
mtn_u32_type=unknown
mtn_s64_type=unknown
mtn_u64_type=unknown

# See if we can get away without 'long long' (LP64 model).
# The order here ensures that 'int' will be used if it happens
# to be the same size as 'short' or 'long'.
MTN_CHOOSE_USE_OF_TYPE([short], [unsigned short])
MTN_CHOOSE_USE_OF_TYPE([long], [unsigned long])
MTN_CHOOSE_USE_OF_TYPE([int], [unsigned int])

# If we didn't get an s16 or an s32 type we are hoz0red.
if test "$mtn_s16_type" = unknown
then AC_MSG_ERROR([*** no signed 16-bit type found])
fi
if test "$mtn_s32_type" = unknown
then AC_MSG_ERROR([*** no signed 32-bit type found])
fi

# If we didn't get an s64 type, try long long.
if test "$mtn_s64_type" = unknown; then
 MTN_CHOOSE_USE_OF_TYPE([long long], [unsigned long long])
 if test "$mtn_s64_type" = unknown
 then AC_MSG_ERROR([*** no signed 64-bit type found])
 fi
fi

AC_DEFINE_UNQUOTED([TYPE_S8],  [$mtn_s8_type],  [Type to use for `s8'.])
AC_DEFINE_UNQUOTED([TYPE_S16], [$mtn_s16_type], [Type to use for `s16'.])
AC_DEFINE_UNQUOTED([TYPE_S32], [$mtn_s32_type], [Type to use for `s32'.])
AC_DEFINE_UNQUOTED([TYPE_S64], [$mtn_s64_type], [Type to use for `s64'.])

AC_DEFINE_UNQUOTED([TYPE_U8],  [$mtn_u8_type],  [Type to use for `u8'.])
AC_DEFINE_UNQUOTED([TYPE_U16], [$mtn_u16_type], [Type to use for `u16'.])
AC_DEFINE_UNQUOTED([TYPE_U32], [$mtn_u32_type], [Type to use for `u32'.])
AC_DEFINE_UNQUOTED([TYPE_U64], [$mtn_u64_type], [Type to use for `u64'.])
])

dnl Local Variables:
dnl mode: autoconf
dnl indent-tabs-mode: nil
dnl End:
