/* Copyright (C) 2008 Zack Weinberg <zackw@panix.com>
   
   This program is made available under the GNU GPL version 2.0 or
   greater. See the accompanying file COPYING for details.

   This program is distributed WITHOUT ANY WARRANTY; without even the
   implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
   PURPOSE.  */

/* The test suite runs this program to decide whether or not to include
   network tests.  It determines whether it is possible to create a
   listening socket on a randomly chosen port on the loopback interface,
   connect to that socket from another process, and ping-pong a byte.

   Will exit successfully, with no output, if everything works; otherwise,
   will exit unsuccessfully and produce diagnostics on stderr.  */

#include "config.h"
#if defined HAVE_SOCKET && defined HAVE_NETINET_IN_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>

static int synchronizer[2];
static const char *who;
static unsigned short port;

static void sigalrm(int unused)
{
  fprintf(stderr, "%s: timeout\n", who);
  exit(1);
}

static void prep_timeout(const char *w)
{
  who = w;
  signal(SIGALRM, sigalrm);
  alarm(5);
}

/* "b_or_c" should be either "bind" or "connect".  Conveniently, they have
   the same signature.  */
static int get_socket(int (*b_or_c)(int, const struct sockaddr *, socklen_t))
{
  int sfd;

  /* try IPv4 first */
  {
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof sin);
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    sfd = socket(PF_INET, SOCK_STREAM, 0);
    if (sfd >= 0)
      {
        if (b_or_c(sfd, (struct sockaddr *)&sin, sizeof sin) == 0)
          return sfd;
        close(sfd);
      }
  }

  /* if that didn't work, and we have library support for it, try IPv6 too */
#ifdef AF_INET6
  {
    struct sockaddr_in6 sin6;
    memset(&sin6, 0, sizeof sin6);
    sin6.sin6_family = AF_INET6;
    sin6.sin6_port = htons(port);
    sin6.sin6_addr = in6addr_loopback;
    
    sfd = socket(PF_INET6, SOCK_STREAM, 0);
    if (sfd >= 0)
      {
        if (b_or_c(sfd, (struct sockaddr *)&sin6, sizeof sin6) == 0)
          return sfd;
        close(sfd);
      }
  }
#endif

  fprintf(stderr, "socket/connect/bind: %s\n", strerror(errno));
  return -1;
}

static int server(void)
{
  int sfd, cfd, n;
  char buf;

  prep_timeout("server");

  sfd = get_socket(bind);
  if (sfd < 0)
    return 1;
  
  if (listen(sfd, 1))
    {
      fprintf(stderr, "server: listen: %s\n", strerror(errno));
      close(sfd);
      return 1;
    }

  /* Client process may proceed.  */
  n = write(synchronizer[1], "x", 1);
  if (n != 1)
    {
      fprintf(stderr, "server: semaphore write: %s\n",
              n == 0 ? "unexpected EOF" : strerror(errno));
      close(sfd);
      return 1;
    }

  cfd = accept(sfd, 0, 0);  /* don't care _who_ connects */
  if (cfd < 0)
    {
      fprintf(stderr, "server: accept: %s\n", strerror(errno));
      close(sfd);
      return 1;
    }

  n = read(cfd, &buf, 1);
  if (n != 1)
    {
      fprintf(stderr, "server: socket read: %s\n",
              n == 0 ? "unexpected EOF" : strerror(errno));
      close(cfd);
      close(sfd);
      return 1;
    }
  if (buf != 'x')
    {
      fprintf(stderr, "server: socket read: got '%c' exp 'x'\n", buf);
      close(cfd);
      close(sfd);
      return 1;
    }
  n = write(cfd, "x", 1);
  if (n != 1)
    {
      fprintf(stderr, "server: socket write: %s\n",
              n == 0 ? "unexpected EOF" : strerror(errno));
      close(cfd);
      close(sfd);
      return 1;
    }

  close(cfd);
  close(sfd);
  return 0;
}

static int client(void)
{
  int sfd, n;
  char buf;

  prep_timeout("client");

  /* wait for server setup */
  n = read(synchronizer[0], &buf, 1);
  if (n != 1)
    {
      fprintf(stderr, "client: semaphore read: %s\n",
              n == 0 ? "unexpected EOF" : strerror(errno));
      return 1;
    }

  sfd = get_socket(connect);
  if (sfd < 0)
    return 1;

  n = write(sfd, "x", 1);
  if (n != 1)
    {
      fprintf(stderr, "client: socket write: %s\n",
              n == 0 ? "unexpected EOF" : strerror(errno));
      close(sfd);
      return 1;
    }

  n = read(sfd, &buf, 1);
  if (n != 1)
    {
      fprintf(stderr, "client: socket read: %s\n",
              n == 0 ? "unexpected EOF" : strerror(errno));
      close(sfd);
      return 1;
    }
  if (buf != 'x')
    {
      fprintf(stderr, "client: socket read: got '%c' exp 'x'\n", buf);
      close(sfd);
      return 1;
    }

  close(sfd);
  return 0;
}

int main(void)
{
  pid_t child, p;
  int status;

  /* Pick a random port in the high half of the range, thus
     unlikely to be used for anything.  */
  srand(time(0));
  do
    {
      port = rand();
    }
  while (port < 32767);

  if (pipe(synchronizer))
    {
      fprintf(stderr, "setup: pipe: %s\n", strerror(errno));
      return 2;
    }

  child = fork();
  if (child < 0)
    {
      fprintf(stderr, "setup: fork: %s\n", strerror(errno));
      return 2;
    }

  if (child == 0)
    return client();

  if (server())
    return 1;

  p = wait(&status);
  if (p < 0)
    {
      fprintf(stderr, "teardown: wait: %s\n", strerror(errno));
      return 2;
    }
  if (p != child)
    {
      fprintf(stderr, "teardown: unexpected child %d != %d\n", p, child);
      return 2;
    }
  if (!WIFEXITED(status))
    {
      fprintf(stderr, "teardown: child crash, status %d\n", status);
      return 2;
    }

  return WEXITSTATUS(status);
}

#else /* no socket, or no netinet/in.h */

int main(void)
{
  fprintf(stderr, "socket headers are missing, cannot test networking\n");
  return 1;
}

#endif

/*
   Local Variables:
   mode: C
   fill-column: 76
   c-file-style: "gnu"
   indent-tabs-mode: nil
   End:
   vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
 */
