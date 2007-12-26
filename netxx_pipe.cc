// Copyright (C) 2005 Christof Petig <christof@petig-baender.de>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <netxx_pipe.hh>
#include "sanity.hh"
#include "platform.hh"
#include <netxx/streamserver.h>
#include <ostream> // for operator<<

#ifdef WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include <cstring> // strerror
#endif

using std::vector;
using std::string;
using std::make_pair;
using std::exit;
using std::perror;
using std::strerror;

Netxx::PipeStream::PipeStream(int _readfd, int _writefd)
    :
#ifdef WIN32
  child(INVALID_HANDLE_VALUE),
  bytes_available(0),
  read_in_progress(false)
#else
  readfd(_readfd),
  writefd(_writefd),
  child(0)
#endif
{
#ifdef WIN32
  if (_setmode(_readfd, _O_BINARY) == -1)
    L(FL("failed to set input file descriptor to binary"));

  if (_setmode(_writefd, _O_BINARY) == -1)
    L(FL("failed to set output file descriptor to binary"));

  named_pipe = (HANDLE)_get_osfhandle(_readfd);

  E(named_pipe != INVALID_HANDLE_VALUE,
    F("pipe handle is invalid"));

  // Create infrastructure for overlapping I/O
  memset(&overlap, 0, sizeof(overlap));
  overlap.hEvent = CreateEvent(0, TRUE, TRUE, 0);
  bytes_available = 0;
  I(overlap.hEvent != 0);
#else
  int flags = fcntl(readfd, F_GETFL, 0);
  I(fcntl(readfd, F_SETFL, flags | O_NONBLOCK) != -1);
  flags = fcntl(writefd, F_GETFL, 0);
  I(fcntl(writefd, F_SETFL, flags | O_NONBLOCK) != -1);
#endif
}


#ifndef WIN32

// Create pipes for stdio and fork subprocess, returns -1 on error, 0
// to child and PID to parent.

static pid_t
pipe_and_fork(int fd1[2], int fd2[2])
{
  pid_t result = -1;
  fd1[0] = -1;
  fd1[1] = -1;
  fd2[0] = -1;
  fd2[1] = -1;

  if (pipe(fd1))
    return -1;

  if (pipe(fd2))
    {
      close(fd1[0]);
      close(fd1[1]);
      return -1;
    }

  result = fork();

  if (result < 0)
    {
      close(fd1[0]);
      close(fd1[1]);
      close(fd2[0]);
      close(fd2[1]);
      return -1;
    }

  else if (!result)
    {
      // fd1[1] for writing, fd2[0] for reading
      close(fd1[0]);
      close(fd2[1]);
      if (dup2(fd2[0], 0) != 0 ||
          dup2(fd1[1], 1) != 1)
        {
          perror("dup2");
          exit(-1); // kill the useless child
        }
      close(fd1[1]);
      close(fd2[0]);
    }

  else
    {
      // fd1[0] for reading, fd2[1] for writing
      close(fd1[1]);
      close(fd2[0]);
    }

  return result;
}
#endif

#ifdef WIN32
static string
err_msg()
{
  char buf[1024];
  I(FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM,
                  NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                  (LPSTR) &buf, sizeof(buf) / sizeof(TCHAR), NULL) != 0);
  return string(buf);
}
#endif


Netxx::PipeStream::PipeStream (const string & cmd,
                               const vector<string> & args)
  :
#ifdef WIN32
  child(INVALID_HANDLE_VALUE),
  bytes_available(0),
  read_in_progress(false)
#else
  readfd(-1),
  writefd(-1),
  child(0)
