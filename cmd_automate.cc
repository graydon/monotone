// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <iosfwd>
#include <iostream>
#include <map>
#include <utility>

#include <boost/function.hpp>
#include <boost/bind.hpp>

#include "cmd.hh"

using std::map;
using std::ostream;
using std::istream;
using std::string;
using std::vector;
using std::pair;
using std::make_pair;

namespace automation {
  // When this is split into multiple files, there will not be any
  // guarantees about initialization order. So, use something we can
  // initialize ourselves.
  static map<string, automate * const> * automations;
  automate::automate(string const &n, string const &p)
   : name(n), params(p)
  {
    static bool first(true);
    if (first)
      {
        first = false;
        automations = new map<string, automate * const>;
      }
    automations->insert(make_pair(name, this));
  }
  automate::~automate() {}
}

void
automate_command(utf8 cmd, vector<utf8> args,
                 string const & root_cmd_name,
                 app_state & app,
                 ostream & output/*,
                 istream & input*/)
{
  map<string, automation::automate * const>::const_iterator
    i = automation::automations->find(cmd());
  if (i == automation::automations->end())
    throw usage(root_cmd_name);
  else
    i->second->run(args, root_cmd_name, app, output/*, input*/);
}

static string const interface_version = "3.1";

// Name: interface_version
// Arguments: none
// Added in: 0.0
// Purpose: Prints version of automation interface.  Major number increments
//   whenever a backwards incompatible change is made; minor number increments
//   whenever any change is made (but is reset when major number increments).
// Output format: "<decimal number>.<decimal number>\n".  Always matches
//   "[0-9]+\.[0-9]+\n".
// Error conditions: None.
AUTOMATE(interface_version, "")
{
  if (args.size() != 0)
    throw usage(help_name);

  output << interface_version << "\n";
}

// Name: stdio
// Arguments: none
// Added in: 1.0
// Purpose: Allow multiple automate commands to be run from one instance
//   of monotone.
//
// Input format: The input is a series of lines of the form
//   'l'<size>':'<string>[<size>':'<string>...]'e', with characters
//   after the 'e' of one command, but before the 'l' of the next ignored.
//   This space is reserved, and should not contain characters other
//   than '\n'.
//   Example:
//     l6:leavese
//     l7:parents40:0e3171212f34839c2e3263e7282cdeea22fc5378e
//
// Output format: <command number>:<err code>:<last?>:<size>:<output>
//   <command number> is a decimal number specifying which command
//   this output is from. It is 0 for the first command, and increases
//   by one each time.
//   <err code> is 0 for success, 1 for a syntax error, and 2 for any
//   other error.
//   <last?> is 'l' if this is the last piece of output for this command,
//   and 'm' if there is more output to come.
//   <size> is the number of bytes in the output.
//   <output> is the output of the command.
//   Example:
//     0:0:l:205:0e3171212f34839c2e3263e7282cdeea22fc5378
//     1f4ef73c3e056883c6a5ff66728dd764557db5e6
//     2133c52680aa2492b18ed902bdef7e083464c0b8
//     23501f8afd1f9ee037019765309b0f8428567f8a
//     2c295fcf5fe20301557b9b3a5b4d437b5ab8ec8c
//     1:0:l:41:7706a422ccad41621c958affa999b1a1dd644e79
//
// Error conditions: Errors encountered by the commands run only set
//   the error code in the output for that command. Malformed input
//   results in exit with a non-zero return value and an error message.

// We use our own stringbuf class so we can put in a callback on write.
// This lets us dump output at a set length, rather than waiting until
// we have all of the output.


class automate_reader
{
  std::istream & in;
  enum location {cmd, none, eof};
  location loc;
  bool get_string(std::string & out)
  {
    out.clear();
    if (loc == none || loc == eof)
      {
        return false;
      }
    size_t size(0);
    char c;
    read(&c, 1);
    if (c == 'e')
      {
        loc = none;
        return false;
      }
    while(c <= '9' && c >= '0')
      {
        size = (size*10)+(c-'0');
        read(&c, 1);
      }
    E(c == ':', F("Bad input to automate stdio"));
    char *str = new char[size];
    size_t got = 0;
    while(got < size)
      {
        int n = read(str, size-got);
        got += n;
      }
    out = std::string(str, size);
    delete str;
    L(FL("Got string '%s'") % out);
    return true;
  }
  static ssize_t read(void *buf, size_t nbytes, bool eof_ok = false)
  {
    ssize_t rv;

    rv = ::read(0, buf, nbytes);

    E(rv >= 0, F("read from client failed with error code: %d") % rv);
    E(eof_ok || rv > 0, F("Bad input to automate stdio (unexpected EOF)"));
    return rv;
  }
  void go_to_next_item()
  {
    if (loc == eof)
      return;
    std::string starters("l");
    std::string foo;
    while (loc != none)
      get_string(foo);
    char c('e');
    while (starters.find(c) == std::string::npos)
      {
        if (read(&c, 1, true) == 0)
          {
            loc = eof;
            return;
          }
      }
    switch (c)
      {
      case 'l': loc = cmd; break;
      }
  }
public:
  automate_reader(std::istream & is) : in(is), loc(none)
  {}
  bool get_command(std::vector<std::string> & cmdline)
  {
    cmdline.clear();
    //done_input = false;
    while (loc == none)
      go_to_next_item();
    if (loc == eof)
      return false;
    E(loc == cmd, F("Bad input to automate stdio"));
    string item;
    while (get_string(item))
      {
        cmdline.push_back(item);
      }
    return true;
  }
};

