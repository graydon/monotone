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

#include <adns.h>

#include "Netxx/Netbuf.h"
#include "Netxx/Stream.h"
#include "Netxx/Timeout.h"

#include <boost/spirit.hpp>

#include <ctype.h>

#include <boost/shared_ptr.hpp>
#include <string>
#include <iostream>

#include "app_state.hh"
#include "constants.hh"
#include "database.hh"
#include "http_tasks.hh"
#include "keys.hh"
#include "lua.hh"
#include "network.hh"
#include "nntp_tasks.hh"
#include "patch_set.hh"
#include "sanity.hh"
#include "smtp_tasks.hh"
#include "transforms.hh"
#include "url.hh"

using namespace std;
using namespace boost::spirit;
using boost::lexical_cast;
using boost::shared_ptr;

bool 
lookup_address(string const & dns_name,
	       string & ip4)
{
  static map<string, string> name_cache;
  adns_state st;
  adns_answer *answer;

  if (dns_name.size() == 0)
    return false;

  map<string, string>::const_iterator it = name_cache.find(dns_name);
  if (it != name_cache.end())
    {
      ip4 = it->second;
      return true;
    }

  L(F("resolving name %s\n") % dns_name);

  if (isdigit(dns_name[0]))
    {
      L(F("%s considered a raw IP address, returning\n") % dns_name);
      ip4 = dns_name;
      return true;
    }

  I(adns_init(&st, adns_if_noerrprint, 0) == 0);
  if (adns_synchronous(st, dns_name.c_str(), adns_r_a, 
		       (adns_queryflags)0, &answer) != 0)
    {
      L(F("IP sync lookup returned false\n"));
      adns_finish(st);
      return false;
    }

  if (answer->status != adns_s_ok)
    {
      L(F("IP sync lookup returned status %d\n") % answer->status);
      free(answer);
      adns_finish(st);
      return false;
    }

  ip4 = string(inet_ntoa(*(answer->rrs.inaddr)));
  name_cache.insert(make_pair(dns_name, ip4));

  L(F("name %s resolved to IP %s\n") % dns_name % ip4);

  free(answer);
  adns_finish(st);
  return true;
}

bool 
lookup_mxs(string const & dns_name,
	   set< pair<int, string> > & mx_names)
{
  typedef shared_ptr< set< pair<int, string> > > mx_set_ptr;
  static map<string, mx_set_ptr> mx_cache;
  adns_state st;
  adns_answer *answer;

  if (dns_name.size() == 0)
    return false;

  map<string, mx_set_ptr>::const_iterator it 
    = mx_cache.find(dns_name);
  if (it != mx_cache.end())
    {
      mx_names = *(it->second);
      return true;
    }

  mx_set_ptr mxs = mx_set_ptr(new set< pair<int, string> >());

  L(F("searching for MX records for %s\n") % dns_name);

  if (isdigit(dns_name[0]))
    {
      L(F("%s considered a raw IP address, returning\n") % dns_name);
      mx_names.insert(make_pair(10, dns_name));
      return true;
    }

  I(adns_init(&st, adns_if_noerrprint, 0) == 0);
  if (adns_synchronous(st, dns_name.c_str(), adns_r_mx_raw, 
		       (adns_queryflags)0, &answer) != 0)
    {
      L(F("MX sync lookup returned false\n"));
      adns_finish(st);
      return false;
    }

  if (answer->status != adns_s_ok)
    {
      L(F("MX sync lookup returned status %d\n") % answer->status);
      free(answer);
      adns_finish(st);
      return false;
    }
  
  L(F("MX sync lookup returned %d results\n") % answer->nrrs);
  for (int i = 0; i < answer->nrrs; ++i)
    {
      string mx = string(answer->rrs.intstr[i].str);
      int prio = answer->rrs.intstr[i].i;
      mxs->insert(make_pair(prio, mx));
      mx_names.insert(make_pair(prio, mx));
      L(F("MX %s : %s priority %d\n") % dns_name % mx % prio);
    }
  mx_cache.insert(make_pair(dns_name, mxs));

  free(answer);
  adns_finish(st);
  return true;
}

struct 
monotone_conn
{
  monotone_conn(string const & host,
		unsigned long port) : 
    client(host.c_str(), port), 
    buf(client), 
    stream(&buf)
  {}
  Netxx::Stream client;
  Netxx::Netbuf<1024> buf;
  std::iostream stream;
};

typedef shared_ptr<monotone_conn> monotone_connection;