#endif
{
  // Unfortunately neither munge_argv_into_cmdline nor execvp do take
  // a vector<string> as argument.

  const unsigned newsize = 64;
  const char *newargv[newsize];
  I(args.size() < (sizeof(newargv) / sizeof(newargv[0])));

  unsigned newargc = 0;
  newargv[newargc++]=cmd.c_str();
  for (vector<string>::const_iterator i = args.begin();
       i != args.end(); ++i)
    newargv[newargc++] = i->c_str();
  newargv[newargc] = 0;

#ifdef WIN32

  // In order to use nonblocking i/o on windows, you must use named
  // pipes and overlapped i/o. There is no other way, alas.

  static unsigned long serial = 0;
  string pipename = (FL("\\\\.\\pipe\\netxx_pipe_%ld_%d")
                          % GetCurrentProcessId()
                          % (++serial)).str();

  // Create the parent's handle to the named pipe.

  named_pipe = CreateNamedPipe(pipename.c_str(),
                               PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                               PIPE_TYPE_BYTE | PIPE_WAIT,
                               1,
                               sizeof(readbuf),
                               sizeof(readbuf),
                               1000,
                               0);

  E(named_pipe != INVALID_HANDLE_VALUE,
    F("CreateNamedPipe(%s,...) call failed: %s")
    % pipename % err_msg());

  // Open the child's handle to the named pipe.

  SECURITY_ATTRIBUTES inherit;
  memset(&inherit,0,sizeof inherit);
  inherit.nLength=sizeof inherit;
  inherit.bInheritHandle = TRUE;

  HANDLE hpipe = CreateFile(pipename.c_str(),
                            GENERIC_READ|GENERIC_WRITE, 0,
                            &inherit,
                            OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OVERLAPPED,0);

  E(hpipe != INVALID_HANDLE_VALUE,
    F("CreateFile(%s,...) call failed: %s")
    % pipename % err_msg());

  // Set up the child with the pipes as stdin/stdout and inheriting stderr.

  PROCESS_INFORMATION piProcInfo;
  STARTUPINFO siStartInfo;

  memset(&piProcInfo, 0, sizeof(piProcInfo));
  memset(&siStartInfo, 0, sizeof(siStartInfo));

  siStartInfo.cb = sizeof(siStartInfo);
  siStartInfo.hStdError = (HANDLE)(_get_osfhandle(2));
  siStartInfo.hStdOutput = hpipe;
  siStartInfo.hStdInput = hpipe;
  siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

  string cmdline = munge_argv_into_cmdline(newargv);
  L(FL("Subprocess command line: '%s'") % cmdline);

  BOOL started = CreateProcess(NULL, // Application name
                               const_cast<CHAR*>(cmdline.c_str()),
                               NULL, // Process attributes
                               NULL, // Thread attributes
                               TRUE, // Inherit handles
                               0,    // Creation flags
                               NULL, // Environment
                               NULL, // Current directory
                               &siStartInfo,
                               &piProcInfo);
  E(started,
    F("CreateProcess(%s,...) call failed: %s")
    % cmdline % err_msg());

  child = piProcInfo.hProcess;

  // create infrastructure for overlapping I/O

  memset(&overlap, 0, sizeof(overlap));
  overlap.hEvent = CreateEvent(0, TRUE, TRUE, 0);
  bytes_available = 0;
  I(overlap.hEvent != 0);

#else // !WIN32

  int fd1[2], fd2[2];
  child = pipe_and_fork(fd1, fd2);
  E(child >= 0, F("pipe/fork failed: %s") % strerror(errno));
  if (!child)
    {
      execvp(newargv[0], const_cast<char * const *>(newargv));
      perror(newargv[0]);
      exit(errno);
    }
  readfd = fd1[0];
  writefd = fd2[1];
  fcntl(readfd, F_SETFL, fcntl(readfd, F_GETFL) | O_NONBLOCK);
#endif

  // P(F("mtn %d: set up i/o channels")
  // % GetCurrentProcessId());
}

// Non blocking read.

Netxx::signed_size_type
Netxx::PipeStream::read (void *buffer, size_type length)
{
#ifdef WIN32

  if (length > bytes_available)
    length = bytes_available;

  if (length)
    {
      memcpy(buffer, readbuf, length);
      if (length < bytes_available)
        memmove(readbuf, readbuf+length, bytes_available-length);
      bytes_available -= length;
    }

  return length;
#else
  return ::read(readfd, buffer, length);
#endif
}

Netxx::signed_size_type
Netxx::PipeStream::write(const void *buffer, size_type length)
{
#ifdef WIN32
  DWORD written = 0;
  BOOL ok = WriteFile(named_pipe, buffer, length, &written, NULL);
  E(ok, F("WriteFile call failed: %s") % err_msg());
#else
  size_t written = ::write(writefd, buffer, length);
#endif
  return written;
}

