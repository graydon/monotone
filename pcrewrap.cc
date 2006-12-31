#include "pcrewrap.hh"
#include <cstring>

#define pcre pcre_t
#include "pcre.h"
#undef pcre

using std::string;
using std::runtime_error;

static void pcre_compile_error(char const * err, int erroff,
                               char const * pattern) NORETURN;
static void pcre_study_error(char const * err) NORETURN;
static void pcre_match_error(int errcode) NORETURN;

inline unsigned int
flags_to_internal(pcre::flags f)
{
  using namespace pcre;
#define C(f_, x) (((f_) & (x)) ? PCRE_##x : 0)
  unsigned int i = 0;
  i |= C(f, NEWLINE_CR);
  i |= C(f, NEWLINE_LF);
  // NEWLINE_CRLF == NEWLINE_CR|NEWLINE_LF and so is handled above
  i |= C(f, ANCHORED);
  i |= C(f, NOTBOL);
  i |= C(f, NOTEOL);
  i |= C(f, NOTEMPTY);
  i |= C(f, CASELESS);
  i |= C(f, DOLLAR_ENDONLY);
  i |= C(f, DOTALL);
  i |= C(f, DUPNAMES);
  i |= C(f, EXTENDED);
  i |= C(f, FIRSTLINE);
  i |= C(f, MULTILINE);
  i |= C(f, UNGREEDY);
#undef C
  return i;
}

inline std::pair<void const *, void const *>
compile(const char * pattern, pcre::flags options)
{
  int erroff;
  char const * err;
  pcre_t const * basedat = pcre_compile(pattern, flags_to_internal(options),
                                        &err, &erroff, 0);
  if (!basedat)
    pcre_compile_error(err, erroff, pattern);

  pcre_extra const * extradat = pcre_study(basedat, 0, &err);
  if (err)
    pcre_study_error(err);

  return std::make_pair(static_cast<void const *>(basedat),
                        static_cast<void const *>(extradat));
}

inline unsigned int
get_capturecount(void const * bd)
{
  unsigned int cc;
  int err = pcre_fullinfo(static_cast<pcre_t const *>(bd), 0,
                          PCRE_INFO_CAPTURECOUNT,
                          static_cast<void *>(&cc));
  I(err == 0);
  return cc;
}

namespace pcre
{
  regex::regex(char const * pattern, flags options)
    : basic_regex(compile(pattern, options))
  {}

  regex::regex(string const & pattern, flags options)
    : basic_regex(compile(pattern.c_str(), options))
  {}

  regex::~regex()
  {
    if (basedat)
      pcre_free(const_cast<void *>(basedat));
    if (extradat)
      pcre_free(const_cast<void *>(extradat));
  }

  bool
  basic_regex::match(string const & subject, matches & result,
                     string::const_iterator startptr,
                     flags options) const
  {
    // pcre_exec wants its caller to provide three integer slots per
    // capturing paren, plus three more for the whole-pattern match.
    // On exit from pcre_exec, the first two-thirds of the vector will be
    // pairs of integers representing [start, end) offsets within the
    // string.  pcre_exec uses the remaining third of the vector for a
    // scratchpad.  (Why can't it allocate its own damn scratchpad?)
    unsigned int capturecount = get_capturecount(basedat);
    std::vector<int> ovec((capturecount + 1) * 3);

    // convert the start pointer to an offset within the string (the &*
    // converts each iterator to a bare pointer, which can be subtracted --
    // you should be able to subtract random-access iterators directly,
    // grumble)
    int startoffset = 0;
    if (startptr != string::const_iterator(0))
      startoffset = &*startptr - &*subject.data();

    int rc = pcre_exec(static_cast<pcre_t const *>(basedat),
                       static_cast<pcre_extra const *>(extradat),
                       subject.data(), subject.size(),
                       startoffset,
                       flags_to_internal(options),
                       &ovec.front(), ovec.size());  // ??? ovec.data()
    if (rc >= 0)
      {
        // If the return value is nonnegative, the pattern matched,
        // and rc is one more than the number of pairs of integers in
        // ovec that are meaningful.
        result.clear();
        result.reserve(capturecount + 1);
        for (int i = 0; i < rc * 2; i += 2)
          {
            if (ovec[i] == -1 && ovec[i+1] == -1)
              result.push_back(capture(string::const_iterator(0),
                                       string::const_iterator(0)));
            else
              {
                I(ovec[i] != -1 && ovec[i+1] != -1);
                result.push_back(capture(subject.begin() + ovec[i],
                                         subject.begin() + ovec[i+1]));
              }
          }
        for (unsigned int i = rc; i < capturecount + 1; i++)
          result.push_back(capture(string::const_iterator(0),
                                   string::const_iterator(0)));
        I(result.size() == capturecount + 1);
        return true;
      }
    else if (rc == PCRE_ERROR_NOMATCH)
      {
        result = matches(capturecount + 1,
                         capture(string::const_iterator(0),
                                 string::const_iterator(0)));
        I(result.size() == capturecount + 1);
        return false;
      }
    else 
      pcre_match_error(rc);
  }

