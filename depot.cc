// copyright (C) 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// this file contains the entire "depot" CGI functionality, 
// which is a very lightweight device for posting packets
// and letting other people retrieve them. it is not meant
// to be a general purpose patch/version/file server, just
// a packet exchanger (a friendlier surrogate for NNTP).

#include <iostream>
#include <string>
#include <map>
#include <stdexcept>

#include <stdlib.h>

#include <boost/tokenizer.hpp>
#include <boost/lexical_cast.hpp>

#include <boost/spirit.hpp>

#include "adler32.hh"
#include "cleanup.hh"
#include "constants.hh"

#include "cryptopp/base64.h"
#include "cryptopp/hex.h"
#include "cryptopp/cryptlib.h"
#include "cryptopp/sha.h"
#include "cryptopp/rsa.h"

#include "sqlite/sqlite.h"

using namespace std;
using namespace boost;

// depot schema is small enough to just include it inline here.

char const * depot_schema =
" CREATE TABLE packets (\n"
"         major      INTEGER,\n"
"         minor      INTEGER,\n"
"         groupname  TEXT NOT NULL,\n"
"         adler32    TEXT NOT NULL,\n"
"         contents   TEXT NOT NULL,\n"
"         unique(groupname, contents),\n"
"         unique(major, minor, groupname)\n"
"         );\n"
" \n"
" CREATE TABLE users (\n"
"         name     TEXT PRIMARY KEY,\n"
"         pubkey   TEXT NOT NULL\n"
"         );\n"; 


template<typename T>
int read_value_cb (void * vp, 
		   int ncols, 
		   char ** values,
		   char ** colnames)
{
  if (ncols != 1)
    return 0;

  if (vp == NULL)
    return 0;
  
  T *p = reinterpret_cast<T *>(vp);

  if (values == NULL || 
      values[0] == NULL)
    return 0;

  try 
    {
      *p = lexical_cast<T>(string(values[0]));
    }
  catch (...)
    {
      // it is better for us to just not modify the value.
      // sqlite gets a bit snickery if you throw exceptions
      // up its call stack.
    }
  return 0;
}

template int read_value_cb<unsigned long>(void *,int,char **, char**);
template int read_value_cb<string>(void *,int,char **, char**);
template int read_value_cb<bool>(void *,int,char **, char**);


// ----- PROCESSING 'SINCE' QUERIES ----- 

void execute_status_query (sqlite *sql)
{
  char *errmsg = NULL;
  unsigned long count = 0;

  int res = sqlite_exec_printf(sql, 
			       "SELECT COUNT(*) FROM packets ",
			       &read_value_cb<unsigned long>, &count, &errmsg);
  if (res == SQLITE_OK)
    {
      cout << "Status: 200 OK\n"
	   << "Content-type: text/plain\n"
	   << "\n"
	   << "depot operational with " << count << " packets.\n";
    }
  else
    {
      cout << "Status: 204 No Content\n"
	   << "Content-type: text/plain\n"
	   << "\n"
	   << "depot error: " << (errmsg != NULL ? errmsg : "unknown") << "\n";
    }
}

// ----- PROCESSING 'SINCE' QUERIES ----- 

int write_since_results(void * dummy, 
			int ncols, 
			char ** values,
			char ** colnames)
{
  if (ncols != 3)
    return 0;

  if (values == NULL || 
      values[0] == NULL || 
      values[1] == NULL || 
      values[2] == NULL)
    return 0;

  cout << values[2] << "\n";
  cout << "[seq " << values[0] << " " << values[1] << "]\n";

  return 0;
}

void execute_since_query (unsigned long maj, 
			  unsigned long min, 
			  string const & group,
			  sqlite *sql)
{
  cout << "Status: 200 OK\n"
       << "Content-type: application/x-monotone-packets\n"
       << "\n";
  
  int res = sqlite_exec_printf(sql, 
			       "SELECT major, minor, contents "
			       "FROM packets "
			       "WHERE major >= %lu AND minor > %lu "
			       "ORDER BY major, minor",
			       &write_since_results, NULL, NULL,
			       maj, min);
  
  if (res != SQLITE_OK)
    throw runtime_error("sqlite returned error");
}


// ----- PROCESSING 'POST' QUERIES ----- 


