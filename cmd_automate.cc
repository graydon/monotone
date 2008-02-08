// Copyright (C) 2002, 2008 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <iostream>
#include <sstream>
#include <map>

#include "cmd.hh"
#include "app_state.hh"
#include "ui.hh"
#include "lua.hh"
#include "lua_hooks.hh"

using std::istream;
using std::make_pair;
using std::map;
using std::ostream;
using std::ostringstream;
using std::pair;
using std::set;
using std::string;
using std::vector;

CMD_GROUP(automate, "automate", "au", CMD_REF(automation),
          N_("Interface for scripted execution"),
          "");

namespace commands {
  automate::automate(string const & name,
                     string const & params,
                     string const & abstract,
                     string const & desc,
                     options::options_type const & opts) :
    command(name, "", CMD_REF(automate), false, false, params, abstract,
            desc, true, opts, false)
  {
  }

  void
  automate::exec(app_state & app,
                 command_id const & execid,
                 args_vector const & args,
                 std::ostream & output) const
  {
    make_io_binary();
    exec_from_automate(app, execid, args, output);
  }

  void
  automate::exec(app_state & app,
                 command_id const & execid,
                 args_vector const & args) const
  {
    exec(app, execid, args, std::cout);
  }
}

static string const interface_version = "7.0";
// Major or minor number only increments once for each monotone release;
// check the most recent release before incrementing this.

