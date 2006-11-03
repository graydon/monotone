#ifndef __PLATFORM_WRAPPED_HH__
#define __PLATFORM_WRAPPED_HH__

#include "paths.hh"
#include "platform.hh"
#include "vocab.hh"

inline utf8 tilde_expand(utf8 const & path)
{
  return tilde_expand(path());
}

inline void change_current_working_dir(any_path const & to)
{
  change_current_working_dir(to.as_external());
}

inline path::status get_path_status(any_path const & path)
{
  std::string p(path.as_external());
  return get_path_status(p.empty()?".":p);
}

inline void rename_clobberingly(any_path const & from, any_path const & to)
{
  rename_clobberingly(from.as_external(), to.as_external());
}

#endif
