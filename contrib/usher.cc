// Timothy Brownawell  <tbrownaw@gmail.com>
// GPL v2
//
// This is an "usher" to allow multiple monotone servers to work from
// the same port. It asks the client what it wants to sync,
// and then looks up the matching server in a table. It then forwards
// the connection to that server. All servers using the same usher need
// to have the same server key.
//
// This requires cooperation from the client, which means it only works
// for recent (0.23 or later) clients. In order to match against hostnames
// a post-0.23 client is needed (0.23 clients can only be matched against
// their include pattern).
//
// Usage: usher [-l address[:port]] [-a address:port] [-p pidfile] <server-file>
//
// options:
// -m   the monotone command, defaults to "monotone"
// -l   address and port to listen on, defaults to 0.0.0.0:4691
// -a   address and port to listen for admin commands
// -p   a file (deleted on program exit) to record the pid of the usher in
// <server-file>   a file that looks like
//   userpass username password
//
//   server monotone
//   host localhost
//   pattern net.venge.monotone
//   remote 66.96.28.3:4691
//   
//   server local
//   host 127.0.0.1
//   pattern *
//   local -d /usr/local/src/managed/mt.db~ *
//
// or in general, one block of one or more lines of
//   userpass <username> <password>
// followed by any number of blocks of a
//   server <name>
// line followed by one or more
//   host <hostname>
// lines and/or one or more
//   pattern <pattern>
// lines, and one of
//    remote <address:port>
//    local <arguments>
// , with blocks separated by blank lines
//
// "userpass" lines specify who is allowed to use the administrative port.
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
//
//
// Admin commands
//
// If the -a option is given, the usher will listen for administrative
// connections on that port. The connecting client gives commands of the form
//   COMMAND [arguments] <newline>
// , and after any command except USERPASS the usher will send a reply and
// close the connection. The reply will always end with a newline.
//
// USERPASS username password
// Required before any other command, so random people can't do bad things.
// If incorrect, the connection will be closed immediately.
//
// STATUS [servername]
// Get the status of a server, as named by the "server" lines in the
// config file. If a server is specified, the result will be one of:
//   REMOTE - this is a remote server without active connections
//   ACTIVE n - this server currently has n active connections
//   WAITING - this (local) server is running, but has no connections
//   SLEEPING - this (local) server is not running, but is available
//   STOPPING n - this (local) server has been asked to stop, but still has
//                n active connections. It will not accept further connections.
//   STOPPED - this (local) server has been stopped, and will not accept
//             connections. The server process is not running.
//   SHUTTINGDOWN - the usher has been shut down, no servers are accepting
//                  connections.
//   SHUTDOWN - the usher has been shut down, all connections have been closed,
//              and all local server processes have been stopped.
// If no server is specified, the repsonse will be SHUTTINGDOWN, SHUTDOWN,
// WAITING, or ACTIVE (with n being the total number of open connections,
// across all servers).
//
// STOP servername
// Prevent the given local server from receiving further connections, and stop
// it once all connections are closed. The result will be the new status of
// that server: ACTIVE local servers become STOPPING, and WAITING and SLEEPING
// servers become STOPPED. Servers in other states are not affected.
//
// START servername
// Allow a stopped or stopping server to receive connections again. The result
// will be the new status of that server. (A server in the "STOPPING" state
// becomes ACTIVE, and a STOPPED server becomes SLEEPING. A server in some
// other state is not affected.)
//
// LIST [state]
// Returns a space-separated list of all servers. If a state is given, only
// list the servers that are in that state.
//
// SHUTDOWN
// Do not accept new connections for any servers, local or remote. Returns "ok".
//
// STARTUP
// Begin accepting connections again after a SHUTDOWN. Returns "ok".
//
// CONNECTIONS
// Returns the number of connections currently open.
//
// RELOAD
// Reload the config file (same as sending SIGHUP). The reply will be "ok",
// and will not be given until the config file has been reloaded.
//

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
#include <sstream>
#include <fstream>
#include <vector>
#include <set>
#include <map>

#include <boost/lexical_cast.hpp>
#include <boost/shared_ptr.hpp>

using std::vector;
using std::max;
using std::string;
using std::list;
using std::set;
using std::map;
using boost::lexical_cast;
using boost::shared_ptr;
using std::cerr;
using std::pair;
using std::make_pair;

