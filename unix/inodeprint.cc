// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <sys/stat.h>
#include <time.h>

#include "botan/botan.h"
#include "botan/sha160.h"

#include "platform.hh"
#include "transforms.hh"
#include "file_io.hh"
#include "constants.hh"

namespace
{
  template <typename T> void
  add_hash(Botan::SHA_160 & hash, T obj)
  {
      // FIXME: this is not endian safe, which will cause problems
      // if the inodeprint listing is shared between machines of
      // different types (over NFS etc).
      size_t size = sizeof(obj);
      hash.update(reinterpret_cast<Botan::byte const *>(&size),
                  sizeof(size));
      hash.update(reinterpret_cast<Botan::byte const *>(&obj),
                  sizeof(obj));
  }
};

// To make this more robust, there are some tricks:
//   -- we refuse to inodeprint files whose times are within a few seconds of
//      'now'.  This is because, we might memorize the inodeprint, then
//      someone writes to the file, and this write does not update the
//      timestamp -- or rather, it does update the timestamp, but nothing
//      happens, because the new value is the same as the old value.  We use
//      "a few seconds" to make sure that it is larger than whatever the
//      filesystem's timekeeping granularity is (rounding to 2 seconds is
//      known to exist in the wild).
//   -- by the same reasoning, we should also refuse to inodeprint files whose
//      time is in the future, because it is possible that someone will write
//      to that file exactly when that future second arrives, and we will
//      never notice.  However, this would create persistent and hard to
//      diagnosis slowdowns, whenever a tree accidentally had its times set
//      into the future.  Therefore, to handle this case, we include a "is
//      this time in the future?" bit in the hashed information.  This bit
//      will change when we pass the future point, and trigger a re-check of
//      the file's contents.
// 
// This is, of course, still not perfect.  There is no way to make our stat
// atomic with the actual read of the file, so there's always a race condition
// there.  Additionally, this handling means that checkout will never actually
// inodeprint anything, but rather the first command after checkout will be
// slow.  There doesn't seem to be anything that could be done about this.

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

bool inodeprint_file(file_path const & file, hexenc<inodeprint> & ip)
{
  struct stat st;
  if (stat(file.as_external().c_str(), &st) < 0)
    return false;

  time_t now;
  time(&now);

  Botan::SHA_160 hash;

  if (should_abort(now, st.st_ctime))
    return false;
  add_hash(hash, st.st_ctime);
  add_hash(hash, is_future(now, st.st_ctime));

  // aah, portability.
#ifdef HAVE_STRUCT_STAT_ST_CTIM_TV_NSEC
  add_hash(hash, st.st_ctim.tv_nsec);
#elif defined(HAVE_STRUCT_STAT_ST_CTIMESPEC_TV_NSEC)
  add_hash(hash, st.st_ctimespec.tv_nsec);
#elif defined(HAVE_STRUCT_STAT_ST_CTIMENSEC)
  add_hash(hash, st.st_ctimensec);
#else
  add_hash(hash, (long)0);
#endif

  if (should_abort(now, st.st_mtime))
    return false;
  add_hash(hash, st.st_mtime);
  add_hash(hash, is_future(now, st.st_mtime));

#ifdef HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC
  add_hash(hash, st.st_mtim.tv_nsec);
#elif defined(HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC)
  add_hash(hash, st.st_mtimespec.tv_nsec);
#elif defined(HAVE_STRUCT_STAT_ST_MTIMENSEC)
  add_hash(hash, st.st_mtimensec);
#else
  add_hash(hash, (long)0);
#endif

  add_hash(hash, st.st_mode);
  add_hash(hash, st.st_ino);
  add_hash(hash, st.st_dev);
  add_hash(hash, st.st_uid);
  add_hash(hash, st.st_gid);
  add_hash(hash, st.st_size);

  char digest[constants::sha1_digest_length];
  hash.final(reinterpret_cast<Botan::byte *>(digest));
  std::string out(digest, constants::sha1_digest_length);
  inodeprint ip_raw(out);
  encode_hexenc(ip_raw, ip);
  return true;
}
