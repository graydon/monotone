// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>
#include <iterator>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <iterator>

#include <boost/regex.hpp>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/convenience.hpp>

#include "app_state.hh"
#include "file_io.hh"
#include "manifest.hh"
#include "transforms.hh"
#include "sanity.hh"
#include "inodeprint.hh"
#include "platform.hh"

// this file defines the class of manifest_map objects, and various comparison
// and i/o functions on them. a manifest specifies exactly which versions
// of each file reside at which path location in a given tree.

using namespace boost;
using namespace std;

// building manifest_maps

class 
manifest_map_builder : public tree_walker
{
  app_state & app;
  manifest_map & man;
public:
  manifest_map_builder(app_state & a, manifest_map & m);
  virtual void visit_file(file_path const & path);
};

manifest_map_builder::manifest_map_builder(app_state & a, manifest_map & m) 
  : app(a), man(m) 
{
}

void 
manifest_map_builder::visit_file(file_path const & path)
{      
  if (app.lua.hook_ignore_file(path))
    return;
  hexenc<id> ident;
  L(F("scanning file %s\n") % path);
  calculate_ident(path, ident, app.lua);
  man.insert(manifest_entry(path, file_id(ident)));
}

void 
build_restricted_manifest_map(path_set const & paths,
                              manifest_map const & m_old, 
                              manifest_map & m_new, 
                              app_state & app)
{
  m_new.clear();
  inodeprint_map ipm;
  if (in_inodeprints_mode())
    {
      data dat;
      read_inodeprints(dat);
      read_inodeprint_map(dat, ipm);
    }
  for (path_set::const_iterator i = paths.begin(); i != paths.end(); ++i)
    {
      if (app.restriction_includes(*i))
        {
          // compute the current sha1 id for included files
          // we might be able to avoid it, if we have an inode fingerprint...
          inodeprint_map::const_iterator old_ip = ipm.find(*i);
          if (old_ip != ipm.end())
            {
              hexenc<inodeprint> ip;
              if (inodeprint_file(*i, ip) && ip == old_ip->second)
                {
                  // the inode fingerprint hasn't changed, so we assume the file
                  // hasn't either.
                  manifest_map::const_iterator k = m_old.find(*i);
                  I(k != m_old.end());
                  m_new.insert(*k);
                  continue;
                }
            }
          // ...ah, well, no good fingerprint, just check directly.
          N(fs::exists(mkpath((*i)())),
            F("file disappeared but exists in new manifest: %s") % (*i)());
          hexenc<id> ident;
          calculate_ident(*i, ident, app.lua);
          m_new.insert(manifest_entry(*i, file_id(ident)));
        }
      else
        {
          // copy the old manifest entry for excluded files
          manifest_map::const_iterator old = m_old.find(*i);
          N(old != m_old.end(),
            F("file restricted but does not exist in old manifest: %s") % *i);
          m_new.insert(*old);
        }
    }
}

// reading manifest_maps

struct 
add_to_manifest_map
{    
  manifest_map & man;
  explicit add_to_manifest_map(manifest_map & m) : man(m) {}
  bool operator()(match_results<std::string::const_iterator> const & res) 
  {
    std::string ident(res[1].first, res[1].second);
    std::string path(res[2].first, res[2].second);
    file_path pth(path);
    man.insert(manifest_entry(pth, hexenc<id>(ident)));
    return true;
  }
};

void 
read_manifest_map(data const & dat,
                  manifest_map & man)
{
  regex expr("^([[:xdigit:]]{40})  ([^[:space:]].*)$");
  regex_grep(add_to_manifest_map(man), dat(), expr, match_not_dot_newline);  
}

void 
read_manifest_map(manifest_data const & dat,
                  manifest_map & man)
{  
  gzip<data> decoded;
  data decompressed;
  decode_base64(dat.inner(), decoded);
  decode_gzip(decoded, decompressed);
  read_manifest_map(decompressed, man);
}



// writing manifest_maps

std::ostream & 
operator<<(std::ostream & out, manifest_entry const & e)
{
  return (out << manifest_entry_id(e) << "  " << manifest_entry_path(e) << "\n");
}


void 
write_manifest_map(manifest_map const & man,
                   manifest_data & dat)
{
  ostringstream sstr;
  copy(man.begin(),
       man.end(),
       ostream_iterator<manifest_entry>(sstr));

  data raw;
  gzip<data> compressed;
  base64< gzip<data> > encoded;

  raw = sstr.str();
  encode_gzip(raw, compressed);
  encode_base64(compressed, encoded);
  dat = manifest_data(encoded);
}

void 
write_manifest_map(manifest_map const & man,
                   data & dat)
{
  ostringstream sstr;
  for (manifest_map::const_iterator i = man.begin();
       i != man.end(); ++i)
    sstr << *i;
  dat = sstr.str();
}


