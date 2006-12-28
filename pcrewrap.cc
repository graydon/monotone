#include "pcrewrap.hh"

#define pcre pcre_t
#include "pcre.h"
#undef pcre

using std::string;
using std::runtime_error;

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
    throw pcre::compile_error(err, erroff, pattern);

  pcre_extra const * extradat = pcre_study(basedat, 0, &err);
  if (err)
    throw pcre::study_error(err);

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
  if (err < 0)
    throw pcre::fullinfo_error(err);
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
            else if (ovec[i] == -1 || ovec[i+1] == -1)
              throw match_error(PCRE_ERROR_INTERNAL);  // should never happen
            else
              result.push_back(capture(subject.begin() + ovec[i],
                                       subject.begin() + ovec[i+1]));
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
      throw match_error(rc);
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
      throw match_error(rc);
  }

  // error handling.

  static string
  compile_error_message(char const * err, int offset, char const * pattern)
  {
    return (F("parse error at char %d in pattern '%s': %s")
            % offset % pattern % err).str();
  }

  compile_error::compile_error(char const * err, int offset,
                               char const * pattern)
    : std::runtime_error(compile_error_message(err, offset, pattern))
  {}

  match_error::match_error(int code)
    : std::runtime_error((F("Error during matching, code %d") % code).str())
  {}

  fullinfo_error::fullinfo_error(int code)
    : std::runtime_error((F("Error getting capture count, code %d") % code)
                         .str())
  {}

} // namespace pcre

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
