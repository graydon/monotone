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
// Usage: usher [-a <bind address>] [-p <port-number>] <server-file>
//
// <bind address> is the address to listen on
// <port-number> is the local port to listen on
// <server-file> is a file containing lines of
//   hostname    remote   ip-address   port-number
//   hostname    local    <server arguments>
//
// Example server-file:
// localhost          local   -d /usr/local/src/project.db *
// venge.net          remote  66.96.28.3 5253
// company.com:5200   remote  192.168.4.5 5200
//
// A request to server "hostname" will be directed to the
// server at <ip-address>:<port-number>, if that stem is marked as remote,
// and to a local server managed by the usher, started with the given
// arguments ("monotone serve --bind=something <server arguments>"),
// if it is marked as local.
// Note that "hostname" has to be an initial substring of who the client asked
// to connect to, but does not have to match exactly. This means that you don't
// have to know in advance whether clients will be asking for
// <host> or <host>:<port> .

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>

#include <string>
#include <list>
#include <iostream>
#include <fstream>
#include <vector>
#include <set>

#include <boost/lexical_cast.hpp>

using std::vector;
using std::max;
using std::string;
using std::list;
using std::set;
using boost::lexical_cast;
using std::cerr;

// defaults, overridden by command line
int listenport = 5253;
string listenaddr = "0.0.0.0";

// keep local servers around for this many seconds after the last
// client disconnects from them (only accurate to ~10 seconds)
int const server_idle_timeout = 60;

// ranges that dynamic (local) servers can be put on
int const minport = 15000;
int const maxport = 65000;
int const minaddr[] = {127, 0, 1, 1};
int const maxaddr[] = {127, 254, 254, 254};
int currport = 0;
int curraddr[] = {0, 0, 0, 0};

char const netsync_version = 5;

string const greeting = " Hello! This is the monotone usher at localhost. What would you like?";

string const errmsg = "!Sorry, I don't know where to find that.";


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
    if (n < 0)
      throw errstr("negative write\n", 0);
    writepos += n;
  }
};

struct sock
{
  int *s;
  static set<int*> all_socks;
  operator int()
  {
    if (!s)
      return -1;
    else
      return s[0];
  }
  sock(int ss)
  {
    s = new int[2];
    s[0] = ss;
    s[1] = 1;
    all_socks.insert(s);
  }
  sock(sock const & ss)
  {
    s = ss.s;
    if (s)
      s[1]++;
  }
  ~sock()
  {
    if (!s || s[1]--)
      return;
    try {
      close();
    } catch(errstr & e) {
      // if you want it to throw errors, call close manually
    }
    delete[] s;
    all_socks.erase(all_socks.find(s));
    s = 0;
  }
  sock operator=(int ss)
  {
    if (!s) {
      s = new int[2];
      all_socks.insert(s);
    }
    s[0]=ss;
  }
  void close()
  {
    if (!s || s[0] == -1)
      return;
    shutdown(s[0], SHUT_RDWR);
    while (::close(s[0]) < 0) {
      if (errno == EIO)
        throw errstr("close failed", errno);
      if (errno != EINTR)
        break;
      }
    s[0]=-1;
  }
  static void close_all_socks()
  {
    for (set<int*>::iterator i = all_socks.begin(); i != all_socks.end(); ++i) {
//      shutdown((*i)[0], SHUT_RDWR);
      while (::close((*i)[0]) < 0)
        if (errno != EINTR)
          break;
    }
  }
  bool read_to(buffer & buf)
  {
    if (!s)
      return false;
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
    if (!s)
      return false;
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
set<int*> sock::all_socks;

bool check_address_empty(string const & addr, int port)
{
  sock s = tosserr(socket(AF_INET, SOCK_STREAM, 0), "socket()");
  int yes = 1;
  tosserr(setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
      &yes, sizeof(yes)), "setsockopt");
  sockaddr_in a;
  memset (&a, 0, sizeof (a));
  if (!inet_aton(addr.c_str(), (in_addr *) &a.sin_addr.s_addr))
    throw errstr("bad ip address format", 0);
  a.sin_port = htons(port);
  a.sin_family = AF_INET;
  int r = bind(s, (sockaddr *) &a, sizeof(a));
  s.close();
  return r == 0;
}

void find_addr(string & addr, int & port)
{
  if (currport == 0) {
    currport = minport-1;
    for(int i = 0; i < 4; ++i)
      curraddr[i] = minaddr[i];
    curraddr[0];
  }
  do {
    // get the next address in our list
    if (++currport > maxport) {
      currport = minport;
      for (int i = 0; i < 4; ++i) {
        if (++curraddr[i] <= maxaddr[i])
          break;
        curraddr[i] = minaddr[i];
      }
    }
    port = currport;
    addr = lexical_cast<string>(curraddr[0]) + "." +
           lexical_cast<string>(curraddr[1]) + "." +
           lexical_cast<string>(curraddr[2]) + "." +
           lexical_cast<string>(curraddr[3]);
  } while (!check_address_empty(addr, port));
}

sock start(string addr, int port)
{
  sock s = tosserr(socket(AF_INET, SOCK_STREAM, 0), "socket()");
  int yes = 1;
  tosserr(setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
      &yes, sizeof(yes)), "setsockopt");
  sockaddr_in a;
  memset (&a, 0, sizeof (a));
  if (!inet_aton(addr.c_str(), (in_addr *) &a.sin_addr.s_addr))
    throw errstr("bad ip address format", 0);
  a.sin_port = htons(port);
  a.sin_family = AF_INET;
  tosserr(bind(s, (sockaddr *) &a, sizeof(a)), "bind");
  cerr<<"bound to "<<addr<<":"<<port<<"\n";
  listen(s, 10);
  return s;
}

