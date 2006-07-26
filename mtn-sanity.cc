#include "mtn-sanity.hh"
#include "ui.hh"

extern sanity & global_sanity;
mtn_sanity real_sanity;
sanity & global_sanity(real_sanity);

mtn_sanity::mtn_sanity() : relaxed(false)
{}

mtn_sanity::~mtn_sanity()
{}

void
mtn_sanity::set_relaxed(bool rel)
{
  relaxed = rel;
}

void
mtn_sanity::inform_log(std::string const &msg)
{
  ui.inform(msg);
}

void
mtn_sanity::inform_message(std::string const &msg)
{
  ui.inform(msg);
}

void
mtn_sanity::inform_warning(std::string const &msg)
{
  ui.warn(msg);
}

void
mtn_sanity::inform_error(std::string const &msg)
{
  ui.inform(msg);
}
