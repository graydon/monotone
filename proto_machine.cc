// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string> 
#include <iostream> 
#include <vector>
#include <sstream>
#include <algorithm>
#include <iterator>

#include <boost/lexical_cast.hpp>
#include <boost/tokenizer.hpp>

#include "proto_machine.hh"
#include "sanity.hh"

// this file describes the interface to a network stream 
// in terms of NNTP / SMTP protocol state machines.

using namespace std;

// lowest level is a bunch of string-encoding functions

string const linesep("\r\n");

struct dot_escape {
  typedef string result_type;
  string operator()(string const & x) const {
    if (x.size() > 0 && x.at(0) == '.')
      return '.' + x;
    return x;
  }
};

struct dot_unescape {
  typedef string result_type;
  string operator()(string const & x) const {
    if (x.size() > 0 && x.at(0) == '.')
      return x.substr(1);
    return x;
  }
};

template <typename T>
struct builder {
  typedef T result_type;
  T operator()(string const & x) const {
    return T(x);
  }
};

typedef vector<string>::const_iterator lines_iter;
typedef boost::transform_iterator_generator <dot_escape, lines_iter>::type dot_escaper;
typedef boost::transform_iterator_generator <dot_unescape, lines_iter>::type dot_unescaper;

template <typename XFM, typename IN, typename OUT_ITER>
void transform(IN const & in, OUT_ITER out)
{
  copy(XFM(in.begin()), XFM(in.end()), out);
}

template<typename T, typename SEQ, typename OUT_ITER>
void interleave(SEQ const & in, 
		T const & sep,
		OUT_ITER out)
{
  typedef typename SEQ::const_iterator iter;
  if (in.size() < 1)
    return;
  iter i = in.begin();
  assert(i != in.end());
  *out = *i; ++out;
  ++i;
  for(;i != in.end(); ++i) {
    *out = sep; ++out;
    *out = *i; ++out;
  }
}

// next layer is concerned with composing and receiving protocol messages
// of the most basic "command-and-args", "code-and-response", and
// "line-set-with-dot" forms

void write_command(ostream & out,
		   string const & cmd, 
		   vector<string> const & args)
{
  out << cmd;
  if (args.size() > 0)
    {
      out << ' ';
      interleave(args, string(", "), ostream_iterator<string>(out));
    }
  out << linesep;
  out.flush();
}

void write_lines(ostream & out,
		 vector<string> const & lines)
{
  transform<dot_escaper>(lines, ostream_iterator<string>(out, linesep.c_str()));
  out << '.' << linesep;
  out.flush();
}

void read_line(istream & in, string & result)
{
  ostringstream oss;
  in.get(*oss.rdbuf(), '\n');

  if (in.good())
    in.ignore(1);

  result = oss.str();
  if (result.size() > 0 &&
      result.at(result.size() - 1) == '\r')
    result.resize(result.size() - 1);
}

void read_lines(istream & in,
		vector<string> & result)
{
  string tmp("");
  vector<string> tvec;
  while(in)
    {
      read_line(in,tmp);
      if (tmp == ".") 
	break;
      tvec.push_back(tmp);
    }
  if (tmp != ".")
    throw oops("stream closed before '.' terminating body response. last line was '" + tmp + "'");
  result.reserve(tvec.size());
  transform<dot_unescaper>(tvec, back_inserter(result));
}


void read_status_response(istream & in,
			  int & code,
			  string & result)
{
  string tmp;
  read_line(in, tmp);
  L(F("RECV <- %s\n") % tmp);

  stringstream ss(tmp);

  if (ss >> code)
    result = tmp.substr(ss.gcount());
  else
    throw oops(string("non-numeric beginning of command response line: '") + tmp + "'");
}


// next layer are protocol-state objects you can wire together into state machines

proto_edge::proto_edge(proto_state * t, int c, string const & m, 
		       vector<string> const & l) :
  targ(t), code(c), msg(m), lines(l) 
{}


proto_edge proto_state::handle_response(istream & net)
{

  string res;
  read_status_response(net, res_code, res);

  vector<string> res_lines;

  // we might end...
  if (codes.find(res_code) == codes.end())
    {
      return proto_edge(NULL, res_code, res, res_lines);
    }
  
  // or we might want a message...
  if (codes[res_code].first)
    {
      read_lines(net, res_lines);
      L(F("RECV <- %d lines\n") % res_lines.size());
    }
  
  // and, in any event, we're at an edge!
  return proto_edge(codes[res_code].second, res_code, res, res_lines);
}


proto_edge proto_state::step_lines(iostream & net, 
				   vector<string> const & send_lines)
{
  if (send_lines.size() > 0)
    {
      write_lines(net, send_lines);  
      L(F("SEND -> %d lines\n") % send_lines.size());
    }
  return handle_response(net);
}

proto_edge proto_state::step_cmd(iostream & net, 
				 string const & cmd,
				 vector<string> const & args)
{
  write_command(net, string(cmd), args);
  stringstream ss;
  write_command(ss, string(cmd), args);
  L(F("SEND -> %s") % ss.str());
  net.flush();
  return handle_response(net);
}

void proto_state::add_edge(int code, proto_state * targ, bool read_lines)
{
  codes[code] = pair<bool,proto_state *>(read_lines, targ);
}

proto_state::~proto_state() {}
  

cmd_state::cmd_state(string const & c) : 
  cmd(c) 
{}

cmd_state::cmd_state(string const & c, 
		     string const & arg1) : 
  cmd(c)
{
  args.push_back(arg1);
}

cmd_state::cmd_state(string const & c, 
		     string const & arg1, 
		     string const & arg2)
{
  args.push_back(arg1);
  args.push_back(arg2);
}

proto_edge cmd_state::drive(iostream & net, 
			    proto_edge const & e)
{
  return step_cmd(net, cmd, args);
}

cmd_state::~cmd_state() {}

void run_proto_state_machine(proto_state * machine,
			     iostream & link)
{

  if (!machine)
    throw oops("null protocol state machine given");

  string res;
  int res_code;
  vector<string> no_lines;

  // NNTP / SMTP sessions start with a greet from their end
  read_status_response(link, res_code, res);

  proto_edge edge(machine, res_code, res, no_lines);
  while(edge.targ != NULL)
    edge = edge.targ->drive(link, edge);      

}

