// Timothy Brownawell  <tbrownaw@gmail.com>
// GPL v2
//
// This is an "usher" to allow multiple monotone servers to work from
// the same port. It asks the client for the pattern it wants to sync,
// and then looks up the matching server in a table. It then forwards
// the connection to that server. All servers using the same usher need
// to have the same server key.
//
// This requires cooperation from the client, which means it only works
// for recent (post-0.22) clients.
//
// Usage: usher <port-number> <server-file>
//
// <port-number> is the local port to listen on
// <server-file> is a file containing lines of
//   stem    ip-address   port-number
//
// A request for a pattern starting with "stem" will be directed to the
// server at <ip-address>:<port-number>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include <string>
#include <list>
#include <iostream>
#include <fstream>

int listenport = 5253;

char const netsync_version = 5;

char const * const greeting = " Hello! This is the monotone usher at localhost. What would you like?";

char const * const errmsg = "!Sorry, I don't know where to find that.";


#undef max
#define max(x,y) ((x) > (y) ? (x) : (y))

struct errstr
{
  std::string name;
  int err;
  errstr(std::string const & s, int e): name(s), err(e) {}
};

int tosserr(int ret, std::string const & name)
{
  if (ret == -1)
    throw errstr(name, errno);
  if (ret < 0)
    throw errstr(name, ret);
  return ret;
}

struct record
{
  std::string stem;
  std::string addr;
  int port;
};

std::list<record> servers;

bool get_server(std::string const & reply, std::string & addr, int & port)
{
  std::list<record>::iterator i;
  for (i = servers.begin(); i != servers.end(); ++i) {
    if (reply.find(i->stem) == 0)
      break;
  }
  if (i == servers.end()) {
    std::cerr<<"no server found for "<<reply<<"\n";
    return false;
  }
  port = i->port;
  addr = i->addr;
//  std::cerr<<"server for "<<reply<<" is "<<addr<<":"<<port<<"\n";
  return true;
}

// packet format is:
// byte version
// byte cmd {100 if we send, 101 if we receive}
// uleb128 {size of everything after this}
// uleb128 {size of everything after this}
// string

// uleb128 is
// byte 0x80 | <low 7 bits>
// byte 0x80 | <next 7 bits>
// ...
// byte 0xff & <remaining bits>
//
// the high bit says that this byte is not the last

void make_packet(std::string const & msg, char * & pkt, int & size)
{
  size = msg.size();
  char const * txt = msg.c_str();
  char header[6];
  header[0] = netsync_version;
  header[1] = 100;
  int headersize;
  if (size >= 128) {
    header[2] = 0x80 | (0x7f & (char)(size+2));
    header[3] = (char)((size+2)>>7);
    header[4] = 0x80 | (0x7f & (char)(size));
    header[5] = (char)(size>>7);
    headersize = 6;
  } else if (size >= 127) {
    header[2] = 0x80 | (0x7f & (char)(size+1));
    header[3] = (char)((size+1)>>7);
    header[4] = (char)(size);
    headersize = 5;
  } else {
    header[2] = (char)(size+1);
    header[3] = (char)(size);
    headersize = 4;
  }
  pkt = new char[headersize + size];
  memcpy(pkt, header, headersize);
  memcpy(pkt + headersize, txt, size);
  size += headersize;
}

struct buffer
{
  static int const buf_size = 2048;
  static int const buf_reset_size = 1024;
  char * ptr;
  int readpos;
  int writepos;
  buffer(): readpos(0), writepos(0)
  {
    ptr = new char[buf_size];
  }
  ~buffer(){delete[] ptr;}
  buffer(buffer const & b)
  {
    ptr = new char[buf_size];
    memcpy(ptr, b.ptr, buf_size);
    readpos = b.readpos;
    writepos = b.writepos;
  }
  bool canread(){return writepos > readpos;}
  bool canwrite(){return writepos < buf_size;}
  void getread(char *& p, int & n)
  {
    p = ptr + readpos;
    n = writepos - readpos;
  }
  void getwrite(char *& p, int & n)
  {
    p = ptr + writepos;
    n = buf_size - writepos;
  }
  void fixread(int n)
  {
    if (n < 0) throw errstr("negative read\n", 0);
    readpos += n;
    if (readpos == writepos) {
      readpos = writepos = 0;
    } else if (readpos > buf_reset_size) {
      memcpy(ptr, ptr+readpos, writepos-readpos);
      writepos -= readpos;
      readpos = 0;
    }
  }
  void fixwrite(int n)
  {
    if (n < 0) throw errstr("negative write\n", 0);
    writepos += n;
  }
};

