// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "transforms.hh"
#include "network.hh"
#include "smtp_tasks.hh"
#include "proto_machine.hh"
#include "sanity.hh"
#include "ui.hh"
 
// this file contains a simple function which builds up an SMTP state
// machine and runs it using the infrastructure in proto_machine.{cc,hh}

using namespace std;

struct postlines_state : public proto_state
{
  string const & to;
  string const & from;
  string const & subject;
  string const & body;
  explicit postlines_state(string const & to,
			   string const & frm, 
			   string const & subj,
			   string const & bod)
    : to(to), from(frm), subject(subj), body(bod)
  {}
  virtual ~postlines_state() {}
  virtual proto_edge drive(iostream & net, proto_edge const & e)
  {
    vector<string> lines, split;
    lines.push_back("to: " + to);
    lines.push_back("from: " + from);
    lines.push_back("subject: " + subject);
    lines.push_back("");
    split_into_lines(body, split);
    copy(split.begin(), split.end(), back_inserter(lines));
    return proto_state::step_lines(net, lines);
  }  
};

bool post_smtp_article(string const & envelope_host,
		       string const & envelope_sender,
		       string const & envelope_recipient,
		       string const & from,
		       string const & to,
		       string const & subject,
		       string const & article,
		       std::iostream & stream)
{
  // build state machine nodes
  cmd_state              helo("HELO", envelope_host);
  cmd_state              mail("MAIL", "FROM:<" + envelope_sender + ">");
  cmd_state              rcpt("RCPT", "TO:<" + envelope_sender + ">");
  cmd_state              data("DATA");
  postlines_state        post(to, from, subject, article);
  cmd_state              quit("QUIT");

  helo.add_edge(250, &mail);
  mail.add_edge(250, &rcpt);
  rcpt.add_edge(250, &data);
  data.add_edge(354, &post);
  post.add_edge(250, &quit);

  run_proto_state_machine(&helo, stream);  
  return (post.get_res_code() == 250);
}