// defaults, overridden by command line
int listenport = 4691;
string listenaddr = "0.0.0.0";
string monotone = "monotone";

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

char const netsync_version = 6;

string const greeting = " Hello! This is the monotone usher at localhost. What would you like?";

string const notfound = "!Sorry, I don't know where to find that.";

string const disabled = "!Sorry, this usher is not currently accepting connections.";

string const srvdisabled = "!Sorry, that server is currently disabled.";

struct errstr
{
  std::string name;
  int err;
  errstr(std::string const & s): name(s), err(0) {}
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
// uleb128 {size of string}
// string
// {
// uleb128 {size of string}
// string
// } // only present if we're receiving

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
  void deref()
  {
    if (s && !(--s[1])) {
      try {
        close();
      } catch(errstr & e) {
        // if you want it to throw errors, call close manually
      }
      delete[] s;
      all_socks.erase(all_socks.find(s));
    }
    s = 0;
  }
  ~sock()
  {
    deref();
  }
  sock const & operator=(int ss)
  {
    deref();
    s = new int[2];
    all_socks.insert(s);
    s[0]=ss;
    return *this;
  }
  sock const & operator=(sock const & ss)
  {
    deref();
    s = ss.s;
    if (s)
      ++s[1];
    return *this;
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
    for (unsigned int i = 0; i < args.size(); ++i) {
      a[i] = new char[args[i].size()+1];
      memcpy(a[i], args[i].c_str(), args[i].size()+1);
    }
    a[args.size()] = 0;

    execvp(a[0], a);
    perror("execvp failed\n");
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
    do {
      r = read(err[0], head + got, 256 - got);
      if (r)
        cerr<<"Read '"<<string(head+got, r)<<"'\n";
      if (r > 0) {
        for (int i = 0; i < r && !line; ++i)
          if (head[got+i] == '\n')
            line = true;
        got += r;
      }
    } while(r > 0 && !line && got < 256);
    head[got] = 0;
    if (string(head).find("beginning service") != string::npos)
      return pid;
    kill(pid, SIGKILL);
    do {r = waitpid(pid, 0, 0);} while (r==-1 && errno == EINTR);
    return -1;
  }
}

bool connections_allowed = true;
int total_connections = 0;

struct serverstate
{
  enum ss {remote, active, waiting, sleeping, stopping,
           stopped, shuttingdown, shutdown, unknown};
  ss state;
  int num;
  serverstate(): state(unknown), num(0) {}
  serverstate const & operator=(string const & s)
  {
    if (s == "REMOTE")
      state = remote;
    else if (s == "ACTIVE")
      state = active;
    else if (s == "WAITING")
      state = waiting;
    else if (s == "SLEEPING")
      state = sleeping;
    else if (s == "STOPPING")
      state = stopping;
    else if (s == "STOPPED")
      state = stopped;
    else if (s == "SHUTTINGDOWN")
      state = shuttingdown;
    else if (s == "SHUTDOWN")
      state = shutdown;
    return *this;
  }
  bool operator==(string const & s)
  {
    serverstate foo;
    foo = s;
    return foo.state == state;
  }
};
std::ostream & operator<<(std::ostream & os, serverstate const & ss)
{
  switch (ss.state) {
  case serverstate::remote:
    os<<"REMOTE";
    break;
  case serverstate::active:
    os<<"ACTIVE "<<ss.num;
    break;
  case serverstate::waiting:
    os<<"WAITING";
    break;
  case serverstate::sleeping:
    os<<"SLEEPING";
    break;
  case serverstate::stopping:
    os<<"STOPPING "<<ss.num;
    break;
  case serverstate::stopped:
    os<<"STOPPED";
    break;
  case serverstate::shuttingdown:
    os<<"SHUTTINGDOWN "<<ss.num;
    break;
  case serverstate::shutdown:
    os<<"SHUTDOWN";
  case serverstate::unknown:
    break;
  }
  return os;
}