struct sock
{
  int *s;
  operator int(){return s[0];}
  sock(int ss)
  {
    s = new int[2];
    s[0] = ss;
    s[1] = 1;
  }
  sock(sock const & ss){s = ss.s; s[1]++;}
  ~sock(){if (s[1]--) return; ::close(s[0]); delete[] s;}
  sock operator=(int ss){s[0]=ss;}
  void close()
  {
    if (s[0] == -1) return;
    tosserr(shutdown(s[0], SHUT_RDWR), "shutdown()");
    while (::close(s[0]) < 0) {
      if (errno != EINTR) throw errstr("close()", 0);
    }
    s[0]=-1;
  }
  bool read_to(buffer & buf)
  {
    char *p;
    int n;
    buf.getwrite(p, n);
    n = read(s[0], p, n);
    if (n < 1) {
      close();
      return false;
    } else
      buf.fixwrite(n);
    return true;
  }
  bool write_from(buffer & buf)
  {
    char *p;
    int n;
    buf.getread(p, n);
    n = write(s[0], p, n);
    if (n < 1) {
      close();
      return false;
    } else
      buf.fixread(n);
    return true;
  }
};

sock start(int port)
{
  sock s = tosserr(socket(AF_INET, SOCK_STREAM, 0), "socket()");
  int yes = 1;
  tosserr(setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
      &yes, sizeof(yes)), "setsockopt");
  sockaddr_in a;
  memset (&a, 0, sizeof (a));
  a.sin_port = htons(port);
  a.sin_family = AF_INET;
  tosserr(bind(s, (sockaddr *) &a, sizeof(a)), "bind");
  listen(s, 10);
  return s;
}

sock make_outgoing(int port, std::string const & address)
{
       sock s = tosserr(socket(AF_INET, SOCK_STREAM, 0), "socket()");

       struct sockaddr_in a;
       memset(&a, 0, sizeof(a));
       a.sin_family = AF_INET;
       a.sin_port = htons(port);

       if (!inet_aton(address.c_str(), (in_addr *) &a.sin_addr.s_addr))
              throw errstr("bad ip address format", 0);

       tosserr(connect(s, (sockaddr *) &a, sizeof (a)), "connect()");
       return s;
}

bool extract_reply(buffer & buf, std::string & out)
{
  char *p;
  int n;
  buf.getread(p, n);
  if (n < 4) return false;
  int b = 2;
  unsigned int psize = p[b];
  ++b;
  if (psize >=128) {
    psize = psize - 128 + ((unsigned int)(p[b])<<7);
    ++b;
  }
  if (n < b+psize) return false;
  unsigned int size = p[b];
  ++b;
  if (size >=128) {
    size = size - 128 + ((unsigned int)(p[b])<<7);
    ++b;
  }
  if (n < b+size) return false;
  out.clear();
  out.append(p+b, size);
  buf.fixread(b + size);
}