static string err2msg (char *em)
{
  if (em != NULL)
    return string(em);
  else
    return string("(null)");
}

void execute_post_query (string const & user, 
			 string const & sig, 
			 string const & group,
			 unsigned long nbytes,
			 sqlite *sql)
{

  char *errmsg = NULL;
  
  if (nbytes >= maxbytes)
    throw runtime_error("uploading too much data");

  // step 1: get incoming data
  string tmp;
  tmp.clear();
  tmp.reserve(nbytes);

  {
    char buf[bufsz];
    while(tmp.size() < maxbytes && 
	  tmp.size() < nbytes && 
	  cin.good())
      {
 	unsigned long count = bufsz;
 	if (nbytes - tmp.size() < count)
 	  count = nbytes - tmp.size();

 	cin.read(buf, count);
 	tmp.append(buf, cin.gcount());
      }

    size_t p;
    if ((p = tmp.find_first_not_of("abcdefghijklmnopqrstuvwxyz"
				   "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
				   "0123456789"
				   "+/=_.@[] \n\t"))
	!= string::npos)
      throw runtime_error(string("illegal character in uploaded data: ") 
			  + lexical_cast<string>(static_cast<int>(tmp.at(p))));

    if (tmp.size() >= maxbytes)
      throw runtime_error("overflowed safety limit for maximum upload");
  }

  // step 2: pick up pubkey
  string pubkey;
  {
    int res = sqlite_exec_printf(sql, 
				 "SELECT pubkey "
				 "FROM users "
				 "WHERE name = '%q' "
				 "LIMIT 1",
				 &read_value_cb<string>,
				 &pubkey, &errmsg,
				 user.c_str());
    if (res != SQLITE_OK)
      throw runtime_error("sqlite returned error for pubkey (user '" + user + "'): " 
			  + err2msg(errmsg));
    if (pubkey == "")
      throw runtime_error("no pubkey found for user '" + user + "'");
  }


  // step 3: confirm sig on incoming data 
  // (yes yes, this repeats code in keys.cc, 
  //  but I don't want to link with it)

  {
    using namespace CryptoPP;
    StringSource keysource(pubkey, true, new Base64Decoder);
    RSASSA_PKCS1v15_SHA_Verifier verifier(keysource);    

    string decodedsig;
    StringSource sigsource(sig, true, new HexDecoder (new StringSink (decodedsig)));
    VerifierFilter *vf = NULL;

    try 
      {
	vf = new VerifierFilter(verifier);
	vf->Put(reinterpret_cast<byte const *>(decodedsig.data()), decodedsig.size());
      }
    catch (...)
      {
	if (vf)
	  delete vf;
	throw runtime_error("signature verifier threw exception");
      }

    if (vf == NULL)
      throw runtime_error("signature verifier became NULL");
    
    StringSource ss(tmp, true, vf);
    if (! vf->GetLastResult())
      throw runtime_error("bad signature value");
  }


  // step 4: begin transaction
  {
    int res = sqlite_exec_printf(sql, "BEGIN", NULL, NULL, &errmsg);
    if (res != SQLITE_OK)
      throw runtime_error("sqlite returned error on BEGIN: " + err2msg(errmsg));
  }
  

  // step 5: chop up data and insert it
  {
    string::size_type lo = string::npos, hi = string::npos;
    string tok("[end]\n");
    for(lo = 0; (hi = tmp.find(tok, lo)) != string::npos; lo = hi+tok.size())
      {
	string content = tmp.substr(lo, (hi+tok.size()) - lo);
	bool exists = false;
	adler32 ad(content.data(), content.size());
	int res = sqlite_exec_printf(sql, 
				     "SELECT COUNT(*) > 0 FROM packets "
				     "WHERE groupname = '%q' AND adler32 = %lu AND contents = '%q'",
				     &read_value_cb<bool>, &exists, &errmsg,
				     group.c_str(),
				     ad.sum(), 
				     content.c_str());

	if (res != SQLITE_OK)
	  throw runtime_error("sqlite returned error on adler32 COUNT: " + err2msg(errmsg));
	
	if (! exists)
	  {
	    unsigned long maj = 0, min = 0;
	    res = sqlite_exec_printf(sql, 
				     "SELECT MAX(major) FROM packets "
				     "WHERE groupname = '%q'",
				     &read_value_cb<unsigned long>, &maj, &errmsg,
				     group.c_str());

	    if (res != SQLITE_OK)
	      throw runtime_error("sqlite returned error on MAX(major): " + err2msg(errmsg));

	    res = sqlite_exec_printf(sql, 
				     "SELECT MAX(minor) FROM packets "
				     "WHERE groupname = '%q'",
				     &read_value_cb<unsigned long>, &min, &errmsg,
				     group.c_str());

	    if (res != SQLITE_OK)
	      throw runtime_error("sqlite returned error on MAX(minor): " + err2msg(errmsg));

	    res = sqlite_exec_printf(sql, 
				     "INSERT INTO packets "
				     "VALUES (%lu, %lu, '%q', %lu, '%q')",
				     NULL, NULL, &errmsg,
				     maj, min+1, group.c_str(), 
				     ad.sum(), content.c_str());

	    if (res != SQLITE_OK)
	      throw runtime_error("sqlite returned error on INSERT : " + err2msg(errmsg));
	  }
      }
  }

  // step 6: end transaction
  {
    int res = sqlite_exec_printf(sql, "COMMIT", NULL, NULL, &errmsg);
    if (res != SQLITE_OK)
      throw runtime_error("sqlite returned error on COMMIT: " + err2msg(errmsg));
  }

  cout << "Status: 202 OK\n"
       << "Content-type: text/plain\n"
       << "\n"
       << "packets accepted, thank you.\n";
}