void
Netxx::PipeStream::close (void)
{

#ifdef WIN32
  if (named_pipe != INVALID_HANDLE_VALUE)
    CloseHandle(named_pipe);
  named_pipe = INVALID_HANDLE_VALUE;

  if (overlap.hEvent != INVALID_HANDLE_VALUE)
    CloseHandle(overlap.hEvent);
  overlap.hEvent = INVALID_HANDLE_VALUE;

  if (child != INVALID_HANDLE_VALUE)
    WaitForSingleObject(child, INFINITE);
  child = INVALID_HANDLE_VALUE;
#else
  if (readfd != -1)
    ::close(readfd);
  readfd = -1;

  if (writefd != -1)
    ::close(writefd);
  writefd = -1;

  if (child)
    while (waitpid(child,0,0) == -1 && errno == EINTR) ;
  child = 0;
#endif
}

Netxx::socket_type
Netxx::PipeStream::get_socketfd (void) const
{
#ifdef WIN32
  return (Netxx::socket_type) named_pipe;
#else
  return Netxx::socket_type(-1);
#endif
}

const Netxx::ProbeInfo*
Netxx::PipeStream::get_probe_info (void) const
{
  return 0;
}

#ifdef WIN32

static string
status_name(DWORD wstatus)
{
  switch (wstatus) {
  case WAIT_TIMEOUT: return "WAIT_TIMEOUT";
  case WAIT_OBJECT_0: return "WAIT_OBJECT_0";
  case WAIT_FAILED: return "WAIT_FAILED";
  case WAIT_OBJECT_0+1: return "WAIT_OBJECT_0+1";
  default: return "UNKNOWN";
  }
}

Netxx::Probe::result_type
Netxx::PipeCompatibleProbe::ready(const Timeout &timeout, ready_type rt)
{
  if (!is_pipe)
    return Probe::ready(timeout, rt);

  // L(F("mtn %d: checking for i/o ready state") % GetCurrentProcessId());

  if (rt == ready_none)
    rt = ready_t; // remembered from add

  if (rt & ready_write)
    {
      return make_pair(pipe->get_socketfd(), ready_write);
    }

  if (rt & ready_read)
    {
      if (pipe->bytes_available == 0)
        {
          // Issue an async request to fill our buffer.
          BOOL ok = ReadFile(pipe->named_pipe, pipe->readbuf,
                             sizeof(pipe->readbuf), NULL, &pipe->overlap);
          E(ok || GetLastError() == ERROR_IO_PENDING,
            F("ReadFile call failed: %s") % err_msg());
          pipe->read_in_progress = true;
        }

      if (pipe->read_in_progress)
        {
          I(pipe->bytes_available == 0);

          // Attempt to wait for the completion of the read-in-progress.

	  int milliseconds = ((timeout.get_sec() * 1000)
                              + (timeout.get_usec() / 1000));

	  L(FL("WaitForSingleObject(,%d)") % milliseconds);

          DWORD wstatus = WAIT_FAILED;

          if (pipe->child != INVALID_HANDLE_VALUE)
            {

              // We're a server; we're going to wait for the client to
              // exit as well as the pipe read status, because
              // apparently you don't find out about closed pipes
              // during an overlapped read request (?)

              HANDLE handles[2];
              handles[0] = pipe->overlap.hEvent;
              handles[1] = pipe->child;

              wstatus = WaitForMultipleObjects(2,
                                               handles,
                                               FALSE,
                                               milliseconds);

              E(wstatus != WAIT_FAILED,
                F("WaitForMultipleObjects call failed: %s") % err_msg());

              if (wstatus == WAIT_OBJECT_0 + 1)
                return make_pair(pipe->get_socketfd(), ready_oobd);
            }
          else
            {
              wstatus = WaitForSingleObject(pipe->overlap.hEvent,
                                            milliseconds);
              E(wstatus != WAIT_FAILED,
                F("WaitForSingleObject call failed: %s") % err_msg());
            }

          if (wstatus == WAIT_TIMEOUT)
            return make_pair(-1, ready_none);

          BOOL ok = GetOverlappedResult(pipe->named_pipe,
                                        &pipe->overlap,
                                        &pipe->bytes_available,
                                        FALSE);

          if (ok)
            {
              // We completed our read.
              pipe->read_in_progress = false;
            }
          else
            {
              // We did not complete our read.
              E(GetLastError() == ERROR_IO_INCOMPLETE,
                F("GetOverlappedResult call failed: %s")
                % err_msg());
            }
        }

      if (pipe->bytes_available != 0)
        {
          return make_pair(pipe->get_socketfd(), ready_read);
        }
    }

  return make_pair(pipe->get_socketfd(), ready_none);
}

