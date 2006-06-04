#ifndef __AUTOMATE_HH__
#define __AUTOMATE_HH__

// Copyright (C) 2004 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <string>
#include <iosfwd>
#include <vector>

class app_state;
class utf8;

void
automate_command(utf8 cmd, std::vector<utf8> args,
                 std::string const & root_cmd_name,
                 app_state & app,
                 std::ostream & output);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __AUTOMATE_HH__
