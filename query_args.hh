// -*- mode: C++; c-file-style: "gnu"; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
// copyright (C) 2006 vinzenz feenstra <evilissimo@c-plusplus.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details
#ifndef __QUERY_ARGS_HH__
#define __QUERY_ARGS_HH__

#include <string>
#include <vector>

struct query_args_param
{
    enum arg_type{ text, blob };
    arg_type type;
    std::string data;
    size_t size;
};

struct query_args
{
    query_args(char const * cmd)
    : sql_cmd(cmd)
    {}
    
    query_args(std::string const & cmd)
    : sql_cmd(cmd)
    {}

    query_args & operator%(query_args_param const & qap)
    {
        args.push_back(qap);
        return *this;
    }
    
    std::vector<query_args_param> args;
    std::string sql_cmd;
};

#endif //__QUERY_ARGS_HH__
