// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <iostream>
#include <string>

#include <boost/optional.hpp>
#include <boost/regex.hpp>
#include <boost/lexical_cast.hpp>

#include "app_state.hh"
#include "network.hh"
#include "packet.hh"
#include "sanity.hh"
#include "transforms.hh"

using namespace boost;
using namespace std;

// --- packet db writer --

packet_db_writer::packet_db_writer(app_state & app, bool take_keys) 
  : app(app), take_keys(take_keys), count(0)
{}

void packet_db_writer::consume_file_data(file_id const & ident, 
					 file_data const & dat)
{
  if (!app.db.file_version_exists(ident))
    app.db.put_file(ident, dat);
  else
    L(F("skipping existing file version %s\n") % ident);
  ++count;
}

void packet_db_writer::consume_file_delta(file_id const & old_id, 
					  file_id const & new_id,
					  file_delta const & del)
{
  if (!app.db.file_version_exists(new_id))
    {
      if (app.db.file_version_exists(old_id))
	app.db.put_file_version(old_id, new_id, del);
      else
	W(F("warning: file delta pre-image '%s' not found in database\n")
	  % old_id); 
    }
  else
    L(F("skipping delta to existing file version %s\n") % new_id);
  ++count;
}

void packet_db_writer::consume_file_cert(file<cert> const & t)
{
  if (!app.db.file_cert_exists(t))
    app.db.put_file_cert(t);
  else
    {
      string s;
      cert_signable_text(t.inner(), s);
      L(F("skipping existing file cert %s\n") % s);
    }
  ++count;
}

void packet_db_writer::consume_manifest_data(manifest_id const & ident, 
					     manifest_data const & dat)
{
  if (!app.db.manifest_version_exists(ident))
    app.db.put_manifest(ident, dat);
  else
    L(F("skipping existing manifest version %s\n") % ident);

  ++count;

  if (server)
    app.db.note_manifest_on_netserver (*server, ident);  
}

void packet_db_writer::consume_manifest_delta(manifest_id const & old_id, 
					      manifest_id const & new_id,
					      manifest_delta const & del)
{
  if (!app.db.manifest_version_exists(new_id))
    {
      if (app.db.manifest_version_exists(old_id))
	app.db.put_manifest_version(old_id, new_id, del);
      else
	W(F("manifest delta pre-image '%s' not found in database\n")
	  % old_id); 
    }
  else
    L(F("skipping delta to existing manifest version %s\n") % new_id);
  
  ++count;

  if (server)
    {
      app.db.note_manifest_on_netserver (*server, old_id);
      app.db.note_manifest_on_netserver (*server, new_id);
    }
}

void packet_db_writer::consume_manifest_cert(manifest<cert> const & t)
{
  if (!app.db.manifest_cert_exists(t))
    app.db.put_manifest_cert(t);
  else
    {
      string s;
      cert_signable_text(t.inner(), s);
      L(F("skipping existing manifest cert %s\n") % s);
    }
  ++count;
}

void packet_db_writer::consume_public_key(rsa_keypair_id const & ident,
					  base64< rsa_pub_key > const & k)
{
  if (!take_keys) 
    {
      W(F("skipping prohibited public key %s\n") % ident);
      return;
    }
  if (!app.db.public_key_exists(ident))
    app.db.put_key(ident, k);
  else
    L(F("skipping existing public key %s\n") % ident);
  ++count;
}

void packet_db_writer::consume_private_key(rsa_keypair_id const & ident,
					   base64< arc4<rsa_priv_key> > const & k)
{
  if (!take_keys) 
    {
      W(F("skipping prohibited private key %s\n") % ident);
      return;
    }
  if (!app.db.private_key_exists(ident))
    app.db.put_key(ident, k);
  else
    L(F("skipping existing private key %s\n") % ident);
  ++count;
}


// --- packet writer ---

packet_writer::packet_writer(ostream & o) : ost(o) {}

void packet_writer::consume_file_data(file_id const & ident, 
				      file_data const & dat)
{
  ost << "[fdata " << ident.inner()() << "]" << endl 
      << trim_ws(dat.inner()()) << endl
      << "[end]" << endl;
}

void packet_writer::consume_file_delta(file_id const & old_id, 
				       file_id const & new_id,
				       file_delta const & del)
{
  ost << "[fdelta " << old_id.inner()() << endl 
      << "        " << new_id.inner()() << "]" << endl 
      << trim_ws(del.inner()()) << endl
      << "[end]" << endl;
}