sock make_outgoing(int port, string const & address)
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

int fork_server(vector<string> const & args)
{
  int err[2];
  if (pipe(err) < 0)
    return false;
  int pid = fork();
  if (pid == -1) {
    close(err[0]);
    close(err[1]);
    cerr<<"Failed to fork server.\n";
    return false;
  } else if (pid == 0) {
    close(err[0]);
    close(0);
    close(1);
    close(2);
    sock::close_all_socks();
    if (dup2(err[1], 2) < 0) {
      exit(1);
    }

    char ** a = new char*[args.size()+1];
    for (int i = 0; i < args.size(); ++i) {
      a[i] = new char[args[i].size()+1];
      memcpy(a[i], args[i].c_str(), args[i].size()+1);
    }
    a[args.size()] = 0;

    execvp(a[0], a);
    exit(1);
  } else {
    close(err[1]);
    char head[256];
    int got = 0;
    int r = 0;
    bool line = false;
    // the first line output on the server's stderr will be either
    // "monotone: beginning service on <interface> : <port>" or
    // "monotone: network error: bind(2) error: Address already in use"
    while(r >= 0 && !line) {
      r = read(err[0], head + got, 256 - got);
      if (r > 0) {
        for (int i = 0; i < r && !line; ++i)
          if (head[got+i] == '\n')
            line = true;
        got += r;
      }
    }
    head[got] = 0;
    if (string(head).find("beginning service") != string::npos)
      return pid;
    kill(pid, SIGKILL);
    do {r = waitpid(pid, 0, 0);} while (r==-1 && errno == EINTR);
    return -1;
  }
}

struct server
{
  bool local;
  int pid;
  string arguments;
  string addr;
  int port;
  int connection_count;
  int last_conn_time;
  server() : pid(-1), local(false), port(0),
   connection_count(0), last_conn_time(0)
  {
  }
  ~server()
  {
    yeskill();
  }
  sock connect()
  {
    if (local && pid == -1) {
      // server needs to be started
      // we'll try 3 times, since there's a delay between our checking that
      // a port's available and the server taking it
      for (int i = 0; i < 3 && pid == -1; ++i) {
        if (i > 0 || port == 0)
          find_addr(addr, port);
        vector<string> args;
        args.push_back("monotone");
        args.push_back("serve");
        args.push_back("--bind=" + addr + ":" + lexical_cast<string>(port));
        int n = 0, m = 0;
        n = arguments.find_first_not_of(" \t");
        while (n != string::npos && m != string::npos) {
          m = arguments.find_first_of(" ", n);
          args.push_back(arguments.substr(n, m-n));
          n = arguments.find_first_not_of(" ", m);
        }
        pid = fork_server(args);
      }
    }
    sock s = make_outgoing(port, addr);
    ++connection_count;
    return s;
  }
  void disconnect()
  {
    if (--connection_count || !local)
      return;
    last_conn_time = time(0);
  }
  void maybekill()
  {
    int difftime = time(0) - last_conn_time;
    if (difftime > server_idle_timeout && !connection_count)
      yeskill();
    else if (waitpid(pid, 0, WNOHANG) == 0) {
      pid = -1;
      port = 0;
    }
  }
  void yeskill()
  {
    if (local && pid != -1) {
      kill(pid, SIGTERM);
      int r;
      do {r = waitpid(pid, 0, 0);} while (r==-1 && errno == EINTR);
      pid = -1;
      port = 0;
    }
  }
  string name()
  {
    if (local && port == 0)
      return "dynamic local server";
    else
      return addr + ":" + lexical_cast<string>(port);
  }
};

struct record
{
  std::string stem;
  server srv;
};

list<record> servers;

server * get_server(std::string const & stem)
{
  std::list<record>::iterator i;
  for (i = servers.begin(); i != servers.end(); ++i) {
    if (stem.find(i->stem) == 0)
      break;
  }
  if (i == servers.end()) {
    std::cerr<<"no server found for "<<stem<<"\n";
    return 0;
  }
  cerr<<"found server "<<i->stem<<" for "<<stem<<"\n";
  return &i->srv;
}

