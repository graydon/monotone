#ifndef __SMTP_TASKS_HH__
#define __SMTP_TASKS_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <string>
#include <iostream>

#include "packet.hh"
#include "vocab.hh"

// this file contains a simple function which builds up an SMTP state
// machine and runs it using the infrastructure in proto_machine.{cc,hh}

bool post_smtp_article(std::string const & envelope_host,
		       std::string const & envelope_sender,
		       std::string const & envelope_recipient,
		       std::string const & from,
		       std::string const & to,
		       std::string const & subject,
		       std::string const & article,
		       std::iostream & stream);

#endif // __SMTP_TASKS_HH__