// ----- GENERIC CODE FOR ALL QUERY TYPES -----


void decode_query(map<string,string> & q)
{
  char *qs = NULL;
  qs = getenv("QUERY_STRING");
  if (qs == NULL)
    throw runtime_error("no QUERY_STRING");

  string query(qs);

  // query string can only contain all alphanumerics, some URL "safe" chars
  // ( '@', '-', '_', '.' ) and the normal query-string separators & and
  // =. this is a restriction on our part, but I don't care much about URL
  // encoding. sorry.

  if (query.find_first_not_of("0123456789@-_."
			      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
			      "abcdefghijklmnopqrstuvwxyz"
			      "=&") != string::npos)
    throw runtime_error("invalid chars in input");

  typedef tokenizer<char_separator<char> > pairtok;
  char_separator<char> sep("&");  
  pairtok pairs(query, sep);
  for (pairtok::iterator pair = pairs.begin(); pair != pairs.end(); ++pair)
    {
      string p(*pair);
      if (count(p.begin(), p.end(), '=') != 1)
	throw runtime_error("bad number of '=' symbols in query pair");
      
      string::size_type eq = p.find('=');

      if (eq == string::npos)
	throw runtime_error("missing '=' in query pair");
      
      if (eq == 0)
	throw runtime_error("empty key for query pair");
      
      if (eq >= (p.size() - 1))
	throw runtime_error("empty value for query pair");
      
      q.insert(make_pair(p.substr(0, eq), p.substr(eq+1)));
	
    }
}

template <typename T> T
param(string const & key, map<string,string> params)
{
  map<string,string>::const_iterator k = params.find(key);
  if (k == params.end())
    throw runtime_error("cannot find query key " + key);
  return lexical_cast<T>(k->second);  
}


void read_and_insert_user(string const & in, sqlite *sql)
{
  // FIXME: yes yes, this grammar should be shared with packet.cc and also
  // packet.cc should use spirit and also I should have had the foresight
  // to know this would happen and also I'm a very bad person etc. etc.

  using namespace boost::spirit;

  string username;
  string pubkey;
  rule<> sp = chset<>("\t\r\n ");
  rule<> head = str_p("[pubkey") >> (+sp) 
				 >> (+chset<>("a-zA-Z0-9_.@-"))[assign(username)] 
				 >> ch_p(']');
  rule<> body = (+chset<>("a-zA-Z0-9+/=\t\r\n "))[assign(pubkey)];
  rule<> end = str_p("[end]") >> (*sp);
  rule<> packet = head >> body >> end;

  if (parse (in.c_str(), packet).full)
    {
      char *errmsg = NULL;
      int res = sqlite_exec_printf(sql, 
				   "INSERT INTO users VALUES ('%q','%q') ",
				   NULL, NULL, &errmsg, 
				   username.c_str(), pubkey.c_str());
      if (res != SQLITE_OK)
	throw runtime_error ("error inserting pubkey: " + err2msg (errmsg));
    }
  else
    throw runtime_error ("failed to parse pubkey packet");
}

