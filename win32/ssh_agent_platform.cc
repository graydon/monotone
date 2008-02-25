// Copyright (C) 2007 Justin Patrin <papercrane@reversefold.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"

#include "../sanity.hh"

#include "ssh_agent_platform.hh"

using std::string;

#define AGENT_COPYDATA_ID 0x804e50ba   /* random goop */
#define AGENT_MAX_MSGLEN  8192

void
ssh_agent_platform::ssh_agent_platform()
  : hwnd(NULL), filemap(NULL), filemap_view(NULL), read_len(0)
{
  char mapname[32];
  L(FL("ssh_agent: connect"));
  hwnd = FindWindow("Pageant", "Pageant");

  if (!hwnd)
    return;

  sprintf(mapname, "PageantRequest%08x", (unsigned)GetCurrentThreadId());
  filemap = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                              0, AGENT_MAX_MSGLEN, mapname);
  if (filemap == NULL || filemap == INVALID_HANDLE_VALUE)
    {
      hwnd = NULL;
      return;
    }

  filemap_view = (char*)MapViewOfFile(filemap, FILE_MAP_WRITE, 0, 0, 0);

  if (filemap_view == 0)
    {
      CloseHandle(filemap);
      filemap = NULL;
      hwnd = NULL;
    }
}

ssh_agent_platform::~ssh_agent_platform()
{
  if (filemap == NULL)
    return;

  UnmapViewOfFile(filemap_view);
  filemap_view = NULL;
  CloseHandle(filemap);
  filemap = NULL;
  hwnd = NULL;
}


bool
ssh_agent_platform::connected()
{
  return hwnd && IsWindow(hwnd);
}

void
ssh_agent_platform::write_data(string const & data)
{
  unsigned char *p;
  int id;
  COPYDATASTRUCT cds;
  char mapname[32];

  I(connected());
  sprintf(mapname, "PageantRequest%08x", (unsigned)GetCurrentThreadId());

  L(FL("ssh_agent_platform::write_data: writing %u bytes to %s")
    % data.length() % mapname);

  E(data.length() < AGENT_MAX_MSGLEN,
    F("Asked to write more than %u to pageant.") %  AGENT_MAX_MSGLEN);

  memcpy(filemap_view, data.c_str(), data.length());
  cds.dwData = AGENT_COPYDATA_ID;
  cds.cbData = 1 + strlen(mapname);
  cds.lpData = mapname;

  id = SendMessage(hwnd, WM_COPYDATA, (WPARAM) NULL, (LPARAM) &cds);

  E(id > 0, F("Error sending message to pageant (%d).") % id);

  //Start our read counter again
  read_len = 0;
}

void
ssh_agent_platform::read_data(u32 const len, string & out)
{
  I(connected());

  L(FL("ssh_agent: read_data: asked to read %u bytes") % len);

  E((read_len + len) < AGENT_MAX_MSGLEN,
    F("Asked to read more than %u from pageant.") % AGENT_MAX_MSGLEN);

  out.append(filemap_view + read_len, len);

  //keep track of how much we've read
  read_len += len;
}