void packet_writer::consume_file_cert(file<cert> const & t)
{
  ost << "[fcert " << t.inner().ident() << endl
      << "       " << t.inner().name() << endl
      << "       " << t.inner().key() << endl
      << "       " << trim_ws(t.inner().value()) << "]" << endl
      << trim_ws(t.inner().sig()) << endl
      << "[end]" << endl;
}

void packet_writer::consume_manifest_data(manifest_id const & ident, 
					  manifest_data const & dat)
{
  ost << "[mdata " << ident.inner()() << "]" << endl 
      << trim_ws(dat.inner()()) << endl
      << "[end]" << endl;
}

void packet_writer::consume_manifest_delta(manifest_id const & old_id, 
					   manifest_id const & new_id,
					   manifest_delta const & del)
{
  ost << "[mdelta " << old_id.inner()() << endl 
      << "        " << new_id.inner()() << "]" << endl 
      << trim_ws(del.inner()()) << endl
      << "[end]" << endl;
}

void packet_writer::consume_manifest_cert(manifest<cert> const & t)
{
  ost << "[mcert " << t.inner().ident() << endl
      << "       " << t.inner().name() << endl
      << "       " << t.inner().key() << endl
      << "       " << trim_ws(t.inner().value()) << "]" << endl
      << trim_ws(t.inner().sig()) << endl
      << "[end]" << endl;
}

void packet_writer::consume_public_key(rsa_keypair_id const & ident,
				       base64< rsa_pub_key > const & k)
{
  ost << "[pubkey " << ident() << "]" << endl
      << trim_ws(k()) << endl
      << "[end]" << endl;
}

void packet_writer::consume_private_key(rsa_keypair_id const & ident,
					base64< arc4<rsa_priv_key> > const & k)
{
  ost << "[privkey " << ident() << "]" << endl
      << trim_ws(k()) << endl
      << "[end]" << endl;
}


// --- packet writer ---

queueing_packet_writer::queueing_packet_writer(app_state & a, set<url> const & t) :
  app(a), targets(t)
{}

void queueing_packet_writer::consume_file_data(file_id const & ident, 
					       file_data const & dat)
{
  ostringstream oss;
  packet_writer pw(oss);
  pw.consume_file_data(ident, dat);
  queue_blob_for_network(targets, oss.str(), app);
}

void queueing_packet_writer::consume_file_delta(file_id const & old_id, 
						file_id const & new_id,
						file_delta const & del)
{
  ostringstream oss;
  packet_writer pw(oss);
  pw.consume_file_delta(old_id, new_id, del);
  queue_blob_for_network(targets, oss.str(), app);
}

void queueing_packet_writer::consume_file_cert(file<cert> const & t)
{
  ostringstream oss;
  packet_writer pw(oss);
  pw.consume_file_cert(t);
  queue_blob_for_network(targets, oss.str(), app);
}

void queueing_packet_writer::consume_manifest_data(manifest_id const & ident, 
						   manifest_data const & dat)
{
  ostringstream oss;
  packet_writer pw(oss);
  pw.consume_manifest_data(ident, dat);
  queue_blob_for_network(targets, oss.str(), app);
}

void queueing_packet_writer::consume_manifest_delta(manifest_id const & old_id, 
						    manifest_id const & new_id,
						    manifest_delta const & del)
{
  ostringstream oss;
  packet_writer pw(oss);
  pw.consume_manifest_delta(old_id, new_id, del);
  queue_blob_for_network(targets, oss.str(), app);
}

void queueing_packet_writer::consume_manifest_cert(manifest<cert> const & t)
{
  ostringstream oss;
  packet_writer pw(oss);
  pw.consume_manifest_cert(t);
  queue_blob_for_network(targets, oss.str(), app);
}

void queueing_packet_writer::consume_public_key(rsa_keypair_id const & ident,
						base64< rsa_pub_key > const & k)
{
  ostringstream oss;
  packet_writer pw(oss);
  pw.consume_public_key(ident, k);
  queue_blob_for_network(targets, oss.str(), app);
}

void queueing_packet_writer::consume_private_key(rsa_keypair_id const & ident,
						 base64< arc4<rsa_priv_key> > const & k)
{
  ostringstream oss;
  packet_writer pw(oss);
  pw.consume_private_key(ident, k);
  queue_blob_for_network(targets, oss.str(), app);
}

// -- remainder just deals with the regexes for reading packets off streams

