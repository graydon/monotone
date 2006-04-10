#include <windows.h>
#include <io.h>
#include <fcntl.h>

#include "platform.h"

void make_io_binary()
{
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
}
