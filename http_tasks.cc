// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// HTTP is a much simpler protocol, so we implement the parts of it
// we need to speak in just this one file, rather than having a 
// separate HTTP machine abstraction.

// FIXME: the layering is weak in here; we might want to stratify
// a bit if more than a couple simple methods appear necessary to
// talk to depots. for now it is simple.

#include "constants.hh"
#include "network.hh"
#include "packet.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "ui.hh"

#include <string>
#include <iostream>
#include <boost/lexical_cast.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/regex.hpp>

using namespace std;
using boost::lexical_cast;
using boost::shared_ptr;

bool post_http_packets(string const & group_name,
		       string const & user,
		       string const & signature,
		       string const & packets,
		       string const & http_host,
		       string const & http_path,
		       unsigned long port,
		       std::iostream & stream)
{
  string query = 
    string("q=post&") +
    "group=" + group_name + "&"
    "user=" + user + "&"
    "sig=" + signature;

  string request = string("POST ")
    + "/" + http_path 
    + "?" + query + " HTTP/1.0";

  stream << request << "\r\n";
  L("HTTP -> '%s'\n", request.c_str());

  stream << "Host: " << http_host << "\r\n";
  L("HTTP -> 'Host: %s'\n", http_host.c_str());

  stream << "Content-Length: " << packets.size() << "\r\n";
  L("HTTP -> 'Content-Length: %d'\n", packets.size());

  stream << "\r\n";
  stream.flush();
  
  stream.write(packets.data(), packets.size());

  // boost::socket appeared to have an incorrect implementation
  // of overflow; I think I have fixed it. if not, comment out
  // the above and re-enable this. it's slow but it works.
  //
  //   for (size_t i = 0; i < packets.size(); ++i)
  //     { stream.put(packets.at(i)); stream.flush(); }

  stream.flush();

  L("HTTP -> %d bytes\n", packets.size());

  stream.flush();

  int response = 0; string http;
  bool ok = (stream >> http >> response &&
	     response >= 200 && 
	     response < 300);
  L("HTTP <- %s %d\n", http.c_str(), response);
  return ok;
}

struct match_seq
{    
  unsigned long & maj; 
  unsigned long & min;
  unsigned long & end;
  explicit match_seq(unsigned long & maj, 
		     unsigned long & min,
		     unsigned long & end) : maj(maj), min(min), end(end) {}
  bool operator()(boost::match_results<std::string::const_iterator, 
		  boost::regex::alloc_type> const & res) 
  {
    I(res.size() == 3);
    std::string maj_s(res[1].first, res[1].second);
    std::string min_s(res[2].first, res[2].second);
    maj = lexical_cast<unsigned long>(maj_s);
    min = lexical_cast<unsigned long>(min_s);
    end = res.position() + res.length();
    return true;
  }
};


static bool scan_for_seq(string const & str, 
			 unsigned long & maj, 
			 unsigned long & min,
			 unsigned long & end)
{
  boost::regex expr("^\\[seq ([[:digit:]]+) ([[:digit:]]+)\\]$");
  return boost::regex_grep(match_seq(maj, min, end), str, expr, 
			   boost::match_not_dot_newline) != 0;
}

void fetch_http_packets(string const & group_name,
			unsigned long & maj_number,
			unsigned long & min_number,
			packet_consumer & consumer,
			string const & http_host,
			string const & http_path,
			unsigned long port,
			std::iostream & stream)
{

  ticker packet_ticker("packet");

  // step 1: make the request
  string query = 
    string("q=since&") +
    "group=" + group_name + "&"
    "maj=" + lexical_cast<string>(maj_number) + "&"
    "min=" + lexical_cast<string>(min_number);

  string request = string("GET ")
    + "/" + http_path 
    + "?" + query + " HTTP/1.0";

  stream << request << "\r\n";
  L("HTTP -> '%s'\n", request.c_str());

  stream << "Host: " << http_host << "\r\n";
  L("HTTP -> 'Host: %s'\n", http_host.c_str());

  stream << "\r\n";
  stream.flush();

  // step 2: skip the headers. either we get packets or we don't; how we
  // get them, or what the HTTP server thinks, is irrelevant.
  {
    bool in_headers = true;
    while(stream.good() && in_headers)
      {
	size_t linesz = 0xfff;
	char line[linesz];
	memset(line, 0, linesz);
	stream.getline(line, linesz, '\n');	
	size_t bytes = stream.gcount();
	N(bytes < linesz, "long header response line from server");
	if (bytes > 0) bytes--;
	string tmp(line, bytes);
	if (bytes == 1 || tmp.empty() || tmp == "\r") 
	  in_headers = false;
	else
	  L("HTTP <- header %d bytes: '%s'\n", bytes, tmp.c_str());
      }
  }

  // step 3: read any packets
  {
    char buf[bufsz];
    string packet;
    packet.reserve(bufsz);
    while(stream.good())
      {
	// WARNING: again, we are reading from the network here.
	// please use the utmost clarity and safety in this part.
	stream.read(buf, bufsz);
	size_t bytes = stream.gcount();
	N(bytes <= bufsz, "long response line from server");
	string tmp(buf, bytes);
	size_t pos = tmp.find_first_not_of("abcdefghijklmnopqrstuvwxyz"
					   "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
					   "0123456789"
					   "+/=_.@[] \n\t");
	if (pos != string::npos)
	  {
	    L("Bad char from network: pos %d, char '%d'\n", 
	      pos,
	      static_cast<int>(packet.at(pos)));
	    continue;
	  }
	
	packet.append(tmp);

	unsigned long end = 0;	
	if (scan_for_seq(packet, maj_number, min_number, end))
	  {
	    // we are at the end of a packet
	    L("got sequence numbers %lu, %lu\n", maj_number, min_number);
	    istringstream pkt(packet.substr(0,end));
	    packet_ticker += read_packets(pkt, consumer);
	    packet.erase(0, end);
	  }
      }

    if (packet.size() != 0)
      {
	L("%d trailing bytes from http\n", packet.size());
	istringstream pkt(packet);
	packet_ticker += read_packets(pkt, consumer);
      }    
  }
  P("http fetch complete\n");
}
