#ifndef __AUTOMATE_HH__
#define __AUTOMATE_HH__

// copyright (C) 2004 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>
#include <iosfwd>
#include <vector>

class app_state;
struct utf8;

void
automate_command(utf8 cmd, std::vector<utf8> args,
                 std::string const & root_cmd_name,
                 app_state & app,
                 std::ostream & output);

#endif  // header guard
