#include <termios.h>
#include <iostream>

#include "app_state.hh"
#include "database.hh"

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

using namespace std;

app_state::app_state() 
  : branch_name(""), db("monotone.db")
{
}

app_state::~app_state()
{
}
