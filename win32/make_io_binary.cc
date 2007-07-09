#define WIN32_LEAN_AND_MEAN
#include "base.hh"
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <stdlib.h>

#include "platform.hh"

void make_io_binary()
{
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