  // This overload is for when you don't care about captures, only
  // whether or not it matched.
  bool
  basic_regex::match(string const & subject,
                     string::const_iterator startptr,
                     flags options) const
  {
    int startoffset = 0;
    if (startptr != string::const_iterator(0))
      startoffset = &*startptr - &*subject.data();
 
    int rc = pcre_exec(static_cast<pcre_t const *>(basedat),
                       static_cast<pcre_extra const *>(extradat),
                       subject.data(), subject.size(),
                       startoffset, flags_to_internal(options), 0, 0);
    if (rc == 0)
      return true;
    else if (rc == PCRE_ERROR_NOMATCH)
      return false;
    else 
      pcre_match_error(rc);
  }
} // namespace pcre

// These functions produce properly translated diagnostics from PCRE
// internal errors.
static void
pcre_compile_error(char const *err, int erroff, char const * pattern)
{
  using std::strcmp;
  // Special case out-of-memory ...
  if (!strcmp(err, "failed to get memory"))
    throw std::bad_alloc();

  // ... and all errors that represent program bugs.
  I(strcmp(err, "erroffset passed as NULL"));
  I(strcmp(err, "unknown option bit(s) set"));
  I(strcmp(err, "this version of PCRE is not compiled with PCRE_UTF8 support"));
  I(strcmp(err, "internal error: code overflow"));
  I(strcmp(err, "internal error: unexpected repeat"));
  I(strcmp(err, "spare error"));
  I(strcmp(err, "invalid UTF-8 string"));
  I(strcmp(err, "no error")); // because we should never get here with that

  // PCRE fails to distinguish between errors at no position and errors at
  // character offset 0 in the pattern, so in practice we give the
  // position-ful variant for all errors, but I'm leaving the == -1 check
  // here in case PCRE gets fixed.
  if (erroff == -1)
    throw pcre::compile_error(F("error in regex \"%s\": %s")
                              % pattern % gettext(err));
  else
    throw pcre::compile_error(F("error near char %d of regex \"%s\": %s")
                              % (erroff + 1) % pattern % gettext(err));
}

static void
pcre_study_error(char const * err)
{
  // If the error is not out-of-memory, it's a bug.
  I(!std::strcmp(err, "failed to get memory"));
  throw std::bad_alloc();
}

static void
pcre_match_error(int errcode)
{
  // This one actually has error codes!  Almost all of which indicate bugs
  // in monotone.
  switch(errcode)
    {
    case PCRE_ERROR_NOMEMORY:
      throw std::bad_alloc();

    case PCRE_ERROR_MATCHLIMIT:
      throw pcre::match_error(F("backtrack limit exceeded"));
      
    case PCRE_ERROR_RECURSIONLIMIT:
      throw pcre::match_error(F("recursion limit exceeded"));

    default:
      global_sanity.invariant_failure((FL("pcre_match returned %d") % errcode)
                                      .str().c_str(), __FILE__, __LINE__);
    }
}

#ifdef XGETTEXT
// This is a copy of the error message table from pcre_compile.c, with
// N_() applied to all the strings that the user will actually see.
static char const * const error_texts[] = {
     "no error",
  N_("\\ at end of pattern"),
  N_("\\c at end of pattern"),
  N_("unrecognized character follows \\"),
  N_("numbers out of order in {} quantifier"),
  /* 5 */
  N_("number too big in {} quantifier"),
  N_("missing terminating ] for character class"),
  N_("invalid escape sequence in character class"),
  N_("range out of order in character class"),
  N_("nothing to repeat"),
  /* 10 */
  N_("operand of unlimited repeat could match the empty string"),
     "internal error: unexpected repeat",
  N_("unrecognized character after (?"),
  N_("POSIX named classes are supported only within a class"),
  N_("missing )"),
  /* 15 */
  N_("reference to non-existent subpattern"),
     "erroffset passed as NULL",
     "unknown option bit(s) set",
  N_("missing ) after comment"),
  N_("parentheses nested too deeply"),
  /* 20 */
  N_("regular expression too large"),
     "failed to get memory",  // std::bad_alloc
  N_("unmatched parentheses"),
     "internal error: code overflow",
  N_("unrecognized character after (?<"),
  /* 25 */
  N_("lookbehind assertion is not fixed length"),
  N_("malformed number or name after (?("),
  N_("conditional group contains more than two branches"),
  N_("assertion expected after (?("),
  N_("(?R or (?digits must be followed by )"),
  /* 30 */
  N_("unknown POSIX class name"),
  N_("POSIX collating elements are not supported"),
     "this version of PCRE is not compiled with PCRE_UTF8 support",
     "spare error",
  N_("character value in \\x{...} sequence is too large"),
  /* 35 */
  N_("invalid condition (?(0)"),
  N_("\\C not allowed in lookbehind assertion"),
  N_("PCRE does not support \\L, \\l, \\N, \\U, or \\u"),
  N_("number after (?C is > 255"),
  N_("closing ) for (?C expected"),
  /* 40 */
  N_("recursive call could loop indefinitely"),
  N_("unrecognized character after (?P"),
  N_("syntax error after (?P"),
  N_("two named subpatterns have the same name"),
     "invalid UTF-8 string",
  /* 45 */
  N_("support for \\P, \\p, and \\X has not been compiled"),
  N_("malformed \\P or \\p sequence"),
  N_("unknown property name after \\P or \\p"),
  N_("subpattern name is too long (maximum 32 characters)"),
  N_("too many named subpatterns (maximum 10,000)"),
  /* 50 */
  N_("repeated subpattern is too long"),
  N_("octal value is greater than \\377 (not in UTF-8 mode)"),
};
#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
