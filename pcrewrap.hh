#ifndef _PCREWRAP_HH
#define _PCREWRAP_HH

#include <string>
#include <vector>
#include <stdexcept>
#include "sanity.hh"

// This is a sensible C++ wrapper interface around the bare C API exported
// by pcre.h.  Note that pcre.h is a very "noisy" header in terms of macro
// definitions and so we don't actually expose it here.

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

  // A capture object is a pair of string iterators, such that
  // [C.first, C.second) is the range of characters in the "subject"
  // string captured by either a full match or some pair of capturing
  // parentheses.  If both C.first and C.second are null, then the
  // associated part of the regex did not match.  It is an invariant
  // that either both or neither C.first and C.second are null.  The
  // object provides a couple of helper operations, matched() and str(),
  // for common use cases.
  struct capture : public std::pair<std::string::const_iterator,
                                    std::string::const_iterator>
  {
    capture(std::string::const_iterator a,
            std::string::const_iterator b)
      : std::pair<std::string::const_iterator, std::string::const_iterator>
        (a, b)
    { I((a == std::string::const_iterator(0)
         && b == std::string::const_iterator(0))
        || (a != std::string::const_iterator(0)
            && b != std::string::const_iterator(0))); }

    bool matched() { return (this->first != std::string::const_iterator(0)); }
    std::string str() { return std::string(this->first, this->second); }
  };
  
  // A matches object stores the result of a PCRE match operation.  It
  // is a vector of capture objects (see above) such that element N
  // corresponds to capture group N of the regexp.  Per usual,
  // match[0] encompasses the string matched by the entire regular
  // expression.
  typedef std::vector<capture> matches;
  
  // A basic_regex object is the compiled form of a PCRE regular expression.
  // You never construct this directly.
  struct basic_regex
  {
  private:
    // disable the default and copy constructors
    basic_regex();
    basic_regex(basic_regex const &);
    basic_regex & operator=(basic_regex const &);

  protected:
    void const * const basedat;
    void const * const extradat;

    // for use only by subclass constructors
    basic_regex(std::pair<void const *, void const *> p)
      : basedat(p.first), extradat(p.second) {}

  public:
    ~basic_regex() {}

    bool match(std::string const & subject, matches & result,
               std::string::const_iterator startoffset 
               = std::string::const_iterator(),
               pcre::flags options = DEFAULT) const;

    bool match(std::string const & subject,
               std::string::const_iterator startoffset 
               = std::string::const_iterator(),
               pcre::flags options = DEFAULT) const;


    // helper function which starts successive matches at the position
    // where the last match left off.
    bool nextmatch(std::string const & subject, matches & result,
                   pcre::flags options = DEFAULT) const
    {
      std::string::const_iterator startoffset(0);
      if (result.size() > 0 && result[0].matched())
        startoffset = result[0].second;
      return match(subject, result, startoffset, options);
    }
  };

  // A regex is the class you are intended to use directly, in normal usage.
  struct regex : public basic_regex
  {
    regex(char const * pattern, pcre::flags options = DEFAULT);
    regex(std::string const & pattern, pcre::flags options = DEFAULT);
    ~regex();
  };

  // exceptions thrown for errors from PCRE APIs
  struct compile_error : public std::runtime_error
  {
    explicit compile_error(i18n_format const & e)
      : runtime_error(e.str().c_str()) {}
    virtual ~compile_error() throw() {}
  };

  struct match_error : public std::runtime_error
  {
    explicit match_error(i18n_format const & e)
      : runtime_error(e.str().c_str()) {}
    virtual ~match_error() throw() {}
  };

} // namespace pcre
#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
