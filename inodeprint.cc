// Copyright (C) 2005 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <iterator>
#include <sstream>
#include <algorithm>
#include <iterator>

#include "inodeprint.hh"
#include "sanity.hh"
#include "platform.hh"
#include "transforms.hh"
#include "constants.hh"
#include "basic_io.hh"

#include "botan/botan.h"
#include "botan/sha160.h"

using std::ostream;
using std::ostringstream;
using std::string;

// this file defines the inodeprint_map structure, and some operations on it.

namespace
{
  namespace syms
  {
    // roster symbols
    symbol const format_version("format_version");
    symbol const file("file");
    symbol const print("print");
  }
}
// reading inodeprint_maps

void
read_inodeprint_map(data const & dat,
                    inodeprint_map & ipm)
{
  // don't bomb out if it's just an old-style inodeprints file
  string start = dat().substr(0, syms::format_version().size());
  if (start != syms::format_version())
    {
      L(FL("inodeprints file format is wrong, skipping it"));
      return;
    }
  
  basic_io::input_source src(dat(), "inodeprint");
  basic_io::tokenizer tok(src);
  basic_io::parser pa(tok);

  {
    pa.esym(syms::format_version);
    string vers;
    pa.str(vers);
    if (vers != "1")
      {
        L(FL("inodeprints file version is unknown, skipping it"));
        return;
      }
  }

  while(pa.symp())
    {
      string path, print;

      pa.esym(syms::file);
      pa.str(path);
      pa.esym(syms::print);
      pa.hex(print);

      ipm.insert(inodeprint_entry(file_path_internal(path),
                                  hexenc<inodeprint>(print)));
    }
  I(src.lookahead == EOF);
}

void
write_inodeprint_map(inodeprint_map const & ipm,
                     data & dat)
{
  basic_io::printer pr;
  {
    basic_io::stanza st;
    st.push_str_pair(syms::format_version, "1");
    pr.print_stanza(st);
  }
  for (inodeprint_map::const_iterator i = ipm.begin();
       i != ipm.end(); ++i)
    {
      basic_io::stanza st;
      st.push_file_pair(syms::file, i->first);
      st.push_hex_pair(syms::print, hexenc<id>(i->second()));
      pr.print_stanza(st);
    }
  dat = data(pr.buf);
}

class my_iprint_calc : public inodeprint_calculator
{
  std::string res;
  Botan::SHA_160 hash;
  bool too_close;
  void add_item(void *dat, size_t size)
  {
    hash.update(reinterpret_cast<Botan::byte const *>(&size),
                sizeof(size));
    hash.update(reinterpret_cast<Botan::byte const *>(dat),
                size);
  }
public:
  my_iprint_calc() : too_close(false)
  {}
  std::string str()
  {
    char digest[constants::sha1_digest_length];
    hash.final(reinterpret_cast<Botan::byte *>(digest));
    return std::string(digest, constants::sha1_digest_length);
  }
  void note_nowish(bool n)
  {
    too_close = n;
  }
  void note_future(bool f)
  {
    inodeprint_calculator::add_item(f);
  }
  bool ok()
  {
    return !too_close;
  }
};

bool inodeprint_file(file_path const & file, hexenc<inodeprint> & ip)
{
  my_iprint_calc calc;
  bool ret = inodeprint_file(file.as_external(), calc);
  inodeprint ip_raw(calc.str());
  if (!ret)
    ip_raw = inodeprint("");
  encode_hexenc(ip_raw, ip);
  return ret && calc.ok();
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