// Name: interface_version
// Arguments: none
// Added in: 0.0
// Purpose: Prints version of automation interface.  Major number increments
//   whenever a backwards incompatible change is made; minor number increments
//   whenever any change is made (but is reset when major number
//   increments).
// Output format: "<decimal number>.<decimal number>\n".  Always matches
//   "[0-9]+\.[0-9]+\n".
// Error conditions: None.
CMD_AUTOMATE(interface_version, "",
             N_("Prints the automation interface's version"),
             "",
             options::opts::none)
{
  N(args.size() == 0,
    F("no arguments needed"));

  output << interface_version << '\n';
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

// automate_streambuf and automate_ostream are so we can dump output at a
// set length, rather than waiting until we have all of the output.


class automate_reader
{
  istream & in;
  enum location {opt, cmd, none, eof};
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
    E(c == ':',
        F("Bad input to automate stdio: expected ':' after string size"));
    char *str = new char[size];
    size_t got = 0;
    while(got < size)
      {
        int n = read(str+got, size-got);
        got += n;
      }
    out = std::string(str, size);
    delete[] str;
    L(FL("Got string '%s'") % out);
    return true;
  }
  std::streamsize read(char *buf, size_t nbytes, bool eof_ok = false)
  {
    std::streamsize rv;

    rv = in.rdbuf()->sgetn(buf, nbytes);

    E(eof_ok || rv > 0, F("Bad input to automate stdio: unexpected EOF"));
    return rv;
  }
  void go_to_next_item()
  {
    if (loc == eof)
      return;
    string starters("ol");
    string whitespace(" \r\n\t");
    string foo;
    while (loc != none)
      get_string(foo);
    char c('e');
    do
      {
        if (read(&c, 1, true) == 0)
          {
            loc = eof;
            return;
          }
      }
    while (whitespace.find(c) != std::string::npos);
    switch (c)
      {
      case 'o': loc = opt; break;
      case 'l': loc = cmd; break;
      default:
        E(false,
            F("Bad input to automate stdio: unknown start token '%c'") % c);
      }
  }
public:
  automate_reader(istream & is) : in(is), loc(none)
  {}
  bool get_command(vector<pair<string, string> > & params,
                   vector<string> & cmdline)
  {
    params.clear();
    cmdline.clear();
    if (loc == none)
      go_to_next_item();
    if (loc == eof)
      return false;
    else if (loc == opt)
      {
        string key, val;
        while(get_string(key) && get_string(val))
          params.push_back(make_pair(key, val));
        go_to_next_item();
      }
    E(loc == cmd, F("Bad input to automate stdio: expected '%c' token") % cmd);
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
  size_t _bufsize;
  std::ostream *out;
  automate_reader *in;
  int cmdnum;
  int err;
public:
  automate_streambuf(size_t bufsize)
    : std::streambuf(), _bufsize(bufsize), out(0), in(0), cmdnum(0), err(0)
  {
    char *inbuf = new char[_bufsize];
    setp(inbuf, inbuf + _bufsize);
  }
  automate_streambuf(std::ostream & o, size_t bufsize)
    : std::streambuf(), _bufsize(bufsize), out(&o), in(0), cmdnum(0), err(0)
  {
    char *inbuf = new char[_bufsize];
    setp(inbuf, inbuf + _bufsize);
  }
  automate_streambuf(automate_reader & i, size_t bufsize)
    : std::streambuf(), _bufsize(bufsize), out(0), in(&i), cmdnum(0), err(0)
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
        (*out) << cmdnum << ':'
            << err << ':'
            << (end?'l':'m') << ':'
            << num << ':' << std::string(pbase(), num);
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

  automate_ostream(std::ostream &out, size_t blocksize)
    : std::ostream(NULL),
      _M_autobuf(out, blocksize)
  { this->init(&_M_autobuf); }

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

CMD_AUTOMATE(stdio, "",
             N_("Automates several commands in one run"),
             "",
             options::opts::automate_stdio_size)
{
  N(args.size() == 0,
    F("no arguments needed"));

  CMD_REQUIRES_DATABASE(app);
  // FIXME: additionally requires some app.opts...

    // initialize the database early so any calling process is notified
    // immediately if a version discrepancy exists
  db.ensure_open();

  automate_ostream os(output, app.opts.automate_stdio_size);
  automate_reader ar(std::cin);
  vector<pair<string, string> > params;
  vector<string> cmdline;
  while(ar.get_command(params, cmdline))//while(!EOF)
    {
      args_vector args;
      vector<string>::iterator i = cmdline.begin();
      E(i != cmdline.end(),
        F("Bad input to automate stdio: command name is missing"));
      for (; i != cmdline.end(); ++i)
        {
          args.push_back(arg_type(*i));
        }
      try
        {
          options::options_type opts;
          opts = options::opts::all_options() - options::opts::globals();
          opts.instantiate(&app.opts).reset();

          command_id id;
          for (args_vector::const_iterator iter = args.begin();
               iter != args.end(); iter++)
            id.push_back(utf8((*iter)()));

          if (!id.empty())
            {
              I(!args.empty());

              set< command_id > matches =
                CMD_REF(automate)->complete_command(id);

              if (matches.size() == 0)
                {
                  N(false, F("no completions for this command"));
                }
              else if (matches.size() > 1)
                {
                  N(false, F("multiple completions possible for this command"));
                }

              id = *matches.begin();

              I(args.size() >= id.size());
              for (command_id::size_type i = 0; i < id.size(); i++)
                args.erase(args.begin());

              command const * cmd = CMD_REF(automate)->find_command(id);
              I(cmd != NULL);
              automate const * acmd = reinterpret_cast< automate const * >(cmd);

              opts = options::opts::globals() | acmd->opts();
              opts.instantiate(&app.opts).from_key_value_pairs(params);
              acmd->exec_from_automate(app, id, args, os);
            }
          else
            opts.instantiate(&app.opts).from_key_value_pairs(params);
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

LUAEXT(mtn_automate, )
{
  args_vector args;
  std::stringstream output;
  bool result = true;
  std::stringstream & os = output;

  try
    {
      app_state* app_p = get_app_state(L);
      I(app_p != NULL);
      I(app_p->lua.check_lua_state(L));
      E(app_p->mtn_automate_allowed,
          F("It is illegal to call the mtn_automate() lua extension,\n"
            "unless from a command function defined by register_command()."));

      // don't allow recursive calls
      app_p->mtn_automate_allowed = false;

      // automate_ostream os(output, app_p->opts.automate_stdio_size);

      int n = lua_gettop(L);

      E(n > 0, F("Bad input to mtn_automate() lua extension: command name is missing"));

      app_p->db.ensure_open();

      L(FL("Starting call to mtn_automate lua hook"));

      for (int i=1; i<=n; i++)
        {
          arg_type next_arg(luaL_checkstring(L, i));
          L(FL("arg: %s")%next_arg());
          args.push_back(next_arg);
        }

      commands::command_id id;
      for (args_vector::const_iterator iter = args.begin();
           iter != args.end(); iter++)
        id.push_back(utf8((*iter)()));

      E(!id.empty(), F("no command found"));

      set< commands::command_id > matches =
        CMD_REF(automate)->complete_command(id);

      if (matches.size() == 0)
        {
          N(false, F("no completions for this command"));
        }
      else if (matches.size() > 1)
        {
          N(false, F("multiple completions possible for this command"));
        }

      id = *matches.begin();

      I(args.size() >= id.size());
      for (commands::command_id::size_type i = 0; i < id.size(); i++)
        args.erase(args.begin());

      commands::command const * cmd = CMD_REF(automate)->find_command(id);
      I(cmd != NULL);
      commands::automate const * acmd = reinterpret_cast< commands::automate const * >(cmd);

      acmd->exec(*app_p, id, args, os);

      // allow further calls
      app_p->mtn_automate_allowed = true;
    }
  catch(informative_failure & f)
    {
      // informative failures are passed back to the caller
      result = false;
      L(FL("Informative failure caught inside lua call to mtn_automate: %s") % f.what());
      os.flush();
      output.flush();
      output.str().clear();
      os << f.what();
    }
  catch (std::logic_error & e)
    {
      // invariant failures are permanent
      result = false;
      ui.fatal(e.what());
      lua_pushstring(L, e.what());
      lua_error(L);
    }

  os.flush();

  lua_pushboolean(L, result);
  lua_pushstring(L, output.str().c_str());
  return 2;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
