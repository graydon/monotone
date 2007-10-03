#ifndef __NETXX_PIPE_HH__
#define __NETXX_PIPE_HH__

// Copyright (C) 2005 Christof Petig <christof@petig-baender.de>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "vector.hh"
#include <netxx/socket.h>
#include <netxx/streambase.h>
#ifdef WIN32
#  include <windows.h>
#endif

/*
   What is this all for?

   If you want to transparently handle a pipe and a socket on unix and
   windows you have to abstract some difficulties:

 - sockets have a single filedescriptor for reading and writing
   pipes usually come in pairs (one for reading and one for writing)

 - process creation is different on unix and windows

 => so Netxx::PipeStream is a Netxx::StreamBase which abstracts two pipes to
   and from an external command

 - windows can select on a socket but not on a pipe

 => so Netxx::PipeCompatibleProbe is a Netxx::Probe like class which
   _can_ handle pipes on windows (emulating select is difficult at best!)
   (on unix Probe and PipeCompatibleProbe are nearly identical: with pipes
   you should not select for both read and write on the same descriptor)

*/

namespace Netxx
  {
  class PipeCompatibleProbe;
  class StreamServer;

  class PipeStream : public StreamBase
    {
#ifdef WIN32
      HANDLE named_pipe;
      HANDLE child;
      char readbuf[1024];
      DWORD bytes_available;
      bool read_in_progress;
      OVERLAPPED overlap;
      friend class PipeCompatibleProbe;
#else
      int readfd, writefd;
      int child;
#endif


    public:
      // do we need Timeout for symmetry with Stream?
      explicit PipeStream (int readfd, int writefd);
      explicit PipeStream (const std::string &cmd, const std::vector<std::string> &args);
      virtual ~PipeStream() { close(); }
      virtual signed_size_type read (void *buffer, size_type length);
      virtual signed_size_type write (const void *buffer, size_type length);
      virtual void close (void);
      virtual socket_type get_socketfd (void) const;
      virtual const ProbeInfo* get_probe_info (void) const;
      int get_readfd(void) const
        {
#ifdef WIN32
          return -1;
#else
          return readfd;
#endif
        }
      int get_writefd(void) const
        {
#ifdef WIN32
          return -1;
#else
          return writefd;
#endif
        }
    };

#ifdef WIN32

  // This probe can either handle _one_ PipeStream or several network
  // Streams so if !is_pipe this acts like a Probe.
  class PipeCompatibleProbe : public Probe
    {
      bool is_pipe;
      // only meaningful if is_pipe is true
      PipeStream *pipe;
      ready_type ready_t;
    public:
      PipeCompatibleProbe() : is_pipe(), pipe(), ready_t()
      {}
      void clear()
      {
        if (is_pipe)
          {
            pipe=0;
            is_pipe=false;
          }
        else
          Probe::clear();
      }
      // This function does all the hard work (emulating a select).
      result_type ready(const Timeout &timeout=Timeout(), ready_type rt=ready_none);
      void add(PipeStream &ps, ready_type rt=ready_none);
      void add(const StreamBase &sb, ready_type rt=ready_none);
      void add(const StreamServer &ss, ready_type rt=ready_none);
    };
#else

  // We only act specially if a PipeStream is added (directly or via
  // the StreamBase parent reference).
  struct PipeCompatibleProbe : Probe
    {
      void add(PipeStream &ps, ready_type rt=ready_none);
      void add(const StreamBase &sb, ready_type rt=ready_none);
      void add(const StreamServer &ss, ready_type rt=ready_none);
    };
#endif

}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __NETXX_PIPE_HH__

