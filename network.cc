// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// this file contains basic utilities for dealing with the network
// protocols monotone knows how to speak, and the queueing / posting system
// which interacts with the network via those protocols.

#include <sys/time.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "boost/socket/address_info.hpp"
#include "boost/socket/any_protocol.hpp"
#include "boost/socket/any_address.hpp"
#include "boost/socket/ip4.hpp"
#include "boost/socket/connector_socket.hpp"
#include "boost/socket/socketstream.hpp"
#include "boost/socket/socket_exception.hpp"

#include <boost/spirit.hpp>

#include <boost/shared_ptr.hpp>
#include <string>
#include <iostream>

#include "app_state.hh"
#include "database.hh"
#include "http_tasks.hh"
#include "keys.hh"
#include "lua.hh"
#include "network.hh"
#include "nntp_tasks.hh"
#include "patch_set.hh"
#include "sanity.hh"
#include "transforms.hh"

using namespace std;
using namespace boost::spirit;
using boost::lexical_cast;
using boost::shared_ptr;


bool parse_url(url const & u,
	       string & proto,
	       string & host,	       
	       string & path,
	       unsigned long & port)
{
  // http://host:port/path
  // nntp://host:port/ignored

  port = 0;
  path = "";

  rule<> prot = (str_p("http") | str_p("nntp"))[assign(proto)];
  rule<> hst = (list_p(+chset<>("a-zA-Z0-9_-"), ch_p('.')))[assign(host)];
  rule<> prt = (ch_p(':') >> uint_p[assign(port)]);
  rule<> pth = (ch_p('/') >> (+chset<>("a-zA-Z0-9_-.~/"))[assign(path)]);
  rule<> r = prot >> str_p("://") >> hst >> !prt >> !pth;
  
  bool parsed_ok = parse(u().c_str(), r).full;

  if (parsed_ok)
    {
      if (proto == "http" && port == 0)
	port = 80;
      else if (proto == "nntp" && port == 0)
	port = 119;
    }

  return parsed_ok;
}
	       

void open_connection(string const & host_name,
		     unsigned long port_num,
		     boost::socket::connector<>::data_connection_t & connection,
		     boost::shared_ptr<iostream> & stream)
{
  using namespace boost::socket;
  string resolved_host;
  
  ip4::address addr;
  ip4::tcp_protocol proto;
  
  {    
    // fixme: boost::socket is currently a little braindead, in that it
    // only takes ascii strings representing IP4 ADDRESSES. duh.
    struct hostent * hent = gethostbyname(host_name.c_str());
    if (hent == NULL)
      throw oops ("host " + host_name + " not found");    
    struct in_addr iad;
    iad.s_addr = * reinterpret_cast<unsigned long *>(hent->h_addr);
    resolved_host = string(inet_ntoa(iad));    
    L("resolved '%s' as '%s'\n", host_name.c_str(), resolved_host.c_str());
  }

  addr.ip(resolved_host.c_str());
  addr.port(port_num);

  L("connecting to port number %lu\n", port_num);
  
  boost::socket::connector<> connector;

  N(connector.connect(connection, proto, addr) == 0,
    "unable to connect to server " 
    + host_name 
    + ":" + lexical_cast<string>(port_num));

  boost::shared_ptr< basic_socket_stream<char> > 
    link(new basic_socket_stream<char>(connection));

  N(link->good(),
    "bad network link, connecting to " 
    + host_name 
    + ":" + lexical_cast<string>(port_num));

  stream = link;
}


