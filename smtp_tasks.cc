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

#include <time.h>
 
// this file contains a simple function which builds up an SMTP state
// machine and runs it using the infrastructure in proto_machine.{cc,hh}

using namespace std;

string curr_date_822()
{
  size_t const sz = 1024;
  char buf[sz];
  time_t now;
  struct tm local;

  I(time(&now) != ((time_t)-1));
  I(localtime_r (&now, &local) != NULL);
  I(strftime(buf, sz, "%a, %d %b %Y %H:%M:%S %z", &local) != 0);

  string result(buf);
  return result;
}

struct smtp_postlines_state : public proto_state
{
  string const & to;
  string const & from;
  string const & subject;
  string const & body;
  explicit smtp_postlines_state(string const & t,
				string const & frm, 
				string const & subj,
				string const & bod)
    : to(t), from(frm), subject(subj), body(bod)
  {}
  virtual ~smtp_postlines_state() {}
  virtual proto_edge drive(iostream & net, proto_edge const & e)
  {
    vector<string> lines, split;
    lines.push_back("Date: " + curr_date_822());
    lines.push_back("From: " + from);
    lines.push_back("Subject: " + subject);
    lines.push_back("To: " + to);
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
  cmd_state              rcpt("RCPT", "TO:<" + envelope_recipient + ">");
  cmd_state              data("DATA");
  smtp_postlines_state   post(to, from, subject, article);
  cmd_state              quit("QUIT");

  helo.add_edge(250, &mail);
  mail.add_edge(250, &rcpt);
  rcpt.add_edge(250, &data);
  data.add_edge(354, &post);
  post.add_edge(250, &quit);

  run_proto_state_machine(&helo, stream);  
  return (post.get_res_code() == 250);
}
