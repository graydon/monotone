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

#include <boost/function.hpp>
#include <boost/bind.hpp>

#include "cmd.hh"

using std::map;
using std::ostream;
using std::string;
using std::vector;

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
                 ostream & output)
{
  map<string, automation::automate * const>::const_iterator
    i = automation::automations->find(cmd());
  if (i == automation::automations->end())
    throw usage(root_cmd_name);
  else
    i->second->run(args, root_cmd_name, app, output);
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

void
automate_command(utf8 cmd, std::vector<utf8> args,
                 std::string const & root_cmd_name,
                 app_state & app,
                 std::ostream & output);

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

typedef std::basic_stringbuf<char,
                             std::char_traits<char>,
                             std::allocator<char> > char_stringbuf;
struct my_stringbuf : public char_stringbuf
{
private:
  std::streamsize written;
  boost::function1<void, int> on_write;
  std::streamsize last_call;
  std::streamsize call_every;
  bool clear;
public:
  my_stringbuf(std::streamsize _call_every) : char_stringbuf(),
                                              written(0),
                                              last_call(0),
                                              call_every(_call_every)
  {}
  virtual std::streamsize
  xsputn(const char_stringbuf::char_type* __s, std::streamsize __n)
  {
    std::streamsize ret=char_stringbuf::xsputn(__s, __n);
    written+=__n;
    while(written>=last_call+call_every)
      {
        if(on_write)
          on_write(call_every);
        last_call+=call_every;
      }
    return ret;
  }
  virtual int sync()
  {
    int ret=char_stringbuf::sync();
    if(on_write)
      on_write(-1);
    last_call=written;
    return ret;
  }
  void set_on_write(boost::function1<void, int> x)
  {
    on_write = x;
  }
};

void print_some_output(int cmdnum,
                       int err,
                       bool last,
                       string const & text,
                       ostream & s,
                       int & pos,
                       int size,
                       size_t automate_stdio_size)
{
  if(size==-1)
    {
      while(text.size()-pos > automate_stdio_size)
        {
          s<<cmdnum<<':'<<err<<':'<<'m'<<':';
          s<<automate_stdio_size<<':'
           <<text.substr(pos, automate_stdio_size);
          pos+=automate_stdio_size;
          s.flush();
        }
      s<<cmdnum<<':'<<err<<':'<<(last?'l':'m')<<':';
      s<<(text.size()-pos)<<':'<<text.substr(pos);
      pos=text.size();
    }
  else
    {
      I((unsigned int)(size) <= automate_stdio_size);
      s<<cmdnum<<':'<<err<<':'<<(last?'l':'m')<<':';
      s<<size<<':'<<text.substr(pos, size);
      pos+=size;
    }
  s.flush();
}

static ssize_t automate_stdio_read(int d, void *buf, size_t nbytes)
{
  ssize_t rv;

  rv = read(d, buf, nbytes);

  E(rv >= 0, F("read from client failed with error code: %d") % rv);
  return rv;
}

AUTOMATE(stdio, "")
{
  if (args.size() != 0)
    throw usage(help_name);
  int cmdnum = 0;
  char c;
  ssize_t n=1;
  while(n)//while(!EOF)
    {
      string x;
      utf8 cmd;
      args.clear();
      bool first=true;
      int toklen=0;
      bool firstchar=true;
      for(n=automate_stdio_read(0, &c, 1); c != 'l' && n; n=automate_stdio_read(0, &c, 1))
        ;
      for(n=automate_stdio_read(0, &c, 1); c!='e' && n; n=automate_stdio_read(0, &c, 1))
        {
          if(c<='9' && c>='0')
            {
              toklen=(toklen*10)+(c-'0');
            }
          else if(c == ':')
            {
              char *tok=new char[toklen];
              int count=0;
              while(count<toklen)
                count+=automate_stdio_read(0, tok+count, toklen-count);
              if(first)
                cmd=utf8(string(tok, toklen));
              else
                args.push_back(utf8(string(tok, toklen)));
              toklen=0;
              delete[] tok;
              first=false;
            }
          else
            {
              N(false, F("Bad input to automate stdio"));
            }
          firstchar=false;
        }
      if(cmd() != "")
        {
          int outpos=0;
          int err;
          std::ostringstream s;
          my_stringbuf sb(app.automate_stdio_size);
          sb.set_on_write(boost::bind(print_some_output,
                                      cmdnum,
                                      boost::ref(err),
                                      false,
                                      boost::bind(&my_stringbuf::str, &sb),
                                      boost::ref(output),
                                      boost::ref(outpos),
                                      _1,
                                      app.automate_stdio_size));
          {
            // Do not use s.std::basic_ios<...>::rdbuf here, 
            // it confuses VC8.
            using std::basic_ios;
            s.basic_ios<char, std::char_traits<char> >::rdbuf(&sb);
          }
          try
            {
              err=0;
              automate_command(cmd, args, help_name, app, s);
            }
          catch(usage &)
            {
              if(sb.str().size())
                s.flush();
              err=1;
              commands::explain_usage(help_name, s);
            }
          catch(informative_failure & f)
            {
              if(sb.str().size())
                s.flush();
              err=2;
              //Do this instead of printing f.what directly so the output
              //will be split into properly-sized blocks automatically.
              s<<f.what();
            }
            print_some_output(cmdnum, err, true, sb.str(),
                              output, outpos, -1, app.automate_stdio_size);
        }
      cmdnum++;
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