static void 
open_connection(string const & proto_name,
		string const & host_name_in,
		unsigned long port_num_in,
		monotone_connection & connection,
		app_state & app)
{
  string resolved_host;

  // check for tunnels
  string host_name = host_name_in;
  unsigned long port_num = port_num_in;
  if (app.lua.hook_get_connect_addr(proto_name,
				    host_name_in, 
				    port_num_in,
				    host_name,
				    port_num))
    {
      P(F("directing connection to %s:%d\n") % host_name % port_num);
    }
  else
    {
      host_name = host_name_in;
      port_num = port_num_in;
    }
  
  {    
    if (! lookup_address(host_name, resolved_host))
      throw oops ("host " + host_name + " not found");    
    L(F("resolved '%s' as '%s'\n") % host_name % resolved_host);
  }


  L(F("connecting to port number %d\n") % port_num);

  connection = monotone_connection(new monotone_conn(resolved_host, port_num));
}


static void 
post_http_blob(url const & targ,
	       string const & blob,
	       string const & group,
	       string const & host,
	       unsigned long port,
	       string const & path,
	       app_state & app,
	       bool & posted_ok)
{
  rsa_keypair_id keyid;
  base64< arc4<rsa_priv_key> > privkey;
  base64<rsa_sha1_signature> signature_base64;
  rsa_sha1_signature signature_plain;
  hexenc<rsa_sha1_signature> signature_hex;
	      
  N(app.lua.hook_get_http_auth(targ, keyid),
    F("missing pubkey for '%s'") % targ); 
	      
  N(app.db.private_key_exists(keyid),
    F("missing private key data for '%s'") % keyid);
  
  app.db.get_key(keyid, privkey);
  make_signature(app.lua, keyid, privkey, blob, signature_base64);
  decode_base64 (signature_base64, signature_plain);
  encode_hexenc (signature_plain, signature_hex);	  
  
  try 
    {
      monotone_connection connection;

      bool is_proxy = false;
      string connect_host_name = host;
      unsigned long connect_port_num = port;
      if (app.lua.hook_get_http_proxy(host, port,
				      connect_host_name, 
				      connect_port_num))
	{
	  P(F("using proxy at %s:%d\n") % connect_host_name % connect_port_num);
	  is_proxy = true;
	}
      else
	{
	  connect_host_name = host;
	  connect_port_num = port;
	}

      open_connection("http", connect_host_name, connect_port_num, 
		      connection, app);
      
      posted_ok = post_http_packets(group, keyid(), 
				    signature_hex(), blob, host, 
				    path, port, is_proxy, 
				    connection->stream);
    }
  catch (std::exception & e)
    {
      L(F("got exception from network: %s\n") % string(e.what()));
    }
}

static void 
post_nntp_blob(url const & targ,
	       string const & blob,
	       string const & group,
	       string const & host,
	       unsigned long port,
	       app_state & app,
	       bool & posted_ok)
{
  string sender;
  N(app.lua.hook_get_news_sender(targ, sender),
    F("missing sender address for '%s'") % targ);
	      
  try 
    {
      monotone_connection connection;
      open_connection("nntp", host, port, connection, app);
      posted_ok = post_nntp_article(group, sender, 
				    // FIXME: maybe some sort of more creative subject line?
				    "[MT] packets", 
				    blob, connection->stream);
    }
  catch (std::exception & e)
    {
      L(F("got exception from network: %s\n") % string(e.what()));
    }	  
}

static void 
post_smtp_blob(url const & targ,
	       string const & blob,
	       string const & user,
	       string const & host,
	       unsigned long port,
	       app_state & app,
	       bool & posted_ok)
{
  string sender, self_hostname;

  N(app.lua.hook_get_mail_sender(targ, sender),
    F("missing sender address for '%s'") % targ);

  N(app.lua.hook_get_mail_hostname(targ, self_hostname),
    F("missing self hostname for '%s'") % targ);

  N(user != "",
    F("empty recipient in mailto: URL %s") % targ);
	  
  try 
    {
      set< pair<int,string> > mxs;
      lookup_mxs (host, mxs);
      if (mxs.empty())
	{
	  L(F("MX lookup is empty, using hostname %s\n") % host);
	  mxs.insert(make_pair(10, host));
	}

      bool found_working_mx = false;
      monotone_connection connection;
      for (set< pair<int, string> >::const_iterator mx = mxs.begin();
	   mx != mxs.end(); ++mx)
	{
	  try 
	    {		      
	      open_connection("smtp", mx->second, port, connection, app);
	      found_working_mx = true;
	      break;
	    }
	  catch (...)
	    {
	      L(F("exception while contacting MX %s\n") % mx->second);
	    }
	}
	      
      // FIXME: maybe hook to modify envelope params?
      if (found_working_mx)
	posted_ok = post_smtp_article(self_hostname, 
				      sender, user + "@" + host,
				      sender, user + "@" + host,
				      "[MT] packets",
				      blob, connection->stream);
    }
  catch (std::exception & e)
    {
      L(F("got exception from network: %s\n") % string(e.what()));
    }	  
}


