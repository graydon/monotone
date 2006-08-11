// Copyright (C) 2005 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <string>
#include <iterator>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <iterator>

#include "app_state.hh"
#include "inodeprint.hh"
#include "sanity.hh"
#include "platform.hh"
#include "constants.hh"
#include "basic_io.hh"

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
    I(vers == "1");
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
      st.push_hex_pair(syms::print, i->second());
      pr.print_stanza(st);
    }
  dat = pr.buf;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
