#ifndef __MTN_SANITY_HH__
#define __MTN_SANITY_HH__

#include "sanity.hh"

struct mtn_sanity : public sanity
{
  bool relaxed;

  mtn_sanity();
  ~mtn_sanity();
  void initialize(int, char **, char const *);

  void set_relaxed(bool rel);

private:
  void inform_log(std::string const &msg);
  void inform_message(std::string const &msg);
  void inform_warning(std::string const &msg);
  void inform_error(std::string const &msg);
};

extern mtn_sanity real_sanity;

#endif
