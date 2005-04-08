// copyright (C) 2005 nathaniel smith <njs@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "platform.hh"
#include "sanity.hh"

bool fingerprint_file(file_path const & file, id & fpr)
{
  W(F("sorry -- reckless mode is not yet supported on win32\n"));
  return false;
}