struct feed_packet_consumer
{
  size_t & count;
  packet_consumer & cons;
  feed_packet_consumer(size_t & count, packet_consumer & c) : count(count), cons(c)
  {}
  bool operator()(match_results<std::string::const_iterator, regex::alloc_type> const & res) const
  {
    if (res.size() != 17)
      throw oops("matched impossible packet with " 
		 + lexical_cast<string>(res.size()) + " matching parts: " +
		 string(res[0].first, res[0].second));
    
    if (res[1].matched)
      {
	L(F("read data packet\n"));
	I(res[2].matched);
	I(res[3].matched);
	string head(res[1].first, res[1].second);
	string ident(res[2].first, res[2].second);
	string body(trim_ws(string(res[3].first, res[3].second)));
	if (head == "mdata")
	  cons.consume_manifest_data(manifest_id(hexenc<id>(ident)), 
				     manifest_data(body));
	else if (head == "fdata")
	  cons.consume_file_data(file_id(hexenc<id>(ident)), 
				 file_data(body));
	else
	  throw oops("matched impossible data packet with head '" + head + "'");
      }
    else if (res[4].matched)
      {
	L(F("read delta packet\n"));
	I(res[5].matched);
	I(res[6].matched);
	I(res[7].matched);
	string head(res[4].first, res[4].second);
	string old_id(res[5].first, res[5].second);
	string new_id(res[6].first, res[6].second);
	string body(trim_ws(string(res[7].first, res[7].second)));
	if (head == "mdelta")
	  cons.consume_manifest_delta(manifest_id(hexenc<id>(old_id)), 
				      manifest_id(hexenc<id>(new_id)),
				      manifest_delta(body));
	else if (head == "fdelta")
	  cons.consume_file_delta(file_id(hexenc<id>(old_id)), 
				  file_id(hexenc<id>(new_id)), 
				  file_delta(body));
	else
	  throw oops("matched impossible delta packet with head '" + head + "'");
      }
    else if (res[8].matched)
      {
	L(F("read cert packet\n"));
	I(res[9].matched);
	I(res[10].matched);
	I(res[11].matched);
	I(res[12].matched);
	I(res[13].matched);
	string head(res[8].first, res[8].second);
	string ident(res[9].first, res[9].second);
	string certname(res[10].first, res[10].second);
	string key(res[11].first, res[11].second);
	string val(res[12].first, res[12].second);
	string body(res[13].first, res[13].second);

	// canonicalize the base64 encodings to permit searches
	cert t = cert(hexenc<id>(ident),
		      cert_name(certname),
		      base64<cert_value>(canonical_base64(val)),
		      rsa_keypair_id(key),
		      base64<rsa_sha1_signature>(canonical_base64(body)));
	if (head == "mcert")
	  cons.consume_manifest_cert(manifest<cert>(t));
	else if (head == "fcert")
	  cons.consume_file_cert(file<cert>(t));
	else
	  throw oops("matched impossible cert packet with head '" + head + "'");
      } 
    else if (res[14].matched)
      {
	L(F("read key data packet\n"));
	I(res[15].matched);
	I(res[16].matched);
	string head(res[14].first, res[14].second);
	string ident(res[15].first, res[15].second);
	string body(trim_ws(string(res[16].first, res[16].second)));
	if (head == "pubkey")
	  cons.consume_public_key(rsa_keypair_id(ident),
				  base64<rsa_pub_key>(body));
	else if (head == "privkey")
	  cons.consume_private_key(rsa_keypair_id(ident),
				   base64< arc4<rsa_priv_key> >(body));
	else
	  throw oops("matched impossible key data packet with head '" + head + "'");
      }
    else
      return true;
    ++count;
    return true;
  }
};

static size_t extract_packets(string const & s, packet_consumer & cons)
{  
  string const ident("([0-9a-f]{40})");
  string const sp("[[:space:]]+");
  string const bra("\\[");
  string const ket("\\]");
  string const certhead("(mcert|fcert)");
  string const datahead("(mdata|fdata)");
  string const deltahead("(mdelta|fdelta)");
  string const keyhead("(pubkey|privkey)");
  string const key("([-a-zA-Z0-9_\\.@]+)");
  string const certname("([-a-zA-Z0-9_\\.@]+)");
  string const base64("([a-zA-Z0-9+/=[:space:]]+)");
  string const end("\\[end\\]");
  string const data = bra + datahead + sp + ident + ket + base64 + end; 
  string const delta = bra + deltahead + sp + ident + sp + ident + ket + base64 + end;
  string const cert = bra 
    + certhead + sp + ident + sp + certname + sp + key + sp + base64 
    + ket 
    + base64 + end; 
  string const keydata = bra + keyhead + sp + key + ket + base64 + end;
  string const biggie = (data + "|" + delta + "|" + cert + "|" + keydata);
  regex expr(biggie);
  size_t count = 0;
  regex_grep(feed_packet_consumer(count, cons), s, expr, match_default);
  return count;
}


