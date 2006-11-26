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
      ANCHORED        = 0x0004,			   // match only at beginning
                                                   // of string (\A in pat)
      // flags usable only with pcre_exec
      NOTBOL	      = 0x0008,	// beginning of string isn't beginning of line
      NOTEOL	      = 0x0010, // end of string isn't end of line
      NOTEMPTY	      = 0x0020, // an empty match is a match failure

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
  
  // A regex object is the compiled form of a PCRE regular expression.
  class regex
  {
    const void *basedat;
    const void *extradat;
    int capturecount;

    // default and copy constructors are restricted
    regex();
    regex(const regex &);
    regex &operator=(const regex &);

    // thanks to silly C++ we have to have an internal "initialize" method
    void init(const char *, pcre::flags);

  public:
    regex(const char *pattern, pcre::flags options = DEFAULT);
    regex(const std::string &pattern, pcre::flags options = DEFAULT);
    ~regex();

    void study();  // do extra upfront work to speed up subsequent matches

    bool match(const std::string &subject, matches &result,
	       std::string::const_iterator startoffset 
	       = std::string::const_iterator(),
	       pcre::flags options = DEFAULT) const;

    bool match(const std::string &subject,
	       std::string::const_iterator startoffset 
	       = std::string::const_iterator(),
	       pcre::flags options = DEFAULT) const;


    // helper function which starts successive matches at the position
    // where the last match left off.
    bool nextmatch(const std::string &subject, matches &result,
		   pcre::flags options = DEFAULT) const
    {
      std::string::const_iterator startoffset(0);
      if (result.size() > 0 && result[0].matched())
	startoffset = result[0].second;
      return match(subject, result, startoffset, options);
    }
  };

  // For later: regex variant that takes monotone's "utf8" pseudostrings and
  // sets PCRE_UTF8; named capture support.

  // exceptions thrown for errors from PCRE APIs
  struct compile_error : public std::runtime_error
  {
    explicit compile_error(char const * error, int offset,
			   char const * pattern);
    virtual ~compile_error() throw() {};
  };

  struct study_error : public std::runtime_error
  {
    explicit study_error(char const * error) : runtime_error(error) {};
    virtual ~study_error() throw() {};
  };

  struct match_error : public std::runtime_error
  {
    explicit match_error(int code);
    virtual ~match_error() throw() {};
  };

} // namespace pcre
#endif
