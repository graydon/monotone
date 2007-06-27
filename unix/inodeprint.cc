// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "base.hh"
#include <sys/stat.h>
#include <time.h>

#include "platform.hh"

inline bool should_abort(time_t now, time_t then)
{
  if (now < 0 || then < 0)
    return false;
  double difference = difftime(now, then);
  return (difference >= -3 && difference <= 3);
}

inline bool is_future(time_t now, time_t then)
{
  if (now < 0 || then < 0)
    return false;
  return difftime(now, then) > 0;
}

bool inodeprint_file(std::string const & file, inodeprint_calculator & calc)
{
  struct stat st;
  if (stat(file.c_str(), &st) < 0)
    return false;

  time_t now;
  time(&now);

  calc.note_nowish(should_abort(now, st.st_ctime));
  calc.add_item(st.st_ctime);
  calc.note_future(is_future(now, st.st_ctime));

  // aah, portability.
#ifdef HAVE_STRUCT_STAT_ST_CTIM_TV_NSEC
  calc.add_item(st.st_ctim.tv_nsec);
#elif defined(HAVE_STRUCT_STAT_ST_CTIMESPEC_TV_NSEC)
  calc.add_item(st.st_ctimespec.tv_nsec);
#elif defined(HAVE_STRUCT_STAT_ST_CTIMENSEC)
  calc.add_item(st.st_ctimensec);
#else
  calc.add_item((long)0);
#endif

  calc.note_nowish(should_abort(now, st.st_mtime));
  calc.add_item(st.st_mtime);
  calc.note_future(is_future(now, st.st_mtime));

#ifdef HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC
  calc.add_item(st.st_mtim.tv_nsec);
#elif defined(HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC)
  calc.add_item(st.st_mtimespec.tv_nsec);
#elif defined(HAVE_STRUCT_STAT_ST_MTIMENSEC)
  calc.add_item(st.st_mtimensec);
#else
  calc.add_item((long)0);
#endif

  calc.add_item(st.st_mode);
  calc.add_item(st.st_ino);
  calc.add_item(st.st_dev);
  calc.add_item(st.st_uid);
  calc.add_item(st.st_gid);
  calc.add_item(st.st_size);

  return true;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