void kill_old_servers()
{
  std::list<record>::iterator i;
  for (i = servers.begin(); i != servers.end(); ++i) {
    i->srv.maybekill();
  }
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
  static int counter;
  int num;
  sock cli;
  sock srv;
  bool have_routed;
  bool no_server;
  buffer cbuf;
  buffer sbuf;
  server * who;
  channel(sock & c): num(++counter),
   cli(c), srv(-1),
   have_routed(false), no_server(false), who(0)
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

    cli.write_from(sbuf);
  }
  ~channel()
  {
    if (who)
      who->disconnect();
  }
  bool is_finished()
  {
    return (cli == -1) && (srv == -1);
  }
  void add_to_select(int & maxfd, fd_set & rd, fd_set & wr, fd_set & er)
  {
    int c = cli;
    int s = srv;

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
    int c = cli;
    int s = srv;
/* NB: read oob data before normal reads */
    if (c > 0 && FD_ISSET(c, &er)) {
      char d;
      errno = 0;
      if (recv(c, &d, 1, MSG_OOB) < 1) 
        cli.close(), c = -1;
      else
        send(s, &d, 1, MSG_OOB);
    }
    if (s > 0 && FD_ISSET(s, &er)) {
      char d;
      errno = 0;
      if (recv(s, &d, 1, MSG_OOB) < 1)
        srv.close(), s = -1;
      else
        send(c, &d, 1, MSG_OOB);
    }

    char *p=0;
    int n;

    if (c > 0 && FD_ISSET(c, &rd)) {
      if (!cli.read_to(cbuf)) c = -1;
      if (!have_routed) {
        std::string reply;
        if (extract_reply(cbuf, reply)) {
          who = get_server(reply);
          if (who) {
            try {
              srv = who->connect();
              have_routed = true;
              s = srv;
            } catch (errstr & e) {
              std::cerr<<"cannot contact server "<<who->name()<<"\n";
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
      if (!srv.read_to(sbuf)) s = -1;
    }

    if (c > 0 && FD_ISSET(c, &wr)) {
      if (!cli.write_from(sbuf)) c = -1;
    }
    if (s > 0 && FD_ISSET(s, &wr)) {
      if (!srv.write_from(cbuf)) s = -1;
    }

    // close sockets we have nothing more to send to
    if (c < 0 && !cbuf.canread()) {
      srv.close(), s = -1;
    }
    if ((no_server || have_routed && s < 0) && !sbuf.canread()) {
      cli.close(), c = -1;
    }
  }
};
int channel::counter = 0;

int main (int argc, char **argv)
{
  {
    char const * conffile = 0;
    int i;
    for (i = 1; i < argc; ++i) {
      if (string(argv[i]) == "-a")
        listenaddr = argv[++i];
      else if (string(argv[i]) == "-p")
        listenport = lexical_cast<int>(argv[++i]);
      else
        conffile = argv[i];
    }
    if (conffile == 0 || i != argc) {
      cerr<<"Usage:\n";
      cerr<<"\tusher [-a <listen-addr>] [-p <listen-port>] <config-file>\n";
      exit (1);
    }
    std::ifstream cf(conffile);
    int pos = 0;
    while(cf) {
      string stem, type;
      cf>>stem;
      cf>>type;
      if (!cf)
        break;
      record rec;
      rec.stem = stem;
      if (type == "local") {
        rec.srv.local = true;
        rec.srv.arguments.clear();
        while(cf.peek() != '\n')
          rec.srv.arguments += cf.get();
      } else if (type == "remote") {
        rec.srv.local = false;
        cf>>rec.srv.addr;
        cf>>rec.srv.port;
      } else {
        cerr<<"Error parsing "<<conffile<<"\n";
        exit(1);
      }
      cerr<<"Adding server for "<<rec.stem<<": "<<rec.srv.name()<<"\n";
      servers.push_back(rec);
    }
  }


  signal (SIGPIPE, SIG_IGN);

  sock h(-1);
  try {
    h = start(listenaddr, listenport);
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

    timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    int r = select(nfds+1, &rd, &wr, &er, &timeout);

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
        cerr<<"During new connection: "<<s.name<<"\n";
      }
    }
    std::list<std::list<channel>::iterator> finished;
    for (std::list<channel>::iterator i = channels.begin();
         i != channels.end(); ++i) {
      try {
        i->process_selected(rd, wr, er);
        if (i->is_finished())
          finished.push_back(i);
      } catch (errstr & e) {
        finished.push_back(i);
        cerr<<"Error proccessing connection "<<i->num<<": "<<e.name<<"\n";
      }
    }
    for (std::list<std::list<channel>::iterator>::iterator i = finished.begin();
         i != finished.end(); ++i)
      channels.erase(*i);
    if (newchan) {
      channels.push_back(*newchan);
      delete newchan;
      newchan = 0;
    }
    kill_old_servers();
  }
  return 0;
}