struct server
{
  bool enabled;
  list<map<string, shared_ptr<server> >::iterator> by_host, by_pat;
  map<string, shared_ptr<server> >::iterator by_name;
  static map<string, shared_ptr<server> > servers_by_host;
  static map<string, shared_ptr<server> > servers_by_pattern;
  static map<string, shared_ptr<server> > servers_by_name;
  static set<shared_ptr<server> > live_servers;
  bool local;
  int pid;
  string arguments;
  string addr;
  int port;
  int connection_count;
  int last_conn_time;
  server() : enabled(true), local(false), pid(-1), port(0),
   connection_count(0), last_conn_time(0)
  {
  }
  ~server()
  {
    yeskill();
  }
  serverstate get_state()
  {
    serverstate ss;
    ss.num = connection_count;
    if (!connections_allowed) {
      if (!total_connections)
        ss.state = serverstate::shutdown;
      else
        ss.state = serverstate::shuttingdown;
    } else if (connection_count) {
      if (enabled)
        ss.state = serverstate::active;
      else
        ss.state = serverstate::stopping;
    } else if (!local)
      ss.state = serverstate::remote;
    else if (!enabled)
      ss.state = serverstate::stopped;
    else if (pid == -1)
      ss.state = serverstate::sleeping;
    else
      ss.state = serverstate::waiting;
    return ss;
  }
  void delist()
  {
    vector<string> foo;
    set_hosts(foo);
    set_patterns(foo);
    servers_by_name.erase(by_name);
    by_name = 0;
  }
  void rename(string const & n)
  {
    shared_ptr<server> me = by_name->second;
    servers_by_name.erase(by_name);
    by_name = servers_by_name.insert(make_pair(n, me)).first;
  }
  void set_hosts(vector<string> const & h)
  {
    shared_ptr<server> me = by_name->second;
    map<string, shared_ptr<server> >::iterator c;
    for (list<map<string, shared_ptr<server> >::iterator>::iterator
           i = by_host.begin(); i != by_host.end(); ++i)
      servers_by_host.erase(*i);
    by_host.clear();
    for (vector<string>::const_iterator i = h.begin(); i != h.end(); ++i) {
      c = servers_by_host.find(*i);
      if (c != servers_by_host.end()) {
        list<map<string, shared_ptr<server> >::iterator>::iterator j;
        for (j = c->second->by_host.begin(); j != c->second->by_host.end(); ++j)
          if ((*j)->first == *i) {
            servers_by_host.erase(*j);
            c->second->by_host.erase(j);
          }
      }
      c = servers_by_host.insert(make_pair(*i, me)).first;
      by_host.push_back(c);
    }
  }
  void set_patterns(vector<string> const & p)
  {
    shared_ptr<server> me = by_name->second;
    map<string, shared_ptr<server> >::iterator c;
    for (list<map<string, shared_ptr<server> >::iterator>::iterator
           i = by_pat.begin(); i != by_pat.end(); ++i)
      servers_by_pattern.erase(*i);
    by_pat.clear();
    for (vector<string>::const_iterator i = p.begin(); i != p.end(); ++i) {
      c = servers_by_pattern.find(*i);
      if (c != servers_by_pattern.end()) {
        list<map<string, shared_ptr<server> >::iterator>::iterator j;
        for (j = c->second->by_pat.begin(); j != c->second->by_pat.end(); ++j)
          if ((*j)->first == *i) {
            servers_by_pattern.erase(*j);
            c->second->by_pat.erase(j);
          }
      }
      c = servers_by_pattern.insert(make_pair(*i, me)).first;
      by_pat.push_back(c);
    }
  }
  sock connect()
  {
    if (!connections_allowed)
      throw errstr("all servers disabled");
    if (!enabled)
      throw errstr("server disabled");
    if (local && pid == -1) {
      // server needs to be started
      // we'll try 3 times, since there's a delay between our checking that
      // a port's available and the server taking it
      for (int i = 0; i < 3 && pid == -1; ++i) {
        if (i > 0 || port == 0)
          find_addr(addr, port);
        vector<string> args;
        args.push_back(monotone);
        args.push_back("serve");
        args.push_back("--bind=" + addr + ":" + lexical_cast<string>(port));
        unsigned int n = 0, m = 0;
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
    if (local && !connection_count) {
      live_servers.insert(by_name->second);
      }
    ++connection_count;
    ++total_connections;
    return s;
  }
  void disconnect()
  {
    --total_connections;
    if (--connection_count || !local)
      return;
    last_conn_time = time(0);
    maybekill();
  }
  void maybekill()
  {
    if (!local)
      return;
    if (pid == -1)
      return;
    int difftime = time(0) - last_conn_time;
    if (!connection_count
        && (difftime > server_idle_timeout || !connections_allowed))
        yeskill();
    else if (waitpid(pid, 0, WNOHANG) > 0) {
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
      live_servers.erase(live_servers.find(by_name->second));
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

map<string, shared_ptr<server> > server::servers_by_host;
map<string, shared_ptr<server> > server::servers_by_pattern;
map<string, shared_ptr<server> > server::servers_by_name;
set<shared_ptr<server> > server::live_servers;

string getline(std::istream & in)
{
  string out;
  char buf[256];
  do {
    in.getline(buf, 256);
    int got = in.gcount()-1;
    if (got > 0)
      out.append(buf, got);
  } while (in.fail() && !in.eof());
  return out;
}

string read_server_record(std::istream & in)
{
  // server foobar
  // hostname foobar.com
  // hostname mtn.foobar.com
  // pattern com.foobar
  // remote 127.5.6.7:80
  //
  // server myproj
  // hostname localhost
  // local -d foo.db *
  vector<string> hosts, patterns;
  string name, desc;
  bool local(false);
  string line = getline(in);
  while (!line.empty()) {
    //    server     foobar
    //    ^     ^    ^
    //    a     b    c
    int a = line.find_first_not_of(" \t");
    int b = line.find_first_of(" \t", a);
    int c = line.find_first_not_of(" \t", b);
    string cmd = line.substr(a, b-a);
    string arg = line.substr(c);
    if (cmd == "server")
      name = arg;
    else if (cmd == "local") {
      local = true;
      desc = arg;
    } else if (cmd == "remote") {
      local = false;
      desc = arg;
    } else if (cmd == "host")
      hosts.push_back(arg);
    else if (cmd == "pattern")
      patterns.push_back(arg);
    line = getline(in);
  }
  if (name.empty())
    return string();
  shared_ptr<server> srv;
  map<string, shared_ptr<server> >::iterator
    i = server::servers_by_name.find(name);
  if (i != server::servers_by_name.end()) {
    if (local && i->second->local && i->second->arguments == desc)
      srv = i->second;
    else
      srv = shared_ptr<server>(new server);
    i->second->delist();
  } else
    srv = shared_ptr<server>(new server);
  srv->by_name = server::servers_by_name.insert(make_pair(name, srv)).first;
  srv->set_hosts(hosts);
  srv->set_patterns(patterns);
  if (local) {
    srv->local = true;
    srv->arguments = desc;
  } else {
    srv->local = false;
    unsigned int c = desc.find(":");
    if (c != desc.npos) {
      srv->addr = desc.substr(0, c);
      srv->port = lexical_cast<int>(desc.substr(c+1));
    } else {
      srv->addr = desc;
      srv->port = 4691;
    }
  }
  return name;
}

shared_ptr<server> get_server(string const & srv, string const & pat)
{
  map<string, shared_ptr<server> >::iterator i;
  for (i = server::servers_by_host.begin();
       i != server::servers_by_host.end(); ++i) {
    if (srv.find(i->first) == 0) {
      return i->second;
    }
  }
  for (i = server::servers_by_pattern.begin();
       i != server::servers_by_pattern.end(); ++i) {
    if (pat.find(i->first) == 0) {
      return i->second;
    }
  }
  std::cerr<<"no server found for '"<<pat<<"' at '"<<srv<<"'\n";
  return shared_ptr<server>();
}

shared_ptr<server> get_server(string const & name)
{
  map<string, shared_ptr<server> >::iterator i;
  for (i = server::servers_by_name.begin();
       i != server::servers_by_name.end(); ++i) {
    if (name == i->first) {
      return i->second;
    }
  }
  return shared_ptr<server>();
}

void kill_old_servers()
{
  set<shared_ptr<server> >::iterator i;
  for (i = server::live_servers.begin(); i != server::live_servers.end(); ++i) {
    (*i)->maybekill();
  }
}

int extract_uleb128(char *p, int maxsize, int & out)
{
  out = 0;
  int b = 0;
  unsigned char got;
  do {
    if (b == maxsize)
      return -1;
    got = p[b];
    out += ((int)(p[b] & 0x7f))<<(b*7);
    ++b;
  } while ((unsigned int)(b*7) < sizeof(int)*8-1 && (got & 0x80));
  return b;
}

int extract_vstr(char *p, int maxsize, string & out)
{
  int size;
  out.clear();
  int chars = extract_uleb128(p, maxsize, size);
  if (chars == -1 || chars + size > maxsize) {
    return -1;
  }
  out.append(p+chars, size);
  return chars+size;
}

bool extract_reply(buffer & buf, string & host, string & pat)
{
  char *p;
  int n, s;
  buf.getread(p, n);
  if (n < 4) return false;
  p += 2; // first 2 bytes are header
  n -= 2;
  // extract size, and make sure we have the entire packet
  int pos = extract_uleb128(p, n, s);
  if (pos == -1 || n < s+pos) {
    return false;
  }
  // extract host vstr
  int num = extract_vstr(p+pos, n-pos, host);
  if (num == -1) {
    return false;
  }
  pos += num;
  // extract pattern vstr
  num = extract_vstr(p+pos, n-pos, pat);
  if (num == -1) {
    cerr<<"old-style reply.\n";
    pat = host;
    host.clear();
    return true;
  }
  pos += num;
  buf.fixread(pos+2);
  return true;
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
  shared_ptr<server> who;
  channel(sock & c): num(++counter),
   cli(c), srv(-1),
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

    cli.write_from(sbuf);
  }
  ~channel()
  {
    if (who && !no_server)
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
        string reply_srv, reply_pat;
        if (extract_reply(cbuf, reply_srv, reply_pat)) {
          who = get_server(reply_srv, reply_pat);
          if (who && who->enabled) {
            try {
              srv = who->connect();
              have_routed = true;
              s = srv;
            } catch (errstr & e) {
              cerr<<"cannot contact server "<<who->name()<<"\n";
              no_server = true;
            }
          } else {
            char * dat;
            int size;
            sbuf.getwrite(p, n);
            if (who)
              make_packet(srvdisabled, dat, size);
            else
              make_packet(notfound, dat, size);
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
    return true;
  }
};
int channel::counter = 0;

bool reload_pending;
map<string, string> admins;
string conffile;

void reload_conffile(string const & file)
{
  reload_pending = false;
  cerr<<"Reloading config file...\n";
  std::ifstream cf(file.c_str());

  string line = getline(cf);
  while (!line.empty()) {
    std::istringstream iss(line);
    string a, b, c;
    iss>>a>>b>>c;
    if (a == "userpass")
      admins.insert(make_pair(b, c));
    line = getline(cf);
  }

  set<string> names;
  while(cf) {
    string n = read_server_record(cf);
    if(!n.empty())
      names.insert(n);
  }
  for (map<string, shared_ptr<server> >::iterator
         i = server::servers_by_name.begin();
       i != server::servers_by_name.end(); ++i) {
    if (names.find(i->first) == names.end())
      i->second->delist();
  }
  cerr<<"Servers:\n";
  for (map<string, shared_ptr<server> >::iterator
         i = server::servers_by_name.begin();
       i != server::servers_by_name.end(); ++i) {
    cerr<<"\t"<<i->first<<"\n";
    list<map<string, shared_ptr<server> >::iterator>::iterator j;
    for (j = i->second->by_host.begin(); j != i->second->by_host.end(); ++j)
      cerr<<"\t\tHost: '"<<(*j)->first<<"'\n";
    for (j = i->second->by_pat.begin(); j != i->second->by_pat.end(); ++j)
      cerr<<"\t\tPattern: '"<<(*j)->first<<"'\n";
  }
  cerr<<"Reload complete.\n";
}
void sched_reload(int sig)
{
  reload_pending = true;
}

struct administrator
{
  sock port;
  struct cstate
  {
    bool auth;
    bool rdone;
    string buf;
    cstate(): auth(false), rdone(false) {}
  };
  list<pair<cstate, sock> > conns;
  administrator(): port(-1)
  {}
  bool process(cstate & cs)
  {
    unsigned int n = cs.buf.find("\n");
    if (n == cs.buf.npos)
      return true;
    string l = cs.buf.substr(0, n);
    cs.buf.erase(0, n+1);
    std::istringstream iss(l);
    string cmd;
    iss>>cmd;
    if (cmd == "USERPASS") {
      string user, pass;
      iss>>user>>pass;
      map<string, string>::iterator i = admins.find(user);
      if (i == admins.end() || i->second != pass) {
        cerr<<"Failed admin login.\n";
        return false;
      } else {
        if (cs.auth == true)
          return false;
        cs.auth = true;
        return process(cs);
      }
    } else if (cmd == "STATUS") {
      string srv;
      iss>>srv;
      std::ostringstream oss;
      if (srv.empty()) {
        serverstate ss;
        ss.num = total_connections;
       if (connections_allowed) {
         if (total_connections)
           ss.state = serverstate::active;
         else
           ss.state = serverstate::waiting;
       } else {
         if (total_connections)
           ss.state = serverstate::shuttingdown;
         else
           ss.state = serverstate::shutdown;
       }
       oss<<ss<<"\n";
      } else {
        map<string, shared_ptr<server> >::iterator i;
        i = server::servers_by_name.find(srv);
        if (i != server::servers_by_name.end())
          oss<<i->second->get_state()<<"\n";
        else
          oss<<"No such server.\n";
      }
      cs.buf = oss.str();
    } else if (cmd == "START") {
      string srv;
      iss>>srv;
      std::ostringstream oss;
      map<string, shared_ptr<server> >::iterator i;
      i = server::servers_by_name.find(srv);
      if (i != server::servers_by_name.end()) {
        i->second->enabled = true;
        oss<<i->second->get_state()<<"\n";
      } else
        oss<<"No such server.\n";
      cs.buf = oss.str();
    } else if (cmd == "STOP") {
      string srv;
      iss>>srv;
      std::ostringstream oss;
      map<string, shared_ptr<server> >::iterator i;
      i = server::servers_by_name.find(srv);
      if (i != server::servers_by_name.end()) {
        i->second->enabled = false;
        i->second->maybekill();
        oss<<i->second->get_state()<<"\n";
      } else
        oss<<"No such server.\n";
      cs.buf = oss.str();
    } else if (cmd == "LIST") {
      string state;
      iss>>state;
      map<string, shared_ptr<server> >::iterator i;
      for (i = server::servers_by_name.begin();
           i != server::servers_by_name.end(); ++i) {
        if (state.empty() || i->second->get_state() == state)
          cs.buf += (cs.buf.empty()?"":" ") + i->first;
      }
      cs.buf += "\n";
    } else if (cmd == "SHUTDOWN") {
      connections_allowed = false;
      kill_old_servers();
      cs.buf = "ok\n";
    } else if (cmd == "CONNECTIONS") {
      cs.buf = lexical_cast<string>(total_connections) + "\n";
    } else if (cmd == "RELOAD") {
      reload_conffile(conffile);
      cs.buf = "ok\n";
    } else if (cmd == "STARTUP") {
      connections_allowed = true;
      cs.buf = "ok\n";
    } else {
      return true;
    }
    cs.rdone = true;
    return true;
  }
  void initialize(string const & ap)
  {
    try {
      int c = ap.find(":");
      string a = ap.substr(0, c);
      int p = lexical_cast<int>(ap.substr(c+1));
      port = start(a, p);
    } catch (errstr & s) {
      cerr<<"Could not initialize admin port: "<<s.name<<"\n";
      return;
    }
  }
  void add_to_select(int & maxfd, fd_set & rd, fd_set & wr, fd_set & er)
  {
    if (int(port) == -1)
      return;
    FD_SET (port, &rd);
    maxfd = max(maxfd, int(port));
    for (list<pair<cstate, sock> >::iterator i = conns.begin();
         i != conns.end(); ++i) {
      int c = i->second;
      if (!i->first.rdone)
        FD_SET(c, &rd);
      else
        FD_SET(c, &wr);
      maxfd = max(maxfd, int(c));
    }
  }
  void process_selected(fd_set & rd, fd_set & wr, fd_set & er)
  {
    if (int(port) == -1)
      return;
    if (FD_ISSET(port, &rd)) {
      try {
        struct sockaddr_in addr;
        unsigned int l = sizeof(addr);
        memset(&addr, 0, l);
        sock nc = tosserr(accept(port, (struct sockaddr *)
                                     &addr, &l), "accept()");
        conns.push_back(make_pair(cstate(), nc));
      } catch(errstr & s) {
        cerr<<"During new admin connection: "<<s.name<<"\n";
      }
    }
    list<list<pair<cstate, sock> >::iterator> del;
    for (list<pair<cstate, sock> >::iterator i = conns.begin();
         i != conns.end(); ++i) {
      int c = i->second;
      if (c <= 0) {
//        cerr<<"Bad socket.\n";
        del.push_back(i);
      } else if (FD_ISSET(c, &rd)) {
        char buf[120];
        int n;
        n = read(c, buf, 120);
        if (n < 1) {
 //         cerr<<"Read failed.\n";
          del.push_back(i);
        }
        i->first.buf.append(buf, n);
        if (!process(i->first)) {
//          cerr<<"Closing connection...\n";
//          i->second.close();
          del.push_back(i);
        }
      }
      else if (FD_ISSET(c, &wr)) {
        int n = write(c, i->first.buf.c_str(), i->first.buf.size());
        if (n < 1) {
//          cerr<<"Write failed.\n";
          del.push_back(i);
        } else {
          i->first.buf.erase(0, n);
          if (i->first.buf.empty() && i->first.rdone) {
//            cerr<<"Done.\n";
            del.push_back(i);
          }
        }
      }
    }
    for (list<list<pair<cstate, sock> >::iterator>::iterator i = del.begin();
         i != del.end(); ++i) {
      conns.erase(*i);
    }
  }
};

struct pidfile
{
  string filename;
  void initialize(string const & file)
  {
    filename = file;
    std::ofstream ofs(filename.c_str());
    ofs<<getpid();
  }
  ~pidfile()
  {
    if (!filename.empty())
      unlink(filename.c_str());
  }
};

bool done;
void sig_end(int sig)
{
  done = true;
}

int main (int argc, char **argv)
{
  pidfile pf;
  administrator admin;
  {
    int i;
    for (i = 1; i < argc; ++i) {
      if (string(argv[i]) == "-l") {
        string lp(argv[++i]);
        unsigned int c = lp.find(":");
        listenaddr = lp.substr(0, c);
        if (c != lp.npos)
          listenport = lexical_cast<int>(lp.substr(c+1));
      } else if (string(argv[i]) == "-m")
        monotone = argv[i++];
      else if (string(argv[i]) == "-a")
        admin.initialize(argv[++i]);
      else if (string(argv[i]) == "-p")
        pf.initialize(argv[++i]);
      else
        conffile = argv[i];
    }
    if (conffile.empty() || i != argc) {
      cerr<<"Usage:\n";
      cerr<<"\tusher [-l addr[:port]] [-a addr:port] <config-file>\n";
      exit (1);
    }
  }
  reload_conffile(conffile);


  struct sigaction sa, sa_old;
  sa.sa_handler = &sched_reload;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  while(sigaction(SIGHUP, &sa, &sa_old) == -1 && errno == EINTR);
  sa.sa_handler = SIG_IGN;
  while(sigaction(SIGPIPE, &sa, &sa_old) == -1 && errno == EINTR);
  sa.sa_handler = sig_end;
  while(sigaction(SIGTERM, &sa, &sa_old) == -1 && errno == EINTR);
  while(sigaction(SIGINT, &sa, &sa_old) == -1 && errno == EINTR);

  sock h(-1);
  try {
    h = start(listenaddr, listenport);
  } catch (errstr & s) {
    std::cerr<<"Error while opening socket: "<<s.name<<"\n";
    exit (1);
  }

  std::list<channel> channels;

  done = false;
  while (!done) {
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

    admin.add_to_select(nfds, rd, wr, er);

    timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    int r = select(nfds+1, &rd, &wr, &er, &timeout);

    if (r < 0 && errno != EINTR) {
      perror ("select()");
      exit (1);
    }
    if (done)
      return 0;
    if (FD_ISSET(h, &rd)) {
      try {
        struct sockaddr_in client_address;
        unsigned int l = sizeof(client_address);
        memset(&client_address, 0, l);
        sock cli = tosserr(accept(h, (struct sockaddr *)
                                     &client_address, &l), "accept()");
        if (connections_allowed)
          newchan = new channel(cli);
        else {
          char * dat;
          int size;
          make_packet(disabled, dat, size);
          write(cli, dat, size);
          delete[] dat;
        }
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
    if (reload_pending)
      reload_conffile(conffile);

    admin.process_selected(rd, wr, er);
  }
  return 0;
}
