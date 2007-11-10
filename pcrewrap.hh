// Copyright (C) 2007 Zack Weinberg <zackw@panix.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef _PCREWRAP_HH
#define _PCREWRAP_HH

// This is a sensible C++ wrapper interface around the bare C API exported
// by pcre.h.  Note that pcre.h is a very "noisy" header in terms of macro
// definitions and so we don't actually expose it here. Unfortunately, this
// means we have to hope this pair of forward declarations will not change...

struct real_pcre;
struct pcre_extra;

namespace pcre
{
  enum flags
    {
      // flags usable with both pcre_compile and pcre_exec
      DEFAULT         = 0x0000,                    // no special behavior
      NEWLINE_CR      = 0x0001,                    // newline is \r
      NEWLINE_LF      = 0x0002,                    // newline is \n
      NEWLINE_CRLF    = (NEWLINE_CR|NEWLINE_LF),   // newline is \r\n
      ANCHORED        = 0x0004,                    // match only at beginning
                                                   // of string (\A in pat)
      // flags usable only with pcre_exec
      NOTBOL          = 0x0008, // beginning of string isn't beginning of line
      NOTEOL          = 0x0010, // end of string isn't end of line
      NOTEMPTY        = 0x0020, // an empty match is a match failure

      // flags usable only with pcre_compile
      CASELESS        = 0x0040, // case insensitive match (?i)
      DOLLAR_ENDONLY  = 0x0080, // only in !MULTILINE mode, $ equiv to \Z
      DOTALL          = 0x0100, // dot matches newline (?s)
      DUPNAMES        = 0x0200, // permit duplicate names for named captures
      EXTENDED        = 0x0400, // whitespace permitted in syntax (?x)
      FIRSTLINE       = 0x0800, // match must begin before first newline
      MULTILINE       = 0x1000, // ^ and $ match at internal newlines (?m)
      UNGREEDY        = 0x4000, // quantifiers aren't greedy unless
                                // followed with ? (opposite of default)
    };

  // A regex object is the compiled form of a PCRE regular expression.
  struct regex
  {
  private:
    // disable the default and copy constructors - we never need to copy
    // these, and this lets us use bare pointers below instead of
    // boost::shared_ptr.
    regex();
    regex(regex const &);
    regex & operator=(regex const &);

    // data
    struct real_pcre const * basedat;
    struct pcre_extra const * extradat;

    // used by constructors
    void init(char const *, pcre::flags);

  public:
    regex(char const * pattern, pcre::flags options = DEFAULT);
    regex(std::string const & pattern, pcre::flags options = DEFAULT);
    ~regex();

    bool match(std::string const & subject,
               pcre::flags options = DEFAULT) const;
  };
} // namespace pcre

#endif // pcrewrap.hh

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
