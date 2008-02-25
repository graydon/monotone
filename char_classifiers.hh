#ifndef __CHAR_CLASSIFIERS_HH__
#define __CHAR_CLASSIFIERS_HH__

// Copyright (C) 2004 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// We don't use the standard C <ctype.h> macros because their results
// may depend on the global locale, and we don't use the standard C++
// <locale> ctype facet (in classic() mode, of course) because, well,
// someone should try it and verify that it's not slower.

// Yes, we are aware that these will break horribly with EBCDIC.

inline bool is_digit(char x)
{
  return (x >= '0' && x <= '9');
}

inline bool is_xdigit(char x)
{
  return ((x >= '0' && x <= '9')
          || (x >= 'a' && x <= 'f'));
}

inline bool is_alpha(char x)
{
  return ((x >= 'a' && x <= 'z')
          || (x >= 'A' && x <= 'Z'));
}

inline bool is_alnum(char x)
{
  return ((x >= '0' && x <= '9')
          || (x >= 'a' && x <= 'z')
          || (x >= 'A' && x <= 'Z'));
}

inline bool is_space(char x)
{
  return (x == ' ')
    || (x == '\n')
    || (x == '\t')
    || (x == '\r')
    || (x == '\v')
    || (x == '\f');
}

inline bool is_upper(char x)
{
  return (x >= 'A' && x <= 'Z');
}

inline bool is_lower(char x)
{
  return (x >= 'a' && x <= 'z');
}

inline char to_upper(char x)
{
  return is_lower(x) ? (x - 'a' + 'A') : x;
}

inline char to_lower(char x)
{
  return is_upper(x) ? (x - 'A' + 'a') : x;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __CHAR_CLASSIFIERS_HH__