struct automate_streambuf : public std::streambuf
{
private:
  static size_t const _bufsize = 8192;
  std::ostream *out;
  automate_reader *in;
  int cmdnum;
  int err;
public:
  automate_streambuf()
   : std::streambuf(), out(0), in(0), cmdnum(0), err(0)
  {
    char *inbuf = new char[_bufsize];
    setp(inbuf, inbuf + _bufsize);
  }
  automate_streambuf(std::ostream & o)
   : std::streambuf(), out(&o), in(0), cmdnum(0), err(0)
  {
    char *inbuf = new char[_bufsize];
    setp(inbuf, inbuf + _bufsize);
  }
  automate_streambuf(automate_reader & i)
   : std::streambuf(), out(0), in(&i), cmdnum(0), err(0)
  {
    char *inbuf = new char[_bufsize];
    setp(inbuf, inbuf + _bufsize);
  }
  ~automate_streambuf()
  {}
  
  void set_err(int e)
  {
    sync();
    err = e;
  }
  
  void end_cmd()
  {
    _M_sync(true);
    ++cmdnum;
    err = 0;
  }
  
  virtual int sync()
  {
    _M_sync();
    return 0;
  }
  
  void _M_sync(bool end = false)
  {
    if (!out)
      {
        setp(pbase(), pbase() + _bufsize);
        return;
      }
    int num = pptr() - pbase();
    if (num || end)
      {
        (*out) << cmdnum << ":"
            << err << ":"
            << (end?'l':'m') << ":"
            << num << ":" << std::string(pbase(), num);
        setp(pbase(), pbase() + _bufsize);
        out->flush();
      }
  }
  int_type
  overflow(int_type c = traits_type::eof())
  {
    sync();
    sputc(c);
    return 0;
  }
};

struct automate_ostream : public std::ostream
{
  automate_streambuf _M_autobuf;
  
  automate_ostream(std::ostream &out)
   : std::ostream(&_M_autobuf),
     _M_autobuf(out)
  {}
  
  ~automate_ostream()
  {}
  
  automate_streambuf *
  rdbuf() const
  { return const_cast<automate_streambuf *>(&_M_autobuf); }
  
  void set_err(int e)
  { _M_autobuf.set_err(e); }
  
  void end_cmd()
  { _M_autobuf.end_cmd(); }
};


AUTOMATE(stdio, "")
{
  if (args.size() != 0)
    throw usage(help_name);
  automate_ostream os(output);
  automate_reader ar(std::cin);
  vector<string> cmdline;
  while(ar.get_command(cmdline))//while(!EOF)
    {
      utf8 cmd;
      vector<utf8> args;
      vector<string>::iterator i = cmdline.begin();
      if (i != cmdline.end())
        cmd = utf8(*i);
      for (++i; i != cmdline.end(); ++i)
        {
          args.push_back(utf8(*i));
        }
      try
        {
          automate_command(cmd, args, help_name, app, os);
        }
      catch(usage &)
        {
          os.set_err(1);
          commands::explain_usage(help_name, os);
        }
      catch(informative_failure & f)
        {
          os.set_err(2);
          //Do this instead of printing f.what directly so the output
          //will be split into properly-sized blocks automatically.
          os<<f.what();
        }
      os.end_cmd();
    }
}


CMD_PARAMS_FN(automate, N_("automation"),
              N_("automation interface"),
    option::automate_stdio_size)
{
  if (args.size() == 0)
    throw usage(name);

  vector<utf8>::const_iterator i = args.begin();
  utf8 cmd = *i;
  ++i;
  vector<utf8> cmd_args(i, args.end());

  make_io_binary();

//  automate_command(cmd, cmd_args, name, app, std::cout, std::cin);
  automate_command(cmd, cmd_args, name, app, std::cout);
}

std::string commands::cmd_automate::params()
{
  std::string out;
  map<string, automation::automate * const>::const_iterator i;
  for (i = automation::automations->begin();
       i != automation::automations->end(); ++i)
    {
      out += i->second->name + " " + i->second->params;
      if (out[out.size()-1] != '\n')
        out += "\n";
    }
  return out;
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
