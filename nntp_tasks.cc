// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "transforms.hh"
#include "network.hh"
#include "nntp_tasks.hh"
#include "proto_machine.hh"
#include "sanity.hh"
#include "ui.hh"

#include <boost/tokenizer.hpp>
#include <boost/lexical_cast.hpp>
 
// this file contains simple functions which build up NNTP state
// machines and run them using the infrastructure in proto_machine.{cc,hh}

using namespace std;
using boost::lexical_cast;
using boost::tokenizer;
using boost::char_separator;

static void 
ws_split(string const & str, vector<string> & out)
{  
  out.clear();
  char_separator<char> whitespace("\r\n\t ");
  tokenizer< char_separator<char> > tokens(str, whitespace);
  copy(tokens.begin(), tokens.end(), back_inserter(out));
}


struct 
cursor_state : public cmd_state
{
  unsigned long & seq_number;
  explicit cursor_state(string const & cmd, unsigned long & seq)
    : cmd_state(cmd), seq_number(seq)
  {}
  virtual ~cursor_state() {}
  virtual proto_edge drive(iostream & net, proto_edge const & e)
  {
    vector<string> response_args;
    ws_split(e.msg, response_args);
    if (response_args.size() > 1)
      seq_number = lexical_cast<unsigned long>(response_args[1]);
    return cmd_state::drive(net, e);
  }  
};


struct 
stat_state : public proto_state
{
  unsigned long & seq_number;
  explicit stat_state(unsigned long & seq)
    : seq_number(seq)
  {}
  virtual ~stat_state() {}
  virtual proto_edge drive(iostream & net, proto_edge const & e)
  {
    vector<string> response_args;
    vector<string> my_args;
    unsigned long low = 0;
    ws_split(e.msg, response_args);
    if (response_args.size() > 2)
      try
	{
	  low = lexical_cast<unsigned long>(response_args[2]);
	}
      catch (...)
	{}
    if (low > seq_number)
      seq_number = low;
    my_args.push_back(lexical_cast<string>(seq_number));
    return proto_state::step_cmd(net, "STAT", my_args);
  }  
};


struct 
nntp_postlines_state : public proto_state
{
  string const & group;
  string const & from;
  string const & subject;
  string const & body;
  explicit nntp_postlines_state(string const & grp, 
				string const & frm, 
				string const & subj,
				string const & bod)
    : group(grp), from(frm), subject(subj), body(bod)
  {}
  virtual ~nntp_postlines_state() {}
  virtual proto_edge drive(iostream & net, proto_edge const & e)
  {
    vector<string> lines, split;
    lines.push_back("From: " + from);
    lines.push_back("Subject: " + subject);
    lines.push_back("Newsgroups: " + group);
    lines.push_back("");
    split_into_lines(body, split);
    copy(split.begin(), split.end(), back_inserter(lines));
    return proto_state::step_lines(net, lines);
  }  
};

struct 
feedlines_state : public cmd_state
{
  ticker count;
  packet_consumer & consumer;
  explicit feedlines_state(packet_consumer & cons)
    : cmd_state("NEXT"), count("packet"), consumer(cons)
  {}
  virtual ~feedlines_state() {}
  virtual proto_edge drive(iostream & net, proto_edge const & e)
  {
    string joined;
    join_lines(e.lines, joined);
    stringstream ss(joined);
    ++count;
    read_packets(ss, consumer);
    return cmd_state::drive(net, e);
  }  
};


bool 
post_nntp_article(string const & group_name,
		  string const & from,
		  string const & subject,
		  string const & article,
		  std::iostream & stream)
{
  // build state machine nodes
  cmd_state              mode_reader("MODE READER");
  cmd_state              post("POST");
  nntp_postlines_state   postlines(group_name, from, subject, article);
  cmd_state              quit("QUIT");

  mode_reader.add_edge(200, &post); // posting ok
  mode_reader.add_edge(201, &quit); // posting not ok

  post.add_edge(340, &postlines);   // ok, send lines 
  post.add_edge(440, &quit);        // posting not allowed
  post.add_edge(441, &quit);        // posting failed
  
  postlines.add_edge(240, &quit);   // posting succeeded
  postlines.add_edge(440, &quit);   // posting not allowed
  postlines.add_edge(441, &quit);   // posting failed

  run_proto_state_machine(&mode_reader, stream);  
  return (postlines.get_res_code() == 240);
}


void 
fetch_nntp_articles(string const & group_name,
		    unsigned long & seq_number,
		    packet_consumer & consumer,
		    std::iostream & stream)
{

  // build state machine nodes
  cmd_state              mode_reader("MODE READER");
  cmd_state              group("GROUP", group_name);
  stat_state             stat(seq_number);
  cursor_state           body("BODY", seq_number);
  feedlines_state        feeder(consumer);
  cmd_state              quit("QUIT");
  
  // wire together edges
  mode_reader.add_edge(200, &group); // posting ok
  mode_reader.add_edge(201, &group); // posting not ok

  group.add_edge(211, &stat);        // group ok
  group.add_edge(411, &quit);        // no such newsgroup

  stat.add_edge(223, &body);         // stat ok -> body

  body.add_edge(220, &feeder, true); // head and body ok -> next
  body.add_edge(221, &feeder, true); // head ok -> next
  body.add_edge(222, &feeder, true); // body ok -> next
  body.add_edge(223, &body);         // stat ok -> next

  feeder.add_edge(223, &body);       // next ok -> fetch body
  feeder.add_edge(412, &quit);       // no newsgroup
  feeder.add_edge(420, &stat);       // no current article
  feeder.add_edge(421, &quit);       // no more articles

  body.add_edge(412, &group);        // no newsgroup
  body.add_edge(420, &stat);         // no current article
  body.add_edge(423, &quit);         // no such article number
  body.add_edge(430, &quit);         // no such article

  // run it
  run_proto_state_machine(&mode_reader, stream);  
  P(F("nntp fetch complete\n"));
}