void run_cmdline(vector<string> const & args)
{
  if (args.size() == 0)
    throw runtime_error("no command-line args");

  if (args[0] == "initdb")
    {
      cleanup_ptr<sqlite *, void> 
	sql(sqlite_open("depot.db", 0755, NULL), &sqlite_close);

      if (sql() == NULL)
	throw runtime_error("cannot open depot.db");

      char *errmsg = NULL;
      if (sqlite_exec(sql(), depot_schema, NULL, NULL, &errmsg) != SQLITE_OK)
	throw runtime_error("database initialization failed: " + string(errmsg));
      return;
    }

  else if (args[0] == "adduser")
    {
      if (args.size() != 1)
	throw runtime_error("wrong number of args to adduser, need just packet input");
      
      cleanup_ptr<sqlite *, void> 
	sql(sqlite_open("depot.db", 0755, NULL), &sqlite_close);

      if (sql() == NULL)
	throw runtime_error("cannot open depot.db");

      string packet;
      char c;
      while (cin.get(c))
	packet += c;

      read_and_insert_user (packet, sql());
      return;
    }

  else if (args[0] == "deluser")
    {
      if (args.size() != 2)
	throw runtime_error("wrong number of args to deluser, need <userid>");
      
      cleanup_ptr<sqlite *, void> 
	sql(sqlite_open("depot.db", 0755, NULL), &sqlite_close);

      if (sql() == NULL)
	throw runtime_error("cannot open depot.db");

      char *errmsg = NULL;
      if (sqlite_exec_printf(sql(), "DELETE FROM users WHERE name = '%q'",
			     NULL, NULL, NULL,
			     args[1].c_str()) != SQLITE_OK)
	throw runtime_error("user deletion failed: " + string(errmsg));
      return;
    }

  throw runtime_error("unrecognized command");
}

int main(int argc, char ** argv)
{

  try 
    {
      
      if (argc > 1 && getenv("GATEWAY_INTERFACE") == NULL)
	{
	  ++argv; --argc;
	  run_cmdline(vector<string>(argv, argv+argc));
	  exit(0);
	}
  

      map<string,string> keys;
      decode_query(keys);

      string q = param<string>("q", keys);

      if (q == "status")
	{
	  cleanup_ptr<sqlite *, void> 
	    sql(sqlite_open("depot.db", 0755, NULL), &sqlite_close);
      
	  if (sql() == NULL)
	    throw runtime_error("cannot open depot.db");
	  execute_status_query (sql());
	  exit(0);	  
	}

      else if (q == "since")
	{
	  cleanup_ptr<sqlite *, void> 
	    sql(sqlite_open("depot.db", 0755, NULL), &sqlite_close);
      
	  if (sql() == NULL)
	    throw runtime_error("cannot open depot.db");

	  unsigned long maj = param<unsigned long>("maj", keys);
	  unsigned long min = param<unsigned long>("min", keys);
	  string group = param<string>("group", keys);
	  execute_since_query (maj, min, group, sql());
	  exit(0);
	}

      else if (q == "post")
	{
	  cleanup_ptr<sqlite *, void> 
	    sql(sqlite_open("depot.db", 0755, NULL), &sqlite_close);
      
	  if (sql() == NULL)
	    throw runtime_error("cannot open depot.db");

	  string user = param<string>("user", keys);
	  string sig = param<string>("sig", keys);
	  string group = param<string>("group", keys);      

	  unsigned long nbytes = 0;
	  {
	    char * clen = NULL;
	    clen = getenv("CONTENT_LENGTH");
	    if (clen == NULL)
	      throw runtime_error("null content length");
	    nbytes = lexical_cast<unsigned long>(string(clen));
	  }

	  execute_post_query (user, sig, group, nbytes, sql());
	  exit(0);
	}

    } 
  catch (runtime_error & e)
    {
      cerr << e.what() << endl;
      cout << "Status: 500 Error\n" 
	   << "Content-type: text/plain\n"
	   << "\n"
	   << "depot error: " << e.what() << "\n";
      exit(1);
    }


}