void
Netxx::PipeCompatibleProbe::add(PipeStream &ps, ready_type rt)
{
  assert(!is_pipe);
  assert(!pipe);
  is_pipe = true;
  pipe = &ps;
  ready_t = rt;
}

void
Netxx::PipeCompatibleProbe::add(StreamBase const &sb, ready_type rt)
{
  // FIXME: This is *still* an unfortunate way of performing a 
  // downcast, though slightly less awful than the old way, which 
  // involved throwing an exception. 
  //
  // Perhaps we should twiddle the caller-visible API.
  
  StreamBase const *sbp = &sb;
  PipeStream const *psp = dynamic_cast<PipeStream const *>(sbp);
  if (psp)
    add(const_cast<PipeStream&>(*psp),rt);
  else
    {
      assert(!is_pipe);
      Probe::add(sb,rt);
    }
}

void
Netxx::PipeCompatibleProbe::add(const StreamServer &ss, ready_type rt)
{
  assert(!is_pipe);
  Probe::add(ss,rt);
}
#else // unix
void
Netxx::PipeCompatibleProbe::add(PipeStream &ps, ready_type rt)
  {
    if (rt == ready_none || rt & ready_read)
      add_socket(ps.get_readfd(), ready_read);
    if (rt == ready_none || rt & ready_write)
      add_socket(ps.get_writefd(), ready_write);
  }

void
Netxx::PipeCompatibleProbe::add(const StreamBase &sb, ready_type rt)
{
  try
    {
      add(const_cast<PipeStream&>(dynamic_cast<const PipeStream&>(sb)),rt);
    }
  catch (...)
    {
      Probe::add(sb,rt);
    }
}

void
Netxx::PipeCompatibleProbe::add(const StreamServer &ss, ready_type rt)
{
  Probe::add(ss,rt);
}
#endif

#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"

UNIT_TEST(pipe, simple_pipe)
{ try
  {
  Netxx::PipeStream pipe("cat",vector<string>());

  string result;
  Netxx::PipeCompatibleProbe probe;
  Netxx::Timeout timeout(2L), short_time(0,1000);

  // time out because no data is available
  probe.clear();
  probe.add(pipe, Netxx::Probe::ready_read);
  Netxx::Probe::result_type res = probe.ready(short_time);
  I(res.second==Netxx::Probe::ready_none);

  // write should be possible
  probe.clear();
  probe.add(pipe, Netxx::Probe::ready_write);
  res = probe.ready(short_time);
  I(res.second & Netxx::Probe::ready_write);
#ifdef WIN32
  I(res.first==pipe.get_socketfd());
#else
  I(res.first==pipe.get_writefd());
#endif

  // try binary transparency
  for (int c = 0; c < 256; ++c)
    {
      char buf[1024];
      buf[0] = c;
      buf[1] = 255 - c;
      pipe.write(buf, 2);

      string result;
      while (result.size() < 2)
        { // wait for data to arrive
          probe.clear();
          probe.add(pipe, Netxx::Probe::ready_read);
          res = probe.ready(timeout);
          E(res.second & Netxx::Probe::ready_read, F("timeout reading data %d") % c);
#ifdef WIN32
          I(res.first == pipe.get_socketfd());
#else
          I(res.first == pipe.get_readfd());
#endif
          int bytes = pipe.read(buf, sizeof(buf));
          result += string(buf, bytes);
        }
      I(result.size() == 2);
      I(static_cast<unsigned char>(result[0]) == c);
      I(static_cast<unsigned char>(result[1]) == 255 - c);
    }

  pipe.close();

  }
catch (informative_failure &e)
  // for some reason boost does not provide
  // enough information
  {
    W(F("Failure %s") % e.what());
    throw;
  }
}
#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
