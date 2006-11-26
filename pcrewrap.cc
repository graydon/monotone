#include <string>
#include <vector>
#include <exception>
#include <sstream>

#include "pcrewrap.hh"

#define pcre pcre_t
#include "pcre.h"
#undef pcre

static unsigned int
flags_to_internal(pcre::flags f)
{
  using namespace pcre;
#define C(f_, x) (((f_) & (x)) ? PCRE_##x : 0)
  unsigned int i = 0;
  i |= C(f, NEWLINE_CR);
  i |= C(f, NEWLINE_LF);
  // NEWLINE_CRLF is handled above
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
  return i;
}

namespace pcre
{
  void regex::init(const char *pattern, pcre::flags options)
  {
    int erroff;
    const char *err;
    basedat = static_cast<const void *>
      (pcre_compile(pattern, flags_to_internal(options), &err, &erroff, 0));
    if (!basedat)
      throw compile_error(err, erroff, pattern);

    int errcode = pcre_fullinfo(static_cast<const pcre_t *>(basedat), 0,
				PCRE_INFO_CAPTURECOUNT,
				static_cast<void *>(&capturecount));
    if (errcode < 0)
      throw compile_error((F("pcre_fullinfo error %d") % errcode).str().c_str(),
			  0, pattern);
  }

  regex::regex(const char *pattern, pcre::flags options)
    : basedat(0), extradat(0), capturecount(0)
  {
    this->init(pattern, options);
  }

  regex::regex(const std::string &pattern, pcre::flags options)
    : basedat(0), extradat(0), capturecount(0)
  {
    this->init(pattern.c_str(), options);
  }

  regex::~regex()
  {
    if (basedat)
      pcre_free(const_cast<void*>(basedat));
    if (extradat)
      pcre_free(const_cast<void*>(extradat));
  }

  void regex::study()
  {
    const char *err;
    extradat = static_cast<const void *>
      (pcre_study(static_cast<const pcre_t *>(basedat), 0, &err));
    if (err)
      throw study_error(err);
  }

  bool
  regex::match(const std::string &subject, matches &result,
	       std::string::const_iterator startptr,
	       pcre::flags options) const
  {
    int startoffset = 0;
    if (startptr != std::string::const_iterator(0))
      startoffset = &*startptr - &*subject.data();
 
    // pcre_exec has a bizarro calling convention.  It wants ovec to
    // provide three integer slots per capturing paren, plus three
    // more (for the whole-pattern match).  The first two-thirds of
    // the vector will contain useful pairs of integers on exit from
    // pcre_exec; the last third will be used as scribble space by
    // pcre_exec.  (Why can't it allocate its own damn scribble space?)
    std::vector<int> ovec((capturecount + 1) * 3);
    int rc = pcre_exec(static_cast<const pcre_t *>(basedat),
		       static_cast<const pcre_extra *>(extradat),
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
	      result.push_back(capture(std::string::const_iterator(0),
				       std::string::const_iterator(0)));
	    else if (ovec[i] == -1 || ovec[i+1] == -1)
	      throw match_error(PCRE_ERROR_INTERNAL);  // should never happen
	    else
	      result.push_back(capture(subject.begin() + ovec[i],
				       subject.begin() + ovec[i+1]));
	  }
	for (int i = rc; i < capturecount + 1; i++)
	  result.push_back(capture(std::string::const_iterator(0),
				   std::string::const_iterator(0)));
	I(result.size() == capturecount + 1);
	return true;
      }
    else if (rc == PCRE_ERROR_NOMATCH)
      {
	result = matches(capturecount + 1,
			 capture(std::string::const_iterator(0),
				 std::string::const_iterator(0)));
	I(result.size() == capturecount + 1);
	return false;
      }
    else 
      throw match_error(rc);
  }

  // This overload is for when you don't care about captures, only
  // whether or not it matched.
  bool
  regex::match(const std::string &subject,
	       std::string::const_iterator startptr,
	       pcre::flags options) const
  {
    int startoffset = 0;
    if (startptr != std::string::const_iterator(0))
      startoffset = &*startptr - &*subject.data();
 
    int rc = pcre_exec(static_cast<const pcre_t *>(basedat),
		       static_cast<const pcre_extra *>(extradat),
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

  static std::string
  compile_error_message(const char *err, int offset, const char *pattern)
  {
    return (F("parse error at char %d in pattern '%s': %s")
	    % offset % pattern % err).str();
  }

  compile_error::compile_error(const char *err, int offset, const char *pattern)
    : std::runtime_error(compile_error_message(err, offset, pattern))
  {}

  match_error::match_error(int code)
    : std::runtime_error((F("Error during matching, code %d") % code).str())
  {}

} // namespace pcre