size_t read_packets(istream & in, packet_consumer & cons)
{
  string accum, tmp;
  size_t count = 0;
  size_t const bufsz = 0xff;
  char buf[bufsz];
  string const end("[end]");
  while(in)
    {
      in.read(buf, bufsz);
      accum.append(buf, in.gcount());      
      string::size_type endpos = string::npos;
      endpos = accum.rfind(end);
      if (endpos != string::npos)
	{
	  endpos += end.size();
	  string tmp = accum.substr(0, endpos);
	  count += extract_packets(tmp, cons);
	  if (endpos < accum.size() - 1)
	    accum = accum.substr(endpos+1);
	  else
	    accum.clear();
	}
    }
  return count;
}


#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"
#include "transforms.hh"
#include "manifest.hh"

static void packet_roundabout_test()
{
  string tmp;

  {
    ostringstream oss;
    packet_writer pw(oss);

    // an fdata packet
    base64< gzip<data> > gzdata;
    pack(data("this is some file data"), gzdata);
    file_data fdata(gzdata);
    file_id fid;
    calculate_ident(fdata, fid);
    pw.consume_file_data(fid, fdata);

    // an fdelta packet    
    base64< gzip<data> > gzdata2;
    pack(data("this is some file data which is not the same as the first one"), gzdata2);
    file_data fdata2(gzdata2);
    file_id fid2;
    calculate_ident(fdata2, fid);
    base64< gzip<delta> > del;
    diff(fdata.inner(), fdata2.inner(), del);
    pw.consume_file_delta(fid, fid2, file_delta(del));

    // a file cert packet
    base64<cert_value> val;
    encode_base64(cert_value("peaches"), val);
    base64<rsa_sha1_signature> sig;
    encode_base64(rsa_sha1_signature("blah blah there is no way this is a valid signature"), sig);    
    cert c(fid.inner(), cert_name("smell"), val, 
	   rsa_keypair_id("fun@moonman.com"), sig);
    pw.consume_file_cert(file<cert>(c));
    
    // a manifest cert packet
    pw.consume_manifest_cert(manifest<cert>(c));

    // a manifest data packet
    manifest_map mm;
    manifest_data mdata;
    manifest_id mid;
    mm.insert(make_pair(file_path("foo/bar.txt"),
			file_id(hexenc<id>("cfb81b30ab3133a31b52eb50bd1c86df67eddec4"))));
    write_manifest_map(mm, mdata);
    calculate_ident(mdata, mid);
    pw.consume_manifest_data(mid, mdata);

    // a manifest delta packet
    manifest_map mm2;
    manifest_data mdata2;
    manifest_id mid2;
    manifest_delta mdelta;
    mm2.insert(make_pair(file_path("foo/bar.txt"),
			 file_id(hexenc<id>("5b20eb5e5bdd9cd674337fc95498f468d80ef7bc"))));
    mm2.insert(make_pair(file_path("bunk.txt"),
			 file_id(hexenc<id>("54f373ed07b4c5a88eaa93370e1bbac02dc432a8"))));
    write_manifest_map(mm2, mdata2);
    calculate_ident(mdata2, mid2);
    base64< gzip<delta> > del2;
    diff(mdata.inner(), mdata2.inner(), del2);
    pw.consume_manifest_delta(mid, mid2, manifest_delta(del));
    
    // a public key packet
    base64<rsa_pub_key> puk;
    encode_base64(rsa_pub_key("this is not a real rsa key"), puk);
    pw.consume_public_key(rsa_keypair_id("test@lala.com"), puk);

    // a private key packet
    base64< arc4<rsa_priv_key> > pik;
    encode_base64(arc4<rsa_priv_key>
		  (rsa_priv_key("this is not a real rsa key either!")), pik);
    
    pw.consume_private_key(rsa_keypair_id("test@lala.com"), pik);
    
  }
  
  for (int i = 0; i < 10; ++i)
    {
      // now spin around sending and receiving this a few times
      ostringstream oss;
      packet_writer pw(oss);      
      istringstream iss(tmp);
      read_packets(iss, pw);
      BOOST_CHECK(oss.str() == tmp);
      tmp = oss.str();
    }
}

void add_packet_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&packet_roundabout_test));
}

#endif // BUILD_UNIT_TESTS
