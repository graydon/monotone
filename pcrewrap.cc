// Copyright (C) 2007 Zack Weinberg <zackw@panix.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "pcrewrap.hh"
#include "sanity.hh"
#include <cstring>

// This dirty trick is necessary to prevent the 'pcre' typedef defined by
// pcre.h from colliding with namespace pcre.
#define pcre pcre_t
#include "pcre_config.h"
#include "pcre.h"
#undef pcre

using std::string;

static NORETURN(void pcre_compile_error(int errcode, char const * err,
                                        int erroff, char const * pattern));
static NORETURN(void pcre_study_error(char const * err, char const * pattern));
static NORETURN(void pcre_match_error(int errcode));

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
  void regex::init(char const * pattern, flags options)
  {
    int errcode;
    int erroff;
    char const * err;
    basedat = pcre_compile2(pattern, flags_to_internal(options),
                            &errcode, &err, &erroff, 0);
    if (!basedat)
      pcre_compile_error(errcode, err, erroff, pattern);

    pcre_extra *ed = pcre_study(basedat, 0, &err);
    if (err)
      pcre_study_error(err, pattern);
    if (!ed)
      {
        // I resent that C++ requires this cast.
        ed = (pcre_extra *)pcre_malloc(sizeof(pcre_extra));
        std::memset(ed, 0, sizeof(pcre_extra));
      }

    // We set a fairly low recursion depth to avoid stack overflow.
    // Per pcrestack(3), one should assume 500 bytes per recursion;
    // it should be safe to let pcre have a megabyte of stack, so
    // that's a depth of 2000, give or take.  (For reference, the
    // default stack limit on Linux is 8MB.)
    ed->flags |= PCRE_EXTRA_MATCH_LIMIT_RECURSION;
    ed->match_limit_recursion = 2000;
    extradat = ed;
  }

  regex::regex(char const * pattern, flags options)
  {
    this->init(pattern, options);
  }

  regex::regex(string const & pattern, flags options)
  {
    this->init(pattern.c_str(), options);
  }

  regex::~regex()
  {
    if (basedat)
      pcre_free(const_cast<pcre_t *>(basedat));
    if (extradat)
      pcre_free(const_cast<pcre_extra *>(extradat));
  }

  bool
  regex::match(string const & subject,
               string::const_iterator startptr,
               flags options) const
  {
    int startoffset = 0;
    if (startptr != string::const_iterator(0))
      startoffset = &*startptr - &*subject.data();

    int rc = pcre_exec(basedat, extradat,
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

// When the library returns an error, these functions discriminate between
// bugs in monotone and user errors in regexp writing.
static void
pcre_compile_error(int errcode, char const * err,
                   int erroff, char const * pattern)
{
  // One of the more entertaining things about the PCRE API is that
  // while the numeric error codes are documented, they do not get
  // symbolic names.

  switch (errcode)
    {
    case 21: // failed to get memory
      throw std::bad_alloc();

    case 10: // [code allegedly not in use]
    case 11: // internal error: unexpected repeat
    case 16: // erroffset passed as NULL
    case 17: // unknown option bit(s) set
    case 19: // [code allegedly not in use]
    case 23: // internal error: code overflow
    case 33: // [code allegedly not in use]
    case 50: // [code allegedly not in use]
    case 52: // internal error: overran compiling workspace
    case 53: // internal error: previously-checked referenced subpattern
             // not found
      throw oops((F("while compiling regex \"%s\": %s") % pattern % err)
                 .str().c_str());

    default:
      // PCRE fails to distinguish between errors at no position and errors at
      // character offset 0 in the pattern, so in practice we give the
      // position-ful variant for all errors, but I'm leaving the == -1 check
      // here in case PCRE gets fixed.
      throw informative_failure((erroff == -1
                                 ? (F("error in regex \"%s\": %s")
                                    % pattern % err)
                                 : (F("error near char %d of regex \"%s\": %s")
                                    % (erroff + 1) % pattern % err)
                                 ).str().c_str());
    }
}

static void
pcre_study_error(char const * err, char const * pattern)
{
  // This interface doesn't even *have* error codes.
  // If the error is not out-of-memory, it's a bug.
  if (!std::strcmp(err, "failed to get memory"))
    throw std::bad_alloc();
  else
    throw oops((F("while studying regex \"%s\": %s") % pattern % err)
               .str().c_str());
}

static void
pcre_match_error(int errcode)
{
  // This interface provides error codes with symbolic constants for them!
  // But it doesn't provide string versions of them.  As most of them
  // indicate bugs in monotone, it's not worth defining our own strings.

  switch(errcode)
    {
    case PCRE_ERROR_NOMEMORY:
      throw std::bad_alloc();

    case PCRE_ERROR_MATCHLIMIT:
      throw informative_failure
        (_("backtrack limit exceeded in regular expression matching"));

    case PCRE_ERROR_RECURSIONLIMIT:
      throw informative_failure
        (_("recursion limit exceeded in regular expression matching"));

    case PCRE_ERROR_BADUTF8:
    case PCRE_ERROR_BADUTF8_OFFSET:
      throw informative_failure
        (_("invalid UTF-8 sequence found during regular expression matching"));

    default:
      throw oops((F("pcre_match returned %d") % errcode)
                 .str().c_str());
    }
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