void post_queued_blobs_to_network(vector< pair<url,group> > const & targets,
				  app_state & app)
{

  L("found %d targets for posting\n", targets.size());

  size_t good_packets = 0, total_packets = 0;

  for (vector< pair<url,group> >::const_iterator targ = targets.begin();
       targ != targets.end(); ++targ)
    {
      string proto, host, path;
      unsigned long port;
      N(parse_url(targ->first, proto, host, path, port),
	("cannot parse url '" + targ->first() + "'"));

      N((proto == "http" || proto == "nntp"),
	("unknown protocol '" + proto + "', only know nntp and http"));

      string postbody;
      vector<string> contents;
      app.db.get_queued_contents(targ->first, targ->second, contents);
      for (vector<string>::const_iterator content = contents.begin();
	   content != contents.end(); ++content)
	postbody.append(*content);

      bool posted_ok = false;
      
      if (proto == "http")
	{
	  rsa_keypair_id keyid;
	  base64< arc4<rsa_priv_key> > privkey;
	  base64<rsa_sha1_signature> signature_base64;
	  rsa_sha1_signature signature_plain;
	  hexenc<rsa_sha1_signature> signature_hex;

	  N(app.lua.hook_get_http_auth(targ->first, targ->second, keyid),
	    ("missing pubkey for '" 
	     + targ->first() + "', group " + targ->second())); 

	  N(app.db.private_key_exists(keyid),
	    "missing private key data for '" + keyid() + "'");
	  
	  app.db.get_key(keyid, privkey);
	  make_signature(app.lua, keyid, privkey, postbody, signature_base64);
	  decode_base64 (signature_base64, signature_plain);
	  encode_hexenc (signature_plain, signature_hex);	  
	  
	  try 
	    {
	      boost::socket::connector<>::data_connection_t connection;
	      boost::shared_ptr<iostream> stream;
	      open_connection(host, port, connection, stream);

	      posted_ok = post_http_packets(targ->second(), keyid(), 
					    signature_hex(), postbody, host, 
					    path, port, *stream);
	    }
	  catch (std::exception & e)
	    {
	      L("got exception from network: %s\n", e.what());
	    }
	}
      
      else if (proto == "nntp")
	{
	  string sender;
	  N(app.lua.hook_get_news_sender(targ->first, targ->second, sender),
	    ("missing sender address for '" 
	     + targ->first() + "', group " + targ->second())); 
	  
	  try 
	    {
	      boost::socket::connector<>::data_connection_t connection;
	      boost::shared_ptr<iostream> stream;
	      open_connection(host, port, connection, stream);
	      posted_ok = post_nntp_article(targ->second(), sender, 
					    // FIXME: maybe some sort of more creative subject line?
					    "[MT] packets", 
					    postbody, *stream);
	    }
	  catch (std::exception & e)
	    {
	      L("got exception from network: %s\n", e.what());
	    }
	  
	}
      if (posted_ok)
	for (vector<string>::const_iterator content = contents.begin();
	     content != contents.end(); ++content)
	  {
	    ++good_packets;
	    app.db.delete_posting(targ->first, targ->second, *content);
	  }
      total_packets += contents.size();
    }
  P("posted %d / %d packets ok\n", good_packets, total_packets);
}

void fetch_queued_blobs_from_network(vector< pair<url,group> > const & sources,
				     app_state & app)
{

  packet_db_writer dbw(app);

  for(vector< pair<url,group> >::const_iterator src = sources.begin();
      src != sources.end(); ++src)
    {
      string proto, host, path;
      unsigned long port;            
      N(parse_url(src->first, proto, host, path, port),
	("cannot parse url '" + src->first() + "'"));
      
      N((proto == "http" || proto == "nntp"),
	("unknown protocol '" + proto + "', only know nntp and http"));
      
      P("fetching packets from group %s at %s\n", 
	src->second().c_str(),
	src->first().c_str());

      dbw.server.reset(*src);
      
      if (proto == "http")
	{
	  unsigned long maj, min;
	  app.db.get_sequences(src->first, src->second, maj, min);
	  boost::socket::connector<>::data_connection_t connection;
	  boost::shared_ptr<iostream> stream;
	  open_connection(host, port, connection, stream);
	  fetch_http_packets(src->second(), maj, min, dbw, host, path, port, *stream);
	  app.db.put_sequences(src->first, src->second, maj, min);
	}

      else if (proto == "nntp")
	{
	  unsigned long maj, min;
	  app.db.get_sequences(src->first, src->second, maj, min);
	  boost::socket::connector<>::data_connection_t connection;
	  boost::shared_ptr<iostream> stream;
	  open_connection(host, port, connection, stream);
	  fetch_nntp_articles(src->second(), min, dbw, *stream);
	  app.db.put_sequences(src->first, src->second, maj, min);
	}
    }
  P("fetched %d packets\n", dbw.count);
}
  

void queue_blob_for_network(vector< pair<url,group> > const & targets,
			    string const & blob,
			    app_state & app)
{
  for (vector< pair<url,group> >::const_iterator targ = targets.begin();
       targ != targets.end(); ++targ)
    {
      string proto, host, path;
      unsigned long port;      
      N(parse_url(targ->first, proto, host, path, port),
	("cannot parse url '" + targ->first() + "'"));

      app.db.queue_posting(targ->first, targ->second, blob);

      P("%d bytes queued to send to group %s at %s\n", 
	blob.size(),
	targ->second().c_str(),
	targ->first().c_str());

    }
}
