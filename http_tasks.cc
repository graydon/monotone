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

bool 
post_http_packets(string const & group_name,
		  string const & user,
		  string const & signature,
		  string const & packets,
		  string const & http_host,
		  string const & http_path,
		  unsigned long port,
		  bool is_proxy,
		  std::iostream & stream)
{
  string query = 
    string("q=post&") +
    "group=" + group_name + "&"
    "user=" + user + "&"
    "sig=" + signature;

  string request = string("POST ");

  // absurdly, HTTP 1.1 mandates *different* forms of request line
  // depending on whether the client thinks it's talking to an origin
  // server or a proxy server. clever.

  if (is_proxy)
    request += "http://" + http_host + ":" + lexical_cast<string>(port) + http_path;
  else
    request += http_path;
    
  request += "?" + query + " HTTP/1.1";

  stream << request << "\r\n";
  L(F("HTTP -> '%s'\n") % request);

  stream << "Host: " << http_host << "\r\n";
  L(F("HTTP -> 'Host: %s'\n") % http_host);

  stream << "Content-Length: " << packets.size() << "\r\n";
  L(F("HTTP -> 'Content-Length: %d'\n") % packets.size());

  stream << "Connection: close\r\n";
  L(F("HTTP -> 'Connection: close'\n"));

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

  L(F("HTTP -> %d bytes\n") % packets.size());

  stream.flush();

  int response = 0; string http;
  bool ok = (stream >> http >> response &&
	     response >= 200 && 
	     response < 300);
  L(F("HTTP <- %s %d\n") % http % response);
  if (! ok)
    {
      string s;
      char c;
      while (stream.get(c))
	s += c;
      L(F("HTTP ERROR: '%s'\n") % s);
    }
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


static bool 
scan_for_seq(string const & str, 
	     unsigned long & maj, 
	     unsigned long & min,
	     unsigned long & end)
{
  boost::regex expr("^\\[seq ([[:digit:]]+) ([[:digit:]]+)\\]$");
  return boost::regex_grep(match_seq(maj, min, end), str, expr, 
			   boost::match_not_dot_newline) != 0;
}

static void 
check_received_bytes(string const & tmp)
{
  size_t pos = tmp.find_first_not_of(constants::legal_packet_bytes);
  N(pos == string::npos, 
    F("Bad char from network: pos %d, char '%d'\n")
    % pos % static_cast<int>(tmp.at(pos)));
}

static void 
read_chunk(std::iostream & stream,
	   string & packet)
{
  char buf[constants::bufsz];
  ios_base::fmtflags flags = stream.flags();
  stream.setf(ios_base::hex, ios_base::basefield);
  size_t chunk_size = 0;
  stream >> chunk_size;
  if (chunk_size == 0)
    return;

  char c = '\0';
  N(stream.good(), F("malformed chunk, stream closed after nonzero chunk size"));
  while (stream.good()) { stream.get(c); if (c != ' ') break; }
  N(c == '\r', F("malformed chunk, no leading CR (got %d)") % static_cast<int>(c));
  N(stream.good(), F("malformed chunk, stream closed after leading CR"));
  stream.get(c); N(c == '\n', F("malformed chunk, no leading LF (got %d)") % static_cast<int>(c));
  N(stream.good(), F("malformed chunk, stream closed after leading LF"));
  
  while(chunk_size > 0)
    {
      size_t read_size = std::min(constants::bufsz, chunk_size);
      stream.read(buf, read_size);
      size_t actual_read_size = stream.gcount();
      N(actual_read_size <= read_size, F("long chunked read from server"));
      string tmp(buf, actual_read_size);
      check_received_bytes(tmp);
      packet.append(tmp);
      chunk_size -= actual_read_size;
    }    

  c = '\0';
  while (stream.good()) { stream.get(c); if (c != ' ') break; }
  N(c == '\r', F("malformed chunk, no trailing CR (got %d)") % static_cast<int>(c));
  N(stream.good(), F("malformed chunk, stream closed after reailing CR"));
  stream.get(c); N(c == '\n', F("malformed chunk, no trailing LF (got %d)") % static_cast<int>(c));
  stream.flags(flags);
}

static void 
read_buffer(std::iostream & stream,
	    string & packet)
{
  char buf[constants::bufsz];
  stream.read(buf, constants::bufsz);
  size_t bytes = stream.gcount();
  N(bytes <= constants::bufsz, F("long read from server"));
  string tmp(buf, bytes);
  check_received_bytes(tmp);
  packet.append(tmp);
}

void 
fetch_http_packets(string const & group_name,
		   unsigned long & maj_number,
		   unsigned long & min_number,
		   packet_consumer & consumer,
		   string const & http_host,
		   string const & http_path,
		   unsigned long port,
		   bool is_proxy,
		   std::iostream & stream)
{

  ticker n_packets("packets");
  ticker n_bytes("bytes");

  // step 1: make the request
  string query = 
    string("q=since&") +
    "group=" + group_name + "&"
    "maj=" + lexical_cast<string>(maj_number) + "&"
    "min=" + lexical_cast<string>(min_number);


  string request = string("GET ");

  // absurdly, HTTP 1.1 mandates *different* forms of request line
  // depending on whether the client thinks it's talking to an origin
  // server or a proxy server. clever.

  if (is_proxy)
    request += "http://" + http_host + ":" + lexical_cast<string>(port) + http_path;
  else
    request += http_path;
    
  request += "?" + query + " HTTP/1.1";

  stream << request << "\r\n";
  L(F("HTTP -> '%s'\n") % request);
  
  stream << "Host: " << http_host << "\r\n";
  L(F("HTTP -> 'Host: %s'\n") % http_host);

  stream << "Content-Length: 0\r\n";
  L(F("HTTP -> 'Content-Length: 0'\n"));

  stream << "Connection: close\r\n";
  L(F("HTTP -> 'Connection: close'\n"));

  stream << "\r\n";
  stream.flush();

  // step 2: skip most of the headers. either we get packets or we don't;
  // how we get them, or what the HTTP server thinks, is mostly
  // irrelevant. unless they send chunked transport encoding, in which case
  // we need to change our read loop slightly.

  bool chunked_transport_encoding = false;

  {
    bool in_headers = true;
    while(stream.good() && in_headers)
      {
	size_t linesz = 0xfff;
	char line[linesz];
	memset(line, 0, linesz);
	stream.getline(line, linesz, '\n');
	size_t bytes = stream.gcount();
	N(bytes < linesz, F("long header response line from server"));
	if (bytes > 0) bytes--;
	string tmp(line, bytes);
	if (bytes == 1 || tmp.empty() || tmp == "\r") 
	  in_headers = false;	
	else if (tmp.find("Transfer-Encoding") != string::npos 
		 && tmp.find("chunked") != string::npos)
	  {
	    L(F("reading response as chunked encoding\n"));
	    chunked_transport_encoding = true;
	  }
	else
	  L(F("HTTP <- header %d bytes: '%s'\n") % bytes % tmp);
      }
  }

  // step 3: read any packets
  string packet;
  packet.reserve(constants::bufsz);
  {
    while(stream.good())
      {
	// WARNING: again, we are reading from the network here.
	// please use the utmost clarity and safety in this part.

	if (chunked_transport_encoding)
	  read_chunk(stream, packet);
	else
	  read_buffer(stream, packet);

	unsigned long end = 0;	
	if (scan_for_seq(packet, maj_number, min_number, end))
	  {
	    // we are at the end of a packet
	    L(F("got sequence numbers %lu, %lu\n") % maj_number % min_number);
	    istringstream pkt(packet.substr(0,end));
	    n_packets += read_packets(pkt, consumer);
	    n_bytes += end;
	    packet.erase(0, end);
	  }
      }

    if (packet.size() != 0)
      {
	L(F("%d trailing bytes from http\n") % packet.size());
	istringstream pkt(packet);
	n_packets += read_packets(pkt, consumer);
	n_bytes += packet.size();
      }    
  }
  P(F("http fetch complete\n"));
}