struct channel
{
  sock client;
  sock server;
  bool have_routed;
  bool no_server;
  buffer cbuf;
  buffer sbuf;
  channel(sock & c): client(c), server(-1),
   have_routed(false), no_server(false)
  {
    char * dat;
    int size;
    make_packet(greeting, dat, size);
    char *p;
    int n;
    sbuf.getwrite(p, n);
    if (n < size) size = n;
    memcpy(p, dat, size);
    sbuf.fixwrite(size);
    delete[] dat;

    client.write_from(sbuf);
  }
  bool is_finished()
  {
    return (client == -1) && (server == -1);
  }
  void add_to_select(int & maxfd, fd_set & rd, fd_set & wr, fd_set & er)
  {
    int c = client;
    int s = server;

    if (c > 0) {
      FD_SET(c, &er);
      if (cbuf.canwrite())
        FD_SET(c, &rd);
      if (sbuf.canread())
        FD_SET(c, &wr);
      maxfd = max(maxfd, c);
    }
    if (s > 0) {
      FD_SET(s, &er);
      if (sbuf.canwrite())
        FD_SET(s, &rd);
      if (cbuf.canread())
        FD_SET(s, &wr);
      maxfd = max(maxfd, s);
    }
  }
  bool process_selected(fd_set & rd, fd_set & wr, fd_set & er)
  {
    int c = client;
    int s = server;
/* NB: read oob data before normal reads */
    if (c > 0 && FD_ISSET(c, &er)) {
      char d;
      errno = 0;
      if (recv(c, &d, 1, MSG_OOB) < 1) 
        client.close(), c = -1;
      else
        send(s, &d, 1, MSG_OOB);
    }
    if (s > 0 && FD_ISSET(s, &er)) {
      char d;
      errno = 0;
      if (recv(s, &d, 1, MSG_OOB) < 1)
        server.close(), s = -1;
      else
        send(c, &d, 1, MSG_OOB);
    }

    char *p=0;
    int n;

    if (c > 0 && FD_ISSET(c, &rd)) {
      if (!client.read_to(cbuf)) c = -1;
      if (!have_routed) {
        std::string reply;
        if (extract_reply(cbuf, reply)) {
          int port;
          std::string addr;
          if (get_server(reply, addr, port)) {
            try {
              server = make_outgoing(port, addr);
              have_routed = true;
              s = server;
            } catch (errstr & e) {
              std::cerr<<"cannot contact server "<<addr<<": "<<e.name<<"\n";
              no_server = true;
            }
          } else {
            char * dat;
            int size;
            sbuf.getwrite(p, n);
            make_packet(errmsg, dat, size);
            if (n < size) size = n;
            memcpy(p, dat, size);
            sbuf.fixwrite(size);
            delete[] dat;
            no_server = true;
          }
        }
      }
    }
    if (s > 0 && FD_ISSET(s, &rd)) {
      if (!server.read_to(sbuf)) s = -1;
    }

    if (c > 0 && FD_ISSET(c, &wr)) {
      if (!client.write_from(sbuf)) c = -1;
    }
    if (s > 0 && FD_ISSET(s, &wr)) {
      if (!server.write_from(cbuf)) s = -1;
    }

    // close sockets we have nothing more to send to
    if (c < 0 && !cbuf.canread()) {
      server.close(), s = -1;
    }
    if ((no_server || have_routed && s < 0) && !sbuf.canread()) {
      client.close(), c = -1;
    }
  }
};

int main (int argc, char **argv)
{
  if (argc != 3) {
    fprintf (stderr, "Usage\n\tusher <listen-port> <config-file>\n");
    exit (1);
  }

  record rec;
  std::ifstream cf(argv[2]);
  int pos = 0;
  while(cf) {
    if (pos == 0)
      cf>>rec.stem;
    else if (pos == 1)
      cf>>rec.addr;
    else if (pos == 2)
      cf>>rec.port;
    else if (pos == 3) {
      pos = 0;
      servers.push_back(rec);
    }
    ++pos;
  }

  signal (SIGPIPE, SIG_IGN);

  sock h(-1);
  try {
    h = start(atoi(argv[1]));
  } catch (errstr & s) {
    std::cerr<<s.name<<"\n";
    exit (1);
  }

  std::list<channel> channels;

  for (;;) {
    fd_set rd, wr, er;
    FD_ZERO (&rd);
    FD_ZERO (&wr);
    FD_ZERO (&er);
    FD_SET (h, &rd);
    int nfds = h;
    channel *newchan = 0;

    for (std::list<channel>::iterator i = channels.begin();
         i != channels.end(); ++i)
      i->add_to_select(nfds, rd, wr, er);

    int r = select(nfds+1, &rd, &wr, &er, NULL);

    if (r == -1 && errno == EINTR)
      continue;
    if (r < 0) {
      perror ("select()");
      exit (1);
    }
    if (FD_ISSET(h, &rd)) {
      try {
        struct sockaddr_in client_address;
        unsigned int l = sizeof(client_address);
        memset(&client_address, 0, l);
        sock cli = tosserr(accept(h, (struct sockaddr *)
                                     &client_address, &l), "accept()");
//        std::cerr<<"connect from "<<inet_ntoa(client_address.sin_addr)<<"\n";
        newchan = new channel(cli);
      } catch(errstr & s) {
        std::cerr<<"During new connection: "<<s.name<<"\n";
      }
    }
    std::list<std::list<channel>::iterator> finished;
    for (std::list<channel>::iterator i = channels.begin();
         i != channels.end(); ++i) {
      i->process_selected(rd, wr, er);
      if (i->is_finished())
        finished.push_back(i);
    }
    for (std::list<std::list<channel>::iterator>::iterator i = finished.begin();
         i != finished.end(); ++i)
      channels.erase(*i);
    if (newchan) {
      channels.push_back(*newchan);
      delete newchan;
      newchan = 0;
    }
  }
  return 0;
}