void 
post_queued_blobs_to_network(set<url> const & targets,
			     app_state & app)
{

  L(F("found %d targets for posting\n") % targets.size());
  bool exception_during_posts = false;

  ticker n_bytes("bytes");
  ticker n_packets("packets");

  for (set<url>::const_iterator targ = targets.begin();
       targ != targets.end(); ++targ)
    {
      try 
	{
	  ace user, host, group;
	  urlenc path;
	  string proto;
	  unsigned long port;
	  N(parse_url(*targ, proto, user, host, path, group, port),
	    F("cannot parse url '%s'") % *targ);

	  N((proto == "http" || proto == "nntp" || proto == "mailto"),
	    F("unknown protocol '%s', only know nntp, http and mailto") % proto);

	  size_t queue_count = 0;
	  app.db.get_queue_count(*targ, queue_count);
      
	  while (queue_count != 0)
	    {
	      L(F("found %d packets for %s\n") % queue_count % *targ);
	      string postbody;
	      vector<string> packets;
	      while (postbody.size() < constants::postsz 
		     && packets.size() < queue_count)
		{
		  string tmp;
		  app.db.get_queued_content(*targ, packets.size(), tmp);
		  packets.push_back(tmp);
		  postbody.append(tmp);
		}
	  
	      if (postbody != "")
		{
		  bool posted_ok = false;

		  L(F("posting %d packets for %s\n") % packets.size() % *targ);
	      
		  if (proto == "http")
		    post_http_blob(*targ, postbody, group(), host(), port, path(), app, posted_ok);
		  else if (proto == "nntp")
		    post_nntp_blob(*targ, postbody, group(), host(), port, app, posted_ok);
		  else if (proto == "mailto")
		    post_smtp_blob(*targ, postbody, user(), host(), port, app, posted_ok);
	      
		  if (!posted_ok)
		    throw informative_failure("unknown failure during post to " + (*targ)());

		  n_packets += packets.size();
		  n_bytes += postbody.size();
		  for (size_t i = 0; i < packets.size(); ++i)
		    app.db.delete_posting(*targ, 0);
		}

	      app.db.get_queue_count(*targ, queue_count);
	    }
	} 
      catch (informative_failure & i)
	{
	  W(F("%s\n") %  i.what);
	  exception_during_posts = true;
	}
    }
  if (exception_during_posts)
    W(F("errors occurred during posts\n"));
}

void 
fetch_queued_blobs_from_network(set<url> const & sources,
				app_state & app)
{

  bool exception_during_fetches = false;
  packet_db_writer dbw(app);

  for(set<url>::const_iterator src = sources.begin();
      src != sources.end(); ++src)
    {
      try
	{
	  ace user, host, group;
	  urlenc path;
	  string proto;
	  unsigned long port;
	  N(parse_url(*src, proto, user, host, path, group, port),
	    F("cannot parse url '%s'") % *src);
      
	  N((proto == "http" || proto == "nntp" || proto == "mailto"),
	    F("unknown protocol '%s', only know nntp, http and mailto") % proto);

	  if (proto == "mailto")
	    {
	      P(F("cannot fetch from mailto url %s, skipping\n") % *src);
	      continue;
	    }

	  P(F("fetching packets from group %s\n") % *src);

	  dbw.server.reset(*src);
	  transaction_guard guard(app.db);
	  if (proto == "http")
	    {
	      unsigned long maj, min;
	      app.db.get_sequences(*src, maj, min);
	      monotone_connection connection;

	      bool is_proxy = false;
	      string connect_host_name = host();
	      unsigned long connect_port_num = port;
	      if (app.lua.hook_get_http_proxy(host(), port,
					      connect_host_name, 
					      connect_port_num))
		{
		  P(F("using proxy at %s:%d\n") % connect_host_name % connect_port_num);
		  is_proxy = true;
		}
	      else
		{
		  connect_host_name = host();
		  connect_port_num = port;
		}

	      open_connection("http", connect_host_name, connect_port_num, 
			      connection, app);
	      fetch_http_packets(group(), maj, min, dbw, host(), path(), port, 
				 is_proxy, connection->stream);
	      app.db.put_sequences(*src, maj, min);
	    }

	  else if (proto == "nntp")
	    {
	      unsigned long maj, min;
	      app.db.get_sequences(*src, maj, min);
	      monotone_connection connection;
	      open_connection("nntp", host(), port, connection, app);
	      fetch_nntp_articles(group(), min, dbw, connection->stream);
	      app.db.put_sequences(*src, maj, min);
	    }
	  guard.commit();
	}
      catch (informative_failure & i)
	{
	  W(F("%s\n") %  i.what);
	  exception_during_fetches = true;
	}
    }
  P(F("fetched %d packets\n") % dbw.count);
  if (exception_during_fetches)
    W(F("errors occurred during fetches\n"));
}
  
